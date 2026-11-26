#include "types.h"
#include "scene_io.h"
#include "image_io.h"
#include "ray.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s <scene_file> [--mode flat|phong] "
            "[--output <file.avs>] [--aa <1..8>] [--depth <0..32>] "
            "[--no-display]\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *scene_file = argv[1];
    const char *output_file = "output.avs";
    RenderMode mode = RENDER_PHONG;
    bool no_display = false;
    int aa_override = 0;
    int depth_override = -1;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --mode requires an argument.\n");
                return EXIT_FAILURE;
            }
            i++;
            if (strcmp(argv[i], "flat") == 0) {
                mode = RENDER_FLAT;
            } else if (strcmp(argv[i], "phong") == 0) {
                mode = RENDER_PHONG;
            } else {
                fprintf(stderr, "Error: Unknown render mode '%s'. "
                        "Use 'flat' or 'phong'.\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --output requires an argument.\n");
                return EXIT_FAILURE;
            }
            i++;
            output_file = argv[i];
        } else if (strcmp(argv[i], "--aa") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --aa requires an integer argument.\n");
                return EXIT_FAILURE;
            }
            i++;
            char *end = NULL;
            long val = strtol(argv[i], &end, 10);
            if (!end || *end != '\0' || val < 1 || val > 8) {
                fprintf(stderr, "Error: --aa must be an integer in [1, 8].\n");
                return EXIT_FAILURE;
            }
            aa_override = (int)val;
        } else if (strcmp(argv[i], "--depth") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --depth requires an integer argument.\n");
                return EXIT_FAILURE;
            }
            i++;
            char *end = NULL;
            long val = strtol(argv[i], &end, 10);
            if (!end || *end != '\0' || val < 0 || val > 32) {
                fprintf(stderr, "Error: --depth must be an integer in [0, 32].\n");
                return EXIT_FAILURE;
            }
            depth_override = (int)val;
        } else if (strcmp(argv[i], "--no-display") == 0) {
            no_display = true;
        } else {
            fprintf(stderr, "Error: Unknown argument '%s'.\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    Scene scene = {0};
    char error_msg[512];
    if (!load_scene(scene_file, &scene, error_msg, sizeof(error_msg))) {
        fprintf(stderr, "Error loading scene: %s\n", error_msg);
        return EXIT_FAILURE;
    }

    if (aa_override > 0) {
        scene.aa_samples = aa_override;
    }
    if (depth_override >= 0) {
        scene.max_depth = depth_override;
    }

    if (scene.width > SIZE_MAX / scene.height ||
        scene.width * scene.height > SIZE_MAX / sizeof(ColorRGB)) {
        fprintf(stderr, "Error: Image dimensions are too large.\n");
        free_scene(&scene);
        return EXIT_FAILURE;
    }

    ColorRGB *framebuffer = malloc(scene.width * scene.height * sizeof(ColorRGB));
    if (!framebuffer || !render_scene(&scene, mode, framebuffer)) {
        fprintf(stderr, "Error: Rendering failed.\n");
        free(framebuffer);
        free_scene(&scene);
        return EXIT_FAILURE;
    }

    if (!save_avs_image(output_file, framebuffer, scene.width, scene.height)) {
        fprintf(stderr, "Error: Failed to save image to '%s'.\n", output_file);
        free(framebuffer);
        free_scene(&scene);
        return EXIT_FAILURE;
    }

    free(framebuffer);
    free_scene(&scene);

    if (!no_display) {
        if (!display_image(output_file)) {
            fprintf(stderr, "Warning: Could not launch image viewer.\n");
        }
    }

    return EXIT_SUCCESS;
}
