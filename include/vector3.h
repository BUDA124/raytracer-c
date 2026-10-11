#ifndef VECTOR3_H
#define VECTOR3_H

#include "types.h"

Vector3 vec3_create(double x, double y, double z);
Vector3 vec3_add(Vector3 a, Vector3 b);
Vector3 vec3_sub(Vector3 a, Vector3 b);
Vector3 vec3_scale(Vector3 v, double scale);
double vec3_dot(Vector3 a, Vector3 b);
Vector3 vec3_cross(Vector3 a, Vector3 b);
double vec3_length(Vector3 v);
Vector3 vec3_normalize(Vector3 v);
Vector3 vec3_reflect(Vector3 incident, Vector3 normal);

#endif
