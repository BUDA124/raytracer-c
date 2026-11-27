#include "geometry.h"
#include "numeric.h"
#include "vector3.h"

#include <math.h>
#include <stdio.h>

#define POLYGON_EPSILON 1e-9
#define PLANE_EPSILON 1e-7

void build_axis_basis(Vector3 axis, Vector3 *out_u, Vector3 *out_w) {
    Vector3 unit_axis = vec3_normalize(axis);
    Vector3 ref = (fabs(unit_axis.z) < 0.9)
        ? vec3_create(0.0, 0.0, -1.0)
        : vec3_create(0.0, -1.0, 0.0);

    Vector3 proj = vec3_sub(ref, vec3_scale(unit_axis, vec3_dot(ref, unit_axis)));
    Vector3 u = vec3_normalize(proj);
    Vector3 w = vec3_normalize(vec3_cross(u, unit_axis));

    if (out_u) *out_u = u;
    if (out_w) *out_w = w;
}

bool prepare_polygon(Polygon *polygon, char *error_message,
                     size_t error_capacity) {
    if (!polygon || !polygon->vertices || polygon->vertex_count < 3) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "A polygon requires at least 3 vertices");
        }
        return false;
    }

    Point3 p0 = polygon->vertices[0];
    Vector3 normal = {0.0, 0.0, 0.0};

    for (size_t i = 1; i + 1 < polygon->vertex_count; i++) {
        Vector3 edge1 = vec3_sub(polygon->vertices[i], p0);

        for (size_t j = i + 1; j < polygon->vertex_count; j++) {
            Vector3 edge2 = vec3_sub(polygon->vertices[j], p0);

            normal = vec3_cross(edge1, edge2);

            if (vec3_length(normal) > POLYGON_EPSILON)
                break;
        }

        if (vec3_length(normal) > POLYGON_EPSILON)
            break;
    }

    double normal_length = vec3_length(normal);

    if (normal_length <= POLYGON_EPSILON) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Polygon vertices are collinear");
        }
        return false;
    }

    normal = vec3_scale(normal, 1.0 / normal_length);

    for (size_t i = 1; i < polygon->vertex_count; i++) {
        Vector3 difference = vec3_sub(polygon->vertices[i], p0);

        if (fabs(vec3_dot(difference, normal)) > PLANE_EPSILON) {
            if (error_message && error_capacity > 0) {
                snprintf(error_message, error_capacity,
                         "Polygon vertices are not coplanar");
            }
            return false;
        }
    }

    polygon->outward_normal = normal;

    polygon->dropped_axis = 0;

    if (fabs(normal.y) > fabs(normal.x))
        polygon->dropped_axis = 1;

    double largest = polygon->dropped_axis == 0
        ? fabs(normal.x) : fabs(normal.y);

    if (fabs(normal.z) > largest)
        polygon->dropped_axis = 2;

    for (size_t i = 0; i < polygon->vertex_count; i++) {
        Point3 v = polygon->vertices[i];
        double px, py;
        if (polygon->dropped_axis == 0) {
            px = v.y; py = v.z;
        } else if (polygon->dropped_axis == 1) {
            px = v.x; py = v.z;
        } else {
            px = v.x; py = v.y;
        }
        if (i == 0 || px < polygon->proj_min_x) polygon->proj_min_x = px;
        if (i == 0 || px > polygon->proj_max_x) polygon->proj_max_x = px;
        if (i == 0 || py < polygon->proj_min_y) polygon->proj_min_y = py;
        if (i == 0 || py > polygon->proj_max_y) polygon->proj_max_y = py;
    }
    polygon->has_proj_bounds = true;

    if (error_message && error_capacity > 0)
        error_message[0] = '\0';

    return true;
}

void init_polygon_default_texmap(const Polygon *polygon,
                                 TextureMapping *out_map) {
    if (!polygon || !out_map || !polygon->vertices || polygon->vertex_count < 3) {
        return;
    }

    Point3 p0 = polygon->vertices[0];
    Vector3 u_axis = vec3_create(1.0, 0.0, 0.0);
    for (size_t i = 1; i < polygon->vertex_count; i++) {
        Vector3 edge = vec3_sub(polygon->vertices[i], p0);
        if (vec3_length(edge) > VECTOR_EPSILON) {
            u_axis = vec3_normalize(edge);
            break;
        }
    }

    Vector3 v_axis = vec3_normalize(vec3_cross(polygon->outward_normal, u_axis));

    double min_u = 0.0, max_u = 0.0;
    double min_v = 0.0, max_v = 0.0;
    for (size_t i = 0; i < polygon->vertex_count; i++) {
        Vector3 rel = vec3_sub(polygon->vertices[i], p0);
        double u = vec3_dot(rel, u_axis);
        double v = vec3_dot(rel, v_axis);
        if (i == 0 || u < min_u) min_u = u;
        if (i == 0 || u > max_u) max_u = u;
        if (i == 0 || v < min_v) min_v = v;
        if (i == 0 || v > max_v) max_v = v;
    }

    double su = max_u - min_u;
    double sv = max_v - min_v;
    if (su < VECTOR_EPSILON) su = 1.0;
    if (sv < VECTOR_EPSILON) sv = 1.0;

    out_map->wrap = WRAP_REPEAT;
    out_map->origin = vec3_add(p0, vec3_add(vec3_scale(u_axis, min_u),
                                            vec3_scale(v_axis, min_v)));
    out_map->u_axis = u_axis;
    out_map->v_axis = v_axis;
    out_map->w_axis = polygon->outward_normal;
    out_map->scale_u = su;
    out_map->scale_v = sv;
    out_map->custom_basis = false;
}

void init_sphere_default_texmap(TextureMapping *out_map) {
    if (!out_map) return;
    out_map->wrap = WRAP_REPEAT;
    out_map->origin = vec3_create(0.0, 0.0, 0.0);
    /* Front of sphere (-Z) maps to u = 0.5; +X maps rightwards; +Y is north pole. */
    out_map->u_axis = vec3_create(0.0, 0.0, -1.0);
    out_map->v_axis = vec3_create(0.0, 1.0, 0.0);
    out_map->w_axis = vec3_create(1.0, 0.0, 0.0);
    out_map->scale_u = 1.0;
    out_map->scale_v = 1.0;
    out_map->custom_basis = false;
}

bool prepare_disk(Disk *disk, char *error_message, size_t error_capacity) {
    if (!disk) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity, "Invalid disk pointer");
        }
        return false;
    }
    if (disk->radius <= 0.0 || disk->inner_radius < 0.0 ||
        disk->inner_radius >= disk->radius) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Disk requires radius > 0 and 0 <= inner_radius < radius");
        }
        return false;
    }
    double nlen = vec3_length(disk->normal);
    if (nlen <= VECTOR_EPSILON) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Disk normal must be non-zero");
        }
        return false;
    }
    disk->normal = vec3_scale(disk->normal, 1.0 / nlen);
    if (error_message && error_capacity > 0) error_message[0] = '\0';
    return true;
}

bool prepare_ellipse(Ellipse *ellipse, char *error_message,
                     size_t error_capacity) {
    if (!ellipse) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity, "Invalid ellipse pointer");
        }
        return false;
    }
    if (ellipse->radius_u <= 0.0 || ellipse->radius_v <= 0.0) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Ellipse semi-axes must be positive");
        }
        return false;
    }
    double nlen = vec3_length(ellipse->normal);
    if (nlen <= VECTOR_EPSILON) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Ellipse normal must be non-zero");
        }
        return false;
    }
    ellipse->normal = vec3_scale(ellipse->normal, 1.0 / nlen);

    /* Project u_axis onto the plane orthogonal to normal. */
    Vector3 u_proj = vec3_sub(
        ellipse->u_axis,
        vec3_scale(ellipse->normal, vec3_dot(ellipse->u_axis, ellipse->normal)));
    double ulen = vec3_length(u_proj);
    if (ulen <= VECTOR_EPSILON) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Ellipse U axis cannot be parallel to normal");
        }
        return false;
    }
    ellipse->u_axis = vec3_scale(u_proj, 1.0 / ulen);
    ellipse->v_axis = vec3_normalize(vec3_cross(ellipse->normal, ellipse->u_axis));

    if (error_message && error_capacity > 0) error_message[0] = '\0';
    return true;
}

bool prepare_cylinder(Cylinder *cylinder, char *error_message,
                      size_t error_capacity) {
    if (!cylinder) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity, "Invalid cylinder pointer");
        }
        return false;
    }
    if (cylinder->radius <= 0.0) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Cylinder radius must be positive");
        }
        return false;
    }
    if (cylinder->h_max <= cylinder->h_min + VECTOR_EPSILON) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Cylinder requires h_max > h_min");
        }
        return false;
    }
    double alen = vec3_length(cylinder->axis);
    if (alen <= VECTOR_EPSILON) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Cylinder axis must be non-zero");
        }
        return false;
    }
    cylinder->axis = vec3_scale(cylinder->axis, 1.0 / alen);
    build_axis_basis(cylinder->axis, &cylinder->u_axis, &cylinder->w_axis);

    if (error_message && error_capacity > 0) error_message[0] = '\0';
    return true;
}

bool prepare_cone(Cone *cone, char *error_message, size_t error_capacity) {
    if (!cone) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity, "Invalid cone pointer");
        }
        return false;
    }
    if (cone->tan_alpha <= VECTOR_EPSILON || !isfinite(cone->tan_alpha)) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Cone half-angle must be strictly between 0 and 90 degrees");
        }
        return false;
    }
    if (cone->h_max <= cone->h_min + VECTOR_EPSILON) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Cone requires h_max > h_min");
        }
        return false;
    }
    double alen = vec3_length(cone->axis);
    if (alen <= VECTOR_EPSILON) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Cone axis must be non-zero");
        }
        return false;
    }
    cone->axis = vec3_scale(cone->axis, 1.0 / alen);
    build_axis_basis(cone->axis, &cone->u_axis, &cone->w_axis);

    if (error_message && error_capacity > 0) error_message[0] = '\0';
    return true;
}

bool prepare_quadric(const Quadric *quadric, char *error_message,
                     size_t error_capacity) {
    if (!quadric) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity, "Invalid quadric pointer");
        }
        return false;
    }
    double mag = fabs(quadric->A) + fabs(quadric->B) + fabs(quadric->C) +
                 fabs(quadric->D) + fabs(quadric->E) + fabs(quadric->F) +
                 fabs(quadric->G) + fabs(quadric->H) + fabs(quadric->I);
    if (mag <= VECTOR_EPSILON) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity,
                     "Quadric requires at least one non-zero coefficient A..I");
        }
        return false;
    }
    if (error_message && error_capacity > 0) error_message[0] = '\0';
    return true;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void translate_object(Object *obj, Vector3 delta) {
    if (!obj) return;
    if (fabs(delta.x) <= 1e-15 && fabs(delta.y) <= 1e-15 &&
        fabs(delta.z) <= 1e-15) {
        return;
    }

    switch (obj->type) {
        case OBJ_SPHERE:
            obj->geo.sphere.center = vec3_add(obj->geo.sphere.center, delta);
            break;
        case OBJ_DISK:
            obj->geo.disk.center = vec3_add(obj->geo.disk.center, delta);
            break;
        case OBJ_ELLIPSE:
            obj->geo.ellipse.center = vec3_add(obj->geo.ellipse.center, delta);
            break;
        case OBJ_CYLINDER:
            obj->geo.cylinder.base = vec3_add(obj->geo.cylinder.base, delta);
            break;
        case OBJ_CONE:
            obj->geo.cone.vertex = vec3_add(obj->geo.cone.vertex, delta);
            break;
        case OBJ_POLYGON: {
            Polygon *poly = &obj->geo.polygon;
            for (size_t i = 0; i < poly->vertex_count; i++) {
                poly->vertices[i] = vec3_add(poly->vertices[i], delta);
            }
            obj->tex_map.origin = vec3_add(obj->tex_map.origin, delta);
            for (size_t i = 0; i < poly->vertex_count; i++) {
                Point3 v = poly->vertices[i];
                double px, py;
                if (poly->dropped_axis == 0) {
                    px = v.y; py = v.z;
                } else if (poly->dropped_axis == 1) {
                    px = v.x; py = v.z;
                } else {
                    px = v.x; py = v.y;
                }
                if (i == 0 || px < poly->proj_min_x) poly->proj_min_x = px;
                if (i == 0 || px > poly->proj_max_x) poly->proj_max_x = px;
                if (i == 0 || py < poly->proj_min_y) poly->proj_min_y = py;
                if (i == 0 || py > poly->proj_max_y) poly->proj_max_y = py;
            }
            break;
        }
        case OBJ_QUADRIC: {
            /* Q'(x, y, z) = Q(x - dx, y - dy, z - dz) = 0 */
            Quadric *q = &obj->geo.quadric;
            double dx = delta.x, dy = delta.y, dz = delta.z;
            double new_G = q->G - (q->A * dx + q->D * dy + q->E * dz);
            double new_H = q->H - (q->D * dx + q->B * dy + q->F * dz);
            double new_I = q->I - (q->E * dx + q->F * dy + q->C * dz);
            double new_J = q->J
                + q->A * dx * dx + q->B * dy * dy + q->C * dz * dz
                + 2.0 * (q->D * dx * dy + q->E * dx * dz + q->F * dy * dz)
                - 2.0 * (q->G * dx + q->H * dy + q->I * dz);
            q->G = new_G;
            q->H = new_H;
            q->I = new_I;
            q->J = new_J;
            break;
        }
    }

    for (size_t i = 0; i < obj->clip_count; i++) {
        ClipPlane *cp = &obj->clip_planes[i];
        cp->d -= (cp->a * delta.x + cp->b * delta.y + cp->c * delta.z);
    }
}

void compute_animation_deltas(const Scene *scene, int frame_index,
                              int total_frames,
                              Vector3 out_deltas[ANIM_ROLE_COUNT]) {
    for (int r = 0; r < ANIM_ROLE_COUNT; r++) {
        out_deltas[r] = vec3_create(0.0, 0.0, 0.0);
    }
    if (!scene || total_frames <= 1) {
        return;
    }

    /* anim_max = Porcupine 1 (rear) forward displacement at peak (w = 1)
     * anim_min = Porcupine 2 (front) small backward recoil at peak (w = 1) */
    Vector3 p2_recoil = scene->has_anim
        ? scene->anim_min : vec3_create(-0.09, 0.0, 0.0);
    Vector3 p1_forward = scene->has_anim
        ? scene->anim_max : vec3_create( 0.30, 0.0, 0.0);

    double tau = (double)frame_index / (double)total_frames;
    double theta = 2.0 * M_PI * tau;

    /* Smooth cycle w in [0, 1]:
     * tau = 0.00 -> w = 0.0 (both at original base pose)
     * tau = 0.50 -> w = 1.0 (P1 moved forward by p1_forward, P2 recoiled backward by p2_recoil)
     * tau = 1.00 -> w = 0.0 (P1 moved back, P2 returned to its original pose)
     */
    double w = 0.5 * (1.0 - cos(theta));
    Vector3 disp1 = vec3_scale(p1_forward, w);
    Vector3 disp2 = vec3_scale(p2_recoil, w);

    double speed_env = fabs(sin(theta));
    double dir_sign = (sin(theta) >= 0.0) ? 1.0 : -1.0;

    /* Porcupine 1 (rear) grounded legs & body cadence */
    double k1 = fmin(1.0, fmax(0.15, vec3_length(p1_forward) / 0.80));
    double bob1 = 0.018 * k1 * speed_env;
    double sw1 = 0.075 * k1 * sin(theta);
    double lf1_a = 0.032 * k1 * fmax(0.0,  sin(theta) * dir_sign);
    double lf1_b = 0.032 * k1 * fmax(0.0, -sin(theta) * dir_sign);

    out_deltas[ANIM_STATIC] = vec3_create(0.0, 0.0, 0.0);
    out_deltas[ANIM_BODY]   = vec3_add(disp1, vec3_create(0.0, bob1, 0.0));
    out_deltas[ANIM_LEG_A]  = vec3_add(disp1, vec3_create( sw1, lf1_a, 0.0));
    out_deltas[ANIM_LEG_B]  = vec3_add(disp1, vec3_create(-sw1, lf1_b, 0.0));

    /* Porcupine 2 (front) smaller recoil & subtle step back/return */
    double k2 = fmin(1.0, fmax(0.08, vec3_length(p2_recoil) / 0.80));
    double bob2 = 0.012 * k2 * speed_env;
    double sw2 = -0.055 * k2 * sin(theta);
    double lf2_a = 0.020 * k2 * fmax(0.0, -sin(theta) * dir_sign);
    double lf2_b = 0.020 * k2 * fmax(0.0,  sin(theta) * dir_sign);

    out_deltas[ANIM_BODY_2] = vec3_add(disp2, vec3_create(0.0, bob2, 0.0));
    out_deltas[ANIM_LEG_2A] = vec3_add(disp2, vec3_create( sw2, lf2_a, 0.0));
    out_deltas[ANIM_LEG_2B] = vec3_add(disp2, vec3_create(-sw2, lf2_b, 0.0));
}
