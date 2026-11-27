#include "types.h"
#include "scene_io.h"
#include "image_io.h"
#include "geometry.h"
#include "vector3.h"
#include "ray.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#endif

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s <scene_file> [--mode flat|phong] "
            "[--output <file.avs>] [--aa <1..8>] [--depth <0..32>] "
            "[--frames <1..360>] [--output-dir <dir>] [--video <file.mp4>] "
            "[--fps <1..120>] [--loops <1..32>] [--jobs <1..32>] "
            "[--no-display]\n", prog);
}

static bool parse_bounded_int(const char *arg, long min_v, long max_v,
                              int *out_val) {
    char *end = NULL;
    long val = strtol(arg, &end, 10);
    if (!end || *end != '\0' || val < min_v || val > max_v) {
        return false;
    }
    *out_val = (int)val;
    return true;
}

static bool render_animation_sequence(Scene *scene, RenderMode mode,
                                      ColorRGB *framebuffer,
                                      const char *output_dir,
                                      int frame_count, int jobs) {
#ifndef _WIN32
    if (mkdir("images", 0775) != 0 && errno != EEXIST) {
        /* Ignore if parent already exists or output_dir is custom */
    }
    if (mkdir(output_dir, 0775) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: Cannot create directory '%s'.\n", output_dir);
        return false;
    }

    int worker_count = (jobs > frame_count) ? frame_count : jobs;
    if (worker_count < 1) worker_count = 1;

    if (worker_count == 1) {
        Vector3 prev_deltas[ANIM_ROLE_COUNT] = {{0}};
        for (int f = 0; f < frame_count; f++) {
            Vector3 target_deltas[ANIM_ROLE_COUNT];
            compute_animation_deltas(scene, f, frame_count, target_deltas);
            for (size_t i = 0; i < scene->object_count; i++) {
                AnimRole role = scene->objects[i].anim_role;
                Vector3 step = vec3_sub(target_deltas[role], prev_deltas[role]);
                translate_object(&scene->objects[i], step);
            }
            for (int r = 0; r < ANIM_ROLE_COUNT; r++) {
                prev_deltas[r] = target_deltas[r];
            }

            char frame_path[512];
            snprintf(frame_path, sizeof(frame_path),
                     "%s/frame_%03d.avs", output_dir, f);
            if (!render_scene(scene, mode, framebuffer) ||
                !save_avs_image(frame_path, framebuffer,
                                scene->width, scene->height)) {
                return false;
            }
            fprintf(stdout, "Rendered frame %d/%d -> %s\n",
                    f + 1, frame_count, frame_path);
            fflush(stdout);
        }
        return true;
    }

    pid_t *pids = malloc((size_t)worker_count * sizeof(pid_t));
    if (!pids) return false;

    for (int w = 0; w < worker_count; w++) {
        pid_t pid = fork();
        if (pid < 0) {
            for (int k = 0; k < w; k++) {
                waitpid(pids[k], NULL, 0);
            }
            free(pids);
            return false;
        }
        if (pid == 0) {
            free(pids);
            Vector3 prev_deltas[ANIM_ROLE_COUNT] = {{0}};
            for (int f = w; f < frame_count; f += worker_count) {
                Vector3 target_deltas[ANIM_ROLE_COUNT];
                compute_animation_deltas(scene, f, frame_count, target_deltas);
                for (size_t i = 0; i < scene->object_count; i++) {
                    AnimRole role = scene->objects[i].anim_role;
                    Vector3 step = vec3_sub(target_deltas[role],
                                            prev_deltas[role]);
                    translate_object(&scene->objects[i], step);
                }
                for (int r = 0; r < ANIM_ROLE_COUNT; r++) {
                    prev_deltas[r] = target_deltas[r];
                }

                char frame_path[512];
                snprintf(frame_path, sizeof(frame_path),
                         "%s/frame_%03d.avs", output_dir, f);
                if (!render_scene(scene, mode, framebuffer) ||
                    !save_avs_image(frame_path, framebuffer,
                                    scene->width, scene->height)) {
                    _exit(EXIT_FAILURE);
                }
                fprintf(stdout, "Rendered frame %d/%d -> %s\n",
                        f + 1, frame_count, frame_path);
                fflush(stdout);
            }
            _exit(EXIT_SUCCESS);
        }
        pids[w] = pid;
    }

    bool all_ok = true;
    for (int w = 0; w < worker_count; w++) {
        int status = 0;
        if (waitpid(pids[w], &status, 0) < 0 ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            all_ok = false;
        }
    }
    free(pids);
    return all_ok;
#else
    (void)scene;
    (void)mode;
    (void)framebuffer;
    (void)output_dir;
    (void)frame_count;
    (void)jobs;
    fprintf(stderr, "Error: Multi-frame animation requires POSIX.\n");
    return false;
#endif
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *scene_file = argv[1];
    const char *output_file = "output.avs";
    const char *output_dir = "images/frames_p4_2";
    const char *video_file = NULL;
    RenderMode mode = RENDER_PHONG;
    bool no_display = false;
    int aa_override = 0;
    int depth_override = -1;
    int frame_count = 1;
    int fps = 24;
    int loops = 3;
    int jobs = 8;

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
        } else if (strcmp(argv[i], "--output-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --output-dir requires an argument.\n");
                return EXIT_FAILURE;
            }
            i++;
            output_dir = argv[i];
        } else if (strcmp(argv[i], "--video") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --video requires an argument.\n");
                return EXIT_FAILURE;
            }
            i++;
            video_file = argv[i];
        } else if (strcmp(argv[i], "--aa") == 0) {
            if (i + 1 >= argc ||
                !parse_bounded_int(argv[++i], 1, 8, &aa_override)) {
                fprintf(stderr, "Error: --aa must be an integer in [1, 8].\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--depth") == 0) {
            if (i + 1 >= argc ||
                !parse_bounded_int(argv[++i], 0, 32, &depth_override)) {
                fprintf(stderr, "Error: --depth must be an integer in [0, 32].\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--frames") == 0) {
            if (i + 1 >= argc ||
                !parse_bounded_int(argv[++i], 1, 360, &frame_count)) {
                fprintf(stderr, "Error: --frames must be an integer in [1, 360].\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--fps") == 0) {
            if (i + 1 >= argc ||
                !parse_bounded_int(argv[++i], 1, 120, &fps)) {
                fprintf(stderr, "Error: --fps must be an integer in [1, 120].\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--loops") == 0) {
            if (i + 1 >= argc ||
                !parse_bounded_int(argv[++i], 1, 32, &loops)) {
                fprintf(stderr, "Error: --loops must be an integer in [1, 32].\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--jobs") == 0) {
            if (i + 1 >= argc ||
                !parse_bounded_int(argv[++i], 1, 32, &jobs)) {
                fprintf(stderr, "Error: --jobs must be an integer in [1, 32].\n");
                return EXIT_FAILURE;
            }
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
    if (!framebuffer) {
        fprintf(stderr, "Error: Out of memory for framebuffer.\n");
        free_scene(&scene);
        return EXIT_FAILURE;
    }

    if (frame_count > 1 || video_file != NULL) {
        int eff_frames = (frame_count > 1) ? frame_count : 24;
        if (!render_animation_sequence(&scene, mode, framebuffer,
                                       output_dir, eff_frames, jobs)) {
            fprintf(stderr, "Error: Animation frame rendering failed.\n");
            free(framebuffer);
            free_scene(&scene);
            return EXIT_FAILURE;
        }

        if (video_file != NULL) {
            fprintf(stdout, "Encoding %d AVS frames (%d loops @ %d fps) -> %s\n",
                    eff_frames, loops, fps, video_file);
            fflush(stdout);
            if (!encode_avs_frames_to_video(output_dir, eff_frames,
                                            scene.width, scene.height,
                                            fps, loops, video_file)) {
                fprintf(stderr, "Error: Video encoding failed.\n");
                free(framebuffer);
                free_scene(&scene);
                return EXIT_FAILURE;
            }
        }

        free(framebuffer);
        free_scene(&scene);
        return EXIT_SUCCESS;
    }

    if (!render_scene(&scene, mode, framebuffer)) {
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
