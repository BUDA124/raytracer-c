#ifndef GEOMETRY_H
#define GEOMETRY_H

#include "types.h"

void build_axis_basis(Vector3 axis, Vector3 *out_u, Vector3 *out_w);

bool prepare_polygon(Polygon *polygon, char *error_message,
                     size_t error_capacity);
void init_polygon_default_texmap(const Polygon *polygon,
                                 TextureMapping *out_map);
void init_sphere_default_texmap(TextureMapping *out_map);
bool prepare_disk(Disk *disk, char *error_message, size_t error_capacity);
bool prepare_ellipse(Ellipse *ellipse, char *error_message,
                     size_t error_capacity);
bool prepare_cylinder(Cylinder *cylinder, char *error_message,
                      size_t error_capacity);
bool prepare_cone(Cone *cone, char *error_message, size_t error_capacity);
bool prepare_quadric(const Quadric *quadric, char *error_message,
                     size_t error_capacity);

#endif
