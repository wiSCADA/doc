#ifndef CAIRO_MULTI_HDMI_RENDERER_H
#define CAIRO_MULTI_HDMI_RENDERER_H

#include "common.h"
#include <cairo/cairo.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct render_target {
    int out_w;
    int out_h;
    int draw_w;
    int draw_h;
    double scale;          /* draw -> out */
    uint8_t *out_pixels;   /* XRGB8888, size out_w * pitch */
    int out_pitch;
    cairo_surface_t *draw_surface; /* may be scaled */
    cairo_t *cr;
    /* Cached static layer (draw resolution). */
    cairo_surface_t *static_layer;
    bool static_valid;
    bool force_full;
} render_target_t;

int render_target_init(render_target_t *rt, int out_w, int out_h, double scale,
                       uint8_t *out_pixels, int out_pitch);
void render_target_destroy(render_target_t *rt);

/* After buffer flip, point to the new back buffer. */
int render_target_rebind(render_target_t *rt, uint8_t *out_pixels, int out_pitch);

/* Begin frame: clear dynamic path; keep static cache unless force_full. */
cairo_t *render_begin(render_target_t *rt, bool redraw_static);

/* Finish: composite static+dynamic and upscale into out_pixels if needed. */
void render_end(render_target_t *rt);

void render_invalidate_static(render_target_t *rt);

#endif
