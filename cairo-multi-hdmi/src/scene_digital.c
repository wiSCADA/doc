#include "scene.h"

#include <stdio.h>

void scene_digital_draw(cairo_t *cr, int w, int h, const scene_ctx_t *ctx) {
    wall_time_t local;
    /* Output index selects timezone demo offsets. */
    static const int offsets[] = { 480, 0, -300, 60, 540, 120, -480, 330 };
    int off = offsets[ctx->output_index % 8];
    wall_time_apply_offset(ctx->utc, off, &local);

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    char hms[32];
    char date[128];
    wall_time_format_hms(&local, hms, sizeof(hms));
    wall_time_format_date_cn(&local, date, sizeof(date));

    double clock_size = h * 0.22;
    if (clock_size < 48)
        clock_size = 48;
    font_set(cr, ctx->fonts, clock_size, true);
    cairo_set_source_rgb(cr, 1, 1, 1);
    font_draw_centered(cr, hms, w * 0.5, h * 0.42);

    font_set(cr, ctx->fonts, h * 0.035, false);
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
    font_draw_centered(cr, date, w * 0.5, h * 0.58);

    char tag[64];
    snprintf(tag, sizeof(tag), "HDMI#%d  UTC%+d", ctx->output_index, off / 60);
    font_set(cr, ctx->fonts, h * 0.022, false);
    cairo_set_source_rgb(cr, 0.45, 0.75, 1.0);
    font_draw_centered(cr, tag, w * 0.5, h * 0.68);
}
