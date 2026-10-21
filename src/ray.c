#include "ray.h"
#include "vector3.h"
#include "intersect.h"
#include "shader.h"
#include "numeric.h"
#include <math.h>
#include <stdio.h>
#include <limits.h>

Ray create_primary_ray(Point3 eye, const ViewWindow *window,
                       int width, int height, int i, int j) {
    Ray ray;
    ray.origin = eye;

    if (window == NULL || width <= 0 || height <= 0 ||
        i < 0 || j < 0 || i >= width || j >= height){
        return ray;
    }

    double xw = window->x_min +
               ((double)i + 0.5) / (double)width * (window->x_max - window->x_min);
    double yw = window->y_min +
               ((double)j + 0.5) / (double)height * (window->y_max - window->y_min);

    Point3 pixel_point = vec3_create(xw, yw, 0.0);

    Vector3 direction = vec3_sub(pixel_point, eye);
    ray.direction = vec3_normalize(direction);

    return ray;
}

bool render_scene(const Scene *scene, RenderMode mode, ColorRGB *framebuffer) {
    size_t w = scene->width;
    size_t h = scene->height;

    if (!scene || !framebuffer || w == 0 || 
        h == 0 || w > INT_MAX || h > INT_MAX){
        return false;
    }
    if (mode != RENDER_FLAT && mode != RENDER_PHONG) {
        return false;
    }

    for (size_t y = 0; y < h; y++) {
        for (size_t x = 0; x < w; x++) {
            Ray ray = create_primary_ray(scene->eye, 
                &scene->window,(int)w, 
                (int)h, (int)x, (int)y);

            HitRecord hit;
            bool has_hit = first_intersection(&ray, scene,
                PRIMARY_T_MIN, INFINITY, &hit);

            ColorRGB color;

            if (!has_hit) {
                color = scene->background_color;
            } else if (mode == RENDER_FLAT){
                color = shade_flat(&hit);
            } else {
                color = shade_phong(&ray,&hit,scene);
            }
            framebuffer[y * scene->width + x] = color;
        }
    }
    return true;
}