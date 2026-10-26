#include "shader.h"
#include "vector3.h"
#include <math.h>

ColorRGB shade_flat(const HitRecord *hit, ColorRGB background,
                    bool has_hit) {
    if (!has_hit) {
        return background;
    }
    return hit->material.color;
}

ColorRGB shade_phong(const Ray *ray, const HitRecord *hit,
                     const Scene *scene) {
    (void)ray; /* Diffuse intensity does not depend on the viewer direction. */

    const Material *material = &hit->material;
    const Vector3 normal = hit->normal; /* Already unit length. */

    /* Ambient intensity is scene-wide, scaled by the object's ka. */
    double intensity = scene->ambient_light.r * material->ka;

    /* Add the diffuse contribution from every light in the scene. */
    for (size_t i = 0; i < scene->light_count; ++i) {
        const Light *light = &scene->lights[i];
        Vector3 to_light = vec3_sub(light->position, hit->point);
        double distance = vec3_length(to_light);
        if (distance <= 1e-9) {
            continue;
        }

        Vector3 direction = vec3_scale(to_light, 1.0 / distance);
        double lambert = vec3_dot(normal, direction);
        if (lambert <= 0.0) {
            continue;
        }

        double denominator = light->c1 + light->c2 * distance
                           + light->c3 * distance * distance;
        /* The scene parser rejects all-zero attenuation coefficients. */
        if (denominator <= 0.0) {
            continue;
        }
        double attenuation = 1.0 / denominator;
        if (attenuation > 1.0) attenuation = 1.0;

        double diffuse = lambert * material->kd * attenuation
                       * light->intensity.r;
        intensity += diffuse;
    }

    /* Clamp intensity to a maximum of 1.0, then modulate the base material color. */
    if (intensity > 1.0) intensity = 1.0;

    ColorRGB result = {
        material->color.r * intensity,
        material->color.g * intensity,
        material->color.b * intensity
    };
    return result;
}

bool is_in_shadow(const HitRecord *hit, Point3 light_position,
                  const Scene *scene) {
    /* Shadow testing not implemented for P2. */
    (void)hit; (void)light_position; (void)scene;
    return false;
}
