#ifndef RAY_H
#define RAY_H

#include "types.h"

Ray create_primary_ray(Point3 eye, const ViewWindow *window,
                       int width, int height, int i, int j);
Ray create_scene_ray(const Scene *scene, double px, double py);
bool render_scene(const Scene *scene, RenderMode mode, ColorRGB *framebuffer);

#endif
