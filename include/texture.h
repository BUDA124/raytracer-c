#ifndef TEXTURE_H
#define TEXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include "types.h"

bool load_texture(const char *filename, Texture *out_texture,
                  char *error_message, size_t error_capacity);
void free_texture(Texture *texture);

ColorRGB sample_texture(const Texture *texture, double u, double v,
                        WrapMode wrap);
ColorRGB evaluate_object_color(const HitRecord *hit, const Scene *scene);

#endif
