#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stddef.h>

typedef struct { double x, y, z; } Vector3;
typedef Vector3 Point3;
typedef struct { double r, g, b; } ColorRGB;

typedef struct {
    Point3 origin;
    Vector3 direction;
} Ray;

typedef struct {
    ColorRGB color;
    double ka, kd, ks;
    double shininess;
} Material;

typedef struct { Point3 center; double radius; } Sphere;

typedef struct {
    size_t vertex_count;
    Point3 *vertices;
    Vector3 outward_normal;
    size_t dropped_axis;
} Polygon;

typedef enum { OBJ_SPHERE, OBJ_POLYGON } ObjectType;

typedef struct {
    ObjectType type;
    Material material;
    union { Sphere sphere; Polygon polygon; } geo;
} Object;

typedef struct {
    double t;
    Point3 point;
    Vector3 normal;
    bool front_face;
    Material material;
} HitRecord;

typedef struct {
    Point3 position;
    ColorRGB intensity;
    double c1, c2, c3;
} Light;
typedef struct { double x_min, y_min, x_max, y_max; } ViewWindow;

typedef struct {
    Point3 eye;
    ViewWindow window;
    size_t width, height;
    ColorRGB background_color;
    ColorRGB ambient_light;
    size_t object_count;
    Object *objects;
    size_t light_count;
    Light *lights;
} Scene;

typedef enum { RENDER_FLAT, RENDER_PHONG } RenderMode;

#endif
