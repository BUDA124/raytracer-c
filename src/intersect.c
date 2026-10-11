#include "intersect.h"
#include "vector3.h"
#include "numeric.h"
#include <math.h>
#include <stdio.h>

bool intersect_sphere(const Ray *ray, const Sphere *sphere,
                      double t_min, double t_max,
                      double *out_t, Vector3 *out_outward_normal) {
    if (!ray || !sphere || !out_t || !out_outward_normal) return false;
    if (t_min <= 0.0 || t_max < t_min) return false;
    if (sphere->radius <= 0.0) return false;

    Vector3 oc = vec3_sub(ray->origin, sphere->center);
    double a = vec3_dot(ray->direction, ray->direction);
    double b = 2.0 * vec3_dot(ray->direction, oc);
    double c = vec3_dot(oc, oc) - sphere->radius * sphere->radius;

    if (a <= 0.0 || !isfinite(a)) return false;

    double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) return false;

    double sqrt_disc = sqrt(discriminant);
    double inv_2a = 1.0 / (2.0 * a);
    double root_near = (-b - sqrt_disc) * inv_2a;
    double root_far  = (-b + sqrt_disc) * inv_2a;

    double t;
    if (root_near >= t_min && root_near <= t_max) {
        t = root_near;
    } else if (root_far >= t_min && root_far <= t_max) {
        t = root_far;
    } else {
        return false;
    }

    *out_t = t;
    Point3 point = vec3_add(ray->origin, vec3_scale(ray->direction, t));
    *out_outward_normal = vec3_scale(
        vec3_sub(point, sphere->center), 1.0 / sphere->radius);
    return true;
}

bool intersect_polygon(const Ray *ray, const Polygon *polygon,
                       double t_min, double t_max,
                       double *out_t, Vector3 *out_outward_normal) {
    /* Polygon intersection not implemented for P2. */
    (void)ray; (void)polygon; (void)t_min; (void)t_max;
    (void)out_t; (void)out_outward_normal;
    return false;
}

bool first_intersection(const Ray *ray, const Scene *scene,
                        double t_min, double t_max, HitRecord *out_hit) {
    if (!ray || !scene || !out_hit) return false;
    if (t_min <= 0.0 || t_max < t_min) return false;

    bool found = false;
    double best_max = t_max;

    for (size_t i = 0; i < scene->object_count; i++) {
        const Object *obj = &scene->objects[i];
        double candidate_t;
        Vector3 outward_normal;
        bool hit = false;

        if (obj->type == OBJ_SPHERE) {
            hit = intersect_sphere(ray, &obj->geo.sphere,
                                   t_min, best_max,
                                   &candidate_t, &outward_normal);
        } else if (obj->type == OBJ_POLYGON) {
            hit = intersect_polygon(ray, &obj->geo.polygon,
                                    t_min, best_max,
                                    &candidate_t, &outward_normal);
        }

        if (hit) {
            found = true;
            best_max = candidate_t;
            out_hit->t = candidate_t;
            out_hit->point = vec3_add(ray->origin,
                                      vec3_scale(ray->direction, candidate_t));
            out_hit->front_face = vec3_dot(ray->direction, outward_normal) < 0.0;
            out_hit->normal = out_hit->front_face
                ? outward_normal
                : vec3_scale(outward_normal, -1.0);
            out_hit->material = obj->material;
        }
    }
    return found;
}

bool any_intersection(const Ray *ray, const Scene *scene,
                      double t_min, double t_max) {
    if (!ray || !scene) return false;
    if (t_min <= 0.0 || t_max < t_min) return false;

    for (size_t i = 0; i < scene->object_count; i++) {
        const Object *obj = &scene->objects[i];
        double candidate_t;
        Vector3 outward_normal;

        if (obj->type == OBJ_SPHERE) {
            if (intersect_sphere(ray, &obj->geo.sphere,
                                 t_min, t_max,
                                 &candidate_t, &outward_normal)) {
                return true;
            }
        } else if (obj->type == OBJ_POLYGON) {
            if (intersect_polygon(ray, &obj->geo.polygon,
                                  t_min, t_max,
                                  &candidate_t, &outward_normal)) {
                return true;
            }
        }
    }
    return false;
}

