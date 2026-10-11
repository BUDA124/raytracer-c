#ifndef SCENE_IO_H
#define SCENE_IO_H

#include "types.h"

bool load_scene(const char *filename, Scene *out_scene,
                char *error_message, size_t error_capacity);
void free_scene(Scene *scene);

#endif
