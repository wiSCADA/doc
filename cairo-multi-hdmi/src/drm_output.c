#include "drm_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

static int create_fb(int fd, int w, int h, drm_fb_t *fb) {
    memset(fb, 0, sizeof(*fb));
    struct drm_mode_create_dumb creq = {
        .width = (uint32_t)w,
        .height = (uint32_t)h,
        .bpp = 32,
    };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) != 0) {
        perror("CREATE_DUMB");
        return -1;
    }
    fb->handle = creq.handle;
    fb->pitch = creq.pitch;
    fb->size = creq.size;
    fb->width = w;
    fb->height = h;

    if (drmModeAddFB(fd, w, h, 24, 32, fb->pitch, fb->handle, &fb->id) != 0) {
        perror("drmModeAddFB");
        struct drm_mode_destroy_dumb dreq = { .handle = fb->handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        return -1;
    }

    struct drm_mode_map_dumb mreq = { .handle = fb->handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
        perror("MAP_DUMB");
        return -1;
    }
    fb->map = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);
    if (fb->map == MAP_FAILED) {
        perror("mmap fb");
        fb->map = NULL;
        return -1;
    }
    memset(fb->map, 0, fb->size);
    return 0;
}

static void destroy_fb(int fd, drm_fb_t *fb) {
    if (!fb->id && !fb->handle)
        return;
    if (fb->map && fb->map != MAP_FAILED)
        munmap(fb->map, fb->size);
    if (fb->id)
        drmModeRmFB(fd, fb->id);
    if (fb->handle) {
        struct drm_mode_destroy_dumb dreq = { .handle = fb->handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    }
    memset(fb, 0, sizeof(*fb));
}

static int mode_score(const drmModeModeInfo *m, const mode_pref_t *prefer) {
    int score = 0;
    int dw = abs((int)m->hdisplay - prefer->width);
    int dh = abs((int)m->vdisplay - prefer->height);
    score += 100000 - (dw + dh) * 10;
    if (m->hdisplay == (uint16_t)prefer->width && m->vdisplay == (uint16_t)prefer->height)
        score += 50000;
    int refresh = (int)m->vrefresh;
    if (refresh <= 0) {
        /* rough estimate */
        refresh = (int)((m->clock * 1000.0) / (m->htotal * (double)m->vtotal) + 0.5);
    }
    score += 1000 - abs(refresh - prefer->refresh_hz) * 5;
    if (refresh == prefer->refresh_hz)
        score += 2000;
    if (m->type & DRM_MODE_TYPE_PREFERRED)
        score += 100;
    return score;
}

static const char *connector_name(uint32_t type, int id) {
    static char buf[64];
    const char *t = "Unknown";
    switch (type) {
    case DRM_MODE_CONNECTOR_HDMIA: t = "HDMI-A"; break;
    case DRM_MODE_CONNECTOR_HDMIB: t = "HDMI-B"; break;
    case DRM_MODE_CONNECTOR_DisplayPort: t = "DP"; break;
    case DRM_MODE_CONNECTOR_eDP: t = "eDP"; break;
    case DRM_MODE_CONNECTOR_DSI: t = "DSI"; break;
    case DRM_MODE_CONNECTOR_DPI: t = "DPI"; break;
    default: break;
    }
    snprintf(buf, sizeof(buf), "%s-%d", t, id);
    return buf;
}

static int pick_crtc(drmModeRes *res, drmModeConnector *conn, drmModeEncoder *enc,
                     const uint32_t *used_crtcs, int used_count) {
    if (enc && enc->crtc_id) {
        for (int i = 0; i < used_count; i++)
            if (used_crtcs[i] == enc->crtc_id)
                goto find_free;
        return (int)enc->crtc_id;
    }
find_free:
    for (int i = 0; i < res->count_crtcs; i++) {
        uint32_t crtc = res->crtcs[i];
        int taken = 0;
        for (int u = 0; u < used_count; u++)
            if (used_crtcs[u] == crtc) { taken = 1; break; }
        if (taken)
            continue;
        if (enc && !(enc->possible_crtcs & (1u << i)))
            continue;
        /* If no encoder yet, allow any free CRTC; encoder will be set later. */
        if (!enc) {
            /* match connector possible encoders' possible_crtcs */
            for (int e = 0; e < conn->count_encoders; e++) {
                /* defer check below */
            }
        }
        return (int)crtc;
    }
    return -1;
}

int drm_display_open(drm_display_t *d, const char *path, int max_outputs,
                     const mode_pref_t *prefer) {
    memset(d, 0, sizeof(*d));
    d->fd = -1;
    if (max_outputs <= 0 || max_outputs > MAX_OUTPUTS)
        max_outputs = MAX_OUTPUTS;

    mode_pref_t pref = { .width = 3840, .height = 2160, .refresh_hz = 60 };
    if (prefer)
        pref = *prefer;

    const char *card = path && path[0] ? path : "/dev/dri/card0";
    int fd = open(card, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror(card);
        return -1;
    }
    d->fd = fd;

    if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
        /* optional */
    }
    if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        fprintf(stderr, "note: atomic modeset not available, using legacy\n");
    }

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        perror("drmModeGetResources");
        close(fd);
        d->fd = -1;
        return -1;
    }

    uint32_t used_crtcs[MAX_OUTPUTS];
    int used_count = 0;

    for (int i = 0; i < res->count_connectors && d->count < max_outputs; i++) {
        drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
        if (!conn)
            continue;
        if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
            drmModeFreeConnector(conn);
            continue;
        }

        int best = -1, best_score = -1000000000;
        for (int m = 0; m < conn->count_modes; m++) {
            int s = mode_score(&conn->modes[m], &pref);
            if (s > best_score) {
                best_score = s;
                best = m;
            }
        }
        if (best < 0) {
            drmModeFreeConnector(conn);
            continue;
        }
        drmModeModeInfo mode = conn->modes[best];

        drmModeEncoder *enc = NULL;
        if (conn->encoder_id)
            enc = drmModeGetEncoder(fd, conn->encoder_id);
        if (!enc && conn->count_encoders > 0)
            enc = drmModeGetEncoder(fd, conn->encoders[0]);

        int crtc_id = pick_crtc(res, conn, enc, used_crtcs, used_count);
        if (crtc_id < 0) {
            fprintf(stderr, "no free CRTC for connector %u\n", conn->connector_id);
            if (enc) drmModeFreeEncoder(enc);
            drmModeFreeConnector(conn);
            continue;
        }

        /* Validate CRTC against encoder possible_crtcs when possible. */
        if (enc) {
            int crtc_index = -1;
            for (int c = 0; c < res->count_crtcs; c++)
                if (res->crtcs[c] == (uint32_t)crtc_id) { crtc_index = c; break; }
            if (crtc_index >= 0 && !(enc->possible_crtcs & (1u << crtc_index))) {
                /* try another */
                if (enc) drmModeFreeEncoder(enc);
                drmModeFreeConnector(conn);
                continue;
            }
        }

        drm_output_t *o = &d->outs[d->count];
        memset(o, 0, sizeof(*o));
        o->connector_id = conn->connector_id;
        o->crtc_id = (uint32_t)crtc_id;
        o->encoder_id = enc ? enc->encoder_id : 0;
        o->connector_type = conn->connector_type;
        snprintf(o->name, sizeof(o->name), "%s",
                 connector_name(conn->connector_type, (int)conn->connector_id));
        o->width = mode.hdisplay;
        o->height = mode.vdisplay;
        o->mode = mode;
        o->refresh_hz = mode.vrefresh > 0 ? mode.vrefresh :
            (int)((mode.clock * 1000.0) / (mode.htotal * (double)mode.vtotal) + 0.5);

        if (create_fb(fd, o->width, o->height, &o->fbs[0]) != 0 ||
            create_fb(fd, o->width, o->height, &o->fbs[1]) != 0) {
            destroy_fb(fd, &o->fbs[0]);
            destroy_fb(fd, &o->fbs[1]);
            if (enc) drmModeFreeEncoder(enc);
            drmModeFreeConnector(conn);
            continue;
        }

        int ret = drmModeSetCrtc(fd, o->crtc_id, o->fbs[0].id, 0, 0,
                                 &o->connector_id, 1, &mode);
        if (ret != 0) {
            fprintf(stderr, "drmModeSetCrtc(%s %dx%d@%d) failed: %s\n",
                    o->name, o->width, o->height, o->refresh_hz, strerror(errno));
            destroy_fb(fd, &o->fbs[0]);
            destroy_fb(fd, &o->fbs[1]);
            if (enc) drmModeFreeEncoder(enc);
            drmModeFreeConnector(conn);
            continue;
        }

        o->front = 0;
        o->ok = true;
        used_crtcs[used_count++] = o->crtc_id;
        fprintf(stderr, "output[%d] %s %dx%d@%d connector=%u crtc=%u\n",
                d->count, o->name, o->width, o->height, o->refresh_hz,
                o->connector_id, o->crtc_id);
        d->count++;

        if (enc) drmModeFreeEncoder(enc);
        drmModeFreeConnector(conn);
    }

    drmModeFreeResources(res);
    if (d->count == 0) {
        fprintf(stderr, "no usable DRM connectors\n");
        close(fd);
        d->fd = -1;
        return -1;
    }
    return 0;
}

void drm_display_close(drm_display_t *d) {
    if (!d)
        return;
    if (d->fd >= 0) {
        for (int i = 0; i < d->count; i++) {
            destroy_fb(d->fd, &d->outs[i].fbs[0]);
            destroy_fb(d->fd, &d->outs[i].fbs[1]);
        }
        close(d->fd);
    }
    memset(d, 0, sizeof(*d));
    d->fd = -1;
}

uint8_t *drm_output_back_map(drm_output_t *o, int *pitch) {
    int back = o->front ^ 1;
    if (pitch)
        *pitch = (int)o->fbs[back].pitch;
    return o->fbs[back].map;
}

int drm_output_flip(drm_display_t *d, drm_output_t *o) {
    int back = o->front ^ 1;
    /* Use SetCrtc for portability without a DRM event loop.
     * On product builds, switch to drmModePageFlip + event wait for smoother pacing. */
    int ret = drmModeSetCrtc(d->fd, o->crtc_id, o->fbs[back].id, 0, 0,
                             &o->connector_id, 1, &o->mode);
    if (ret != 0) {
        perror("drmModeSetCrtc flip");
        return -1;
    }
    o->front = back;
    return 0;
}
