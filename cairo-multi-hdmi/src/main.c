#include "backend.h"
#include "common.h"
#include "drm_output.h"
#include "font.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_run = 1;

static void on_signal(int sig) {
    (void)sig;
    g_run = 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --backend cairo|gles            render backend (default: cairo)\n"
        "  --present share|per-output      multi-HDMI present policy (default: share)\n"
        "  --scene digital|world|weather   UI scene (default: digital)\n"
        "  --outputs N                     max DRM outputs (default: 4)\n"
        "  --width W --height H --hz R     preferred mode (default: 3840 2160 60)\n"
        "  --fps N                         UI redraw FPS (default: 30)\n"
        "  --scale S                       logical render scale 0.25..1.0 (default: 0.5)\n"
        "  --font NAME                     fontconfig family\n"
        "  --card PATH                     DRM card (default: /dev/dri/card0)\n"
        "  --offline DIR                   no DRM; write PNG frames into DIR\n"
        "  --offline-frames N              frames to write offline (default: 1)\n"
        "  --help\n"
        "\n"
        "GPU-first tip for 4–8 HDMI:\n"
        "  Prefer --backend gles --present share (draw once, RGA/clone to N).\n"
        "  See docs/GPU_ARCHITECTURE.md\n",
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
    cfg->render_scale = 0.5;
    cfg->offline = false;
    cfg->offline_dir = "offline_frames";
    cfg->offline_frames = 1;
    cfg->font_family = NULL;
    cfg->drm_path = "/dev/dri/card0";
    cfg->backend = "cairo";
    cfg->present = "share";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 1;
        } else if (!strcmp(argv[i], "--backend") && i + 1 < argc) {
            cfg->backend = argv[++i];
        } else if (!strcmp(argv[i], "--present") && i + 1 < argc) {
            cfg->present = argv[++i];
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

static void apply_present_cli(const app_config_t *cfg, present_policy_t *p) {
    if (cfg->present && !strcmp(cfg->present, "per-output"))
        p->mode = PRESENT_PER_OUTPUT;
    else
        p->mode = PRESENT_SHARE_CLONE;
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

    render_backend_t backend;
    if (backend_create(backend_parse(cfg.backend), &backend) != 0) {
        fonts_shutdown(&fonts);
        return 1;
    }
    if (backend.init(&backend, &cfg, &fonts) != 0) {
        fprintf(stderr, "backend init failed\n");
        backend_destroy(&backend);
        fonts_shutdown(&fonts);
        return 1;
    }

    present_policy_t policy = present_policy_default(&cfg);
    apply_present_cli(&cfg, &policy);

    fprintf(stderr,
            "backend=%s present=%s scene=%d outputs<=%d mode=%dx%d@%d ui_fps=%d scale=%.2f logical=%dx%d\n",
            backend.name,
            policy.mode == PRESENT_SHARE_CLONE ? "share" : "per-output",
            (int)cfg.scene, cfg.max_outputs,
            cfg.prefer.width, cfg.prefer.height, cfg.prefer.refresh_hz,
            cfg.target_fps, cfg.render_scale, policy.logical_w, policy.logical_h);

    int rc = 0;
    if (cfg.offline) {
        for (int f = 0; f < cfg.offline_frames && g_run; f++) {
            wall_time_t utc;
            wall_time_utc_now(&utc);
            utc.second = (utc.second + f) % 60;
            if (backend.draw_frame(&backend, &cfg, &policy, &utc, NULL) != 0) {
                rc = -1;
                break;
            }
        }
    } else {
        drm_display_t disp;
        if (drm_display_open(&disp, cfg.drm_path, cfg.max_outputs, &cfg.prefer) != 0) {
            rc = -1;
        } else {
            const double frame_ns = 1e9 / (double)cfg.target_fps;
            while (g_run) {
                struct timespec ts0;
                clock_gettime(CLOCK_MONOTONIC, &ts0);

                wall_time_t utc;
                wall_time_utc_now(&utc);
                if (backend.draw_frame(&backend, &cfg, &policy, &utc, &disp) != 0) {
                    rc = -1;
                    break;
                }

                struct timespec ts1;
                clock_gettime(CLOCK_MONOTONIC, &ts1);
                double elapsed = (ts1.tv_sec - ts0.tv_sec) * 1e9 + (ts1.tv_nsec - ts0.tv_nsec);
                if (elapsed < frame_ns) {
                    struct timespec req = { .tv_sec = 0, .tv_nsec = (long)(frame_ns - elapsed) };
                    nanosleep(&req, NULL);
                }
            }
            drm_display_close(&disp);
        }
    }

    backend_destroy(&backend);
    fonts_shutdown(&fonts);
    return rc == 0 ? 0 : 1;
}
