#ifndef SHADER_H
#define SHADER_H

#include <stdbool.h>
#include "types.h"

ColorRGB shade_flat(const HitRecord *hit, ColorRGB background,
                    bool has_hit);
ColorRGB shade_phong(const Ray *ray, const HitRecord *hit,
                     const Scene *scene);
bool is_in_shadow(const HitRecord *hit, Point3 light_position,
                  const Scene *scene);

#endif
