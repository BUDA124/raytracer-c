# Ray Tracer - Projects 2 & 3

A ray tracer written in C99 for the Computer Graphics course.

## Team Members

- Person 1 (Leader): Parser, CLI, AVS export, integration
- Person 2: Vector algebra, camera, ray generation, render loop
- Person 3: Sphere/polygon intersection, geometry validation
- Person 4: Flat color, Phong shading, shadows, scene composition

## Dependencies

- GCC with C99 support
- Math library (`-lm`)
- ImageMagick (`display` command) for visualization
- Linux (POSIX)

## Build

```bash
make
```

## Usage

```bash
./raytracer <scene_file> --mode flat|phong --output <output.avs> [--no-display]
```

### Examples

```bash
# P2: Flat color rendering
./raytracer scenes/p2.scene --mode flat --output images/p2.avs

# P3: Phong shading
./raytracer scenes/p3_1.scene --mode phong --output images/p3_1.avs
./raytracer scenes/p3_2.scene --mode phong --output images/p3_2.avs
```

### Makefile targets

```bash
make run_p2      # Render P2 scene
make run_p3_1    # Render P3 scene 1
make run_p3_2    # Render P3 scene 2
make clean       # Remove build artifacts
```

## Scene File Format

```text
WINDOW xmin ymin xmax ymax
EYE x y z
RES width height
BG r g b                                    # Optional, default (0,0,0)
AMBIENT r g b                               # Optional, default (0,0,0)
LIGHT x y z r g b                           # Repeatable
SPHERE x y z radius r g b                   # Short form (P2)
SPHERE x y z radius r g b ka kd ks shin     # Long form (P3)
POLYGON vertex_count r g b ka kd ks shin    # P3 only
x0 y0 z0
x1 y1 z1
...
```

- Lines starting with `#` are comments
- Colors and coefficients in [0, 1]
- Radius > 0, shininess >= 1
- eye.z must be < 0

## Output

- AVS format: 8-byte header (width, height as uint32_t big-endian) + ARGB pixels
- JPG conversion: `magick images/p2.avs -quality 100 images/p2.jpg`

## Project Status

- [x] P2: Flat color rendering of 8+ spheres at 1008x567
- [ ] P3: Phong shading with polygons at 1300x760
