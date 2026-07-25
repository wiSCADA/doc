#ifndef CAIRO_MULTI_HDMI_COMMON_H
#define CAIRO_MULTI_HDMI_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_OUTPUTS 8
#define MAX_CITY_CLOCKS 10

typedef enum {
    SCENE_DIGITAL = 0,
    SCENE_WORLD = 1,
    SCENE_WEATHER = 2,
    SCENE_COUNT
} scene_id_t;

typedef struct {
    int width;
    int height;
    int refresh_hz; /* preferred refresh, e.g. 60 */
} mode_pref_t;

typedef struct {
    char name[64];
    int tz_offset_min; /* minutes east of UTC */
} city_clock_t;

typedef struct {
    /* Prefer 4K@60; falls back to best available. */
    mode_pref_t prefer;
    int max_outputs;          /* e.g. 4 */
    scene_id_t scene;
    int target_fps;           /* UI redraw FPS; display mode can still be 60Hz */
    double render_scale;      /* 1.0 native; 0.5 draw half-res then upscale */
    bool offline;             /* no DRM: render PNG frames */
    const char *offline_dir;
    int offline_frames;
    const char *font_family;
    const char *drm_path;     /* default /dev/dri/card0 */
    const char *backend;      /* "cairo" or "gles" */
    const char *present;      /* "share" (clone) or "per-output" */
} app_config_t;

typedef struct {
    int year, month, day;
    int hour, minute, second;
    int msec;
    int wday; /* 0=Sunday */
} wall_time_t;

void wall_time_utc_now(wall_time_t *out);
void wall_time_apply_offset(const wall_time_t *utc, int offset_min, wall_time_t *out);
void wall_time_format_hms(const wall_time_t *t, char *buf, size_t n);
void wall_time_format_date_cn(const wall_time_t *t, char *buf, size_t n);

#endif
