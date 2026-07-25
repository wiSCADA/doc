#ifndef CAIRO_MULTI_HDMI_SCENE_H
#define CAIRO_MULTI_HDMI_SCENE_H

#include "common.h"
#include "font.h"
#include <cairo/cairo.h>
#include <stdbool.h>

typedef struct scene_ctx {
    const app_fonts_t *fonts;
    const wall_time_t *utc;
    int output_index;
    int output_count;
    bool draw_static; /* true when static layer must be rebuilt */
} scene_ctx_t;

void scene_digital_draw(cairo_t *cr, int w, int h, const scene_ctx_t *ctx);
void scene_world_draw(cairo_t *cr, int w, int h, const scene_ctx_t *ctx);
void scene_weather_draw(cairo_t *cr, int w, int h, const scene_ctx_t *ctx);

void scene_draw(scene_id_t id, cairo_t *cr, int w, int h, const scene_ctx_t *ctx);

#endif
