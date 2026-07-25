#include "renderer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

int render_target_init(render_target_t *rt, int out_w, int out_h, double scale,
                       uint8_t *out_pixels, int out_pitch) {
    memset(rt, 0, sizeof(*rt));
    if (scale <= 0.0)
        scale = 1.0;
    if (scale > 1.0)
        scale = 1.0;

    rt->out_w = out_w;
    rt->out_h = out_h;
    rt->scale = scale;
    rt->draw_w = (int)lround(out_w * scale);
    rt->draw_h = (int)lround(out_h * scale);
    if (rt->draw_w < 64) rt->draw_w = 64;
    if (rt->draw_h < 64) rt->draw_h = 64;
    rt->out_pixels = out_pixels;
    rt->out_pitch = out_pitch;
    rt->force_full = true;

    if (fabs(scale - 1.0) < 1e-6) {
        rt->draw_surface = cairo_image_surface_create_for_data(
            out_pixels, CAIRO_FORMAT_RGB24, out_w, out_h, out_pitch);
    } else {
        rt->draw_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                     rt->draw_w, rt->draw_h);
    }
    if (cairo_surface_status(rt->draw_surface) != CAIRO_STATUS_SUCCESS)
        return -1;

    rt->cr = cairo_create(rt->draw_surface);
    return 0;
}

void render_target_destroy(render_target_t *rt) {
    if (!rt)
        return;
    if (rt->cr)
        cairo_destroy(rt->cr);
    if (rt->draw_surface)
        cairo_surface_destroy(rt->draw_surface);
    if (rt->static_layer)
        cairo_surface_destroy(rt->static_layer);
    memset(rt, 0, sizeof(*rt));
}

int render_target_rebind(render_target_t *rt, uint8_t *out_pixels, int out_pitch) {
    rt->out_pixels = out_pixels;
    rt->out_pitch = out_pitch;
    if (fabs(rt->scale - 1.0) >= 1e-6)
        return 0;

    /* Native scale paints directly into FB — rebuild image surface for new BO. */
    if (rt->cr) {
        cairo_destroy(rt->cr);
        rt->cr = NULL;
    }
    if (rt->draw_surface) {
        cairo_surface_destroy(rt->draw_surface);
        rt->draw_surface = NULL;
    }
    rt->draw_surface = cairo_image_surface_create_for_data(
        out_pixels, CAIRO_FORMAT_RGB24, rt->out_w, rt->out_h, out_pitch);
    if (cairo_surface_status(rt->draw_surface) != CAIRO_STATUS_SUCCESS)
        return -1;
    rt->cr = cairo_create(rt->draw_surface);
    return 0;
}

void render_invalidate_static(render_target_t *rt) {
    rt->static_valid = false;
    rt->force_full = true;
}

cairo_t *render_begin(render_target_t *rt, bool redraw_static) {
    (void)redraw_static;
    cairo_set_operator(rt->cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(rt->cr, 0, 0, 0);
    cairo_paint(rt->cr);
    cairo_set_operator(rt->cr, CAIRO_OPERATOR_OVER);
    return rt->cr;
}

static void blit_scale_nearest(const uint8_t *src, int sw, int sh, int spitch,
                               uint8_t *dst, int dw, int dh, int dpitch) {
    for (int y = 0; y < dh; y++) {
        int sy = y * sh / dh;
        const uint32_t *srow = (const uint32_t *)(src + sy * spitch);
        uint32_t *drow = (uint32_t *)(dst + y * dpitch);
        for (int x = 0; x < dw; x++) {
            int sx = x * sw / dw;
            drow[x] = srow[sx];
        }
    }
}

void render_end(render_target_t *rt) {
    cairo_surface_flush(rt->draw_surface);
    rt->force_full = false;
    if (fabs(rt->scale - 1.0) < 1e-6)
        return;

    unsigned char *src = cairo_image_surface_get_data(rt->draw_surface);
    int spitch = cairo_image_surface_get_stride(rt->draw_surface);
    blit_scale_nearest(src, rt->draw_w, rt->draw_h, spitch,
                       rt->out_pixels, rt->out_w, rt->out_h, rt->out_pitch);
}
