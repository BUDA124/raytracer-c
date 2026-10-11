#ifndef RAY_H
#define RAY_H

#include "types.h"

Ray create_primary_ray(Point3 eye, const ViewWindow *window,
                       size_t width, size_t height, size_t x, size_t y);
bool render_scene(const Scene *scene, RenderMode mode, ColorRGB *framebuffer);

#endif
