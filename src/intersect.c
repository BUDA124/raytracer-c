
#include "intersect.h"
#include "vector3.h"
#include "numeric.h"
#include <math.h>

#define POLYGON_EPSILON 1e-9
#define PLANE_EPSILON 1e-7

static void project_point(Point3 p, size_t axis,
                          double *x, double *y) {
    if (axis == 0) {
        *x = p.y;
        *y = p.z;
    } else if (axis == 1) {
        *x = p.x;
        *y = p.z;
    } else {
        *x = p.x;
        *y = p.y;
    }
}

static bool point_on_segment(double px, double py,
                             double ax, double ay,
                             double bx, double by) {
    double dx = bx - ax;
    double dy = by - ay;
    double length = hypot(dx, dy);

    if (length < POLYGON_EPSILON)
        return hypot(px - ax, py - ay) <= POLYGON_EPSILON;

    double cross = (px - ax) * dy - (py - ay) * dx;

    if (fabs(cross) > POLYGON_EPSILON * length)
        return false;

    double dot = (px - ax) * dx + (py - ay) * dy;

    return dot >= -POLYGON_EPSILON * length &&
           dot <= dx * dx + dy * dy +
                  POLYGON_EPSILON * length;
}

static bool point_in_polygon(Point3 point,
                             const Polygon *polygon,
                             size_t axis) {
    double px, py;
    project_point(point, axis, &px, &py);

    bool inside = false;
    size_t count = polygon->vertex_count;

    for (size_t i = 0, j = count - 1;
         i < count; j = i++) {

        double xi, yi, xj, yj;

        project_point(polygon->vertices[i], axis, &xi, &yi);
        project_point(polygon->vertices[j], axis, &xj, &yj);

        if (point_on_segment(px, py, xi, yi, xj, yj))
            return true;

        if ((yi > py) != (yj > py)) {
            double x_intersection =
                xi + (py - yi) * (xj - xi) / (yj - yi);

            if (px < x_intersection)
                inside = !inside;
        }
    }

    return inside;
}

bool intersect_sphere(const Ray *ray, const Sphere *sphere,
                      double t_min, double t_max,
                      double *out_t, Vector3 *out_normal) {
    if (!ray || !sphere || !out_t || !out_normal) return false;
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
    *out_normal = vec3_normalize(vec3_sub(point, sphere->center));
    return true;
}

bool intersect_polygon(const Ray *ray, const Polygon *polygon,
                       double t_min, double t_max,
                       double *out_t, Vector3 *out_normal) {
    if (!ray || !polygon || !out_t || !out_normal)
        return false;

    if (!polygon->vertices || polygon->vertex_count < 3)
        return false;

    if (t_min <= 0.0 || t_max < t_min)
        return false;

    Point3 p0 = polygon->vertices[0];
    Vector3 normal = {0.0, 0.0, 0.0};
    bool valid = false;

    for (size_t i = 1; i + 1 < polygon->vertex_count; i++) {
        Vector3 edge1 = vec3_sub(polygon->vertices[i], p0);

        for (size_t j = i + 1; j < polygon->vertex_count; j++) {
            Vector3 edge2 = vec3_sub(polygon->vertices[j], p0);
            normal = vec3_cross(edge1, edge2);

            if (vec3_length(normal) > POLYGON_EPSILON) {
                normal = vec3_normalize(normal);
                valid = true;
                break;
            }
        }

        if (valid)
            break;
    }

    if (!valid)
        return false;

    for (size_t i = 0; i < polygon->vertex_count; i++) {
        Vector3 diff = vec3_sub(polygon->vertices[i], p0);

        if (fabs(vec3_dot(diff, normal)) > PLANE_EPSILON)
            return false;
    }

    double denominator = vec3_dot(normal, ray->direction);

    if (fabs(denominator) < POLYGON_EPSILON)
        return false;

    Vector3 diff = vec3_sub(p0, ray->origin);
    double t = vec3_dot(diff, normal) / denominator;

    if (t < t_min || t > t_max)
        return false;

    Point3 point = vec3_add(
        ray->origin,
        vec3_scale(ray->direction, t)
    );

    size_t axis = 0;

    if (fabs(normal.y) > fabs(normal.x))
        axis = 1;

    if (fabs(normal.z) > fabs(
            axis == 0 ? normal.x : normal.y))
        axis = 2;

    if (!point_in_polygon(point, polygon, axis))
        return false;

    if (vec3_length(polygon->outward_normal) > POLYGON_EPSILON &&
        vec3_dot(normal, polygon->outward_normal) < 0.0) {
        normal = vec3_scale(normal, -1.0);
    }

    *out_t = t;
    *out_normal = normal;

    return true;
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
