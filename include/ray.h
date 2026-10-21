#ifndef RAY_H
#define RAY_H

#include "types.h"

Ray create_primary_ray(Point3 eye, const ViewWindow *window,
                       int width, int height, int i, int j);
bool render_scene(const Scene *scene, RenderMode mode, ColorRGB *framebuffer);

#endif
