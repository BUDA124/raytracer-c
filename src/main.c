#include "types.h"
#include "scene_io.h"
#include "image_io.h"
#include "ray.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s <scene_file> [--mode flat|phong] "
            "[--output <file.avs>] [--no-display]\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *scene_file = NULL;
    const char *output_file = "output.avs";
    RenderMode mode = RENDER_FLAT;
    bool no_display = false;

    /* Parse arguments. */
    scene_file = argv[1];
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
        } else if (strcmp(argv[i], "--no-display") == 0) {
            no_display = true;
        } else {
            fprintf(stderr, "Error: Unknown argument '%s'.\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!scene_file) {
        fprintf(stderr, "Error: No scene file specified.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* RenderMode is stored but render_scene currently only does flat. */
    (void)mode;

    /* Load scene. */
    Scene scene = {0};
    char error_msg[512];
    if (!load_scene(scene_file, &scene, error_msg, sizeof(error_msg))) {
        fprintf(stderr, "Error loading scene: %s\n", error_msg);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Scene loaded: %zux%zu, %zu objects\n",
            scene.width, scene.height, scene.object_count);

    /* Render. */
    ColorRGB *framebuffer = NULL;
    render_scene(&scene, &framebuffer);
    if (!framebuffer) {
        fprintf(stderr, "Error: Rendering failed.\n");
        free_scene(&scene);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Render complete.\n");

    /* Save image. */
    if (!save_avs_image(output_file, framebuffer, scene.width, scene.height)) {
        fprintf(stderr, "Error: Failed to save image to '%s'.\n", output_file);
        free(framebuffer);
        free_scene(&scene);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Image saved to '%s'.\n", output_file);

    /* Clean up render resources. */
    free(framebuffer);
    free_scene(&scene);

    /* Display unless disabled. */
    if (!no_display) {
        if (!display_image(output_file)) {
            fprintf(stderr, "Warning: Could not launch image viewer.\n");
            /* Image was saved successfully, so don't return failure. */
        }
    }

    return EXIT_SUCCESS;
}
