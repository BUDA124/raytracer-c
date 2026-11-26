#include "intersect.h"
#include "vector3.h"
#include "numeric.h"
#include <math.h>

#define POLYGON_EPSILON 1e-9
#define PLANE_EPSILON 1e-7

bool passes_clip_planes(Point3 point, const ClipPlane *clips,
                        size_t clip_count) {
    for (size_t i = 0; i < clip_count; i++) {
        double val = clips[i].a * point.x +
                     clips[i].b * point.y +
                     clips[i].c * point.z +
                     clips[i].d;
        if (val < -GEOMETRY_EPSILON) {
            return false;
        }
    }
    return true;
}

static size_t solve_quadratic_sorted(double qa, double qb, double qc,
                                     double roots[2]) {
    if (!isfinite(qa) || !isfinite(qb) || !isfinite(qc)) {
        return 0;
    }

    if (fabs(qa) < PARALLEL_EPSILON) {
        if (fabs(qb) < PARALLEL_EPSILON) {
            return 0;
        }
        roots[0] = -qc / qb;
        return isfinite(roots[0]) ? 1 : 0;
    }

    double disc = qb * qb - 4.0 * qa * qc;
    if (disc < 0.0) {
        return 0;
    }

    double sqrt_disc = sqrt(disc);
    double inv_2a = 0.5 / qa;
    double r0 = (-qb - sqrt_disc) * inv_2a;
    double r1 = (-qb + sqrt_disc) * inv_2a;

    if (r0 <= r1) {
        roots[0] = r0;
        roots[1] = r1;
    } else {
        roots[0] = r1;
        roots[1] = r0;
    }
    return 2;
}

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

    if (polygon->has_proj_bounds) {
        if (px < polygon->proj_min_x - POLYGON_EPSILON ||
            px > polygon->proj_max_x + POLYGON_EPSILON ||
            py < polygon->proj_min_y - POLYGON_EPSILON ||
            py > polygon->proj_max_y + POLYGON_EPSILON) {
            return false;
        }
    }

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
                      const ClipPlane *clips, size_t clip_count,
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

    double roots[2];
    size_t root_count = solve_quadratic_sorted(a, b, c, roots);

    for (size_t i = 0; i < root_count; i++) {
        double t = roots[i];
        if (t < t_min || t > t_max) continue;

        Point3 point = vec3_add(ray->origin, vec3_scale(ray->direction, t));
        if (!passes_clip_planes(point, clips, clip_count)) continue;

        *out_t = t;
        *out_normal = vec3_normalize(vec3_sub(point, sphere->center));
        return true;
    }

    return false;
}

bool intersect_polygon(const Ray *ray, const Polygon *polygon,
                       const ClipPlane *clips, size_t clip_count,
                       double t_min, double t_max,
                       double *out_t, Vector3 *out_normal) {
    if (!ray || !polygon || !out_t || !out_normal)
        return false;

    if (!polygon->vertices || polygon->vertex_count < 3)
        return false;

    if (t_min <= 0.0 || t_max < t_min)
        return false;

    Point3 p0 = polygon->vertices[0];
    Vector3 normal = polygon->outward_normal;
    size_t axis = polygon->dropped_axis;

    if (vec3_length(normal) <= POLYGON_EPSILON) {
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
            if (valid) break;
        }
        if (!valid) return false;

        for (size_t i = 0; i < polygon->vertex_count; i++) {
            Vector3 diff = vec3_sub(polygon->vertices[i], p0);
            if (fabs(vec3_dot(diff, normal)) > PLANE_EPSILON)
                return false;
        }

        axis = 0;
        if (fabs(normal.y) > fabs(normal.x)) axis = 1;
        if (fabs(normal.z) > fabs(axis == 0 ? normal.x : normal.y)) axis = 2;
    }

    double denominator = vec3_dot(normal, ray->direction);
    if (fabs(denominator) < POLYGON_EPSILON)
        return false;

    Vector3 diff = vec3_sub(p0, ray->origin);
    double t = vec3_dot(diff, normal) / denominator;

    if (t < t_min || t > t_max)
        return false;

    Point3 point = vec3_add(ray->origin, vec3_scale(ray->direction, t));

    if (!point_in_polygon(point, polygon, axis))
        return false;

    if (!passes_clip_planes(point, clips, clip_count))
        return false;

    *out_t = t;
    *out_normal = normal;
    return true;
}

bool intersect_disk(const Ray *ray, const Disk *disk,
                    const ClipPlane *clips, size_t clip_count,
                    double t_min, double t_max,
                    double *out_t, Vector3 *out_normal) {
    if (!ray || !disk || !out_t || !out_normal) return false;
    if (t_min <= 0.0 || t_max < t_min) return false;

    double denom = vec3_dot(disk->normal, ray->direction);
    if (fabs(denom) < PARALLEL_EPSILON) return false;

    double t = vec3_dot(vec3_sub(disk->center, ray->origin), disk->normal) / denom;
    if (t < t_min || t > t_max) return false;

    Point3 point = vec3_add(ray->origin, vec3_scale(ray->direction, t));
    Vector3 rel = vec3_sub(point, disk->center);
    double dist_sq = vec3_dot(rel, rel);

    double r_out_sq = disk->radius * disk->radius;
    double r_in_sq = disk->inner_radius * disk->inner_radius;
    if (dist_sq > r_out_sq + GEOMETRY_EPSILON) return false;
    if (disk->inner_radius > 0.0 && dist_sq < r_in_sq - GEOMETRY_EPSILON) return false;

    if (!passes_clip_planes(point, clips, clip_count)) return false;

    *out_t = t;
    *out_normal = disk->normal;
    return true;
}

bool intersect_ellipse(const Ray *ray, const Ellipse *ellipse,
                       const ClipPlane *clips, size_t clip_count,
                       double t_min, double t_max,
                       double *out_t, Vector3 *out_normal) {
    if (!ray || !ellipse || !out_t || !out_normal) return false;
    if (t_min <= 0.0 || t_max < t_min) return false;

    double denom = vec3_dot(ellipse->normal, ray->direction);
    if (fabs(denom) < PARALLEL_EPSILON) return false;

    double t = vec3_dot(vec3_sub(ellipse->center, ray->origin), ellipse->normal) / denom;
    if (t < t_min || t > t_max) return false;

    Point3 point = vec3_add(ray->origin, vec3_scale(ray->direction, t));
    Vector3 rel = vec3_sub(point, ellipse->center);
    double u = vec3_dot(rel, ellipse->u_axis) / ellipse->radius_u;
    double v = vec3_dot(rel, ellipse->v_axis) / ellipse->radius_v;

    if (u * u + v * v > 1.0 + GEOMETRY_EPSILON) return false;
    if (!passes_clip_planes(point, clips, clip_count)) return false;

    *out_t = t;
    *out_normal = ellipse->normal;
    return true;
}

bool intersect_cylinder(const Ray *ray, const Cylinder *cylinder,
                        const ClipPlane *clips, size_t clip_count,
                        double t_min, double t_max,
                        double *out_t, Vector3 *out_normal) {
    if (!ray || !cylinder || !out_t || !out_normal) return false;
    if (t_min <= 0.0 || t_max < t_min) return false;

    Vector3 oc = vec3_sub(ray->origin, cylinder->base);
    double d_dot_a = vec3_dot(ray->direction, cylinder->axis);
    double oc_dot_a = vec3_dot(oc, cylinder->axis);

    Vector3 d_perp = vec3_sub(ray->direction, vec3_scale(cylinder->axis, d_dot_a));
    Vector3 oc_perp = vec3_sub(oc, vec3_scale(cylinder->axis, oc_dot_a));

    double qa = vec3_dot(d_perp, d_perp);
    if (qa < PARALLEL_EPSILON) return false;

    double qb = 2.0 * vec3_dot(d_perp, oc_perp);
    double qc = vec3_dot(oc_perp, oc_perp) - cylinder->radius * cylinder->radius;

    double roots[2];
    size_t root_count = solve_quadratic_sorted(qa, qb, qc, roots);

    for (size_t i = 0; i < root_count; i++) {
        double t = roots[i];
        if (t < t_min || t > t_max) continue;

        Point3 point = vec3_add(ray->origin, vec3_scale(ray->direction, t));
        Vector3 rel = vec3_sub(point, cylinder->base);
        double h = vec3_dot(rel, cylinder->axis);
        if (h < cylinder->h_min || h > cylinder->h_max) continue;

        if (!passes_clip_planes(point, clips, clip_count)) continue;

        Vector3 radial = vec3_sub(rel, vec3_scale(cylinder->axis, h));
        *out_t = t;
        *out_normal = vec3_normalize(radial);
        return true;
    }

    return false;
}

bool intersect_cone(const Ray *ray, const Cone *cone,
                    const ClipPlane *clips, size_t clip_count,
                    double t_min, double t_max,
                    double *out_t, Vector3 *out_normal) {
    if (!ray || !cone || !out_t || !out_normal) return false;
    if (t_min <= 0.0 || t_max < t_min) return false;

    Vector3 q0 = vec3_sub(ray->origin, cone->vertex);
    double m = 1.0 + cone->tan_alpha * cone->tan_alpha;
    double da = vec3_dot(ray->direction, cone->axis);
    double q0a = vec3_dot(q0, cone->axis);

    double qa = vec3_dot(ray->direction, ray->direction) - m * da * da;
    double qb = 2.0 * (vec3_dot(ray->direction, q0) - m * da * q0a);
    double qc = vec3_dot(q0, q0) - m * q0a * q0a;

    double roots[2];
    size_t root_count = solve_quadratic_sorted(qa, qb, qc, roots);

    for (size_t i = 0; i < root_count; i++) {
        double t = roots[i];
        if (t < t_min || t > t_max) continue;

        Point3 point = vec3_add(ray->origin, vec3_scale(ray->direction, t));
        Vector3 q = vec3_sub(point, cone->vertex);
        double h = vec3_dot(q, cone->axis);
        if (h < cone->h_min || h > cone->h_max) continue;

        if (!passes_clip_planes(point, clips, clip_count)) continue;

        Vector3 grad = vec3_sub(q, vec3_scale(cone->axis, m * h));
        if (vec3_length(grad) < VECTOR_EPSILON) {
            *out_normal = vec3_scale(cone->axis, -1.0);
        } else {
            *out_normal = vec3_normalize(grad);
        }
        *out_t = t;
        return true;
    }

    return false;
}

bool intersect_quadric(const Ray *ray, const Quadric *q,
                       const ClipPlane *clips, size_t clip_count,
                       double t_min, double t_max,
                       double *out_t, Vector3 *out_normal) {
    if (!ray || !q || !out_t || !out_normal) return false;
    if (t_min <= 0.0 || t_max < t_min) return false;

    double ox = ray->origin.x, oy = ray->origin.y, oz = ray->origin.z;
    double dx = ray->direction.x, dy = ray->direction.y, dz = ray->direction.z;

    double qa = q->A * dx * dx + q->B * dy * dy + q->C * dz * dz +
                2.0 * (q->D * dx * dy + q->E * dx * dz + q->F * dy * dz);

    double qb = 2.0 * (q->A * ox * dx + q->B * oy * dy + q->C * oz * dz +
                       q->D * (ox * dy + oy * dx) +
                       q->E * (ox * dz + oz * dx) +
                       q->F * (oy * dz + oz * dy) +
                       q->G * dx + q->H * dy + q->I * dz);

    double qc = q->A * ox * ox + q->B * oy * oy + q->C * oz * oz +
                2.0 * (q->D * ox * oy + q->E * ox * oz + q->F * oy * oz +
                       q->G * ox + q->H * oy + q->I * oz) +
                q->J;

    double roots[2];
    size_t root_count = solve_quadratic_sorted(qa, qb, qc, roots);

    for (size_t i = 0; i < root_count; i++) {
        double t = roots[i];
        if (t < t_min || t > t_max) continue;

        Point3 point = vec3_add(ray->origin, vec3_scale(ray->direction, t));
        if (!passes_clip_planes(point, clips, clip_count)) continue;

        Vector3 grad = vec3_create(
            2.0 * (q->A * point.x + q->D * point.y + q->E * point.z + q->G),
            2.0 * (q->D * point.x + q->B * point.y + q->F * point.z + q->H),
            2.0 * (q->E * point.x + q->F * point.y + q->C * point.z + q->I)
        );

        if (vec3_length(grad) < VECTOR_EPSILON) {
            *out_normal = vec3_scale(ray->direction, -1.0);
        } else {
            *out_normal = vec3_normalize(grad);
        }
        *out_t = t;
        return true;
    }

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

        switch (obj->type) {
            case OBJ_SPHERE:
                hit = intersect_sphere(ray, &obj->geo.sphere,
                                       obj->clip_planes, obj->clip_count,
                                       t_min, best_max,
                                       &candidate_t, &outward_normal);
                break;
            case OBJ_POLYGON:
                hit = intersect_polygon(ray, &obj->geo.polygon,
                                        obj->clip_planes, obj->clip_count,
                                        t_min, best_max,
                                        &candidate_t, &outward_normal);
                break;
            case OBJ_DISK:
                hit = intersect_disk(ray, &obj->geo.disk,
                                     obj->clip_planes, obj->clip_count,
                                     t_min, best_max,
                                     &candidate_t, &outward_normal);
                break;
            case OBJ_ELLIPSE:
                hit = intersect_ellipse(ray, &obj->geo.ellipse,
                                        obj->clip_planes, obj->clip_count,
                                        t_min, best_max,
                                        &candidate_t, &outward_normal);
                break;
            case OBJ_CYLINDER:
                hit = intersect_cylinder(ray, &obj->geo.cylinder,
                                         obj->clip_planes, obj->clip_count,
                                         t_min, best_max,
                                         &candidate_t, &outward_normal);
                break;
            case OBJ_CONE:
                hit = intersect_cone(ray, &obj->geo.cone,
                                     obj->clip_planes, obj->clip_count,
                                     t_min, best_max,
                                     &candidate_t, &outward_normal);
                break;
            case OBJ_QUADRIC:
                hit = intersect_quadric(ray, &obj->geo.quadric,
                                        obj->clip_planes, obj->clip_count,
                                        t_min, best_max,
                                        &candidate_t, &outward_normal);
                break;
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
            out_hit->object = obj;
        }
    }
    return found;
}
