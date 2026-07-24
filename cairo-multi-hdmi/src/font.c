#include "font.h"

#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <string.h>

static const char *k_candidates[] = {
    "WenQuanYi Micro Hei",
    "Noto Sans CJK SC",
    "Noto Sans SC",
    "Droid Sans Fallback",
    "Source Han Sans SC",
    "DejaVu Sans",
    "sans-serif",
    NULL
};

int fonts_init(app_fonts_t *f, const char *preferred_family) {
    memset(f, 0, sizeof(*f));
    if (!FcInit()) {
        fprintf(stderr, "fontconfig init failed\n");
        return -1;
    }

    const char *pick = preferred_family && preferred_family[0] ? preferred_family : NULL;
    if (pick) {
        FcPattern *pat = FcNameParse((const FcChar8 *)pick);
        FcConfigSubstitute(NULL, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult result;
        FcPattern *match = FcFontMatch(NULL, pat, &result);
        FcPatternDestroy(pat);
        if (match) {
            FcChar8 *family = NULL;
            if (FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch) {
                snprintf(f->family, sizeof(f->family), "%s", (const char *)family);
                f->ok = true;
            }
            FcPatternDestroy(match);
        }
    }

    if (!f->ok) {
        for (int i = 0; k_candidates[i]; i++) {
            FcPattern *pat = FcNameParse((const FcChar8 *)k_candidates[i]);
            FcConfigSubstitute(NULL, pat, FcMatchPattern);
            FcDefaultSubstitute(pat);
            FcResult result;
            FcPattern *match = FcFontMatch(NULL, pat, &result);
            FcPatternDestroy(pat);
            if (!match)
                continue;
            FcChar8 *file = NULL;
            if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
                FcChar8 *family = NULL;
                FcPatternGetString(match, FC_FAMILY, 0, &family);
                snprintf(f->family, sizeof(f->family), "%s",
                         family ? (const char *)family : k_candidates[i]);
                f->ok = true;
                fprintf(stderr, "font: %s (%s)\n", f->family, (const char *)file);
                FcPatternDestroy(match);
                break;
            }
            FcPatternDestroy(match);
        }
    }

    if (!f->ok) {
        snprintf(f->family, sizeof(f->family), "sans-serif");
        f->ok = true;
        fprintf(stderr, "font: fallback sans-serif\n");
    }
    return 0;
}

void fonts_shutdown(app_fonts_t *f) {
    (void)f;
    FcFini();
}

void font_set(cairo_t *cr, const app_fonts_t *f, double size_px, bool bold) {
    cairo_select_font_face(cr, f->family,
                           CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size_px);
}

void font_text_extents(cairo_t *cr, const char *utf8, double *w, double *h) {
    cairo_text_extents_t ext;
    cairo_text_extents(cr, utf8, &ext);
    if (w) *w = ext.width;
    if (h) *h = ext.height;
}

void font_draw_centered(cairo_t *cr, const char *utf8, double cx, double cy) {
    cairo_text_extents_t ext;
    cairo_text_extents(cr, utf8, &ext);
    double x = cx - (ext.width / 2.0 + ext.x_bearing);
    double y = cy - (ext.height / 2.0 + ext.y_bearing);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, utf8);
}
