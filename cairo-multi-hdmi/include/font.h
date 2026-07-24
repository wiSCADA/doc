#ifndef CAIRO_MULTI_HDMI_FONT_H
#define CAIRO_MULTI_HDMI_FONT_H

#include <cairo/cairo.h>
#include <stdbool.h>

typedef struct app_fonts {
    char family[128];
    bool ok;
} app_fonts_t;

int fonts_init(app_fonts_t *f, const char *preferred_family);
void fonts_shutdown(app_fonts_t *f);

void font_set(cairo_t *cr, const app_fonts_t *f, double size_px, bool bold);
void font_text_extents(cairo_t *cr, const char *utf8, double *w, double *h);
void font_draw_centered(cairo_t *cr, const char *utf8, double cx, double cy);

#endif
