#ifndef IMAGE_IO_H
#define IMAGE_IO_H

#include "types.h"

bool save_avs_image(const char *filename, const ColorRGB *framebuffer,
                    size_t width, size_t height);
bool display_image(const char *filename);

#endif
