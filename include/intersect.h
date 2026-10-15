#ifndef INTERSECT_H
#define INTERSECT_H

#include <stdbool.h>
#include "types.h"

bool intersect_sphere(const Ray *ray, const Sphere *sphere,
                      double t_min, double t_max,
                      double *out_t, Vector3 *out_normal);
bool intersect_polygon(const Ray *ray, const Polygon *polygon,
                       double t_min, double t_max,
                       double *out_t, Vector3 *out_normal);
bool first_intersection(const Ray *ray, const Scene *scene,
                        double t_min, double t_max, HitRecord *out_hit);

#endif
