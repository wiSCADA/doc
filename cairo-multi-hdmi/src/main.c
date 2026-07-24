#include "common.h"
#include "drm_output.h"
#include "renderer.h"
#include "font.h"
#include "scene.h"

#include <cairo/cairo.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

static volatile sig_atomic_t g_run = 1;

static void on_signal(int sig) {
    (void)sig;
    g_run = 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --scene digital|world|weather   UI scene (default: digital)\n"
        "  --outputs N                     max DRM outputs (default: 4)\n"
        "  --width W --height H --hz R     preferred mode (default: 3840 2160 60)\n"
        "  --fps N                         UI redraw FPS (default: 30)\n"
        "  --scale S                       render scale 0.25..1.0 (default: 0.5 for 4K)\n"
        "  --font NAME                     fontconfig family\n"
        "  --card PATH                     DRM card (default: /dev/dri/card0)\n"
        "  --offline DIR                   no DRM; write PNG frames into DIR\n"
        "  --offline-frames N              frames to write offline (default: 3)\n"
        "  --help\n"
        "\n"
        "Notes:\n"
        "  Display mode can be 4K@60 while UI redraw uses --fps.\n"
        "  For 4x4K, prefer --scale 0.5 (or RGA upscale on device).\n",
        argv0);
}

static scene_id_t parse_scene(const char *s) {
    if (!s) return SCENE_DIGITAL;
    if (!strcmp(s, "world")) return SCENE_WORLD;
    if (!strcmp(s, "weather")) return SCENE_WEATHER;
    return SCENE_DIGITAL;
}

static int parse_args(int argc, char **argv, app_config_t *cfg) {
    cfg->prefer.width = 3840;
    cfg->prefer.height = 2160;
    cfg->prefer.refresh_hz = 60;
    cfg->max_outputs = 4;
    cfg->scene = SCENE_DIGITAL;
    cfg->target_fps = 30;
    cfg->render_scale = 0.5; /* practical default for multi-4K CPU draw */
    cfg->offline = false;
    cfg->offline_dir = "offline_frames";
    cfg->offline_frames = 3;
    cfg->font_family = NULL;
    cfg->drm_path = "/dev/dri/card0";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 1;
        } else if (!strcmp(argv[i], "--scene") && i + 1 < argc) {
            cfg->scene = parse_scene(argv[++i]);
        } else if (!strcmp(argv[i], "--outputs") && i + 1 < argc) {
            cfg->max_outputs = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--width") && i + 1 < argc) {
            cfg->prefer.width = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--height") && i + 1 < argc) {
            cfg->prefer.height = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--hz") && i + 1 < argc) {
            cfg->prefer.refresh_hz = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
            cfg->target_fps = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            cfg->render_scale = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--font") && i + 1 < argc) {
            cfg->font_family = argv[++i];
        } else if (!strcmp(argv[i], "--card") && i + 1 < argc) {
            cfg->drm_path = argv[++i];
        } else if (!strcmp(argv[i], "--offline") && i + 1 < argc) {
            cfg->offline = true;
            cfg->offline_dir = argv[++i];
        } else if (!strcmp(argv[i], "--offline-frames") && i + 1 < argc) {
            cfg->offline_frames = atoi(argv[++i]);
        } else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            usage(argv[0]);
            return -1;
        }
    }
    if (cfg->max_outputs < 1) cfg->max_outputs = 1;
    if (cfg->max_outputs > MAX_OUTPUTS) cfg->max_outputs = MAX_OUTPUTS;
    if (cfg->target_fps < 1) cfg->target_fps = 1;
    if (cfg->target_fps > 60) cfg->target_fps = 60;
    if (cfg->render_scale < 0.25) cfg->render_scale = 0.25;
    if (cfg->render_scale > 1.0) cfg->render_scale = 1.0;
    return 0;
}

typedef struct offline_out {
    int width, height, pitch;
    uint8_t *pixels;
} offline_out_t;

static int mkdir_p(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;
    if (mkdir(path, 0755) == 0)
        return 0;
    return -1;
}

static int run_offline(const app_config_t *cfg, const app_fonts_t *fonts) {
    if (mkdir_p(cfg->offline_dir) != 0) {
        perror(cfg->offline_dir);
        return -1;
    }

    int n = cfg->max_outputs;
    offline_out_t outs[MAX_OUTPUTS];
    render_target_t rts[MAX_OUTPUTS];
    memset(outs, 0, sizeof(outs));
    memset(rts, 0, sizeof(rts));

    for (int i = 0; i < n; i++) {
        outs[i].width = cfg->prefer.width;
        outs[i].height = cfg->prefer.height;
        outs[i].pitch = outs[i].width * 4;
        size_t bytes = (size_t)outs[i].pitch * (size_t)outs[i].height;
        outs[i].pixels = calloc(1, bytes);
        if (!outs[i].pixels)
            return -1;
        if (render_target_init(&rts[i], outs[i].width, outs[i].height,
                               cfg->render_scale, outs[i].pixels, outs[i].pitch) != 0) {
            fprintf(stderr, "render_target_init failed\n");
            return -1;
        }
    }

    for (int f = 0; f < cfg->offline_frames && g_run; f++) {
        wall_time_t utc;
        wall_time_utc_now(&utc);
        utc.second = (utc.second + f) % 60;

        for (int i = 0; i < n; i++) {
            scene_ctx_t ctx = {
                .fonts = fonts,
                .utc = &utc,
                .output_index = i,
                .output_count = n,
                .draw_static = true,
            };
            cairo_t *cr = render_begin(&rts[i], true);
            scene_draw(cfg->scene, cr, rts[i].draw_w, rts[i].draw_h, &ctx);
            render_end(&rts[i]);

            char path[512];
            snprintf(path, sizeof(path), "%s/out%d_frame%02d.png",
                     cfg->offline_dir, i, f);
            cairo_surface_t *png = cairo_image_surface_create_for_data(
                outs[i].pixels, CAIRO_FORMAT_RGB24,
                outs[i].width, outs[i].height, outs[i].pitch);
            cairo_status_t st = cairo_surface_write_to_png(png, path);
            cairo_surface_destroy(png);
            if (st != CAIRO_STATUS_SUCCESS) {
                fprintf(stderr, "write png failed: %s\n", path);
            } else {
                fprintf(stderr, "wrote %s\n", path);
            }
        }
    }

    for (int i = 0; i < n; i++) {
        render_target_destroy(&rts[i]);
        free(outs[i].pixels);
    }
    return 0;
}

typedef struct out_runtime {
    drm_output_t *out;
    render_target_t rt;
    int index;
} out_runtime_t;

static int run_drm(const app_config_t *cfg, const app_fonts_t *fonts) {
    drm_display_t disp;
    if (drm_display_open(&disp, cfg->drm_path, cfg->max_outputs, &cfg->prefer) != 0)
        return -1;

    out_runtime_t rts[MAX_OUTPUTS];
    memset(rts, 0, sizeof(rts));

    for (int i = 0; i < disp.count; i++) {
        rts[i].out = &disp.outs[i];
        rts[i].index = i;
        int pitch = 0;
        uint8_t *map = drm_output_back_map(rts[i].out, &pitch);
        if (render_target_init(&rts[i].rt, rts[i].out->width, rts[i].out->height,
                               cfg->render_scale, map, pitch) != 0) {
            fprintf(stderr, "render init failed on output %d\n", i);
            drm_display_close(&disp);
            return -1;
        }
    }

    fprintf(stderr, "running %d output(s), scene=%d, ui_fps=%d, scale=%.2f\n",
            disp.count, (int)cfg->scene, cfg->target_fps, cfg->render_scale);

    const double frame_ns = 1e9 / (double)cfg->target_fps;

    while (g_run) {
        struct timespec ts0;
        clock_gettime(CLOCK_MONOTONIC, &ts0);

        wall_time_t utc;
        wall_time_utc_now(&utc);

        for (int i = 0; i < disp.count; i++) {
            int pitch = 0;
            uint8_t *map = drm_output_back_map(rts[i].out, &pitch);
            if (render_target_rebind(&rts[i].rt, map, pitch) != 0) {
                fprintf(stderr, "rebind render failed\n");
                g_run = 0;
                break;
            }

            scene_ctx_t ctx = {
                .fonts = fonts,
                .utc = &utc,
                .output_index = i,
                .output_count = disp.count,
                .draw_static = true,
            };
            cairo_t *cr = render_begin(&rts[i].rt, true);
            scene_draw(cfg->scene, cr, rts[i].rt.draw_w, rts[i].rt.draw_h, &ctx);
            render_end(&rts[i].rt);
            drm_output_flip(&disp, rts[i].out);
        }

        struct timespec ts1;
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        double elapsed = (ts1.tv_sec - ts0.tv_sec) * 1e9 + (ts1.tv_nsec - ts0.tv_nsec);
        if (elapsed < frame_ns) {
            struct timespec req = {
                .tv_sec = 0,
                .tv_nsec = (long)(frame_ns - elapsed)
            };
            nanosleep(&req, NULL);
        }
    }

    for (int i = 0; i < disp.count; i++)
        render_target_destroy(&rts[i].rt);
    drm_display_close(&disp);
    return 0;
}

int main(int argc, char **argv) {
    app_config_t cfg;
    int pr = parse_args(argc, argv, &cfg);
    if (pr == 1)
        return 0;
    if (pr < 0)
        return 2;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    app_fonts_t fonts;
    if (fonts_init(&fonts, cfg.font_family) != 0)
        return 1;

    int rc;
    if (cfg.offline)
        rc = run_offline(&cfg, &fonts);
    else
        rc = run_drm(&cfg, &fonts);

    fonts_shutdown(&fonts);
    return rc == 0 ? 0 : 1;
}
