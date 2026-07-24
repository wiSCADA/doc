#ifndef CAIRO_MULTI_HDMI_BACKEND_H
#define CAIRO_MULTI_HDMI_BACKEND_H

#include "common.h"
#include "drm_output.h"
#include "font.h"
#include "scene.h"

#include <stdbool.h>

typedef enum {
    BACKEND_CAIRO = 0,
    BACKEND_GLES = 1
} backend_id_t;

typedef struct present_policy {
    /*
     * SHARE_CLONE: render shared scene once, present same pixels to all outputs
     *   (best GPU efficiency for identical / template walls).
     * PER_OUTPUT: render per output (only for truly different full UIs).
     */
    enum {
        PRESENT_SHARE_CLONE = 0,
        PRESENT_PER_OUTPUT = 1
    } mode;
    /* If true, draw at logical size then upscale during present (RGA on device). */
    bool upscale_on_present;
    int logical_w;
    int logical_h;
} present_policy_t;

typedef struct render_backend render_backend_t;

struct render_backend {
    backend_id_t id;
    const char *name;
    void *impl;

    int (*init)(render_backend_t *b, const app_config_t *cfg, const app_fonts_t *fonts);
    void (*shutdown)(render_backend_t *b);

    /*
     * Draw one frame for the given policy.
     * outs/out_count are DRM outputs (NULL in offline). Offline backends ignore outs.
     */
    int (*draw_frame)(render_backend_t *b,
                      const app_config_t *cfg,
                      const present_policy_t *policy,
                      const wall_time_t *utc,
                      drm_display_t *disp);
};

backend_id_t backend_parse(const char *s);
present_policy_t present_policy_default(const app_config_t *cfg);

int backend_create(backend_id_t id, render_backend_t *out);
void backend_destroy(render_backend_t *b);

/* Always available. */
int backend_cairo_create(render_backend_t *out);

/* Built only when ENABLE_GLES=ON. */
#if defined(ENABLE_GLES) && ENABLE_GLES
int backend_gles_create(render_backend_t *out);
#else
static inline int backend_gles_create(render_backend_t *out) {
    (void)out;
    return -1;
}
#endif

#endif
