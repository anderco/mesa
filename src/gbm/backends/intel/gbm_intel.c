/*
 * Copyright © 2011 Intel Corporation
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
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "gbm_intel.h"

#include "gbmint.h"

static int
gbm_intel_is_format_supported(struct gbm_device *gbm,
                            uint32_t format,
                            uint32_t usage)
{
   switch (format) {
   case GBM_BO_FORMAT_XRGB8888:
   case GBM_FORMAT_XRGB8888:
      break;
   case GBM_BO_FORMAT_ARGB8888:
   case GBM_FORMAT_ARGB8888:
      if (usage & GBM_BO_USE_SCANOUT)
         return 0;
      break;
   default:
      return 0;
   }

   if (usage & GBM_BO_USE_CURSOR_64X64 &&
       usage & GBM_BO_USE_RENDERING)
      return 0;

   return 1;
}

static int
gbm_intel_bo_write(struct gbm_bo *bo, const void *buf, size_t count)
{
   struct gbm_intel_bo *ibo = gbm_intel_bo(bo);
   int ret;

   ret = drm_intel_bo_map(ibo->bo, 1);
   if (ret < 0)
      return ret;

   memcpy(ibo->bo->virtual, buf, count);

   return drm_intel_bo_unmap(ibo->bo);
}

static void
gbm_intel_bo_destroy(struct gbm_bo *_bo)
{
   struct gbm_intel_bo *ibo = gbm_intel_bo(_bo);

   drm_intel_bo_unreference(ibo->bo);

   free(ibo);
}

static inline int
align(int value, int size)
{
   return (value + size - 1) & ~(size - 1);
}

static struct gbm_intel_bo *
gbm_intel_bo_create_with_bo(struct gbm_device *gbm,
                            uint32_t width, uint32_t height, uint32_t stride,
                            uint32_t format, uint32_t usage,
                            drm_intel_bo *bo)
{
   struct gbm_intel_bo *ibo;

   ibo = calloc(1, sizeof *ibo);
   if (!ibo)
      return NULL;

   ibo->bo = bo;

   ibo->base.base.gbm = gbm;
   ibo->base.base.width = width;
   ibo->base.base.height = height;
   ibo->base.base.stride = stride;
   ibo->base.base.format = format;
   ibo->base.base.handle.s32 = ibo->bo->handle;

   return ibo;
}

static struct gbm_bo *
gbm_intel_bo_create(struct gbm_device *gbm,
                    uint32_t width, uint32_t height,
                    uint32_t format, uint32_t usage)
{
   struct gbm_intel_device *igbm = gbm_intel_device(gbm);
   struct gbm_intel_bo *ibo;
   drm_intel_bo *bo;
   int size, stride;

   switch (format) {
   case GBM_BO_FORMAT_XRGB8888:
   case GBM_FORMAT_XRGB8888:
   case GBM_BO_FORMAT_ARGB8888:
   case GBM_FORMAT_ARGB8888:
      break;
   default:
      return NULL;
   }

   stride = align(width * 4, 64);
   size = align(stride * height, 4096);
   bo = drm_intel_bo_alloc(igbm->bufmgr, "intel gbm", size, 0);
   if (!bo)
      return NULL;

   ibo = gbm_intel_bo_create_with_bo(gbm, width, height, stride,
                                     format, usage, bo);
   if (!ibo) {
      drm_intel_bo_unreference(bo);
      return NULL;
   }

   return &ibo->base.base;
}

static struct gbm_bo *
gbm_intel_bo_import(struct gbm_device *gbm,
                  uint32_t type, void *buffer, uint32_t usage)
{
   return NULL;
}

static struct gbm_surface *
gbm_intel_surface_create(struct gbm_device *gbm,
                       uint32_t width, uint32_t height,
		       uint32_t format, uint32_t flags)
{
   struct gbm_intel_surface *surf;

   surf = calloc(1, sizeof *surf);
   if (surf == NULL)
      return NULL;

   surf->base.gbm = gbm;
   surf->base.width = width;
   surf->base.height = height;
   surf->base.format = format;
   surf->base.flags = flags;

   return &surf->base;
}

static void
gbm_intel_surface_destroy(struct gbm_surface *_surf)
{
   struct gbm_intel_surface *surf = gbm_intel_surface(_surf);

   free(surf);
}

static void
gbm_intel_destroy(struct gbm_device *gbm)
{
   struct gbm_intel_device *igbm = gbm_intel_device(gbm);

   free(igbm);
}

static struct gbm_device *
gbm_intel_device_create(int fd)
{
   struct gbm_intel_device *igbm;

   igbm = calloc(1, sizeof *igbm);

   igbm->base.base.fd = fd;
   igbm->base.base.bo_create = gbm_intel_bo_create;
   igbm->base.base.bo_import = gbm_intel_bo_import;
   igbm->base.base.is_format_supported = gbm_intel_is_format_supported;
   igbm->base.base.bo_write = gbm_intel_bo_write;
   igbm->base.base.bo_destroy = gbm_intel_bo_destroy;
   igbm->base.base.destroy = gbm_intel_destroy;
   igbm->base.base.surface_create = gbm_intel_surface_create;
   igbm->base.base.surface_destroy = gbm_intel_surface_destroy;

   igbm->base.type = GBM_DRM_DRIVER_TYPE_OTHER;
   igbm->base.base.name = "intel";

   igbm->bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
   if (!igbm->bufmgr) {
      free(igbm);
      return NULL;
   }

   return &igbm->base.base;
}

struct gbm_backend gbm_intel_backend = {
   .backend_name = "intel",
   .create_device = gbm_intel_device_create,
};
