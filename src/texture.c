#include "texture.h"
#include "numeric.h"
#include "vector3.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_uint32_be(const uint8_t buf[4]) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] <<  8) |
           ((uint32_t)buf[3]);
}

static bool read_ppm_token(FILE *fp, char *buf, size_t cap) {
    int c;
    for (;;) {
        c = fgetc(fp);
        if (c == EOF) return false;
        if (c == '#') {
            while ((c = fgetc(fp)) != EOF && c != '\n') {}
            if (c == EOF) return false;
            continue;
        }
        if (!isspace(c)) break;
    }

    size_t len = 0;
    while (c != EOF && !isspace(c) && c != '#') {
        if (len + 1 >= cap) return false;
        buf[len++] = (char)c;
        c = fgetc(fp);
    }
    buf[len] = '\0';

    if (c == '#') {
        while ((c = fgetc(fp)) != EOF && c != '\n') {}
    } else if (c != EOF) {
        ungetc(c, fp);
    }
    return len > 0;
}

static bool load_ppm_texture(FILE *fp, Texture *out_texture,
                             char *error_message, size_t error_capacity) {
    char magic[16];
    char w_tok[32], h_tok[32], max_tok[32];

    if (!read_ppm_token(fp, magic, sizeof(magic)) ||
        (strcmp(magic, "P6") != 0 && strcmp(magic, "P3") != 0)) {
        snprintf(error_message, error_capacity,
                 "Unsupported PPM magic (expected P3 or P6)");
        return false;
    }

    if (!read_ppm_token(fp, w_tok, sizeof(w_tok)) ||
        !read_ppm_token(fp, h_tok, sizeof(h_tok)) ||
        !read_ppm_token(fp, max_tok, sizeof(max_tok))) {
        snprintf(error_message, error_capacity, "Truncated PPM header");
        return false;
    }

    char *end = NULL;
    unsigned long w = strtoul(w_tok, &end, 10);
    if (!end || *end != '\0' || w == 0) {
        snprintf(error_message, error_capacity, "Invalid PPM width");
        return false;
    }
    unsigned long h = strtoul(h_tok, &end, 10);
    if (!end || *end != '\0' || h == 0) {
        snprintf(error_message, error_capacity, "Invalid PPM height");
        return false;
    }
    unsigned long maxval = strtoul(max_tok, &end, 10);
    if (!end || *end != '\0' || maxval == 0 || maxval > 65535) {
        snprintf(error_message, error_capacity, "Invalid PPM maxval");
        return false;
    }

    size_t width = (size_t)w;
    size_t height = (size_t)h;
    if (width > SIZE_MAX / height ||
        width * height > SIZE_MAX / sizeof(ColorRGB)) {
        snprintf(error_message, error_capacity, "PPM dimensions too large");
        return false;
    }

    size_t total = width * height;
    ColorRGB *pixels = malloc(total * sizeof(ColorRGB));
    if (!pixels) {
        snprintf(error_message, error_capacity, "Out of memory loading PPM");
        return false;
    }

    double inv_max = 1.0 / (double)maxval;

    if (strcmp(magic, "P6") == 0) {
        /* Consume the single whitespace separator after maxval. */
        int sep = fgetc(fp);
        if (sep == EOF) {
            free(pixels);
            snprintf(error_message, error_capacity, "Unexpected EOF in PPM");
            return false;
        }
        if (maxval < 256) {
            uint8_t rgb[3];
            for (size_t i = 0; i < total; i++) {
                if (fread(rgb, 1, 3, fp) != 3) {
                    free(pixels);
                    snprintf(error_message, error_capacity,
                             "Unexpected EOF in P6 pixel data");
                    return false;
                }
                pixels[i].r = (double)rgb[0] * inv_max;
                pixels[i].g = (double)rgb[1] * inv_max;
                pixels[i].b = (double)rgb[2] * inv_max;
            }
        } else {
            uint8_t rgb16[6];
            for (size_t i = 0; i < total; i++) {
                if (fread(rgb16, 1, 6, fp) != 6) {
                    free(pixels);
                    snprintf(error_message, error_capacity,
                             "Unexpected EOF in 16-bit P6 pixel data");
                    return false;
                }
                unsigned r16 = ((unsigned)rgb16[0] << 8) | rgb16[1];
                unsigned g16 = ((unsigned)rgb16[2] << 8) | rgb16[3];
                unsigned b16 = ((unsigned)rgb16[4] << 8) | rgb16[5];
                pixels[i].r = (double)r16 * inv_max;
                pixels[i].g = (double)g16 * inv_max;
                pixels[i].b = (double)b16 * inv_max;
            }
        }
    } else {
        char val_tok[32];
        for (size_t i = 0; i < total; i++) {
            unsigned long comps[3];
            for (int c = 0; c < 3; c++) {
                if (!read_ppm_token(fp, val_tok, sizeof(val_tok))) {
                    free(pixels);
                    snprintf(error_message, error_capacity,
                             "Unexpected EOF in P3 pixel data");
                    return false;
                }
                comps[c] = strtoul(val_tok, &end, 10);
                if (!end || *end != '\0' || comps[c] > maxval) {
                    free(pixels);
                    snprintf(error_message, error_capacity,
                             "Invalid P3 sample value");
                    return false;
                }
            }
            pixels[i].r = (double)comps[0] * inv_max;
            pixels[i].g = (double)comps[1] * inv_max;
            pixels[i].b = (double)comps[2] * inv_max;
        }
    }

    out_texture->width = width;
    out_texture->height = height;
    out_texture->pixels = pixels;
    return true;
}

static bool load_avs_texture(FILE *fp, Texture *out_texture,
                             char *error_message, size_t error_capacity) {
    uint8_t header[8];
    if (fread(header, 1, 8, fp) != 8) {
        snprintf(error_message, error_capacity, "Truncated AVS header");
        return false;
    }

    size_t width = (size_t)read_uint32_be(header);
    size_t height = (size_t)read_uint32_be(header + 4);
    if (width == 0 || height == 0 ||
        width > SIZE_MAX / height ||
        width * height > SIZE_MAX / sizeof(ColorRGB)) {
        snprintf(error_message, error_capacity, "Invalid AVS dimensions");
        return false;
    }

    size_t total = width * height;
    ColorRGB *pixels = malloc(total * sizeof(ColorRGB));
    if (!pixels) {
        snprintf(error_message, error_capacity, "Out of memory loading AVS");
        return false;
    }

    uint8_t argb[4];
    for (size_t i = 0; i < total; i++) {
        if (fread(argb, 1, 4, fp) != 4) {
            free(pixels);
            snprintf(error_message, error_capacity,
                     "Unexpected EOF in AVS pixel data");
            return false;
        }
        pixels[i].r = (double)argb[1] / 255.0;
        pixels[i].g = (double)argb[2] / 255.0;
        pixels[i].b = (double)argb[3] / 255.0;
    }

    out_texture->width = width;
    out_texture->height = height;
    out_texture->pixels = pixels;
    return true;
}

bool load_texture(const char *filename, Texture *out_texture,
                  char *error_message, size_t error_capacity) {
    if (!filename || !out_texture || !error_message || error_capacity == 0) {
        return false;
    }

    memset(out_texture, 0, sizeof(*out_texture));

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        snprintf(error_message, error_capacity,
                 "Cannot open texture file '%s'", filename);
        return false;
    }

    uint8_t probe[3] = {0, 0, 0};
    size_t probed = fread(probe, 1, 3, fp);
    rewind(fp);

    bool is_ppm = (probed >= 2 && probe[0] == 'P' &&
                   (probe[1] == '6' || probe[1] == '3') &&
                   (probed == 2 || isspace(probe[2]) || probe[2] == '#'));

    bool ok;
    if (is_ppm) {
        ok = load_ppm_texture(fp, out_texture, error_message, error_capacity);
    } else {
        ok = load_avs_texture(fp, out_texture, error_message, error_capacity);
    }

    fclose(fp);
    return ok;
}

void free_texture(Texture *texture) {
    if (!texture) return;
    free(texture->pixels);
    texture->pixels = NULL;
    texture->width = 0;
    texture->height = 0;
}

static double clamp01(double v) {
    if (!isfinite(v) || v <= 0.0) return 0.0;
    if (v >= 1.0) return 1.0;
    return v;
}

static double wrap_coord(double c, WrapMode wrap) {
    if (!isfinite(c)) return 0.0;
    if (wrap == WRAP_REPEAT) {
        double f = c - floor(c);
        if (f >= 1.0) f = 0.0;
        if (f < 0.0) f = 0.0;
        return f;
    }
    return clamp01(c);
}

/* Bilinear sampling on top-down image buffer where v = 0 is bottom, v = 1 is top. */
ColorRGB sample_texture(const Texture *texture, double u, double v,
                        WrapMode wrap) {
    ColorRGB black = {0.0, 0.0, 0.0};
    if (!texture || !texture->pixels || texture->width == 0 || texture->height == 0) {
        return black;
    }

    double uw = wrap_coord(u, wrap);
    double vw = wrap_coord(v, wrap);

    double fx = uw * (double)(texture->width - 1);
    double fy = (1.0 - vw) * (double)(texture->height - 1);

    size_t x0 = (size_t)floor(fx);
    size_t y0 = (size_t)floor(fy);
    double tx = fx - (double)x0;
    double ty = fy - (double)y0;

    size_t x1 = (x0 + 1 < texture->width)
        ? (x0 + 1)
        : (wrap == WRAP_REPEAT ? 0 : texture->width - 1);
    size_t y1 = (y0 + 1 < texture->height)
        ? (y0 + 1)
        : (wrap == WRAP_REPEAT ? 0 : texture->height - 1);

    if (x0 >= texture->width) x0 = texture->width - 1;
    if (y0 >= texture->height) y0 = texture->height - 1;

    const ColorRGB c00 = texture->pixels[y0 * texture->width + x0];
    const ColorRGB c10 = texture->pixels[y0 * texture->width + x1];
    const ColorRGB c01 = texture->pixels[y1 * texture->width + x0];
    const ColorRGB c11 = texture->pixels[y1 * texture->width + x1];

    double w00 = (1.0 - tx) * (1.0 - ty);
    double w10 = tx * (1.0 - ty);
    double w01 = (1.0 - tx) * ty;
    double w11 = tx * ty;

    ColorRGB result = {
        w00 * c00.r + w10 * c10.r + w01 * c01.r + w11 * c11.r,
        w00 * c00.g + w10 * c10.g + w01 * c01.g + w11 * c11.g,
        w00 * c00.b + w10 * c10.b + w01 * c01.b + w11 * c11.b
    };
    return result;
}

ColorRGB evaluate_object_color(const HitRecord *hit, const Scene *scene) {
    if (!hit || !scene || !hit->object) {
        return hit ? hit->material.color : (ColorRGB){0.0, 0.0, 0.0};
    }

    int tex_idx = hit->material.texture;
    if (tex_idx <= 0 || (size_t)tex_idx > scene->texture_count) {
        return hit->material.color;
    }

    const Texture *tex = &scene->textures[(size_t)(tex_idx - 1)];
    const Object *obj = hit->object;
    const TextureMapping *map = &obj->tex_map;

    double u = 0.0;
    double v = 0.0;
    double su = (fabs(map->scale_u) > VECTOR_EPSILON) ? map->scale_u : 1.0;
    double sv = (fabs(map->scale_v) > VECTOR_EPSILON) ? map->scale_v : 1.0;

    switch (obj->type) {
        case OBJ_POLYGON: {
            Vector3 rel = vec3_sub(hit->point, map->origin);
            u = vec3_dot(rel, map->u_axis) / su;
            v = vec3_dot(rel, map->v_axis) / sv;
            break;
        }
        case OBJ_SPHERE: {
            Vector3 d = vec3_scale(vec3_sub(hit->point, obj->geo.sphere.center),
                                   1.0 / obj->geo.sphere.radius);
            double x_loc = vec3_dot(d, map->u_axis);
            double y_loc = vec3_dot(d, map->v_axis);
            double z_loc = vec3_dot(d, map->w_axis);

            if (y_loc < -1.0) y_loc = -1.0;
            if (y_loc > 1.0) y_loc = 1.0;

            double u_raw = (atan2(z_loc, x_loc) + M_PI) / (2.0 * M_PI);
            /* acos(y_loc)/pi is 0 at north pole (+Y) and 1 at south pole (-Y);
             * invert to match sample_texture's v=1 top, v=0 bottom convention. */
            double v_raw = 1.0 - acos(y_loc) / M_PI;
            u = u_raw * su;
            v = v_raw * sv;
            break;
        }
        case OBJ_CYLINDER: {
            const Cylinder *cyl = &obj->geo.cylinder;
            Vector3 rel = vec3_sub(hit->point, cyl->base);
            double h = vec3_dot(rel, cyl->axis);
            double u_coord = vec3_dot(rel, cyl->u_axis);
            double w_coord = vec3_dot(rel, cyl->w_axis);

            double u_raw = (atan2(w_coord, u_coord) + M_PI) / (2.0 * M_PI);
            double v_raw = (h - cyl->h_min) / (cyl->h_max - cyl->h_min);
            u = u_raw * su;
            v = v_raw * sv;
            break;
        }
        case OBJ_CONE: {
            const Cone *cone = &obj->geo.cone;
            Vector3 q = vec3_sub(hit->point, cone->vertex);
            double h = vec3_dot(q, cone->axis);
            double qu = vec3_dot(q, cone->u_axis);
            double qw = vec3_dot(q, cone->w_axis);

            double u_raw = (hypot(qu, qw) < VECTOR_EPSILON)
                ? 0.0
                : (atan2(qw, qu) + M_PI) / (2.0 * M_PI);
            double v_raw = (h - cone->h_min) / (cone->h_max - cone->h_min);
            u = u_raw * su;
            v = v_raw * sv;
            break;
        }
        default:
            return hit->material.color;
    }

    return sample_texture(tex, u, v, map->wrap);
}
