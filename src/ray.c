#include "ray.h"
#include "vector3.h"
#include "intersect.h"
#include "shader.h"
#include "numeric.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

Ray create_primary_ray(Point3 eye, const ViewWindow *window,
                       int width, int height, int i, int j) {
    double u = window->x_min +
               ((double)i + 0.5) / (double)width * (window->x_max - window->x_min);
    double v = window->y_min +
               ((double)j + 0.5) / (double)height * (window->y_max - window->y_min);

    Point3 pixel_point = vec3_create(u, v, 0.0);
    Vector3 direction = vec3_normalize(vec3_sub(pixel_point, eye));

    Ray ray;
    ray.origin = eye;
    ray.direction = direction;
    return ray;
}

void render_scene(const Scene *scene, ColorRGB **framebuffer) {
    if (!scene || !framebuffer) {
        fprintf(stderr, "Error: render_scene received NULL arguments.\n");
        return;
    }

    int w = (int)scene->width;
    int h = (int)scene->height;

    size_t pixel_count = (size_t)w * (size_t)h;
    *framebuffer = malloc(pixel_count * sizeof(ColorRGB));
    if (!*framebuffer) {
        fprintf(stderr, "Error: Failed to allocate framebuffer.\n");
        return;
    }

    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            Ray ray = create_primary_ray(scene->eye, &scene->window,
                                         w, h, i, j);
            HitRecord hit;
            bool has_hit = first_intersection(&ray, scene,
                                              PRIMARY_T_MIN, INFINITY, &hit);
            (*framebuffer)[j * w + i] = shade_flat(&hit, scene->background_color,
                                                   has_hit);
        }
    }
}
