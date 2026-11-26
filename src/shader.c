#include "shader.h"
#include "vector3.h"
#include "intersect.h"
#include "texture.h"
#include "numeric.h"
#include <math.h>

/*
 * Shading module ("De_que_color" in the course notation).
 *
 * Notation used throughout this file:
 *   P    intersection point            Q    material of the object hit
 *   N    unit normal at P (faces the incoming ray)
 *   D    unit direction of the incoming ray
 *   V    unit vector from P back to the eye (V = -D)
 *   L    unit vector from P toward the light
 *   R    reflection of L about N (specular highlight)
 *   I    diffuse + ambient intensity    E    specular intensity
 *   Ia   ambient light intensity        Ip   intensity of the current light
 *   Ka, Kd, Ks, Kn   ambient, diffuse, specular coefficients and exponent
 *   Fatt = min(1, 1 / (c1 + c2 * dL + c3 * dL^2)), dL = distance to the light
 *   O1, O2, O3       weights of own, mirrored and transmitted colors
 */

/* Upper bound on transparent layers crossed by a single shadow ray. */
#define MAX_SHADOW_LAYERS 64

static double light_transmission(Point3 P, Vector3 N, Point3 light_position,
                                 const Scene *scene);
static ColorRGB trace_secondary_ray(const Ray *ray, const Scene *scene,
                                    int depth);
static ColorRGB shade_intersection(const Ray *ray, const HitRecord *hit,
                                   const Scene *scene, int depth);

ColorRGB shade_flat(const HitRecord *hit, ColorRGB background,
                    bool has_hit) {
    if (!has_hit) {
        return background;
    }
    return hit->material.color;
}

/*
 * Top-level De_que_color call for a primary ray. The render loop has
 * already found the first intersection, so shading starts from it with
 * the full recursion budget defined by the scene.
 */
ColorRGB shade_phong(const Ray *ray, const HitRecord *hit,
                     const Scene *scene) {
    return shade_intersection(ray, hit, scene, scene->max_depth);
}

bool is_in_shadow(const HitRecord *hit, Point3 light_position,
                  const Scene *scene) {
    return light_transmission(hit->point, hit->normal,
                              light_position, scene) <= 0.0;
}

/*
 * Fraction of a light that reaches P: 1 when the path is clear, 0 when an
 * opaque object blocks it. Each transparent object crossed lets only its
 * O3 share of the light through.
 */
static double light_transmission(Point3 P, Vector3 N, Point3 light_position,
                                 const Scene *scene) {
    Vector3 to_light = vec3_sub(light_position, P);
    double dL = vec3_length(to_light);

    if (dL <= SHADOW_BIAS) {
        return 1.0;
    }

    Vector3 L = vec3_scale(to_light, 1.0 / dL);

    /* Offset the origin along N so the surface does not shadow itself. */
    Ray shadow_ray;
    shadow_ray.origin = vec3_add(P, vec3_scale(N, SHADOW_BIAS));
    shadow_ray.direction = L;

    double t_max = dL - SHADOW_BIAS;
    double transmission = 1.0;

    for (int layer = 0; layer < MAX_SHADOW_LAYERS; ++layer) {
        HitRecord obstacle;

        if (!first_intersection(&shadow_ray, scene, SHADOW_T_MIN,
                                t_max, &obstacle)) {
            return transmission;
        }

        transmission *= obstacle.material.o3;

        if (transmission <= 0.0) {
            return 0.0;
        }

        /* Continue toward the light from just past the obstacle. */
        shadow_ray.origin = vec3_add(obstacle.point,
                                     vec3_scale(L, SHADOW_BIAS));
        t_max = vec3_length(vec3_sub(light_position, shadow_ray.origin))
              - SHADOW_BIAS;

        if (t_max <= SHADOW_T_MIN) {
            return transmission;
        }
    }

    return 0.0;
}

/* (R . V)^Kn with the integer exponent Kn used in class. */
static double specular_power(double R_dot_V, double Kn) {
    double result = 1.0;

    for (int k = 0; k < (int)Kn; ++k) {
        result *= R_dot_V;
    }

    return result;
}

/*
 * Base color of the surface at P. Objects flagged with a texture take the
 * texel from the texture mapping stage; otherwise the object's own color
 * is used.
 */
static ColorRGB object_color(const HitRecord *hit, const Scene *scene) {
    return evaluate_object_color(hit, scene);
}

/*
 * Recursive De_que_color for mirror and transparency rays: find the first
 * intersection of the ray and shade it with one less recursion level.
 */
static ColorRGB trace_secondary_ray(const Ray *ray, const Scene *scene,
                                    int depth) {
    HitRecord hit;

    if (!first_intersection(ray, scene, SHADOW_T_MIN, INFINITY, &hit)) {
        return scene->background_color;
    }

    return shade_intersection(ray, &hit, scene, depth);
}

static ColorRGB shade_intersection(const Ray *ray, const HitRecord *hit,
                                   const Scene *scene, int depth) {
    const Material *Q = &hit->material;
    const Point3 P = hit->point;
    const Vector3 N = hit->normal;
    const Vector3 D = ray->direction;
    const Vector3 V = vec3_scale(D, -1.0);
    const double Ia = scene->ambient_light.r;
    const double Kn = Q->shininess;

    /* Ambient term: I = Ia * Ka. */
    double I = Ia * Q->ka;
    double E = 0.0;

    for (size_t k = 0; k < scene->light_count; ++k) {
        const Light *light = &scene->lights[k];
        Vector3 to_light = vec3_sub(light->position, P);
        double dL = vec3_length(to_light);

        if (dL <= SHADOW_BIAS) {
            continue;
        }

        Vector3 L = vec3_scale(to_light, 1.0 / dL);
        double N_dot_L = vec3_dot(N, L);

        /* The light is behind the surface. */
        if (N_dot_L <= 0.0) {
            continue;
        }

        double transmission = light_transmission(P, N, light->position,
                                                 scene);

        /* P is in full shadow for this light. */
        if (transmission <= 0.0) {
            continue;
        }

        double Fatt = 1.0 / (light->c1 + light->c2 * dL
                             + light->c3 * dL * dL);

        if (Fatt > 1.0) {
            Fatt = 1.0;
        }

        double Ip = light->intensity.r * transmission;

        /* Diffuse term: I += (N . L) * Kd * Ip * Fatt. */
        I += N_dot_L * Q->kd * Ip * Fatt;

        /* Specular term: R = 2N(N . L) - L, E += (R . V)^Kn * Ks * Ip * Fatt. */
        Vector3 R = vec3_sub(vec3_scale(N, 2.0 * N_dot_L), L);
        double R_dot_V = vec3_dot(R, V);

        if (R_dot_V > 0.0 && Q->ks > 0.0) {
            E += specular_power(R_dot_V, Kn) * Q->ks * Ip * Fatt;
        }
    }

    if (I > 1.0) {
        I = 1.0;
    }

    if (E > 1.0) {
        E = 1.0;
    }

    /* Local color: color = I * Q.color. */
    ColorRGB base = object_color(hit, scene);
    ColorRGB color = { base.r * I, base.g * I, base.b * I };

    /*
     * Mirrors and non-refractive transparency:
     *   color = O1 * local + O2 * reflected + O3 * transmitted.
     * At the last recursion level the local color is kept as is.
     */
    if (depth > 0 && (Q->o2 > 0.0 || Q->o3 > 0.0)) {
        ColorRGB reflected = { 0.0, 0.0, 0.0 };
        ColorRGB transmitted = { 0.0, 0.0, 0.0 };

        if (Q->o2 > 0.0) {
            /* Mirror direction: R = 2N(N . V) - V. */
            Ray mirror_ray;
            mirror_ray.origin = vec3_add(P, vec3_scale(N, SHADOW_BIAS));
            mirror_ray.direction = vec3_normalize(vec3_sub(
                vec3_scale(N, 2.0 * vec3_dot(N, V)), V));
            reflected = trace_secondary_ray(&mirror_ray, scene, depth - 1);
        }

        if (Q->o3 > 0.0) {
            /* The ray keeps its direction D through the object. */
            Ray transparency_ray;
            transparency_ray.origin = vec3_add(P, vec3_scale(D, SHADOW_BIAS));
            transparency_ray.direction = D;
            transmitted = trace_secondary_ray(&transparency_ray, scene,
                                              depth - 1);
        }

        color.r = Q->o1 * color.r + Q->o2 * reflected.r + Q->o3 * transmitted.r;
        color.g = Q->o1 * color.g + Q->o2 * reflected.g + Q->o3 * transmitted.g;
        color.b = Q->o1 * color.b + Q->o2 * reflected.b + Q->o3 * transmitted.b;
    }

    /* Specular highlight toward white: color = color + E * (1 - color). */
    ColorRGB result = {
        color.r + E * (1.0 - color.r),
        color.g + E * (1.0 - color.g),
        color.b + E * (1.0 - color.b)
    };

    return result;
}
