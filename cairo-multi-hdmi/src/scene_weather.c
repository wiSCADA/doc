#include "scene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    if (r < 1) r = 1;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

static void draw_moon(cairo_t *cr, double cx, double cy, double r) {
    cairo_new_path(cr);
    cairo_set_source_rgb(cr, 1.0, 0.85, 0.2);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.12, 0.16, 0.28);
    cairo_arc(cr, cx + r * 0.35, cy - r * 0.1, r * 0.78, 0, 2 * M_PI);
    cairo_fill(cr);
}

void scene_weather_draw(cairo_t *cr, int w, int h, const scene_ctx_t *ctx) {
    /* Night gradient background */
    cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, h);
    cairo_pattern_add_color_stop_rgb(bg, 0.0, 0.05, 0.08, 0.18);
    cairo_pattern_add_color_stop_rgb(bg, 1.0, 0.02, 0.03, 0.08);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);

    double card_w = w * 0.42;
    double card_h = h * 0.72;
    double card_x = (w - card_w) * 0.5;
    double card_y = (h - card_h) * 0.5;
    double rr = card_w * 0.04;

    rounded_rect(cr, card_x, card_y, card_w, card_h, rr);
    cairo_set_source_rgba(cr, 0.10, 0.14, 0.24, 0.92);
    cairo_fill(cr);

    /* Header */
    font_set(cr, ctx->fonts, card_h * 0.045, true);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, card_x + card_w * 0.06, card_y + card_h * 0.08);
    cairo_show_text(cr, "西安市");

    /* Current */
    draw_moon(cr, card_x + card_w * 0.18, card_y + card_h * 0.26, card_h * 0.08);

    wall_time_t local;
    wall_time_apply_offset(ctx->utc, 480, &local);
    char temp[32];
    snprintf(temp, sizeof(temp), "%d°", 28 + (local.hour % 5));

    font_set(cr, ctx->fonts, card_h * 0.14, true);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, card_x + card_w * 0.36, card_y + card_h * 0.30);
    cairo_show_text(cr, temp);

    font_set(cr, ctx->fonts, card_h * 0.04, false);
    cairo_set_source_rgb(cr, 0.85, 0.88, 0.95);
    cairo_move_to(cr, card_x + card_w * 0.36, card_y + card_h * 0.38);
    cairo_show_text(cr, "晴朗");

    /* Tabs */
    double tab_y = card_y + card_h * 0.48;
    font_set(cr, ctx->fonts, card_h * 0.035, true);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, card_x + card_w * 0.08, tab_y);
    cairo_show_text(cr, "逐小时");
    cairo_set_source_rgb(cr, 0.55, 0.60, 0.70);
    cairo_move_to(cr, card_x + card_w * 0.28, tab_y);
    cairo_show_text(cr, "逐日");
    cairo_set_source_rgb(cr, 0.2, 0.85, 0.85);
    cairo_set_line_width(cr, 3);
    cairo_move_to(cr, card_x + card_w * 0.08, tab_y + card_h * 0.02);
    cairo_line_to(cr, card_x + card_w * 0.20, tab_y + card_h * 0.02);
    cairo_stroke(cr);

    /* Hourly row */
    double row_y = card_y + card_h * 0.55;
    double cell_w = card_w * 0.16;
    double cell_h = card_h * 0.28;
    double start_x = card_x + card_w * 0.08;
    for (int i = 0; i < 5; i++) {
        double x = start_x + i * (cell_w + card_w * 0.02);
        rounded_rect(cr, x, row_y, cell_w, cell_h, cell_w * 0.12);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
        cairo_fill(cr);

        char hh[16];
        snprintf(hh, sizeof(hh), "%02d:00", (local.hour + i) % 24);
        font_set(cr, ctx->fonts, card_h * 0.028, false);
        cairo_set_source_rgb(cr, 0.8, 0.85, 0.95);
        font_draw_centered(cr, hh, x + cell_w * 0.5, row_y + cell_h * 0.18);

        draw_moon(cr, x + cell_w * 0.5, row_y + cell_h * 0.45, cell_h * 0.12);

        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%d°", 30 - i);
        font_set(cr, ctx->fonts, card_h * 0.032, true);
        cairo_set_source_rgb(cr, 1, 1, 1);
        font_draw_centered(cr, tbuf, x + cell_w * 0.5, row_y + cell_h * 0.72);

        char pbuf[16];
        snprintf(pbuf, sizeof(pbuf), "%d%%", 1 + i);
        font_set(cr, ctx->fonts, card_h * 0.024, false);
        cairo_set_source_rgb(cr, 0.45, 0.75, 1.0);
        font_draw_centered(cr, pbuf, x + cell_w * 0.5, row_y + cell_h * 0.88);
    }

    /* Footer button */
    double bx = card_x + card_w * 0.55;
    double by = card_y + card_h * 0.88;
    double bw = card_w * 0.38;
    double bh = card_h * 0.07;
    rounded_rect(cr, bx, by, bw, bh, bh * 0.5);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_fill(cr);
    font_set(cr, ctx->fonts, card_h * 0.028, false);
    cairo_set_source_rgb(cr, 1, 1, 1);
    font_draw_centered(cr, "查看完整预报", bx + bw * 0.5, by + bh * 0.55);

    char tag[64];
    snprintf(tag, sizeof(tag), "Weather demo · HDMI#%d", ctx->output_index);
    font_set(cr, ctx->fonts, h * 0.02, false);
    cairo_set_source_rgb(cr, 0.6, 0.7, 0.85);
    font_draw_centered(cr, tag, w * 0.5, h * 0.04);
}
