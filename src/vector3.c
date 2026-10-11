#include "vector3.h"
#include "numeric.h"
#include <math.h>

/* Vector operations */

Vector3 vec3_create(double x, double y, double z) {
    Vector3 v = {x, y, z};
    return v;
}

Vector3 vec3_add(Vector3 a, Vector3 b) {
    return vec3_create(a.x + b.x, a.y + b.y, a.z + b.z);
}

Vector3 vec3_sub(Vector3 a, Vector3 b) {
    return vec3_create(a.x - b.x, a.y - b.y, a.z - b.z);
}

Vector3 vec3_scale(Vector3 v, double scale) {
    return vec3_create(v.x * scale, v.y * scale, v.z * scale);
}

double vec3_dot(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 vec3_cross(Vector3 a, Vector3 b) {
    return vec3_create(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

double vec3_length(Vector3 v) {
    return sqrt(vec3_dot(v, v));
}

Vector3 vec3_normalize(Vector3 v) {
    double len = vec3_length(v);
    if (len < VECTOR_EPSILON) {
        return vec3_create(0.0, 0.0, 0.0);
    }
    return vec3_scale(v, 1.0 / len);
}

Vector3 vec3_reflect(Vector3 incident, Vector3 normal) {
    /* reflect(I, N) = I - 2*(I.N)*N */
    double d = vec3_dot(incident, normal);
    return vec3_sub(incident, vec3_scale(normal, 2.0 * d));
}

