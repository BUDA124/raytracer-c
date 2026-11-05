
#include "geometry.h"
#include "vector3.h"

#include <math.h>
#include <stdio.h>

#define POLYGON_EPSILON 1e-9
#define PLANE_EPSILON 1e-7

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

    if (error_message && error_capacity > 0)
        error_message[0] = '\0';

    return true;
}

