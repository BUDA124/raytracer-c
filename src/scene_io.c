#include "scene_io.h"
#include "numeric.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>

#define MAX_LINE_LENGTH 1024
#define INITIAL_OBJECT_CAPACITY 16

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

/* Strip comments and trailing whitespace in place. */
static void strip_comment(char *line) {
    char *p = strchr(line, '#');
    if (p) *p = '\0';
    /* Trim trailing whitespace. */
    size_t len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) {
        line[--len] = '\0';
    }
}

/* Skip leading whitespace and return pointer to first non-space. */
static const char *skip_spaces(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Check that a string is empty (only whitespace) after stripping. */
static bool is_blank(const char *s) {
    s = skip_spaces(s);
    return *s == '\0';
}

/* Parse a double from string, advancing *pos past it. Returns false on failure. */
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

/* Parse a size_t (positive integer) from string. */
static bool parse_size(const char **pos, size_t *out) {
    const char *start = skip_spaces(*pos);
    if (*start == '\0') return false;
    /* Reject negative. */
    if (*start == '-') return false;

    char *end;
    errno = 0;
    unsigned long val = strtoul(start, &end, 10);
    if (end == start || errno == ERANGE || val == 0) return false;
    *out = (size_t)val;
    *pos = end;
    return true;
}

static bool validate_color(double r, double g, double b) {
    return r >= 0.0 && r <= 1.0 &&
           g >= 0.0 && g <= 1.0 &&
           b >= 0.0 && b <= 1.0;
}

/* Ensure nothing follows after parsed tokens (except whitespace). */
static bool expect_end(const char *pos) {
    return is_blank(pos);
}

/* Add an object, growing the array if needed. */
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

/* ------------------------------------------------------------------ */
/*  free_scene                                                        */
/* ------------------------------------------------------------------ */

void free_scene(Scene *scene) {
    if (!scene) return;

    if (scene->objects) {
        for (size_t i = 0; i < scene->object_count; i++) {
            if (scene->objects[i].type == OBJ_POLYGON &&
                scene->objects[i].geo.polygon.vertices) {
                free(scene->objects[i].geo.polygon.vertices);
            }
        }
        free(scene->objects);
    }
    if (scene->lights) {
        free(scene->lights);
    }

    /* Zero the entire structure so it can be freed again safely. */
    memset(scene, 0, sizeof(Scene));
}

/* ------------------------------------------------------------------ */
/*  load_scene                                                        */
/* ------------------------------------------------------------------ */

bool load_scene(const char *filename, Scene *out_scene,
                char *error_message, size_t error_capacity) {
    if (!filename || !out_scene || !error_message || error_capacity == 0) {
        if (error_message && error_capacity > 0)
            snprintf(error_message, error_capacity, "Invalid arguments");
        return false;
    }

    /* Require out_scene to be zeroed. */
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        snprintf(error_message, error_capacity,
                 "Cannot open file '%s'", filename);
        return false;
    }

    /* Temporary scene built during parsing. */
    Scene tmp = {0};
    bool has_window = false, has_eye = false, has_res = false;
    bool has_bg = false, has_ambient = false;

    Object *objects = NULL;
    size_t obj_count = 0, obj_capacity = 0;

    char line_buf[MAX_LINE_LENGTH];
    size_t line_num = 0;
    bool success = true;

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        line_num++;

        /* Detect truncated lines (line too long for buffer). */
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

        /* Extract the directive keyword. */
        char keyword[32];
        int kw_len = 0;
        while (*pos && !isspace((unsigned char)*pos) && kw_len < 31) {
            keyword[kw_len++] = *pos++;
        }
        keyword[kw_len] = '\0';

        if (kw_len == 0) continue;

        /* --- WINDOW --- */
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
        /* --- EYE --- */
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
            if (z >= 0.0) {
                snprintf(error_message, error_capacity,
                         "Line %zu: EYE z must be negative (got %g)",
                         line_num, z);
                success = false;
                break;
            }
            tmp.eye.x = x;
            tmp.eye.y = y;
            tmp.eye.z = z;
            has_eye = true;
        }
        /* --- RES --- */
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
            /* Check AVS representability (uint32_t max). */
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
        /* --- BG --- */
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
        /* --- AMBIENT --- */
        else if (strcmp(keyword, "AMBIENT") == 0) {
            if (has_ambient) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Duplicate AMBIENT directive", line_num);
                success = false;
                break;
            }
            double r, g, b;
            if (!parse_double(&pos, &r) || !parse_double(&pos, &g) ||
                !parse_double(&pos, &b)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: AMBIENT requires 3 finite numbers",
                         line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: AMBIENT has extra tokens", line_num);
                success = false;
                break;
            }
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
        /* --- SPHERE --- */
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

            /* Try parsing extended form: ka kd ks shininess */
            double ka = 0.1, kd = 0.7, ks = 0.0, shininess = 32.0;
            const char *saved_pos = pos;
            double maybe_ka;
            if (parse_double(&pos, &maybe_ka)) {
                /* Extended form detected. */
                double maybe_kd, maybe_ks, maybe_shin;
                if (!parse_double(&pos, &maybe_kd) ||
                    !parse_double(&pos, &maybe_ks) ||
                    !parse_double(&pos, &maybe_shin)) {
                    snprintf(error_message, error_capacity,
                             "Line %zu: SPHERE extended form requires "
                             "ka kd ks shininess after color", line_num);
                    success = false;
                    break;
                }
                ka = maybe_ka;
                kd = maybe_kd;
                ks = maybe_ks;
                shininess = maybe_shin;
            } else {
                pos = saved_pos;
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
            obj.geo.sphere.center.x = x;
            obj.geo.sphere.center.y = y;
            obj.geo.sphere.center.z = z;
            obj.geo.sphere.radius = radius;

            if (!add_object(&objects, &obj_count, &obj_capacity, &obj)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: Memory allocation failed for SPHERE",
                         line_num);
                success = false;
                break;
            }
        }
        /* --- LIGHT (accept and store for forward compatibility) --- */
        else if (strcmp(keyword, "LIGHT") == 0) {
            /* Accept LIGHT for forward compatibility with P3 scenes. */
            double lx, ly, lz, lr, lg, lb;
            if (!parse_double(&pos, &lx) || !parse_double(&pos, &ly) ||
                !parse_double(&pos, &lz) || !parse_double(&pos, &lr) ||
                !parse_double(&pos, &lg) || !parse_double(&pos, &lb)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: LIGHT requires 6 values (x y z r g b)",
                         line_num);
                success = false;
                break;
            }
            if (!expect_end(pos)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: LIGHT has extra tokens", line_num);
                success = false;
                break;
            }
            if (!validate_color(lr, lg, lb)) {
                snprintf(error_message, error_capacity,
                         "Line %zu: LIGHT intensity must be in [0, 1]",
                         line_num);
                success = false;
                break;
            }
            /* Store light - grow array. */
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
            tmp.lights[tmp.light_count].position.x = lx;
            tmp.lights[tmp.light_count].position.y = ly;
            tmp.lights[tmp.light_count].position.z = lz;
            tmp.lights[tmp.light_count].intensity.r = lr;
            tmp.lights[tmp.light_count].intensity.g = lg;
            tmp.lights[tmp.light_count].intensity.b = lb;
            tmp.light_count = new_count;
        }
        /* --- POLYGON (not supported in P2) --- */
        else if (strcmp(keyword, "POLYGON") == 0) {
            snprintf(error_message, error_capacity,
                     "Line %zu: POLYGON directive is not yet supported "
                     "(will be available in P3)", line_num);
            success = false;
            break;
        }
        /* --- Unknown directive --- */
        else {
            snprintf(error_message, error_capacity,
                     "Line %zu: Unknown directive '%s'", line_num, keyword);
            success = false;
            break;
        }
    }

    fclose(fp);

    /* Verify mandatory directives. */
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
    if (success && obj_count == 0) {
        snprintf(error_message, error_capacity,
                 "Scene contains no objects");
        success = false;
    }

    if (!success) {
        /* Clean up partial allocations. */
        free(objects);
        free(tmp.lights);
        memset(out_scene, 0, sizeof(Scene));
        return false;
    }

    /* Transfer to output scene. */
    tmp.objects = objects;
    tmp.object_count = obj_count;
    *out_scene = tmp;
    return true;
}
