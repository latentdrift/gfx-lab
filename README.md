# Graphics Lab

A small native desktop instrument for inspecting realtime rendering techniques one operation at a time.

## Run

```sh
nix develop
cmake -S . -B build -G Ninja
cmake --build build
./build/graphics-lab
```

Or build the package directly with `nix build` and run `./result/bin/graphics-lab`.

## Controls

- Left drag in the viewport: orbit
- Middle or right drag: pan
- Mouse wheel: zoom

The current renderer state is **A**. Use **Copy A to B** to preserve it, change A, then choose **B** or **Split A/B** in the toolbar.

The camera presentation has a fixed 4:3 aspect ratio. It expands to the maximum size that fits the viewport pane, then letterboxes the remaining width or height instead of stretching the rendered image.

## Pipeline controls

- Model-space vertex position precision
- Perspective or orthographic projection, field of view, view height, and near clipping plane
- World-space clipping plane
- Perspective-correct or affine texture-coordinate interpolation
- Front-face, backface, or no face culling
- Texture, UV-coordinate, normal, and vertex-color visualization
- Flat or smooth normal interpolation and wireframe overlay
- Nearest or bilinear texture filtering, mipmapping, trilinear interpolation, anisotropy, and texture addressing
- Unlit, Gouraud, Phong-shaded Lambert, Phong reflection, and Blinn-Phong reflection models
- Alpha discard and source-alpha blending
- Depth testing, depth writes, comparison functions, 16- or 24-bit attachments, and depth visualization
- Multisample anti-aliasing with 1, 2, 4, or 8 samples
- Encoded-RGB or linear-light calculations, output color depth, and ordered Bayer dithering
- Linear distance fog
- Internal render resolution and independent framebuffer upscaling filter
