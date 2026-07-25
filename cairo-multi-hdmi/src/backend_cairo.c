#include "backend.h"

#include "renderer.h"

#include <cairo/cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    const app_fonts_t *fonts;
} cairo_backend_impl_t;

static int mkdir_p(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;
    return mkdir(path, 0755);
}

static int cairo_init(render_backend_t *b, const app_config_t *cfg, const app_fonts_t *fonts) {
    (void)cfg;
    cairo_backend_impl_t *impl = calloc(1, sizeof(*impl));
    if (!impl)
        return -1;
    impl->fonts = fonts;
    b->impl = impl;
    return 0;
}

static void cairo_shutdown(render_backend_t *b) {
    free(b->impl);
    b->impl = NULL;
}

static int draw_one(cairo_backend_impl_t *impl, const app_config_t *cfg,
                    int out_index, int out_count,
                    const wall_time_t *utc,
                    uint8_t *pixels, int width, int height, int pitch,
                    double scale) {
    render_target_t rt;
    if (render_target_init(&rt, width, height, scale, pixels, pitch) != 0)
        return -1;
    scene_ctx_t ctx = {
        .fonts = impl->fonts,
        .utc = utc,
        .output_index = out_index,
        .output_count = out_count,
        .draw_static = true,
    };
    cairo_t *cr = render_begin(&rt, true);
    scene_draw(cfg->scene, cr, rt.draw_w, rt.draw_h, &ctx);
    render_end(&rt);
    render_target_destroy(&rt);
    return 0;
}

static int cairo_draw_frame(render_backend_t *b, const app_config_t *cfg,
                            const present_policy_t *policy, const wall_time_t *utc,
                            drm_display_t *disp) {
    cairo_backend_impl_t *impl = b->impl;

    if (cfg->offline) {
        if (mkdir_p(cfg->offline_dir) != 0) {
            perror(cfg->offline_dir);
            return -1;
        }
        int n = cfg->max_outputs;
        int w = policy->logical_w > 0 ? policy->logical_w : cfg->prefer.width;
        int h = policy->logical_h > 0 ? policy->logical_h : cfg->prefer.height;
        int pitch = w * 4;
        uint8_t *pix = calloc(1, (size_t)pitch * (size_t)h);
        if (!pix)
            return -1;

        if (policy->mode == PRESENT_SHARE_CLONE) {
            if (draw_one(impl, cfg, 0, n, utc, pix, w, h, pitch, 1.0) != 0) {
                free(pix);
                return -1;
            }
            char path0[512];
            snprintf(path0, sizeof(path0), "%s/out0.png", cfg->offline_dir);
            cairo_surface_t *png = cairo_image_surface_create_for_data(
                pix, CAIRO_FORMAT_RGB24, w, h, pitch);
            cairo_surface_write_to_png(png, path0);
            cairo_surface_destroy(png);
            fprintf(stderr, "wrote %s (shared)\n", path0);
            for (int j = 1; j < n; j++) {
                char dst[512];
                snprintf(dst, sizeof(dst), "%s/out%d.png", cfg->offline_dir, j);
                FILE *in = fopen(path0, "rb");
                FILE *out = fopen(dst, "wb");
                if (in && out) {
                    char buf[8192];
                    size_t r;
                    while ((r = fread(buf, 1, sizeof(buf), in)) > 0)
                        fwrite(buf, 1, r, out);
                    fprintf(stderr, "cloned %s -> %s\n", path0, dst);
                }
                if (in) fclose(in);
                if (out) fclose(out);
            }
        } else {
            for (int i = 0; i < n; i++) {
                if (draw_one(impl, cfg, i, n, utc, pix, w, h, pitch, 1.0) != 0) {
                    free(pix);
                    return -1;
                }
                char path[512];
                snprintf(path, sizeof(path), "%s/out%d.png", cfg->offline_dir, i);
                cairo_surface_t *png = cairo_image_surface_create_for_data(
                    pix, CAIRO_FORMAT_RGB24, w, h, pitch);
                cairo_surface_write_to_png(png, path);
                cairo_surface_destroy(png);
                fprintf(stderr, "wrote %s\n", path);
            }
        }
        free(pix);
        return 0;
    }

    if (!disp)
        return -1;

    if (policy->mode == PRESENT_SHARE_CLONE && disp->count > 0) {
        /* Draw once into output 0 backbuffer, then memcpy to other backbuffers.
         * On device this memcpy should become RGA blit (see docs/GPU_ARCHITECTURE.md). */
        drm_output_t *o0 = &disp->outs[0];
        int pitch0 = 0;
        uint8_t *map0 = drm_output_back_map(o0, &pitch0);
        if (draw_one(impl, cfg, 0, disp->count, utc, map0,
                     o0->width, o0->height, pitch0, cfg->render_scale) != 0)
            return -1;
        drm_output_flip(disp, o0);

        for (int i = 1; i < disp->count; i++) {
            drm_output_t *oi = &disp->outs[i];
            int pitch = 0;
            uint8_t *map = drm_output_back_map(oi, &pitch);
            /* Same mode assumed; naive copy. Replace with RGA. */
            if (oi->width == o0->width && oi->height == o0->height && pitch == pitch0) {
                /* front of o0 is the just-flipped buffer */
                int front = o0->front;
                memcpy(map, o0->fbs[front].map, o0->fbs[front].size);
            } else {
                if (draw_one(impl, cfg, 0, disp->count, utc, map,
                             oi->width, oi->height, pitch, cfg->render_scale) != 0)
                    return -1;
            }
            drm_output_flip(disp, oi);
        }
        return 0;
    }

    for (int i = 0; i < disp->count; i++) {
        drm_output_t *o = &disp->outs[i];
        int pitch = 0;
        uint8_t *map = drm_output_back_map(o, &pitch);
        if (draw_one(impl, cfg, i, disp->count, utc, map,
                     o->width, o->height, pitch, cfg->render_scale) != 0)
            return -1;
        drm_output_flip(disp, o);
    }
    return 0;
}

int backend_cairo_create(render_backend_t *out) {
    memset(out, 0, sizeof(*out));
    out->id = BACKEND_CAIRO;
    out->name = "cairo";
    out->init = cairo_init;
    out->shutdown = cairo_shutdown;
    out->draw_frame = cairo_draw_frame;
    return 0;
}
