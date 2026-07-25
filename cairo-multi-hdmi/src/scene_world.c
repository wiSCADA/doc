#include "scene.h"

#include <math.h>
#include <stdio.h>

static const city_clock_t k_cities[MAX_CITY_CLOCKS] = {
    {"Washington D.C.", -300},
    {"New York", -300},
    {"Rio de Janeiro", -180},
    {"London", 0},
    {"Rome", 60},
    {"Cairo", 120},
    {"Moscow", 180},
    {"Beijing", 480},
    {"Singapore", 480},
    {"Tokyo", 540},
};

static void draw_analog_clock(cairo_t *cr, double cx, double cy, double r,
                              const wall_time_t *t, const app_fonts_t *fonts,
                              const char *name) {
    font_set(cr, fonts, r * 0.22, true);
    cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
    font_draw_centered(cr, name, cx, cy - r * 1.28);

    cairo_new_path(cr);
    cairo_set_line_width(cr, r * 0.035);
    cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_stroke(cr);

    for (int i = 0; i < 60; i++) {
        double a = i * (2 * M_PI / 60.0) - M_PI / 2.0;
        double inner = (i % 5 == 0) ? r * 0.82 : r * 0.90;
        double outer = r * 0.96;
        cairo_set_line_width(cr, (i % 5 == 0) ? r * 0.03 : r * 0.012);
        cairo_move_to(cr, cx + cos(a) * inner, cy + sin(a) * inner);
        cairo_line_to(cr, cx + cos(a) * outer, cy + sin(a) * outer);
        cairo_stroke(cr);
    }

    double sec = t->second + t->msec / 1000.0;
    double min = t->minute + sec / 60.0;
    double hour = (t->hour % 12) + min / 60.0;

    double ah = hour * (2 * M_PI / 12.0) - M_PI / 2.0;
    double am = min * (2 * M_PI / 60.0) - M_PI / 2.0;
    double as = sec * (2 * M_PI / 60.0) - M_PI / 2.0;

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
    cairo_set_line_width(cr, r * 0.06);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx + cos(ah) * r * 0.55, cy + sin(ah) * r * 0.55);
    cairo_stroke(cr);

    cairo_set_line_width(cr, r * 0.045);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx + cos(am) * r * 0.75, cy + sin(am) * r * 0.75);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.85, 0.05, 0.05);
    cairo_set_line_width(cr, r * 0.02);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx + cos(as) * r * 0.88, cy + sin(as) * r * 0.88);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
    cairo_arc(cr, cx, cy, r * 0.04, 0, 2 * M_PI);
    cairo_fill(cr);
}

void scene_world_draw(cairo_t *cr, int w, int h, const scene_ctx_t *ctx) {
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    const int cols = 5;
    const int rows = 2;
    double margin_x = w * 0.04;
    double margin_y = h * 0.08;
    double cell_w = (w - margin_x * 2) / cols;
    double cell_h = (h - margin_y * 2) / rows;
    double radius = (cell_w < cell_h ? cell_w : cell_h) * 0.28;

    for (int i = 0; i < MAX_CITY_CLOCKS; i++) {
        int row = i / cols;
        int col = i % cols;
        double cx = margin_x + cell_w * (col + 0.5);
        double cy = margin_y + cell_h * (row + 0.55);
        wall_time_t local;
        wall_time_apply_offset(ctx->utc, k_cities[i].tz_offset_min, &local);
        draw_analog_clock(cr, cx, cy, radius, &local, ctx->fonts, k_cities[i].name);
    }

    char tag[64];
    snprintf(tag, sizeof(tag), "World Clock  ·  output %d/%d",
             ctx->output_index + 1, ctx->output_count);
    font_set(cr, ctx->fonts, h * 0.025, false);
    cairo_set_source_rgb(cr, 0.25, 0.25, 0.25);
    font_draw_centered(cr, tag, w * 0.5, h * 0.035);
}
