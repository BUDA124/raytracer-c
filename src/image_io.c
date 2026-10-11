#include "image_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

/* Platform-specific includes for display. */
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#endif

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

/* Convert big-endian uint32_t portably. */
static void write_uint32_be(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >>  8) & 0xFF);
    buf[3] = (uint8_t)((val      ) & 0xFF);
}

/* Clamp a double to [0, 1] and convert to byte. */
static uint8_t color_to_byte(double c) {
    if (!isfinite(c)) return 0;
    if (c <= 0.0) return 0;
    if (c >= 1.0) return 255;
    return (uint8_t)(c * 255.0 + 0.5);
}

/* ------------------------------------------------------------------ */
/*  save_avs_image                                                    */
/* ------------------------------------------------------------------ */

bool save_avs_image(const char *filename, const ColorRGB *framebuffer,
                    size_t width, size_t height) {
    if (!filename || !framebuffer || width == 0 || height == 0) {
        fprintf(stderr, "Error: save_avs_image received invalid arguments.\n");
        return false;
    }

    /* Check AVS uint32_t representability. */
    if (width > (size_t)UINT32_MAX || height > (size_t)UINT32_MAX) {
        fprintf(stderr, "Error: Dimensions exceed AVS uint32 limits.\n");
        return false;
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open '%s' for writing.\n", filename);
        return false;
    }

    /* Write 8-byte header: width, height as big-endian uint32_t. */
    uint8_t header[8];
    write_uint32_be(header, (uint32_t)width);
    write_uint32_be(header + 4, (uint32_t)height);
    if (fwrite(header, 1, 8, fp) != 8) {
        fprintf(stderr, "Error: Failed to write AVS header.\n");
        fclose(fp);
        remove(filename);
        return false;
    }

    /* Write pixels row by row, top to bottom (invert Y). */
    bool write_ok = true;
    for (size_t row = height; row > 0; --row) {
        size_t y = row - 1;
        for (size_t x = 0; x < width; x++) {
            const ColorRGB *pixel = &framebuffer[y * width + x];

            /* Check for non-finite values. */
            if (!isfinite(pixel->r) || !isfinite(pixel->g) ||
                !isfinite(pixel->b)) {
                fprintf(stderr,
                        "Warning: Non-finite color at pixel (%zu, %zu). "
                        "Clamping to black.\n", x, y);
            }

            uint8_t argb[4];
            argb[0] = 255;  /* Alpha */
            argb[1] = color_to_byte(pixel->r);
            argb[2] = color_to_byte(pixel->g);
            argb[3] = color_to_byte(pixel->b);

            if (fwrite(argb, 1, 4, fp) != 4) {
                write_ok = false;
                break;
            }
        }
        if (!write_ok) break;
    }

    if (!write_ok) {
        fprintf(stderr, "Error: Failed to write AVS pixel data.\n");
        fclose(fp);
        remove(filename);
        return false;
    }

    if (fclose(fp) != 0) {
        fprintf(stderr, "Error: Failed to close AVS file.\n");
        remove(filename);
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/*  display_image                                                     */
/* ------------------------------------------------------------------ */

bool display_image(const char *filename) {
    if (!filename) return false;

#ifdef _WIN32
    /* Windows: try to launch ImageMagick display. */
    int ret = _spawnlp(_P_NOWAIT, "magick", "magick", "display",
                       filename, NULL);
    if (ret == -1) {
        fprintf(stderr, "Warning: Could not launch 'magick display'. "
                "Is ImageMagick installed?\n");
        return false;
    }
    return true;
#else
    /* POSIX: fork + exec without shell interpolation. */
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Warning: fork() failed for image display.\n");
        return false;
    }
    if (pid == 0) {
        /* Child: exec display. */
        execlp("display", "display", filename, (char *)NULL);
        /* If exec fails: */
        _exit(127);
    }
    /* Parent: do not block. Reap zombie asynchronously. */
    /* Set SIGCHLD to SIG_IGN or use a non-blocking wait. */
    int status;
    /* Use WNOHANG to avoid blocking. If not reaped now, it'll be
       reaped on exit. For a short-lived program this is acceptable. */
    waitpid(pid, &status, WNOHANG);
    return true;
#endif
}
