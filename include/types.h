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
    /* Color mixing weights (O1 + O2 + O3 = 1): own color, mirror, transparency. */
    double o1, o2, o3;
    /* 0 = use the base color; > 0 = 1-based index of the texture to sample. */
    int texture;
} Material;

typedef enum {
    WRAP_REPEAT = 0,
    WRAP_CLAMP = 1
} WrapMode;

typedef struct {
    size_t width;
    size_t height;
    ColorRGB *pixels;
} Texture;

typedef struct {
    WrapMode wrap;
    Point3 origin;    /* Ot for polygons. */
    Vector3 u_axis;   /* U (polygon) or X_loc (sphere). */
    Vector3 v_axis;   /* V (polygon) or Y_loc (sphere). */
    Vector3 w_axis;   /* Z_loc (sphere). */
    double scale_u;   /* su for polygons or U tiling scale. */
    double scale_v;   /* sv for polygons or V tiling scale. */
    bool custom_basis;
} TextureMapping;

typedef struct {
    double a, b, c, d; /* Accepted region: a*x + b*y + c*z + d >= 0. */
} ClipPlane;

typedef struct { Point3 center; double radius; } Sphere;

typedef struct {
    size_t vertex_count;
    Point3 *vertices;
    Vector3 outward_normal;
    size_t dropped_axis;
    double proj_min_x, proj_max_x;
    double proj_min_y, proj_max_y;
    bool has_proj_bounds;
} Polygon;

typedef struct {
    Point3 center;
    Vector3 normal;
    double radius;
    double inner_radius;
} Disk;

typedef struct {
    Point3 center;
    Vector3 normal;
    Vector3 u_axis;
    Vector3 v_axis;
    double radius_u;
    double radius_v;
} Ellipse;

typedef struct {
    Point3 base;
    Vector3 axis;
    Vector3 u_axis;
    Vector3 w_axis;
    double radius;
    double h_min;
    double h_max;
} Cylinder;

typedef struct {
    Point3 vertex;
    Vector3 axis;
    Vector3 u_axis;
    Vector3 w_axis;
    double tan_alpha;
    double h_min;
    double h_max;
} Cone;

typedef struct {
    /* Ax^2 + By^2 + Cz^2 + 2Dxy + 2Exz + 2Fyz + 2Gx + 2Hy + 2Iz + J = 0 */
    double A, B, C, D, E, F, G, H, I, J;
} Quadric;

typedef enum {
    OBJ_SPHERE,
    OBJ_POLYGON,
    OBJ_DISK,
    OBJ_ELLIPSE,
    OBJ_CYLINDER,
    OBJ_CONE,
    OBJ_QUADRIC
} ObjectType;

typedef struct Object {
    ObjectType type;
    Material material;
    TextureMapping tex_map;
    ClipPlane *clip_planes;
    size_t clip_count;
    union {
        Sphere sphere;
        Polygon polygon;
        Disk disk;
        Ellipse ellipse;
        Cylinder cylinder;
        Cone cone;
        Quadric quadric;
    } geo;
} Object;

typedef struct {
    double t;
    Point3 point;
    Vector3 normal;
    bool front_face;
    Material material;
    const Object *object;
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
    bool has_look_at;
    Point3 look_at;
    Vector3 cam_right;
    Vector3 cam_up;
    size_t width, height;
    ColorRGB background_color;
    ColorRGB ambient_light;
    size_t object_count;
    Object *objects;
    size_t light_count;
    Light *lights;
    size_t texture_count;
    Texture *textures;
    int max_depth;   /* Recursion levels for mirror and transparency rays. */
    int aa_samples;  /* Supersampling grid dimension (N x N rays per pixel). */
} Scene;

typedef enum { RENDER_FLAT, RENDER_PHONG } RenderMode;

#endif
