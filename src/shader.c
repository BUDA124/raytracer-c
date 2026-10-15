#include "shader.h"

ColorRGB shade_flat(const HitRecord *hit, ColorRGB background,
                    bool has_hit) {
    if (!has_hit) {
        return background;
    }
    return hit->material.color;
}

ColorRGB shade_phong(const Ray *ray, const HitRecord *hit,
                     const Scene *scene) {
    /* Phong shading not implemented for P2. */
    (void)ray; (void)scene;
    return hit->material.color;
}

bool is_in_shadow(const HitRecord *hit, Point3 light_position,
                  const Scene *scene) {
    /* Shadow testing not implemented for P2. */
    (void)hit; (void)light_position; (void)scene;
    return false;
}
