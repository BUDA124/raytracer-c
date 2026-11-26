#ifndef INTERSECT_H
#define INTERSECT_H

#include <stdbool.h>
#include "types.h"

bool passes_clip_planes(Point3 point, const ClipPlane *clips,
                        size_t clip_count);

bool intersect_sphere(const Ray *ray, const Sphere *sphere,
                      const ClipPlane *clips, size_t clip_count,
                      double t_min, double t_max,
                      double *out_t, Vector3 *out_normal);
bool intersect_polygon(const Ray *ray, const Polygon *polygon,
                       const ClipPlane *clips, size_t clip_count,
                       double t_min, double t_max,
                       double *out_t, Vector3 *out_normal);
bool intersect_disk(const Ray *ray, const Disk *disk,
                    const ClipPlane *clips, size_t clip_count,
                    double t_min, double t_max,
                    double *out_t, Vector3 *out_normal);
bool intersect_ellipse(const Ray *ray, const Ellipse *ellipse,
                       const ClipPlane *clips, size_t clip_count,
                       double t_min, double t_max,
                       double *out_t, Vector3 *out_normal);
bool intersect_cylinder(const Ray *ray, const Cylinder *cylinder,
                        const ClipPlane *clips, size_t clip_count,
                        double t_min, double t_max,
                        double *out_t, Vector3 *out_normal);
bool intersect_cone(const Ray *ray, const Cone *cone,
                    const ClipPlane *clips, size_t clip_count,
                    double t_min, double t_max,
                    double *out_t, Vector3 *out_normal);
bool intersect_quadric(const Ray *ray, const Quadric *quadric,
                       const ClipPlane *clips, size_t clip_count,
                       double t_min, double t_max,
                       double *out_t, Vector3 *out_normal);
bool first_intersection(const Ray *ray, const Scene *scene,
                        double t_min, double t_max, HitRecord *out_hit);

#endif
