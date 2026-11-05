#include "shader.h"
#include "vector3.h"
#include "intersect.h"
#include "numeric.h"
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
    const Material *material = &hit->material;
    const Vector3 normal = hit->normal;
    const Vector3 view = vec3_scale(ray->direction, -1.0);

    double intensity = scene->ambient_light.r * material->ka;
    double specular = 0.0;

    for (size_t i = 0; i < scene->light_count; ++i) {
        const Light *light = &scene->lights[i];
        Vector3 to_light = vec3_sub(light->position, hit->point);
        double distance = vec3_length(to_light);

        if (distance <= SHADOW_BIAS) {
            continue;
        }

        Vector3 direction = vec3_scale(to_light, 1.0 / distance);
        double lambert = vec3_dot(normal, direction);

        if (lambert <= 0.0) {
            continue;
        }

        if (is_in_shadow(hit, light->position, scene)) {
            continue;
        }

        double denominator = light->c1 + light->c2 * distance
                           + light->c3 * distance * distance;
        double attenuation = 1.0 / denominator;

        if (attenuation > 1.0) {
            attenuation = 1.0;
        }

        double diffuse = lambert * material->kd
                       * light->intensity.r * attenuation;

        intensity += diffuse;

        Vector3 reflection = vec3_sub(
            vec3_scale(normal, 2.0 * lambert),
            direction
        );

        double rv = vec3_dot(reflection, view);

        if (rv > 0.0 && material->ks > 0.0) {
            double specular_power = 1.0;

            for (int k = 0; k < (int)material->shininess; ++k) {
                specular_power *= rv;
            }

            specular += specular_power * material->ks
                      * light->intensity.r * attenuation;
        }
    }

    if (intensity > 1.0) {
        intensity = 1.0;
    }

    if (specular > 1.0) {
        specular = 1.0;
    }

    ColorRGB diffuse_color = {
        material->color.r * intensity,
        material->color.g * intensity,
        material->color.b * intensity
    };

    ColorRGB result = {
        diffuse_color.r + specular * (1.0 - diffuse_color.r),
        diffuse_color.g + specular * (1.0 - diffuse_color.g),
        diffuse_color.b + specular * (1.0 - diffuse_color.b)
    };

    return result;
}

bool is_in_shadow(const HitRecord *hit, Point3 light_position,
                  const Scene *scene) {
    Vector3 to_light = vec3_sub(light_position, hit->point);
    double distance = vec3_length(to_light);

    if (distance <= SHADOW_BIAS) {
        return false;
    }

    Vector3 direction = vec3_scale(to_light, 1.0 / distance);

    Ray shadow_ray;
    shadow_ray.origin = vec3_add(
        hit->point,
        vec3_scale(hit->normal, SHADOW_BIAS)
    );
    shadow_ray.direction = direction;

    HitRecord shadow_hit;

    return first_intersection(
        &shadow_ray,
        scene,
        SHADOW_T_MIN,
        distance - SHADOW_BIAS,
        &shadow_hit
    );
}
