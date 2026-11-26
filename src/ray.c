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
    ray.direction = vec3_create(0.0, 0.0, 1.0);

    if (window == NULL || width <= 0 || height <= 0 ||
        i < 0 || j < 0 || i >= width || j >= height) {
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

Ray create_scene_ray(const Scene *scene, double px, double py) {
    Ray ray;
    ray.origin = scene->eye;

    double xw = scene->window.x_min +
                (px / (double)scene->width) *
                (scene->window.x_max - scene->window.x_min);
    double yw = scene->window.y_min +
                (py / (double)scene->height) *
                (scene->window.y_max - scene->window.y_min);

    Point3 pixel_point;
    if (scene->has_look_at) {
        pixel_point = vec3_add(
            scene->look_at,
            vec3_add(vec3_scale(scene->cam_right, xw),
                     vec3_scale(scene->cam_up, yw)));
    } else {
        pixel_point = vec3_create(xw, yw, 0.0);
    }

    ray.direction = vec3_normalize(vec3_sub(pixel_point, scene->eye));
    return ray;
}

bool render_scene(const Scene *scene, RenderMode mode, ColorRGB *framebuffer) {
    if (!scene || !framebuffer) {
        return false;
    }

    size_t w = scene->width;
    size_t h = scene->height;

    if (w == 0 || h == 0 || w > INT_MAX || h > INT_MAX) {
        return false;
    }
    if (mode != RENDER_FLAT && mode != RENDER_PHONG) {
        return false;
    }

    int aa = scene->aa_samples > 0 ? scene->aa_samples : 1;
    double inv_samples = 1.0 / (double)(aa * aa);

    for (size_t y = 0; y < h; y++) {
        for (size_t x = 0; x < w; x++) {
            ColorRGB accum = {0.0, 0.0, 0.0};

            for (int sy = 0; sy < aa; sy++) {
                double py = (double)y + ((double)sy + 0.5) / (double)aa;
                for (int sx = 0; sx < aa; sx++) {
                    double px = (double)x + ((double)sx + 0.5) / (double)aa;
                    Ray ray = create_scene_ray(scene, px, py);

                    HitRecord hit;
                    bool has_hit = first_intersection(&ray, scene,
                                                      PRIMARY_T_MIN, INFINITY,
                                                      &hit);

                    ColorRGB sample;
                    if (!has_hit) {
                        sample = scene->background_color;
                    } else if (mode == RENDER_FLAT) {
                        sample = shade_flat(&hit, scene->background_color, true);
                    } else {
                        sample = shade_phong(&ray, &hit, scene);
                    }

                    accum.r += sample.r;
                    accum.g += sample.g;
                    accum.b += sample.b;
                }
            }

            framebuffer[y * w + x] = (ColorRGB){
                accum.r * inv_samples,
                accum.g * inv_samples,
                accum.b * inv_samples
            };
        }
    }
    return true;
}
