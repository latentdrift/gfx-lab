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

## Source architecture

The executable is divided by rendering responsibility:

```text
src/
  main.cpp                    process entry point only
  app/
    Application.cpp           GLFW/ImGui lifecycle and workspace orchestration
    Animation.cpp             keyframe capture, interpolation, and stack evaluation
    HardwareProfile.cpp       target capabilities and state normalization
    RenderStack.cpp           pass definitions, compositing state, and stack export
    State.cpp                 explicit renderer state, scene setups, JSON export
  renderer/
    Renderer.cpp              OpenGL resources, state application, and render passes
    Shaders.hpp               scene, output, shadow, and analysis GLSL programs
    TestGeometry.cpp          procedural diagnostic meshes
  ui/
    Inspector.cpp             pipeline-category controls and visual styling
    AnimationEditor.cpp       transport, playhead, and selected-pass key controls
    PassInspector.cpp         pass perturbation and composite controls
  handbook/
    Handbook.cpp              searchable articles, diagrams, and live comparisons
```

`Renderer.hpp` is the narrow boundary between application code and OpenGL implementation. `State.hpp` is the shared, serializable vocabulary used by the renderer, inspector, handbook examples, and clipboard export.

## Controls

- Left drag in the viewport: orbit
- Middle or right drag: pan
- Mouse wheel: zoom

The left panel is a bottom-to-top **render-pass stack**. Select a pass to edit its full renderer state, or press **Duplicate pass** to make a correlated copy and perturb its geometry, camera, UVs, or output buffer. Later passes combine sequentially with the accumulated image using explicit per-channel operations: absolute or signed difference, one-sided subtraction, multiply, screen, exclusion, minimum, maximum, `A x (1 - B)`, centered sum, or relative difference. Each step exposes opacity, gain, bias, encoded-RGB versus linear-light arithmetic, clamp/preserve/wrap range behavior, and optional luminance/depth/edge masks.

The **Animation** strip keyframes the selected pass. It interpolates continuous pass perturbations, composite gain/bias/opacity, vertex precision, normal-map strength, lighting quantities, fog and depth cue distances/colors, and N64 primitive/environment colors. Algorithm choices and binary switches remain static. Build a state and set a key, move the playhead, use **Edit playhead sample**, alter the controls, set another key, then press **Play**. Playback evaluates a temporary stack and never overwrites the authored pass state.

The **Target** selector defaults to **Unrestricted**. **PlayStation (PS1)** and **Nintendo 64** normalize every pass to target-representable state, remove unavailable categories and controls, narrow shared controls to supported choices, and display important forced values or labelled emulation substitutes as profile facts. Switching back to Unrestricted unlocks the controls but does not restore values discarded during normalization. Reset buttons are also normalized by the active target.

The Nintendo 64 target exposes the standard RSP/RDP/VI model: one- or two-cycle `(A - B) x C + D` color combiners, named combiner sources, primitive/environment registers, RDP surface and alpha-compare modes, point/three-point/box texture filters, mip/trilinear/sharpen/detail modes, nine texture formats, tile addressing and calculated 4096-byte TMEM use, RSP texture generation and vertex fog, Z compare/update, coverage antialiasing, RGBA16/RGBA32 framebuffer state, color dithering, and VI reconstruction/divot filters. Coverage and VI behavior are explicitly labelled approximations; the combiner, format quantization, three-point filter, alpha comparison, and TMEM accounting are modeled directly.

Use **Copy stack JSON** to place a human-readable `graphics-lab.render-stack.v1` document on the clipboard. It contains every complete renderer state, pass perturbation, selected output buffer, composite equation, mask, color space, range behavior, animation timeline and keyframe, scene, and camera using explicit names suitable for sharing with other tools or coding agents.

Use **Handbook** to open the built-in technical reference. Start with **From mesh to pixel**, then follow its related-concept links or browse the knowledge map. Every article begins with a plain causal **Quick read** before preserving the precise definition, pipeline location, visible results, interactions, engine vocabulary, and technical diagram. The map groups the pipeline, shading, visibility and output, engine systems, animation, and ray tracing. Reading never changes renderer state; example buttons apply configurations deliberately.

The toolbar scene selector provides purpose-built geometry for texture minification, depth precision, transparency ordering, and lighting interpolation experiments in addition to the default torus.

Changing the selected scene preserves the current renderer state and camera. **Reset scene setup** explicitly applies that scene's recommended starting state and camera framing:

- Torus: conventional neutral rendering
- Texture minification: trilinear mipmapping and 8x anisotropy
- Depth precision: 16-bit depth with a near plane at 0.01 units
- Transparency: straight-alpha blending, depth testing, and disabled depth writes
- Lighting comparison: linear-light Blinn-Phong, mipmapping, and filtered directional shadows
- Stencil mask: unlit two-pass equal-reference masking

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
- Tangent-space normal mapping with normal, tangent, and bitangent visualization
- Alpha discard and source-alpha blending
- Straight alpha, premultiplied alpha, additive, and multiply blend factors with reversible object order
- Original PlayStation average, additive, subtractive, and quarter-add semitransparency equations
- Direct-color, 8-bit indexed, and 4-bit indexed texture storage with actual CLUT palette lookup
- Vertex-evaluated depth cueing toward a configurable far color
- Object-granularity ordering-table submission with configurable depth buckets
- Nintendo 64 RDP one/two-cycle color combiner with explicit `(A - B) x C + D` operands
- N64 point, three-point, and box texture filters; mip, trilinear, sharpen, and detail modes
- N64 RGBA, CI/TLUT, intensity-alpha, and intensity formats with tile and TMEM accounting
- N64 RSP texture-coordinate generation and vertex fog
- N64 opaque, translucent, decal, and interpenetrating surface/Z modes with alpha compare
- N64 coverage-AA approximation, framebuffer formats/dither, and VI reconstruction/divot filters
- Polygon offset factor and units for coplanar depth conflicts
- Depth testing, depth writes, comparison functions, 16- or 24-bit attachments, and depth visualization
- Multisample anti-aliasing with 1, 2, 4, or 8 samples
- Two-pass 8-bit stencil masking with replace, equal, and not-equal operations
- Directional shadow mapping with resolution, depth bias, 3 x 3 PCF, and light-depth visualization
- Additive floating-point overdraw counting with a configurable heat-map range
- Encoded-RGB or linear-light calculations, output color depth, and ordered Bayer dithering
- Linear distance fog
- Internal render resolution and independent framebuffer upscaling filter
