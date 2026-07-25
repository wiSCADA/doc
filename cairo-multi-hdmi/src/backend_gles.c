/*
 * GLES/GBM backend skeleton for RK3588 Mali-G610.
 *
 * Production path (on device):
 *   1) GBM+EGL create shared render target (logical 1080p or 4K)
 *   2) GLES draw scene once into that target
 *   3) Export DMA-BUF → RGA scale/blit into each CRTC FB (or EGL image per output)
 *   4) DRM page-flip each connector
 *
 * This translation unit compiles only with -DENABLE_GLES=1 and links EGL/GBM/GLESv2.
 * Without a working Mali/Panthor stack it will fail init and the app falls back to Cairo.
 */

#include "backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ENABLE_GLES) && ENABLE_GLES

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct {
    int drm_fd;
    struct gbm_device *gbm;
    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface surf;
    struct gbm_surface *gsurf;
    int logical_w, logical_h;
    const app_fonts_t *fonts;
    bool ok;
} gles_impl_t;

static int gles_init(render_backend_t *b, const app_config_t *cfg, const app_fonts_t *fonts) {
    gles_impl_t *impl = calloc(1, sizeof(*impl));
    if (!impl)
        return -1;
    impl->fonts = fonts;
    impl->drm_fd = -1;
    impl->dpy = EGL_NO_DISPLAY;
    impl->ctx = EGL_NO_CONTEXT;
    impl->surf = EGL_NO_SURFACE;

    present_policy_t pol = present_policy_default(cfg);
    impl->logical_w = pol.logical_w;
    impl->logical_h = pol.logical_h;

    const char *card = cfg->drm_path ? cfg->drm_path : "/dev/dri/card0";
    impl->drm_fd = open(card, O_RDWR | O_CLOEXEC);
    if (impl->drm_fd < 0) {
        perror(card);
        free(impl);
        return -1;
    }

    impl->gbm = gbm_create_device(impl->drm_fd);
    if (!impl->gbm) {
        fprintf(stderr, "gbm_create_device failed\n");
        close(impl->drm_fd);
        free(impl);
        return -1;
    }

    impl->dpy = eglGetDisplay((EGLNativeDisplayType)impl->gbm);
    if (impl->dpy == EGL_NO_DISPLAY || !eglInitialize(impl->dpy, NULL, NULL)) {
        fprintf(stderr, "eglInitialize failed\n");
        gbm_device_destroy(impl->gbm);
        close(impl->drm_fd);
        free(impl);
        return -1;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "eglBindAPI ES failed\n");
        eglTerminate(impl->dpy);
        gbm_device_destroy(impl->gbm);
        close(impl->drm_fd);
        free(impl);
        return -1;
    }

    const EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig egl_cfg;
    EGLint n = 0;
    if (!eglChooseConfig(impl->dpy, cfg_attrs, &egl_cfg, 1, &n) || n < 1) {
        fprintf(stderr, "eglChooseConfig failed\n");
        eglTerminate(impl->dpy);
        gbm_device_destroy(impl->gbm);
        close(impl->drm_fd);
        free(impl);
        return -1;
    }

    const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    impl->ctx = eglCreateContext(impl->dpy, egl_cfg, EGL_NO_CONTEXT, ctx_attrs);
    impl->gsurf = gbm_surface_create(impl->gbm, impl->logical_w, impl->logical_h,
                                     GBM_FORMAT_XRGB8888,
                                     GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
    if (!impl->ctx || !impl->gsurf) {
        fprintf(stderr, "egl/gbm surface/context create failed\n");
        if (impl->ctx) eglDestroyContext(impl->dpy, impl->ctx);
        eglTerminate(impl->dpy);
        gbm_device_destroy(impl->gbm);
        close(impl->drm_fd);
        free(impl);
        return -1;
    }

    impl->surf = eglCreateWindowSurface(impl->dpy, egl_cfg,
                                        (EGLNativeWindowType)impl->gsurf, NULL);
    if (impl->surf == EGL_NO_SURFACE ||
        !eglMakeCurrent(impl->dpy, impl->surf, impl->surf, impl->ctx)) {
        fprintf(stderr, "eglMakeCurrent failed\n");
        if (impl->surf != EGL_NO_SURFACE) eglDestroySurface(impl->dpy, impl->surf);
        eglDestroyContext(impl->dpy, impl->ctx);
        gbm_surface_destroy(impl->gsurf);
        eglTerminate(impl->dpy);
        gbm_device_destroy(impl->gbm);
        close(impl->drm_fd);
        free(impl);
        return -1;
    }

    fprintf(stderr, "GLES backend ready: EGL_VENDOR=%s RENDERER=%s logical=%dx%d\n",
            eglQueryString(impl->dpy, EGL_VENDOR),
            (const char *)glGetString(GL_RENDERER),
            impl->logical_w, impl->logical_h);

    impl->ok = true;
    b->impl = impl;
    return 0;
}

static void gles_shutdown(render_backend_t *b) {
    gles_impl_t *impl = b->impl;
    if (!impl)
        return;
    if (impl->dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(impl->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (impl->surf != EGL_NO_SURFACE) eglDestroySurface(impl->dpy, impl->surf);
        if (impl->ctx != EGL_NO_CONTEXT) eglDestroyContext(impl->dpy, impl->ctx);
        eglTerminate(impl->dpy);
    }
    if (impl->gsurf) gbm_surface_destroy(impl->gsurf);
    if (impl->gbm) gbm_device_destroy(impl->gbm);
    if (impl->drm_fd >= 0) close(impl->drm_fd);
    free(impl);
    b->impl = NULL;
}

static int gles_draw_frame(render_backend_t *b, const app_config_t *cfg,
                           const present_policy_t *policy, const wall_time_t *utc,
                           drm_display_t *disp) {
    (void)cfg;
    (void)utc;
    gles_impl_t *impl = b->impl;
    if (!impl || !impl->ok)
        return -1;

    /* Placeholder fill — replace with textured/widget GLES scene. */
    float t = (utc->second % 60) / 60.0f;
    glViewport(0, 0, impl->logical_w, impl->logical_h);
    glClearColor(0.05f, 0.08f + 0.2f * t, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(impl->dpy, impl->surf);

    /*
     * TODO(on-device):
     *  - gbm_surface_lock_front_buffer → dmabuf fd
     *  - RGA: scale/blit shared BO into each disp->outs[i] FB
     *  - drm_output_flip for each CRTC
     *  - gbm_surface_release_buffer
     *
     * Until RGA present is wired, if DRM outputs exist we still need a present path.
     * Fall back: report not-fully-implemented for DRM present so caller can use Cairo.
     */
    if (cfg->offline) {
        fprintf(stderr, "GLES offline PNG dump not implemented; use --backend cairo --offline\n");
        return -1;
    }
    if (!disp || disp->count < 1) {
        fprintf(stderr, "GLES draw ok (no DRM present wired yet)\n");
        return 0;
    }

    fprintf(stderr,
            "GLES rendered shared %dx%d frame; RGA→%d outputs not wired in this skeleton.\n"
            "Use --backend cairo --present share for functional multi-HDMI today,\n"
            "or extend this file with Rockchip RGA blit (see docs/GPU_ARCHITECTURE.md).\n",
            impl->logical_w, impl->logical_h, disp->count);
    (void)policy;
    return -1;
}

int backend_gles_create(render_backend_t *out) {
    memset(out, 0, sizeof(*out));
    out->id = BACKEND_GLES;
    out->name = "gles";
    out->init = gles_init;
    out->shutdown = gles_shutdown;
    out->draw_frame = gles_draw_frame;
    return 0;
}

#endif /* ENABLE_GLES */
