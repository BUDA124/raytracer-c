# Ray Tracer - Projects 2, 3 & 4 (PR04-CG)

A ray tracer written in standard C99 on Linux for the Computer Graphics course, implementing all algorithms from scratch without external graphics libraries (`-lm` only).

## Team Members

- Person 1 (Leader): Parser, CLI, AVS export, integration
- Person 2: Vector algebra, camera, ray generation, render loop
- Person 3: Geometry, intersections, clipping planes, textures
- Person 4: Shading, shadows, mirrors, non-refractive transparency, scene composition

## Dependencies

- GCC with C99 support
- Math library (`-lm`)
- ImageMagick (`display` / `magick`) for visualization and `.jpg` conversion
- Linux (POSIX)

## Build

```bash
make          # Optimized build (-O3)
make asan     # Debug build with AddressSanitizer & UndefinedBehaviorSanitizer
make clean    # Remove build artifacts
```

## Usage

```bash
./raytracer <scene_file> [--mode flat|phong] [--output <output.avs>] [--aa <1..8>] [--depth <0..32>] [--no-display]
```

Default mode is `phong`. When `--no-display` is omitted, ImageMagick `display` is launched automatically to show the generated `.avs` image.

### Examples

```bash
# P4: Final scenes (1300x760, AA 2, all 22 elements)
./raytracer scenes/P4_1.scene --output images/P4_1.avs
./raytracer scenes/P4_2.scene --output images/P4_2.avs

# Escena race P4
./raytracer scenes/race.scene --output images/race.avs

# Mirror & transparency demo scene
./raytracer scenes/sala_cristal.scene --mode phong --output images/pcristal_1.avs

# P2 & P3 legacy scenes
./raytracer scenes/p2.scene --mode flat --output images/p2.avs
./raytracer scenes/P3_1.scene --mode phong --output images/p3_1.avs
./raytracer scenes/P3_2.scene --mode phong --output images/p3_2.avs
```

### Makefile targets

```bash
make run_p4_1    # Render P4 scene 1 (Observatorio Astronomico de Cristal)
make run_p4_2    # Render P4 scene 2 (Taller de Geometria y Arte Moderno)
make run_p2      # Render P2 scene
make run_p3_1    # Render P3 scene 1
make run_p3_2    # Render P3 scene 2
make asan        # Build sanitizer-instrumented binary
make clean       # Remove build artifacts
```

## Scene File Format

```text
WINDOW xmin ymin xmax ymax
EYE x y z
LOOKAT lx ly lz [ux uy uz]                           # Optional camera orientation
RES width height                                     # 1300 760 for final scenes
BG r g b                                             # Optional, default (0,0,0)
AMBIENT Ia | AMBIENT r g b                           # Optional, white ambient intensity
LIGHT x y z Ip [c1 c2 c3]                            # Repeatable white point light
LIGHT x y z r g b [c1 c2 c3]                         # Legacy form (white: r = g = b)
DEPTH n                                              # Optional, recursion levels [0, 32], default 5
AA n                                                 # Optional, NxN supersampling [1, 8], default 1
TEXTURE [id] filepath                                # Loads .ppm (P3/P6) or .avs texture (1-based)

# Primitives (material tail: r g b ka kd ks shin [o1 o2 o3 [texture]])
SPHERE x y z radius r g b [ka kd ks shin [o1 o2 o3 [texture]]]
POLYGON vertex_count r g b ka kd ks shin [o1 o2 o3 [texture]]
  x0 y0 z0
  ...
DISK cx cy cz nx ny nz radius inner_radius r g b ka kd ks shin [o1 o2 o3 [texture]]
ELLIPSE cx cy cz nx ny nz ux uy uz a b r g b ka kd ks shin [o1 o2 o3 [texture]]
CYLINDER bx by bz ax ay az radius h_min h_max r g b ka kd ks shin [o1 o2 o3 [texture]]
CONE vx vy vz ax ay az angle_deg h_min h_max r g b ka kd ks shin [o1 o2 o3 [texture]]
QUADRIC A B C D E F G H I J r g b ka kd ks shin [o1 o2 o3 [texture]]

# Object modifiers (apply to the preceding primitive)
CLIP a b c d                                         # Repeatable: keeps a*x + b*y + c*z + d >= 0
MAP_POLY otx oty otz ux uy uz vx vy vz su sv [repeat|clamp]
MAP_SPHERE x1 x2 x3 y1 y2 y3 [su sv] [repeat|clamp]
MAP_AXIS ux uy uz [su sv] [repeat|clamp]             # Custom U reference for CYLINDER / CONE
TEX_SCALE su sv [repeat|clamp]
```

- Lines starting with `#` are comments.
- Colors and coefficients (`ka, kd, ks, o1, o2, o3`) are in `[0, 1]` with `o1 + o2 + o3 = 1`.
- `texture = 0` uses the base color; `texture > 0` samples the 1-based loaded texture.
- Radius > 0, `shininess >= 1`, `eye.z < 0` when `LOOKAT` is omitted.
- Only white lights are used (colored lights, color filters, refraction, bump mapping, and opacity maps are excluded).

## Output & Packaging

- AVS format: 8-byte header (`width`, `height` as `uint32_t` big-endian) + ARGB pixels.
- Convert final scenes to maximum-quality JPG:
  ```bash
  magick images/P4_1.avs -quality 100 images/P4_1.jpg
  magick images/P4_2.avs -quality 100 images/P4_2.jpg
  ```
- Create submission archive:
  ```bash
  tar -czvf proyecto4_fuentes.tgz Makefile README.md include/ src/ scenes/ textures/
  ```

## Project Status

- [x] P2: Flat color rendering of 8+ spheres at 1300x760
- [x] P3: Phong shading with polygons at 1300x760
- [x] P4: Advanced ray tracing at 1300x760 — all 22 elements implemented and verified:
  - General code, spheres, polygons, disks, ellipses, cylinders, cones, quadric surfaces, clipping planes
  - White lights, intensity, multiple lights, diffuse illumination, specular reflection, opaque & transparent-attenuated shadows
  - Mirrors (`O2`), non-refractive transparency (`O3`), supersampling antialiasing (`AA`)
  - Pure-C texture loader (`.ppm`/`.avs`) and UV mapping on polygons, cylinders, spheres, and cones
