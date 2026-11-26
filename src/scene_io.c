#include "scene_io.h"
#include "numeric.h"
#include "vector3.h"
#include "geometry.h"
#include "texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>

#define MAX_LINE_LENGTH 1024
#define INITIAL_OBJECT_CAPACITY 16
#define DEFAULT_MAX_DEPTH 5
#define MAX_ALLOWED_DEPTH 32
#define DEFAULT_AA_SAMPLES 1
#define MAX_ALLOWED_AA 8

static void strip_comment(char *line) {
    char *p = strchr(line, '#');
    if (p) *p = '\0';

    size_t len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) {
        line[--len] = '\0';
    }
}

static const char *skip_spaces(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static bool is_blank(const char *s) {
    s = skip_spaces(s);
    return *s == '\0';
}

static bool parse_double(const char **pos, double *out) {
    const char *start = skip_spaces(*pos);
    if (*start == '\0') return false;

    char *end;
    errno = 0;
    double val = strtod(start, &end);
    if (end == start || errno == ERANGE || !isfinite(val)) return false;
    *out = val;
    *pos = end;
    return true;
}

static bool parse_size(const char **pos, size_t *out) {
    const char *start = skip_spaces(*pos);
    if (*start == '\0') return false;

    if (*start == '-') return false;

    char *end;
    errno = 0;
    unsigned long val = strtoul(start, &end, 10);
    if (end == start || errno == ERANGE || val == 0) return false;
    *out = (size_t)val;
    *pos = end;
    return true;
}

/* Parses a base-10 integer token. The token must end at whitespace or at
 * the end of the line, so values such as "5.5" or "5x" are rejected. */
static bool parse_int(const char **pos, long *out) {
    const char *start = skip_spaces(*pos);
    if (*start == '\0') return false;

    char *end;
    errno = 0;
    long val = strtol(start, &end, 10);
    if (end == start || errno == ERANGE) return false;
    if (*end != '\0' && !isspace((unsigned char)*end)) return false;
    *out = val;
    *pos = end;
    return true;
}

static bool parse_token(const char **pos, char *out, size_t cap) {
    const char *start = skip_spaces(*pos);
    if (*start == '\0' || cap == 0) return false;

    size_t len = 0;
    while (*start && !isspace((unsigned char)*start)) {
        if (len + 1 >= cap) return false;
        out[len++] = *start++;
    }
    out[len] = '\0';
    *pos = start;
    return len > 0;
}

/* Parses up to max_count optional numbers and returns how many were read.
 * The position is left just after the last number successfully parsed. */
static size_t parse_optional_doubles(const char **pos, double *out,
                                     size_t max_count) {
    size_t count = 0;
    while (count < max_count && parse_double(pos, &out[count])) {
        count++;
    }
    return count;
}

/* The color mixing weights must each lie in [0, 1] and add up to 1. */
static bool validate_mix(double o1, double o2, double o3) {
    if (o1 < 0.0 || o1 > 1.0 || o2 < 0.0 || o2 > 1.0 ||
        o3 < 0.0 || o3 > 1.0) {
        return false;
    }
    return fabs(o1 + o2 + o3 - 1.0) <= 1e-6;
}

/* A texture index is a non-negative integer; 0 means no texture. */
static bool to_texture_index(double value, int *out) {
    if (value < 0.0 || value != floor(value) || value > (double)INT_MAX) {
        return false;
    }
    *out = (int)value;
    return true;
}

static bool validate_color(double r, double g, double b) {
    return r >= 0.0 && r <= 1.0 &&
           g >= 0.0 && g <= 1.0 &&
           b >= 0.0 && b <= 1.0;
}

static bool expect_end(const char *pos) {
    return is_blank(pos);
}

static bool parse_wrap_mode(const char **pos, WrapMode *out_wrap) {
    const char *saved = *pos;
    char tok[32];
    if (!parse_token(pos, tok, sizeof(tok))) {
        *pos = saved;
        return false;
    }
    if (strcmp(tok, "repeat") == 0) {
        *out_wrap = WRAP_REPEAT;
        return true;
    }
    if (strcmp(tok, "clamp") == 0) {
        *out_wrap = WRAP_CLAMP;
        return true;
    }
    *pos = saved;
    return false;
}

static bool parse_material_tail(const char **pos, Material *out_mat,
                                const char *kind, size_t line_num,
                                char *error_message, size_t error_capacity) {
    double r, g, b, ka, kd, ks, shininess;
    if (!parse_double(pos, &r) || !parse_double(pos, &g) ||
        !parse_double(pos, &b) || !parse_double(pos, &ka) ||
        !parse_double(pos, &kd) || !parse_double(pos, &ks) ||
        !parse_double(pos, &shininess)) {
        snprintf(error_message, error_capacity,
                 "Line %zu: %s requires material r g b ka kd ks shininess",
                 line_num, kind);
        return false;
    }

    double o1 = 1.0, o2 = 0.0, o3 = 0.0;
    int texture = 0;
    double extra[4];
    size_t extra_count = parse_optional_doubles(pos, extra, 4);

    if ((extra_count != 0 && extra_count != 3 && extra_count != 4) ||
        !expect_end(*pos)) {
        snprintf(error_message, error_capacity,
                 "Line %zu: %s accepts optional [o1 o2 o3 [texture]] after material",
                 line_num, kind);
        return false;
    }

    if (extra_count >= 3) {
        o1 = extra[0];
        o2 = extra[1];
        o3 = extra[2];
    }
    if (extra_count == 4 && !to_texture_index(extra[3], &texture)) {
        snprintf(error_message, error_capacity,
                 "Line %zu: %s texture must be a non-negative integer",
                 line_num, kind);
        return false;
    }
    if (!validate_color(r, g, b) ||
        ka < 0.0 || ka > 1.0 ||
        kd < 0.0 || kd > 1.0 ||
        ks < 0.0 || ks > 1.0 ||
        shininess < 1.0) {
        snprintf(error_message, error_capacity,
                 "Line %zu: Invalid %s color or material coefficients",
                 line_num, kind);
        return false;
    }
    if (!validate_mix(o1, o2, o3)) {
        snprintf(error_message, error_capacity,
                 "Line %zu: %s O1, O2, O3 must be in [0, 1] and sum to 1",
                 line_num, kind);
        return false;
    }

    out_mat->color = (ColorRGB){r, g, b};
    out_mat->ka = ka;
    out_mat->kd = kd;
    out_mat->ks = ks;
    out_mat->shininess = shininess;
    out_mat->o1 = o1;
    out_mat->o2 = o2;
    out_mat->o3 = o3;
    out_mat->texture = texture;
    return true;
}

static void init_default_texmap(TextureMapping *map) {
    memset(map, 0, sizeof(*map));
    map->wrap = WRAP_REPEAT;
    map->scale_u = 1.0;
    map->scale_v = 1.0;
}

static bool add_object(Object **objects, size_t *count, size_t *capacity,
                       const Object *obj) {
    if (*count >= *capacity) {
        size_t new_cap = (*capacity == 0) ? INITIAL_OBJECT_CAPACITY
                                          : *capacity * 2;
        Object *tmp = realloc(*objects, new_cap * sizeof(Object));
        if (!tmp) return false;
        *objects = tmp;
        *capacity = new_cap;
    }
    (*objects)[*count] = *obj;
    (*count)++;
    return true;
}

static bool add_clip_plane(Object *obj, ClipPlane plane) {
    size_t new_count = obj->clip_count + 1;
    ClipPlane *tmp = realloc(obj->clip_planes, new_count * sizeof(ClipPlane));
    if (!tmp) return false;
    obj->clip_planes = tmp;
    obj->clip_planes[obj->clip_count] = plane;
    obj->clip_count = new_count;
    return true;
}

static void free_object_resources(Object *obj) {
    if (!obj) return;
    if (obj->type == OBJ_POLYGON && obj->geo.polygon.vertices) {
        free(obj->geo.polygon.vertices);
        obj->geo.polygon.vertices = NULL;
    }
    if (obj->clip_planes) {
        free(obj->clip_planes);
        obj->clip_planes = NULL;
        obj->clip_count = 0;
    }
}

void free_scene(Scene *scene) {
    if (!scene) return;

    if (scene->objects) {
        for (size_t i = 0; i < scene->object_count; i++) {
            free_object_resources(&scene->objects[i]);
        }
        free(scene->objects);
    }
    if (scene->lights) {
        free(scene->lights);
    }
    if (scene->textures) {
        for (size_t i = 0; i < scene->texture_count; i++) {
            free_texture(&scene->textures[i]);
        }
        free(scene->textures);
    }

    memset(scene, 0, sizeof(Scene));
}

bool load_scene(const char *filename, Scene *out_scene,
                char *error_message, size_t error_capacity) {
    if (!filename || !out_scene || !error_message || error_capacity == 0) {
        if (error_message && error_capacity > 0)
            snprintf(error_message, error_capacity, "Invalid arguments");
        return false;
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        snprintf(error_message, error_capacity,
                 "Cannot open file '%s'", filename);
        return false;
    }

    Scene tmp = {0};
    bool has_window = false, has_eye = false, has_res = false;
    bool has_bg = false, has_ambient = false, has_depth = false;
    bool has_aa = false, has_look_at = false;
    Vector3 look_up = vec3_create(0.0, 1.0, 0.0);

    tmp.max_depth = DEFAULT_MAX_DEPTH;
    tmp.aa_samples = DEFAULT_AA_SAMPLES;

    Object *objects = NULL;
    size_t obj_count = 0, obj_capacity = 0;

    char line_buf[MAX_LINE_LENGTH];
    size_t line_num = 0;
    bool success = true;

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        line_num++;

        size_t line_len = strlen(line_buf);
        if (line_len > 0 && line_buf[line_len - 1] != '\n' && !feof(fp)) {
            snprintf(error_message, error_capacity,
                     "Line %zu: Line too long (max %d characters)",
                     line_num, MAX_LINE_LENGTH - 2);
            success = false;
            break;
        }

        strip_comment(line_buf);
        if (is_blank(line_buf)) continue;

        const char *pos = skip_spaces(line_buf);

        char keyword[32];
        int kw_len = 0;
        while (*pos && !isspace((unsigned char)*pos) && kw_len < 31) {
            keyword[kw_len++] = *pos++;
        }
        keyword[kw_len] = '\0';

        if (kw_len == 0) continue;

        if (strcmp(keyword, "WINDOW") == 0) {
            if (has_window) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Duplicate WINDOW directive", line_num);
                success = false;
                break;
            }
            double xmin, ymin, xmax, ymax;
            if (!parse_double(&pos, &xmin) || !parse_double(&pos, &ymin) ||
                !parse_double(&pos, &xmax) || !parse_double(&pos, &ymax)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: WINDOW requires 4 finite numbers", line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: WINDOW has extra tokens", line_num);
                success = false;
                break;
            }
            if (xmax <= xmin || ymax <= ymin) {
                snprintf(error_message, error_capacity,
                         "Line %zu: WINDOW requires xmax > xmin and ymax > ymin",
                         line_num);
                success = false;
                break;
            }
            tmp.window.x_min = xmin;
            tmp.window.y_min = ymin;
            tmp.window.x_max = xmax;
            tmp.window.y_max = ymax;
            has_window = true;
        }

        else if (strcmp(keyword, "EYE") == 0) {
            if (has_eye) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Duplicate EYE directive", line_num);
                success = false;
                break;
            }
            double x, y, z;
            if (!parse_double(&pos, &x) || !parse_double(&pos, &y) ||
                !parse_double(&pos, &z)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: EYE requires 3 finite numbers", line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: EYE has extra tokens", line_num);
                success = false;
                break;
            }
            tmp.eye.x = x;
            tmp.eye.y = y;
            tmp.eye.z = z;
            has_eye = true;
        }

        else if (strcmp(keyword, "LOOKAT") == 0) {
            if (has_look_at) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Duplicate LOOKAT directive", line_num);
                success = false;
                break;
            }
            double vals[6];
            size_t count = parse_optional_doubles(&pos, vals, 6);
            if ((count != 3 && count != 6) || !expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: LOOKAT requires lx ly lz [ux uy uz]", line_num);
                success = false;
                break;
            }
            tmp.look_at = vec3_create(vals[0], vals[1], vals[2]);
            if (count == 6) {
                look_up = vec3_create(vals[3], vals[4], vals[5]);
            }
            tmp.has_look_at = true;
            has_look_at = true;
        }

        else if (strcmp(keyword, "RES") == 0) {
            if (has_res) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Duplicate RES directive", line_num);
                success = false;
                break;
            }
            size_t w, h;
            if (!parse_size(&pos, &w) || !parse_size(&pos, &h)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: RES requires 2 positive integers", line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: RES has extra tokens", line_num);
                success = false;
                break;
            }
            if (w > (size_t)UINT32_MAX || h > (size_t)UINT32_MAX) {
                snprintf(error_message, error_capacity,
                         "Line %zu: RES dimensions exceed AVS limits", line_num);
                success = false;
                break;
            }
            tmp.width = w;
            tmp.height = h;
            has_res = true;
        }

        else if (strcmp(keyword, "BG") == 0) {
            if (has_bg) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Duplicate BG directive", line_num);
                success = false;
                break;
            }
            double r, g, b;
            if (!parse_double(&pos, &r) || !parse_double(&pos, &g) ||
                !parse_double(&pos, &b)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: BG requires 3 finite numbers", line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: BG has extra tokens", line_num);
                success = false;
                break;
            }
            if (!validate_color(r, g, b)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: BG color components must be in [0, 1]",
                         line_num);
                success = false;
                break;
            }
            tmp.background_color.r = r;
            tmp.background_color.g = g;
            tmp.background_color.b = b;
            has_bg = true;
        }

        else if (strcmp(keyword, "AMBIENT") == 0) {
            if (has_ambient) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Duplicate AMBIENT directive", line_num);
                success = false;
                break;
            }
            double vals[3];
            size_t count = parse_optional_doubles(&pos, vals, 3);
            if ((count != 1 && count != 3) || !expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: AMBIENT requires Ia or r g b", line_num);
                success = false;
                break;
            }
            double r = vals[0];
            double g = (count == 3) ? vals[1] : vals[0];
            double b = (count == 3) ? vals[2] : vals[0];
            if (!validate_color(r, g, b)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: AMBIENT components must be in [0, 1]",
                         line_num);
                success = false;
                break;
            }
            tmp.ambient_light.r = r;
            tmp.ambient_light.g = g;
            tmp.ambient_light.b = b;
            has_ambient = true;
        }

        else if (strcmp(keyword, "DEPTH") == 0) {
            if (has_depth) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Duplicate DEPTH directive", line_num);
                success = false;
                break;
            }
            long depth;
            if (!parse_int(&pos, &depth)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: DEPTH requires an integer", line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: DEPTH has extra tokens", line_num);
                success = false;
                break;
            }
            if (depth < 0 || depth > MAX_ALLOWED_DEPTH) {
                snprintf(error_message, error_capacity,
                         "Line %zu: DEPTH must be in [0, %d] (got %ld)",
                         line_num, MAX_ALLOWED_DEPTH, depth);
                success = false;
                break;
            }
            tmp.max_depth = (int)depth;
            has_depth = true;
        }

        else if (strcmp(keyword, "AA") == 0) {
            if (has_aa) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Duplicate AA directive", line_num);
                success = false;
                break;
            }
            long aa;
            if (!parse_int(&pos, &aa) || !expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: AA requires an integer", line_num);
                success = false;
                break;
            }
            if (aa < 1 || aa > MAX_ALLOWED_AA) {
                snprintf(error_message, error_capacity,
                         "Line %zu: AA must be in [1, %d] (got %ld)",
                         line_num, MAX_ALLOWED_AA, aa);
                success = false;
                break;
            }
            tmp.aa_samples = (int)aa;
            has_aa = true;
        }

        else if (strcmp(keyword, "TEXTURE") == 0) {
            char first[512];
            char second[512];
            if (!parse_token(&pos, first, sizeof(first))) {
                snprintf(error_message, error_capacity,
                         "Line %zu: TEXTURE requires [id] <filepath>", line_num);
                success = false;
                break;
            }
            const char *path = first;
            if (parse_token(&pos, second, sizeof(second))) {
                const char *id_pos = first;
                long explicit_id = 0;
                if (!parse_int(&id_pos, &explicit_id) || !expect_end(id_pos) ||
                    explicit_id != (long)(tmp.texture_count + 1)) {
                    snprintf(error_message, error_capacity,
                             "Line %zu: TEXTURE id must be sequential (expected %zu)",
                             line_num, tmp.texture_count + 1);
                    success = false;
                    break;
                }
                path = second;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: TEXTURE has extra tokens", line_num);
                success = false;
                break;
            }

            Texture tex;
            char tex_err[256];
            if (!load_texture(path, &tex, tex_err, sizeof(tex_err))) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Failed to load TEXTURE '%s': %s",
                         line_num, path, tex_err);
                success = false;
                break;
            }

            size_t new_count = tmp.texture_count + 1;
            Texture *new_textures = realloc(tmp.textures,
                                            new_count * sizeof(Texture));
            if (!new_textures) {
                free_texture(&tex);
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for TEXTURE",
                         line_num);
                success = false;
                break;
            }
            tmp.textures = new_textures;
            tmp.textures[tmp.texture_count] = tex;
            tmp.texture_count = new_count;
        }

        else if (strcmp(keyword, "LIGHT") == 0) {
            double vals[9];
            size_t count = parse_optional_doubles(&pos, vals, 9);
            if ((count != 4 && count != 6 && count != 7 && count != 9) ||
                !expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: LIGHT requires x y z Ip [c1 c2 c3] or "
                         "x y z r g b [c1 c2 c3]", line_num);
                success = false;
                break;
            }

            double lx = vals[0], ly = vals[1], lz = vals[2];
            double lr, lg, lb;
            double c1 = 1.0, c2 = 0.0, c3 = 0.0;

            if (count == 4 || count == 7) {
                lr = lg = lb = vals[3];
                if (count == 7) {
                    c1 = vals[4];
                    c2 = vals[5];
                    c3 = vals[6];
                }
            } else {
                lr = vals[3];
                lg = vals[4];
                lb = vals[5];
                if (count == 9) {
                    c1 = vals[6];
                    c2 = vals[7];
                    c3 = vals[8];
                }
            }

            if (!validate_color(lr, lg, lb)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: LIGHT intensity must be in [0, 1]",
                         line_num);
                success = false;
                break;
            }
            if (c1 < 0.0 || c2 < 0.0 || c3 < 0.0 ||
                (c1 == 0.0 && c2 == 0.0 && c3 == 0.0)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: LIGHT requires nonnegative c1/c2/c3, not all zero",
                         line_num);
                success = false;
                break;
            }

            size_t new_count = tmp.light_count + 1;
            Light *new_lights = realloc(tmp.lights, new_count * sizeof(Light));
            if (!new_lights) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for LIGHT",
                         line_num);
                success = false;
                break;
            }
            tmp.lights = new_lights;
            Light *light = &tmp.lights[tmp.light_count];
            light->position = vec3_create(lx, ly, lz);
            light->intensity.r = lr;
            light->intensity.g = lg;
            light->intensity.b = lb;
            light->c1 = c1;
            light->c2 = c2;
            light->c3 = c3;
            tmp.light_count = new_count;
        }

        else if (strcmp(keyword, "SPHERE") == 0) {
            double x, y, z, radius, r, g, b;
            if (!parse_double(&pos, &x) || !parse_double(&pos, &y) ||
                !parse_double(&pos, &z) || !parse_double(&pos, &radius) ||
                !parse_double(&pos, &r) || !parse_double(&pos, &g) ||
                !parse_double(&pos, &b)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: SPHERE requires at least 7 values "
                         "(x y z radius r g b)", line_num);
                success = false;
                break;
            }

            double ka = 0.1, kd = 0.7, ks = 0.0, shininess = 32.0;
            double o1 = 1.0, o2 = 0.0, o3 = 0.0;
            int texture = 0;
            double extra[8];
            size_t extra_count = parse_optional_doubles(&pos, extra, 8);

            if (extra_count != 0 && extra_count != 4 &&
                extra_count != 7 && extra_count != 8) {
                snprintf(error_message, error_capacity,
                         "Line %zu: SPHERE requires 7, 11, 14 or 15 values "
                         "(x y z radius r g b [ka kd ks shininess "
                         "[o1 o2 o3 [texture]]])", line_num);
                success = false;
                break;
            }
            if (extra_count >= 4) {
                ka = extra[0];
                kd = extra[1];
                ks = extra[2];
                shininess = extra[3];
            }
            if (extra_count >= 7) {
                o1 = extra[4];
                o2 = extra[5];
                o3 = extra[6];
            }
            if (extra_count == 8 && !to_texture_index(extra[7], &texture)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: SPHERE texture must be a non-negative "
                         "integer", line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: SPHERE has extra tokens", line_num);
                success = false;
                break;
            }
            if (radius <= 0.0) {
                snprintf(error_message, error_capacity,
                         "Line %zu: SPHERE radius must be positive", line_num);
                success = false;
                break;
            }
            if (!validate_color(r, g, b)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: SPHERE color must be in [0, 1]", line_num);
                success = false;
                break;
            }
            if (ka < 0.0 || ka > 1.0 || kd < 0.0 || kd > 1.0 ||
                ks < 0.0 || ks > 1.0) {
                snprintf(error_message, error_capacity,
                         "Line %zu: SPHERE coefficients must be in [0, 1]",
                         line_num);
                success = false;
                break;
            }
            if (shininess < 1.0) {
                snprintf(error_message, error_capacity,
                         "Line %zu: SPHERE shininess must be >= 1", line_num);
                success = false;
                break;
            }
            if (!validate_mix(o1, o2, o3)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: SPHERE O1, O2, O3 must be in [0, 1] and "
                         "O1 + O2 + O3 must equal 1", line_num);
                success = false;
                break;
            }

            Object obj;
            memset(&obj, 0, sizeof(obj));
            obj.type = OBJ_SPHERE;
            obj.material.color.r = r;
            obj.material.color.g = g;
            obj.material.color.b = b;
            obj.material.ka = ka;
            obj.material.kd = kd;
            obj.material.ks = ks;
            obj.material.shininess = shininess;
            obj.material.o1 = o1;
            obj.material.o2 = o2;
            obj.material.o3 = o3;
            obj.material.texture = texture;
            obj.geo.sphere.center = vec3_create(x, y, z);
            obj.geo.sphere.radius = radius;
            init_sphere_default_texmap(&obj.tex_map);

            if (!add_object(&objects, &obj_count, &obj_capacity, &obj)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for SPHERE",
                         line_num);
                success = false;
                break;
            }
        }

        else if (strcmp(keyword, "POLYGON") == 0) {
            size_t vertex_count;
            size_t polygon_line = line_num;

            if (!parse_size(&pos, &vertex_count) || vertex_count < 3) {
                snprintf(error_message, error_capacity,
                         "Line %zu: POLYGON requires vertex_count >= 3",
                         polygon_line);
                success = false;
                break;
            }

            Material mat;
            if (!parse_material_tail(&pos, &mat, "POLYGON", polygon_line,
                                     error_message, error_capacity)) {
                success = false;
                break;
            }

            if (vertex_count > SIZE_MAX / sizeof(Point3)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Too many POLYGON vertices", polygon_line);
                success = false;
                break;
            }

            Polygon polygon = {0};
            polygon.vertex_count = vertex_count;
            polygon.vertices = malloc(vertex_count * sizeof(Point3));
            if (!polygon.vertices) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for POLYGON",
                         polygon_line);
                success = false;
                break;
            }

            for (size_t i = 0; i < vertex_count; ) {
                if (!fgets(line_buf, sizeof(line_buf), fp)) {
                    snprintf(error_message, error_capacity,
                             "Line %zu: POLYGON expected %zu vertices, got %zu",
                             polygon_line, vertex_count, i);
                    success = false;
                    break;
                }

                ++line_num;
                line_len = strlen(line_buf);
                if (line_len > 0 &&
                    line_buf[line_len - 1] != '\n' && !feof(fp)) {
                    snprintf(error_message, error_capacity,
                             "Line %zu: Polygon vertex line is too long",
                             line_num);
                    success = false;
                    break;
                }

                strip_comment(line_buf);
                if (is_blank(line_buf)) continue;

                const char *vertex_pos = skip_spaces(line_buf);
                double x, y, z;
                if (!parse_double(&vertex_pos, &x) ||
                    !parse_double(&vertex_pos, &y) ||
                    !parse_double(&vertex_pos, &z) ||
                    !expect_end(vertex_pos)) {
                    snprintf(error_message, error_capacity,
                             "Line %zu: POLYGON vertex requires x y z",
                             line_num);
                    success = false;
                    break;
                }

                polygon.vertices[i] = vec3_create(x, y, z);
                ++i;
            }

            if (success && !prepare_polygon(&polygon, error_message,
                                            error_capacity)) {
                char detail[128];
                snprintf(detail, sizeof(detail), "%s", error_message);
                snprintf(error_message, error_capacity,
                         "Line %zu: Invalid POLYGON: %s",
                         polygon_line, detail);
                success = false;
            }

            if (!success) {
                free(polygon.vertices);
                break;
            }

            Object obj;
            memset(&obj, 0, sizeof(obj));
            obj.type = OBJ_POLYGON;
            obj.material = mat;
            obj.geo.polygon = polygon;
            init_polygon_default_texmap(&obj.geo.polygon, &obj.tex_map);

            if (!add_object(&objects, &obj_count, &obj_capacity, &obj)) {
                free(polygon.vertices);
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for POLYGON",
                         polygon_line);
                success = false;
                break;
            }
        }

        else if (strcmp(keyword, "DISK") == 0) {
            double cx, cy, cz, nx, ny, nz, radius, inner_radius;
            if (!parse_double(&pos, &cx) || !parse_double(&pos, &cy) ||
                !parse_double(&pos, &cz) || !parse_double(&pos, &nx) ||
                !parse_double(&pos, &ny) || !parse_double(&pos, &nz) ||
                !parse_double(&pos, &radius) || !parse_double(&pos, &inner_radius)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: DISK requires cx cy cz nx ny nz radius "
                         "inner_radius followed by material", line_num);
                success = false;
                break;
            }

            Material mat;
            if (!parse_material_tail(&pos, &mat, "DISK", line_num,
                                     error_message, error_capacity)) {
                success = false;
                break;
            }

            Disk disk;
            disk.center = vec3_create(cx, cy, cz);
            disk.normal = vec3_create(nx, ny, nz);
            disk.radius = radius;
            disk.inner_radius = inner_radius;

            char detail[128];
            if (!prepare_disk(&disk, detail, sizeof(detail))) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Invalid DISK: %s", line_num, detail);
                success = false;
                break;
            }

            Object obj;
            memset(&obj, 0, sizeof(obj));
            obj.type = OBJ_DISK;
            obj.material = mat;
            obj.geo.disk = disk;
            init_default_texmap(&obj.tex_map);

            if (!add_object(&objects, &obj_count, &obj_capacity, &obj)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for DISK",
                         line_num);
                success = false;
                break;
            }
        }

        else if (strcmp(keyword, "ELLIPSE") == 0) {
            double cx, cy, cz, nx, ny, nz, ux, uy, uz, a, b;
            if (!parse_double(&pos, &cx) || !parse_double(&pos, &cy) ||
                !parse_double(&pos, &cz) || !parse_double(&pos, &nx) ||
                !parse_double(&pos, &ny) || !parse_double(&pos, &nz) ||
                !parse_double(&pos, &ux) || !parse_double(&pos, &uy) ||
                !parse_double(&pos, &uz) || !parse_double(&pos, &a) ||
                !parse_double(&pos, &b)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: ELLIPSE requires cx cy cz nx ny nz ux uy uz "
                         "a b followed by material", line_num);
                success = false;
                break;
            }

            Material mat;
            if (!parse_material_tail(&pos, &mat, "ELLIPSE", line_num,
                                     error_message, error_capacity)) {
                success = false;
                break;
            }

            Ellipse ellipse;
            memset(&ellipse, 0, sizeof(ellipse));
            ellipse.center = vec3_create(cx, cy, cz);
            ellipse.normal = vec3_create(nx, ny, nz);
            ellipse.u_axis = vec3_create(ux, uy, uz);
            ellipse.radius_u = a;
            ellipse.radius_v = b;

            char detail[128];
            if (!prepare_ellipse(&ellipse, detail, sizeof(detail))) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Invalid ELLIPSE: %s", line_num, detail);
                success = false;
                break;
            }

            Object obj;
            memset(&obj, 0, sizeof(obj));
            obj.type = OBJ_ELLIPSE;
            obj.material = mat;
            obj.geo.ellipse = ellipse;
            init_default_texmap(&obj.tex_map);

            if (!add_object(&objects, &obj_count, &obj_capacity, &obj)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for ELLIPSE",
                         line_num);
                success = false;
                break;
            }
        }

        else if (strcmp(keyword, "CYLINDER") == 0) {
            double bx, by, bz, ax, ay, az, radius, h_min, h_max;
            if (!parse_double(&pos, &bx) || !parse_double(&pos, &by) ||
                !parse_double(&pos, &bz) || !parse_double(&pos, &ax) ||
                !parse_double(&pos, &ay) || !parse_double(&pos, &az) ||
                !parse_double(&pos, &radius) || !parse_double(&pos, &h_min) ||
                !parse_double(&pos, &h_max)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: CYLINDER requires bx by bz ax ay az radius "
                         "h_min h_max followed by material", line_num);
                success = false;
                break;
            }

            Material mat;
            if (!parse_material_tail(&pos, &mat, "CYLINDER", line_num,
                                     error_message, error_capacity)) {
                success = false;
                break;
            }

            Cylinder cyl;
            memset(&cyl, 0, sizeof(cyl));
            cyl.base = vec3_create(bx, by, bz);
            cyl.axis = vec3_create(ax, ay, az);
            cyl.radius = radius;
            cyl.h_min = h_min;
            cyl.h_max = h_max;

            char detail[128];
            if (!prepare_cylinder(&cyl, detail, sizeof(detail))) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Invalid CYLINDER: %s", line_num, detail);
                success = false;
                break;
            }

            Object obj;
            memset(&obj, 0, sizeof(obj));
            obj.type = OBJ_CYLINDER;
            obj.material = mat;
            obj.geo.cylinder = cyl;
            init_default_texmap(&obj.tex_map);

            if (!add_object(&objects, &obj_count, &obj_capacity, &obj)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for CYLINDER",
                         line_num);
                success = false;
                break;
            }
        }

        else if (strcmp(keyword, "CONE") == 0) {
            double vx, vy, vz, ax, ay, az, angle_deg, h_min, h_max;
            if (!parse_double(&pos, &vx) || !parse_double(&pos, &vy) ||
                !parse_double(&pos, &vz) || !parse_double(&pos, &ax) ||
                !parse_double(&pos, &ay) || !parse_double(&pos, &az) ||
                !parse_double(&pos, &angle_deg) || !parse_double(&pos, &h_min) ||
                !parse_double(&pos, &h_max)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: CONE requires vx vy vz ax ay az angle_deg "
                         "h_min h_max followed by material", line_num);
                success = false;
                break;
            }

            if (angle_deg <= 0.0 || angle_deg >= 90.0) {
                snprintf(error_message, error_capacity,
                         "Line %zu: CONE angle_deg must be in (0, 90)",
                         line_num);
                success = false;
                break;
            }

            Material mat;
            if (!parse_material_tail(&pos, &mat, "CONE", line_num,
                                     error_message, error_capacity)) {
                success = false;
                break;
            }

            Cone cone;
            memset(&cone, 0, sizeof(cone));
            cone.vertex = vec3_create(vx, vy, vz);
            cone.axis = vec3_create(ax, ay, az);
            cone.tan_alpha = tan(angle_deg * M_PI / 180.0);
            cone.h_min = h_min;
            cone.h_max = h_max;

            char detail[128];
            if (!prepare_cone(&cone, detail, sizeof(detail))) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Invalid CONE: %s", line_num, detail);
                success = false;
                break;
            }

            Object obj;
            memset(&obj, 0, sizeof(obj));
            obj.type = OBJ_CONE;
            obj.material = mat;
            obj.geo.cone = cone;
            init_default_texmap(&obj.tex_map);

            if (!add_object(&objects, &obj_count, &obj_capacity, &obj)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for CONE",
                         line_num);
                success = false;
                break;
            }
        }

        else if (strcmp(keyword, "QUADRIC") == 0) {
            double A, B, C, D, E, F, G, H, I, J;
            if (!parse_double(&pos, &A) || !parse_double(&pos, &B) ||
                !parse_double(&pos, &C) || !parse_double(&pos, &D) ||
                !parse_double(&pos, &E) || !parse_double(&pos, &F) ||
                !parse_double(&pos, &G) || !parse_double(&pos, &H) ||
                !parse_double(&pos, &I) || !parse_double(&pos, &J)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: QUADRIC requires 10 coefficients A..J "
                         "followed by material", line_num);
                success = false;
                break;
            }

            Material mat;
            if (!parse_material_tail(&pos, &mat, "QUADRIC", line_num,
                                     error_message, error_capacity)) {
                success = false;
                break;
            }

            Quadric q = {A, B, C, D, E, F, G, H, I, J};
            char detail[128];
            if (!prepare_quadric(&q, detail, sizeof(detail))) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Invalid QUADRIC: %s", line_num, detail);
                success = false;
                break;
            }

            Object obj;
            memset(&obj, 0, sizeof(obj));
            obj.type = OBJ_QUADRIC;
            obj.material = mat;
            obj.geo.quadric = q;
            init_default_texmap(&obj.tex_map);

            if (!add_object(&objects, &obj_count, &obj_capacity, &obj)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for QUADRIC",
                         line_num);
                success = false;
                break;
            }
        }

        else if (strcmp(keyword, "CLIP") == 0) {
            if (obj_count == 0) {
                snprintf(error_message, error_capacity,
                         "Line %zu: CLIP must follow an object definition",
                         line_num);
                success = false;
                break;
            }
            double a, b, c, d;
            if (!parse_double(&pos, &a) || !parse_double(&pos, &b) ||
                !parse_double(&pos, &c) || !parse_double(&pos, &d) ||
                !expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: CLIP requires a b c d", line_num);
                success = false;
                break;
            }
            if (a * a + b * b + c * c <= VECTOR_EPSILON) {
                snprintf(error_message, error_capacity,
                         "Line %zu: CLIP plane normal (a, b, c) must be non-zero",
                         line_num);
                success = false;
                break;
            }
            ClipPlane plane = {a, b, c, d};
            if (!add_clip_plane(&objects[obj_count - 1], plane)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for CLIP",
                         line_num);
                success = false;
                break;
            }
        }

        else if (strcmp(keyword, "MAP_POLY") == 0) {
            if (obj_count == 0 || objects[obj_count - 1].type != OBJ_POLYGON) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_POLY must follow a POLYGON", line_num);
                success = false;
                break;
            }
            double ox, oy, oz, ux, uy, uz, vx, vy, vz, su, sv;
            if (!parse_double(&pos, &ox) || !parse_double(&pos, &oy) ||
                !parse_double(&pos, &oz) || !parse_double(&pos, &ux) ||
                !parse_double(&pos, &uy) || !parse_double(&pos, &uz) ||
                !parse_double(&pos, &vx) || !parse_double(&pos, &vy) ||
                !parse_double(&pos, &vz) || !parse_double(&pos, &su) ||
                !parse_double(&pos, &sv)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_POLY requires otx oty otz ux uy uz "
                         "vx vy vz su sv [repeat|clamp]", line_num);
                success = false;
                break;
            }
            WrapMode wrap = WRAP_REPEAT;
            if (!is_blank(pos) && !parse_wrap_mode(&pos, &wrap)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_POLY wrap mode must be 'repeat' or 'clamp'",
                         line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_POLY has extra tokens", line_num);
                success = false;
                break;
            }
            Vector3 u = vec3_create(ux, uy, uz);
            Vector3 v = vec3_create(vx, vy, vz);
            if (vec3_length(u) <= VECTOR_EPSILON ||
                vec3_length(v) <= VECTOR_EPSILON ||
                fabs(su) <= VECTOR_EPSILON || fabs(sv) <= VECTOR_EPSILON) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_POLY requires non-zero U, V and non-zero scales",
                         line_num);
                success = false;
                break;
            }
            TextureMapping *map = &objects[obj_count - 1].tex_map;
            map->origin = vec3_create(ox, oy, oz);
            map->u_axis = vec3_normalize(u);
            map->v_axis = vec3_normalize(v);
            map->scale_u = su;
            map->scale_v = sv;
            map->wrap = wrap;
            map->custom_basis = true;
        }

        else if (strcmp(keyword, "MAP_SPHERE") == 0) {
            if (obj_count == 0 || objects[obj_count - 1].type != OBJ_SPHERE) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_SPHERE must follow a SPHERE", line_num);
                success = false;
                break;
            }
            double ux, uy, uz, vx, vy, vz;
            if (!parse_double(&pos, &ux) || !parse_double(&pos, &uy) ||
                !parse_double(&pos, &uz) || !parse_double(&pos, &vx) ||
                !parse_double(&pos, &vy) || !parse_double(&pos, &vz)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_SPHERE requires x1 x2 x3 y1 y2 y3 "
                         "[su sv] [repeat|clamp]", line_num);
                success = false;
                break;
            }
            double su = 1.0, sv = 1.0;
            double scales[2];
            size_t sc = parse_optional_doubles(&pos, scales, 2);
            if (sc == 1) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_SPHERE scales require both su and sv",
                         line_num);
                success = false;
                break;
            }
            if (sc == 2) {
                su = scales[0];
                sv = scales[1];
            }
            WrapMode wrap = WRAP_REPEAT;
            if (!is_blank(pos) && !parse_wrap_mode(&pos, &wrap)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_SPHERE wrap mode must be 'repeat' or 'clamp'",
                         line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_SPHERE has extra tokens", line_num);
                success = false;
                break;
            }
            Vector3 x_loc = vec3_normalize(vec3_create(ux, uy, uz));
            Vector3 y_raw = vec3_create(vx, vy, vz);
            Vector3 y_orth = vec3_sub(y_raw, vec3_scale(x_loc, vec3_dot(y_raw, x_loc)));
            if (vec3_length(x_loc) <= VECTOR_EPSILON ||
                vec3_length(y_orth) <= VECTOR_EPSILON ||
                fabs(su) <= VECTOR_EPSILON || fabs(sv) <= VECTOR_EPSILON) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_SPHERE requires non-collinear X, Y and non-zero scales",
                         line_num);
                success = false;
                break;
            }
            Vector3 y_loc = vec3_normalize(y_orth);
            Vector3 z_loc = vec3_normalize(vec3_cross(x_loc, y_loc));
            TextureMapping *map = &objects[obj_count - 1].tex_map;
            map->u_axis = x_loc;
            map->v_axis = y_loc;
            map->w_axis = z_loc;
            map->scale_u = su;
            map->scale_v = sv;
            map->wrap = wrap;
            map->custom_basis = true;
        }

        else if (strcmp(keyword, "MAP_AXIS") == 0) {
            if (obj_count == 0 ||
                (objects[obj_count - 1].type != OBJ_CYLINDER &&
                 objects[obj_count - 1].type != OBJ_CONE)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_AXIS must follow a CYLINDER or CONE",
                         line_num);
                success = false;
                break;
            }
            double ux, uy, uz;
            if (!parse_double(&pos, &ux) || !parse_double(&pos, &uy) ||
                !parse_double(&pos, &uz)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_AXIS requires ux uy uz [su sv] [repeat|clamp]",
                         line_num);
                success = false;
                break;
            }
            double su = 1.0, sv = 1.0;
            double scales[2];
            size_t sc = parse_optional_doubles(&pos, scales, 2);
            if (sc == 1) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_AXIS scales require both su and sv",
                         line_num);
                success = false;
                break;
            }
            if (sc == 2) {
                su = scales[0];
                sv = scales[1];
            }
            WrapMode wrap = WRAP_REPEAT;
            if (!is_blank(pos) && !parse_wrap_mode(&pos, &wrap)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_AXIS wrap mode must be 'repeat' or 'clamp'",
                         line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_AXIS has extra tokens", line_num);
                success = false;
                break;
            }

            Object *last = &objects[obj_count - 1];
            Vector3 axis = (last->type == OBJ_CYLINDER)
                ? last->geo.cylinder.axis : last->geo.cone.axis;
            Vector3 u_raw = vec3_create(ux, uy, uz);
            Vector3 u_proj = vec3_sub(u_raw, vec3_scale(axis, vec3_dot(u_raw, axis)));
            if (vec3_length(u_proj) <= VECTOR_EPSILON ||
                fabs(su) <= VECTOR_EPSILON || fabs(sv) <= VECTOR_EPSILON) {
                snprintf(error_message, error_capacity,
                         "Line %zu: MAP_AXIS U must not be parallel to axis and scales non-zero",
                         line_num);
                success = false;
                break;
            }
            Vector3 u_unit = vec3_normalize(u_proj);
            Vector3 w_unit = vec3_normalize(vec3_cross(u_unit, axis));
            if (last->type == OBJ_CYLINDER) {
                last->geo.cylinder.u_axis = u_unit;
                last->geo.cylinder.w_axis = w_unit;
            } else {
                last->geo.cone.u_axis = u_unit;
                last->geo.cone.w_axis = w_unit;
            }
            last->tex_map.scale_u = su;
            last->tex_map.scale_v = sv;
            last->tex_map.wrap = wrap;
            last->tex_map.custom_basis = true;
        }

        else if (strcmp(keyword, "TEX_SCALE") == 0) {
            if (obj_count == 0) {
                snprintf(error_message, error_capacity,
                         "Line %zu: TEX_SCALE must follow an object definition",
                         line_num);
                success = false;
                break;
            }
            double su, sv;
            if (!parse_double(&pos, &su) || !parse_double(&pos, &sv)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: TEX_SCALE requires su sv [repeat|clamp]",
                         line_num);
                success = false;
                break;
            }
            WrapMode wrap = objects[obj_count - 1].tex_map.wrap;
            if (!is_blank(pos) && !parse_wrap_mode(&pos, &wrap)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: TEX_SCALE wrap mode must be 'repeat' or 'clamp'",
                         line_num);
                success = false;
                break;
            }
            if (!expect_end(pos) || fabs(su) <= VECTOR_EPSILON ||
                fabs(sv) <= VECTOR_EPSILON) {
                snprintf(error_message, error_capacity,
                         "Line %zu: TEX_SCALE requires non-zero su sv",
                         line_num);
                success = false;
                break;
            }
            objects[obj_count - 1].tex_map.scale_u = su;
            objects[obj_count - 1].tex_map.scale_v = sv;
            objects[obj_count - 1].tex_map.wrap = wrap;
        }

        else {
            snprintf(error_message, error_capacity,
                     "Line %zu: Unknown directive '%s'", line_num, keyword);
            success = false;
            break;
        }
    }

    fclose(fp);

    if (success && !has_window) {
        snprintf(error_message, error_capacity,
                 "Missing mandatory WINDOW directive");
        success = false;
    }
    if (success && !has_eye) {
        snprintf(error_message, error_capacity,
                 "Missing mandatory EYE directive");
        success = false;
    }
    if (success && !has_res) {
        snprintf(error_message, error_capacity,
                 "Missing mandatory RES directive");
        success = false;
    }
    if (success && !has_look_at && tmp.eye.z >= 0.0) {
        snprintf(error_message, error_capacity,
                 "EYE z must be negative when LOOKAT is not specified (got %g)",
                 tmp.eye.z);
        success = false;
    }
    if (success && has_look_at) {
        Vector3 forward = vec3_sub(tmp.look_at, tmp.eye);
        if (vec3_length(forward) <= VECTOR_EPSILON) {
            snprintf(error_message, error_capacity,
                     "LOOKAT target cannot equal EYE position");
            success = false;
        } else {
            forward = vec3_normalize(forward);
            Vector3 right = vec3_cross(look_up, forward);
            if (vec3_length(right) <= VECTOR_EPSILON) {
                snprintf(error_message, error_capacity,
                         "LOOKAT up vector cannot be parallel to viewing direction");
                success = false;
            } else {
                tmp.cam_right = vec3_normalize(right);
                tmp.cam_up = vec3_normalize(vec3_cross(forward, tmp.cam_right));
            }
        }
    }
    if (success && obj_count == 0) {
        snprintf(error_message, error_capacity,
                 "Scene contains no objects");
        success = false;
    }
    if (success) {
        for (size_t i = 0; i < obj_count; i++) {
            int tid = objects[i].material.texture;
            if (tid < 0 || (size_t)tid > tmp.texture_count) {
                snprintf(error_message, error_capacity,
                         "Object %zu references texture %d, but only %zu textures loaded",
                         i + 1, tid, tmp.texture_count);
                success = false;
                break;
            }
        }
    }

    if (!success) {
        for (size_t i = 0; i < obj_count; i++) {
            free_object_resources(&objects[i]);
        }
        free(objects);
        free(tmp.lights);
        if (tmp.textures) {
            for (size_t i = 0; i < tmp.texture_count; i++) {
                free_texture(&tmp.textures[i]);
            }
            free(tmp.textures);
        }

        memset(out_scene, 0, sizeof(Scene));
        return false;
    }

    tmp.objects = objects;
    tmp.object_count = obj_count;
    *out_scene = tmp;
    return true;
}
