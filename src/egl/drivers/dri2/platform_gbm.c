/*
 * Copyright © 2011-2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Ander Conselvan de Oliveira <ander.conselvan.de.oliveira@intel.com>
 *    Kristian Høgsberg <krh@bitplanet.net>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xf86drm.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "egl_dri2.h"
#include "loader.h"

#include "gbmint.h"

static struct gbm_bo *
lock_front_buffer(struct gbm_surface *surf)
{
   struct dri2_egl_surface *dri2_surf = surf->priv;
   struct gbm_bo *bo;

   if (dri2_surf->current == NULL) {
      _eglError(EGL_BAD_SURFACE, "no front buffer");
      return NULL;
   }

   bo = dri2_surf->current->bo;
   dri2_surf->current->locked = 1;
   dri2_surf->current = NULL;

   return bo;
}

static void
release_buffer(struct gbm_surface *surf, struct gbm_bo *bo)
{
   struct dri2_egl_surface *dri2_surf = surf->priv;
   int i;

   for (i = 0; i < ARRAY_SIZE(dri2_surf->color_buffers); i++) {
      if (dri2_surf->color_buffers[i].bo == bo) {
	 dri2_surf->color_buffers[i].locked = 0;
      }
   }
}

static int
has_free_buffers(struct gbm_surface *surf)
{
   struct dri2_egl_surface *dri2_surf = surf->priv;
   int i;

   for (i = 0; i < ARRAY_SIZE(dri2_surf->color_buffers); i++)
      if (!dri2_surf->color_buffers[i].locked)
	 return 1;

   return 0;
}

static _EGLSurface *
dri2_create_surface(_EGLDriver *drv, _EGLDisplay *disp, EGLint type,
		    _EGLConfig *conf, EGLNativeWindowType window,
		    const EGLint *attrib_list)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   struct dri2_egl_config *dri2_conf = dri2_egl_config(conf);
   struct dri2_egl_surface *dri2_surf;
   struct gbm_surface *surf;

   (void) drv;

   dri2_surf = calloc(1, sizeof *dri2_surf);
   if (!dri2_surf) {
      _eglError(EGL_BAD_ALLOC, "dri2_create_surface");
      return NULL;
   }

   if (!_eglInitSurface(&dri2_surf->base, disp, type, conf, attrib_list))
      goto cleanup_surf;

   switch (type) {
   case EGL_WINDOW_BIT:
      if (!window)
         return NULL;
      surf = (struct gbm_surface *) window;
      dri2_surf->gbm_surf = surf;
      dri2_surf->base.Width =  surf->width;
      dri2_surf->base.Height = surf->height;
      surf->priv = dri2_surf;
      break;
   default:
      goto cleanup_surf;
   }

   dri2_surf->dri_drawable =
      (*dri2_dpy->dri2->createNewDrawable) (dri2_dpy->dri_screen,
					    dri2_conf->dri_double_config,
					    dri2_surf);

   if (dri2_surf->dri_drawable == NULL) {
      _eglError(EGL_BAD_ALLOC, "dri2->createNewDrawable");
      goto cleanup_surf;
   }

   return &dri2_surf->base;

 cleanup_surf:
   free(dri2_surf);

   return NULL;
}

static _EGLSurface *
dri2_create_window_surface(_EGLDriver *drv, _EGLDisplay *disp,
			   _EGLConfig *conf, EGLNativeWindowType window,
			   const EGLint *attrib_list)
{
   return dri2_create_surface(drv, disp, EGL_WINDOW_BIT, conf,
			      window, attrib_list);
}

static EGLBoolean
dri2_destroy_surface(_EGLDriver *drv, _EGLDisplay *disp, _EGLSurface *surf)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   struct dri2_egl_surface *dri2_surf = dri2_egl_surface(surf);
   int i;

   if (!_eglPutSurface(surf))
      return EGL_TRUE;

   (*dri2_dpy->core->destroyDrawable)(dri2_surf->dri_drawable);

   for (i = 0; i < ARRAY_SIZE(dri2_surf->color_buffers); i++) {
      if (dri2_surf->color_buffers[i].bo)
	 gbm_bo_destroy(dri2_surf->color_buffers[i].bo);
   }

   for (i = 0; i < __DRI_BUFFER_COUNT; i++) {
      if (dri2_surf->dri_buffers[i])
         dri2_dpy->dri2->releaseBuffer(dri2_dpy->dri_screen,
                                       dri2_surf->dri_buffers[i]);
   }

   free(surf);

   return EGL_TRUE;
}

static int
get_back_bo(struct dri2_egl_surface *dri2_surf)
{
   struct gbm_surface *surf;
   int i;

   if (dri2_surf->back == NULL) {
      for (i = 0; i < ARRAY_SIZE(dri2_surf->color_buffers); i++) {
	 if (!dri2_surf->color_buffers[i].locked) {
	    dri2_surf->back = &dri2_surf->color_buffers[i];
	    break;
	 }
      }
   }

   surf = dri2_surf->gbm_surf;

   if (dri2_surf->back == NULL)
      return -1;
   if (dri2_surf->back->bo == NULL)
      dri2_surf->back->bo =
	 gbm_bo_create(surf->gbm, surf->width, surf->height,
		       surf->format, surf->flags);
   if (dri2_surf->back->bo == NULL)
      return -1;

   return 0;
}

static __DRIimage *
bo_to_dri_image(struct gbm_bo *bo, struct dri2_egl_display *dri2_dpy,
                void *loaderPrivate);

static int
image_get_buffers(__DRIdrawable *driDrawable,
                  unsigned int format,
                  uint32_t *stamp,
                  void *loaderPrivate,
                  uint32_t buffer_mask,
                  struct __DRIimageList *buffers)
{
   struct dri2_egl_surface *dri2_surf = loaderPrivate;
   struct dri2_egl_display *dri2_dpy =
      dri2_egl_display(dri2_surf->base.Resource.Display);

   if (get_back_bo(dri2_surf) < 0)
      return 0;

   buffers->image_mask = __DRI_IMAGE_BUFFER_BACK;
   buffers->back = bo_to_dri_image(dri2_surf->back->bo, dri2_dpy, NULL);

   return 1;
}

static void
dri2_flush_front_buffer(__DRIdrawable * driDrawable, void *loaderPrivate)
{
   (void) driDrawable;
   (void) loaderPrivate;
}

static const __DRIimageLoaderExtension image_loader_extension = {
   { __DRI_IMAGE_LOADER, 1 },
   image_get_buffers,
   dri2_flush_front_buffer
};


static EGLBoolean
dri2_swap_buffers(_EGLDriver *drv, _EGLDisplay *disp, _EGLSurface *draw)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   struct dri2_egl_surface *dri2_surf = dri2_egl_surface(draw);
   int i;

   if (dri2_surf->base.Type == EGL_WINDOW_BIT) {
      if (dri2_surf->current)
	 _eglError(EGL_BAD_SURFACE, "dri2_swap_buffers");
      for (i = 0; i < ARRAY_SIZE(dri2_surf->color_buffers); i++)
         if (dri2_surf->color_buffers[i].age > 0)
            dri2_surf->color_buffers[i].age++;
      dri2_surf->current = dri2_surf->back;
      dri2_surf->current->age = 1;
      dri2_surf->back = NULL;
   }

   (*dri2_dpy->flush->flush)(dri2_surf->dri_drawable);
   (*dri2_dpy->flush->invalidate)(dri2_surf->dri_drawable);

   return EGL_TRUE;
}

static EGLint
dri2_query_buffer_age(_EGLDriver *drv,
                      _EGLDisplay *disp, _EGLSurface *surface)
{
   struct dri2_egl_surface *dri2_surf = dri2_egl_surface(surface);

   if (get_back_bo(dri2_surf) < 0) {
      _eglError(EGL_BAD_ALLOC, "dri2_query_buffer_age");
      return 0;
   }

   return dri2_surf->back->age;
}

static __DRIimage *
bo_to_dri_image(struct gbm_bo *bo, struct dri2_egl_display *dri2_dpy,
                void *loaderPrivate)
{
   __DRIimage *image;
   struct gbm_drm_bo *drm_bo;
   int width, height, pitch, format, cpp;

   width = gbm_bo_get_width(bo);
   height = gbm_bo_get_height(bo);

   switch (gbm_bo_get_format(bo)) {
   case GBM_FORMAT_RGB565:
      cpp = 2;
      format = __DRI_IMAGE_FORMAT_RGB565;
      break;
   case GBM_FORMAT_XRGB8888:
   case GBM_BO_FORMAT_XRGB8888:
      cpp = 4;
      format = __DRI_IMAGE_FORMAT_XRGB8888;
      break;
   case GBM_BO_FORMAT_ARGB8888:
   case GBM_FORMAT_ARGB8888:
      cpp = 4;
      format = __DRI_IMAGE_FORMAT_ARGB8888;
      break;
   default:
      return NULL;
   }

   pitch = gbm_bo_get_stride(bo) / cpp;

   drm_bo = (struct gbm_drm_bo *) bo;

   image =
      dri2_dpy->image->createImageFromHandle(dri2_dpy->dri_screen,
                                             width, height, format,
                                             drm_bo->bo, pitch, loaderPrivate);

   return image;
}

static _EGLImage *
dri2_create_image_khr_pixmap(_EGLDisplay *disp, _EGLContext *ctx,
			     EGLClientBuffer buffer, const EGLint *attr_list)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   struct gbm_bo *bo = (struct gbm_bo *) buffer;
   struct dri2_egl_image *dri2_img;

   dri2_img = malloc(sizeof *dri2_img);
   if (!dri2_img) {
      _eglError(EGL_BAD_ALLOC, "dri2_create_image_khr_pixmap");
      return NULL;
   }

   if (!_eglInitImage(&dri2_img->base, disp)) {
      free(dri2_img);
      return NULL;
   }

   dri2_img->dri_image = bo_to_dri_image(bo, dri2_dpy, dri2_img);
   if (dri2_img->dri_image == NULL) {
      free(dri2_img);
      _eglError(EGL_BAD_ALLOC, "dri2_create_image_khr_pixmap");
      return NULL;
   }

   return &dri2_img->base;
}

static _EGLImage *
dri2_drm_create_image_khr(_EGLDriver *drv, _EGLDisplay *disp,
                          _EGLContext *ctx, EGLenum target,
                          EGLClientBuffer buffer, const EGLint *attr_list)
{
   (void) drv;

   switch (target) {
   case EGL_NATIVE_PIXMAP_KHR:
      return dri2_create_image_khr_pixmap(disp, ctx, buffer, attr_list);
   default:
      return dri2_create_image_khr(drv, disp, ctx, target, buffer, attr_list);
   }
}

static int
dri2_drm_authenticate(_EGLDisplay *disp, uint32_t id)
{
   struct gbm_device *gbm = disp->PlatformDisplay;
   int fd = gbm_device_get_fd(gbm);

   return drmAuthMagic(fd, id);
}

EGLBoolean
dri2_initialize_gbm(_EGLDriver *drv, _EGLDisplay *disp)
{
   struct dri2_egl_display *dri2_dpy;
   struct gbm_device *gbm;
   struct gbm_drm_device *gbm_drm;
   int fd = -1;
   int i;

   loader_set_logger(_eglLog);

   dri2_dpy = calloc(1, sizeof *dri2_dpy);
   if (!dri2_dpy)
      return _eglError(EGL_BAD_ALLOC, "eglInitialize");

   disp->DriverData = (void *) dri2_dpy;

   gbm = disp->PlatformDisplay;
   if (gbm == NULL) {
      fd = open("/dev/dri/card0", O_RDWR);
      dri2_dpy->own_device = 1;
      gbm = gbm_create_device(fd);
      if (gbm == NULL)
         return EGL_FALSE;
   }

   if (fd < 0) {
      fd = dup(gbm_device_get_fd(gbm));
      if (fd < 0) {
         free(dri2_dpy);
         return EGL_FALSE;
      }
   }

   dri2_dpy->fd = fd;
   dri2_dpy->device_name = loader_get_device_name_for_fd(fd);
   dri2_dpy->driver_name = loader_get_driver_for_fd(dri2_dpy->fd, 0);

   if (!dri2_load_driver(disp))
      goto clean_up_names;

   gbm_drm = gbm_drm_device(gbm);
   if (gbm_drm->type != GBM_DRM_DRIVER_TYPE_NATIVE ||
       gbm_drm->bufmgr == NULL)
      goto clean_up_dri_driver;

   dri2_dpy->shared_bufmgr_extension.base.name = __DRI_SHARED_BUFMGR;
   dri2_dpy->shared_bufmgr_extension.base.version = 1;
   dri2_dpy->shared_bufmgr_extension.bufmgr = gbm_drm->bufmgr;


   dri2_dpy->extensions[0] = &image_loader_extension.base;
   dri2_dpy->extensions[1] = &image_lookup_extension.base;
   dri2_dpy->extensions[2] = &use_invalidate.base;
   dri2_dpy->extensions[3] = &dri2_dpy->shared_bufmgr_extension.base;
   dri2_dpy->extensions[4] = NULL;

   if (!dri2_create_screen(disp))
      goto clean_up_dri_driver;

   if (dri2_dpy->image->base.version < 9 ||
       dri2_dpy->image->createImageFromHandle == NULL)
      goto clean_up_dri_screen;

   gbm->surface_lock_front_buffer = lock_front_buffer;
   gbm->surface_release_buffer = release_buffer;
   gbm->surface_has_free_buffers = has_free_buffers;

   for (i = 0; dri2_dpy->driver_configs[i]; i++)
      dri2_add_config(disp, dri2_dpy->driver_configs[i],
                      i + 1, EGL_WINDOW_BIT, NULL, NULL);

   drv->API.CreateWindowSurface = dri2_create_window_surface;
   drv->API.DestroySurface = dri2_destroy_surface;
   drv->API.SwapBuffers = dri2_swap_buffers;
   drv->API.CreateImageKHR = dri2_drm_create_image_khr;
   drv->API.QueryBufferAge = dri2_query_buffer_age;

   disp->Extensions.EXT_buffer_age = EGL_TRUE;

#ifdef HAVE_WAYLAND_PLATFORM
   disp->Extensions.WL_bind_wayland_display = EGL_TRUE;
#endif
   dri2_dpy->authenticate = dri2_drm_authenticate;

   /* we're supporting EGL 1.4 */
   disp->VersionMajor = 1;
   disp->VersionMinor = 4;

   return EGL_TRUE;

clean_up_dri_screen:
   dri2_dpy->core->destroyScreen(dri2_dpy->dri_screen);
clean_up_dri_driver:
   dlclose(dri2_dpy->driver);
clean_up_names:
   free(dri2_dpy->device_name);
   free(dri2_dpy->driver_name);

   if (dri2_dpy->own_device) {
      gbm_device_destroy(gbm);
      close(fd);
   }

   free(dri2_dpy);

   return EGL_FALSE;
}
