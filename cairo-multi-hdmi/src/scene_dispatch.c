#include "scene.h"

void scene_draw(scene_id_t id, cairo_t *cr, int w, int h, const scene_ctx_t *ctx) {
    switch (id) {
    case SCENE_WORLD:
        scene_world_draw(cr, w, h, ctx);
        break;
    case SCENE_WEATHER:
        scene_weather_draw(cr, w, h, ctx);
        break;
    case SCENE_DIGITAL:
    default:
        scene_digital_draw(cr, w, h, ctx);
        break;
    }
}
