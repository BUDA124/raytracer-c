#ifndef RAY_H
#define RAY_H

#include "types.h"

Ray create_primary_ray(Point3 eye, const ViewWindow *window,
                       int width, int height, int i, int j);
void render_scene(const Scene *scene, ColorRGB **framebuffer);

#endif
