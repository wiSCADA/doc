#include "backend.h"

#include <stdio.h>
#include <string.h>

backend_id_t backend_parse(const char *s) {
    if (s && !strcmp(s, "gles"))
        return BACKEND_GLES;
    return BACKEND_CAIRO;
}

present_policy_t present_policy_default(const app_config_t *cfg) {
    present_policy_t p = {
        .mode = PRESENT_SHARE_CLONE,
        .upscale_on_present = true,
        .logical_w = 1920,
        .logical_h = 1080,
    };
    /* If user forces scale==1 and native 4K draw, keep logical = prefer. */
    if (cfg->render_scale >= 0.999) {
        p.logical_w = cfg->prefer.width;
        p.logical_h = cfg->prefer.height;
        p.upscale_on_present = false;
    } else if (cfg->prefer.width > 0 && cfg->prefer.height > 0) {
        p.logical_w = (int)(cfg->prefer.width * cfg->render_scale + 0.5);
        p.logical_h = (int)(cfg->prefer.height * cfg->render_scale + 0.5);
        if (p.logical_w < 640) p.logical_w = 640;
        if (p.logical_h < 360) p.logical_h = 360;
    }
    return p;
}

int backend_create(backend_id_t id, render_backend_t *out) {
    if (id == BACKEND_GLES) {
#if defined(ENABLE_GLES) && ENABLE_GLES
        if (backend_gles_create(out) == 0)
            return 0;
        fprintf(stderr, "GLES backend unavailable, falling back to cairo\n");
#else
        fprintf(stderr, "GLES backend not compiled (cmake -DENABLE_GLES=ON). Using cairo.\n");
#endif
        return backend_cairo_create(out);
    }
    return backend_cairo_create(out);
}

void backend_destroy(render_backend_t *b) {
    if (!b)
        return;
    if (b->shutdown)
        b->shutdown(b);
    memset(b, 0, sizeof(*b));
}
