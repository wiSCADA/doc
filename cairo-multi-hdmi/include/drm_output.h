#ifndef CAIRO_MULTI_HDMI_DRM_OUTPUT_H
#define CAIRO_MULTI_HDMI_DRM_OUTPUT_H

#include "common.h"
#include <stdint.h>
#include <stdbool.h>
#include <xf86drmMode.h>

typedef struct drm_fb {
    uint32_t id;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    uint8_t *map;
    int width;
    int height;
} drm_fb_t;

typedef struct drm_output {
    uint32_t connector_id;
    uint32_t crtc_id;
    uint32_t encoder_id;
    int connector_type;
    char name[64];
    int width;
    int height;
    int refresh_hz;
    drmModeModeInfo mode;
    drm_fb_t fbs[2];
    int front; /* index currently displayed */
    bool ok;
} drm_output_t;

typedef struct drm_display {
    int fd;
    drm_output_t outs[MAX_OUTPUTS];
    int count;
} drm_display_t;

/* Open card, claim up to max_outputs connectors, prefer 3840x2160@60. */
int drm_display_open(drm_display_t *d, const char *path, int max_outputs,
                     const mode_pref_t *prefer);

void drm_display_close(drm_display_t *d);

/* Map back buffer pixels for Cairo (XRGB8888). */
uint8_t *drm_output_back_map(drm_output_t *o, int *pitch);

/* Page-flip back -> front. Returns 0 on success. */
int drm_output_flip(drm_display_t *d, drm_output_t *o);

#endif
