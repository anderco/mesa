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
 */

#ifndef _GBM_INTEL_INTERNAL_H_
#define _GBM_INTEL_INTERNAL_H_

#include <libdrm/intel_bufmgr.h>

#include "gbmint.h"

#include "common.h"
#include "common_drm.h"

struct wl_drm;
struct gbm_intel_surface;

struct gbm_intel_device {
   struct gbm_drm_device base;

   drm_intel_bufmgr *bufmgr;

   struct wl_drm *wl_drm;
};

struct gbm_intel_bo {
   struct gbm_drm_bo base;

   drm_intel_bo *bo;
   uint32_t name;
};

struct gbm_intel_surface {
   struct gbm_surface base;
};

static inline struct gbm_intel_device *
gbm_intel_device(struct gbm_device *gbm)
{
   return (struct gbm_intel_device *) gbm;
}

static inline struct gbm_intel_bo *
gbm_intel_bo(struct gbm_bo *bo)
{
   return (struct gbm_intel_bo *) bo;
}

static inline struct gbm_intel_surface *
gbm_intel_surface(struct gbm_surface *surface)
{
   return (struct gbm_intel_surface *) surface;
}

#endif
