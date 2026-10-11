#include "ray.h"
#include "vector3.h"
#include "intersect.h"
#include "shader.h"
#include "numeric.h"
#include <math.h>
#include <stdio.h>

Ray create_primary_ray(Point3 eye, const ViewWindow *window,
                       size_t width, size_t height, size_t x, size_t y) {
    double u = window->x_min +
               ((double)x + 0.5) / (double)width * (window->x_max - window->x_min);
    double v = window->y_min +
               ((double)y + 0.5) / (double)height * (window->y_max - window->y_min);

    Point3 pixel_point = vec3_create(u, v, 0.0);
    Vector3 direction = vec3_normalize(vec3_sub(pixel_point, eye));

    Ray ray;
    ray.origin = eye;
    ray.direction = direction;
    return ray;
}

bool render_scene(const Scene *scene, RenderMode mode, ColorRGB *framebuffer) {
    if (!scene || !framebuffer) {
        fprintf(stderr, "Error: render_scene received NULL arguments.\n");
        return false;
    }

    if (mode == RENDER_PHONG) {
        fprintf(stderr, "Error: Phong rendering mode is not yet implemented.\n");
        return false;
    }

    if (mode != RENDER_FLAT) {
        fprintf(stderr, "Error: Unknown rendering mode.\n");
        return false;
    }

    size_t w = scene->width;
    size_t h = scene->height;

    for (size_t y = 0; y < h; y++) {
        for (size_t x = 0; x < w; x++) {
            Ray ray = create_primary_ray(scene->eye, &scene->window,
                                         w, h, x, y);
            HitRecord hit;
            bool has_hit = first_intersection(&ray, scene,
                                              PRIMARY_T_MIN, INFINITY, &hit);
            if (!has_hit) {
                framebuffer[y * w + x] = scene->background_color;
            } else {
                framebuffer[y * w + x] = shade_flat(&hit);
            }
        }
    }
    return true;
}

