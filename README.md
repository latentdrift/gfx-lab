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

In the viewport, choose **Translate** or **Scale** to manipulate the current global-base or selected-pass
model transform directly. The gizmo writes the same parameters shown in the pass inspector, follows Auto Key,
and participates in undo/redo. Middle/right drag and the mouse wheel continue to pan and zoom while a transform
tool is active.

Open **Window → Texture Inspector** to inspect the selected pass, base pass, or composite render target.
The inspector provides cursor-centered zoom, panning, texel coordinates, and raw floating-point RGBA values;
this makes clipped, negative, and above-white composite results visible as data even when the viewport cannot
display their full numerical range.

The Animation Timeline can switch between its dope sheet and a focused **Curve view**. Select a key first,
then inspect or drag the chosen vector/color component in time and value; the exact key editor remains beside it
for precise entry and interpolation selection.

## Source architecture

The executable is divided by rendering responsibility:

```text
src/
  main.cpp                    process entry point only
  app/
    Application.cpp           GLFW/ImGui lifecycle and workspace orchestration
    Animation.cpp             keyframe capture, interpolation, and stack evaluation
    EditorHistory.cpp         document snapshots and coalesced undo/redo transactions
    HardwareProfile.cpp       target capabilities and state normalization
    PassEditing.cpp           global/local inspector reconciliation
    RenderStack.cpp           pass definitions, compositing state, and stack export
    StackDocument.cpp         validated JSON document loading and file persistence
    State.cpp                 explicit renderer state, scene setups, JSON export
    Validation.cpp            opt-in renderer, document, import, and UI validation suite
  assets/
    ModelAsset.cpp            mesh/material import, image decoding, validation, and normalization
  renderer/
    Renderer.cpp              OpenGL resources, state application, and render passes
    Shaders.hpp               scene, output, shadow, and analysis GLSL programs
    TestGeometry.cpp          procedural diagnostic meshes
  ui/
    AnimationControls.cpp     inspector diamonds and property-key editing
    Inspector.cpp             pipeline-category controls and visual styling
    AnimationEditor.cpp       transport, dope sheet, tracks, and selected-key editing
    PassInspector.cpp         pass perturbation and composite controls
    PassDifferenceAudit.cpp  authored selected/reference pass comparison and restore
    Workspace.cpp             desktop menu, docking layout, and primary tool windows
  handbook/
    Handbook.cpp              searchable articles, diagrams, and live comparisons
```

`Renderer.hpp` is the narrow boundary between application code and OpenGL implementation. `State.hpp` is the shared, serializable vocabulary used by the renderer, inspector, handbook examples, and clipboard export.

## Controls

- Left drag in the viewport: orbit
- Middle or right drag: pan
- Mouse wheel: zoom
- Undo: Ctrl+Z
- Redo: Ctrl+Shift+Z or Ctrl+Y
- Play/pause animation: Space

Use **File → Save Stack JSON…** (`Ctrl+S`) to save the complete authored document, and
**File → Open Stack JSON…** (`Ctrl+O`) to restore it. Loading restores the global base, sparse pass
overrides, pass identities and operands, animation tracks and timeline, camera, scene, hardware target,
display reconstruction, and imported asset references. A parse or asset error leaves the current document
untouched.

The [`examples`](examples) directory contains loadable stack documents. Start with
[`rod-cone-xor-sdf.json`](examples/rod-cone-xor-sdf.json): it XORs two related SDF render passes, then sends
the result through the quantized rod/cone observer comparison. Its parameters remain ordinary editable lab
state after loading.

Graphics Lab uses a persistent dockable workspace rather than one fixed application page. **Scene**, **Render Passes**, **Pass Properties**, **Viewport**, every rendering-pipeline category, **Texture Mapping**, **Display Reconstruction**, **Animation Timeline**, and **Pass Differences** are independent tool windows. The pipeline, mapping, and final-display tools begin as tabs in one dock node; tear out or rearrange the tools needed for an experiment and manage them under **Window**. **Window → Restore Default Layout** rebuilds the supplied compact workspace. ImGui saves subsequent window and docking changes between runs.

Use **View → UI Scale** to resize text, controls, spacing, tabs, and interaction targets together from 75% to
200%. `Ctrl+-` and `Ctrl+=` step between the supplied sizes; `Ctrl+0` returns to 100%. Every font size is loaded
once at startup, so enlarged text remains crisp and repeatedly changing scale does not rebuild live GPU resources.

Floating tools are checked against the current monitor work areas. If a saved position belongs to a disconnected
or rearranged display, the tool is resized if necessary and recovered to the center of the main display. The
Animation Timeline also changes between side-by-side and stacked panes as its available width changes.

Use **File > Import Model** or the Scene window to load OBJ, glTF, or GLB geometry through the native file chooser. The importer applies scene-node transforms, triangulates faces, generates missing smooth normals and usable tangents, preserves UV0, vertex color 0, submesh material assignments, base-color factors, and external or embedded base-color images. It centers the result and scales its longest bounds extent to exactly 3.0 lab units so camera distance, fog, quantization, and pass perturbations remain comparable between unrelated assets. The Scene window reports geometry, material, texture, and attribute facts; missing image references appear as explicit warnings. **Use Model** and **Unload Model** are undoable.

The **Texture source** control can be authored on the global base and inherited by every pass. A selected pass stores a local override only when it differs. **Scene material** uses imported submesh materials, **Built-in checker** substitutes the diagnostic texture, **White texel** removes image variation while retaining material factors, and **Imported override** applies a separately imported PNG, JPEG, TGA, or BMP to any scene. A locally overridden texture is copied when its pass is duplicated, so correlated renders can perturb its UVs, sampling, color precision, and compositing independently. The inspector reports dimensions and alpha and exposes sRGB-color versus linear-data interpretation.

This first material boundary intentionally imports only base color and alpha. Normal, emissive, metallic-roughness, occlusion, multiple UV sets, skeletal animation, morph targets, cameras, and lights remain separate future systems. Copying stack JSON records source paths, content hashes, dimensions, material facts, and normalization scale rather than embedding image pixels or vertex buffers.

**Texture Mapping** keeps image assignment and application in one tool. It previews the imported source image and controls mesh UV0 versus model-space planar projection, scale/tiling, rotation and pivot, offset, axis flips, repeat/clamp addressing, filtering, mipmapping, and sRGB interpretation. Every mapping parameter follows the shared Global Base / Selected Pass scope; UV transforms and coordinate source can also be keyed in the animation timeline.

Undo and redo cover the authored render stack, all renderer and composite settings, pass order and names, animation tracks, camera, scene, hardware target, and timeline configuration. Continuous sliders and viewport-camera drags collapse into one history entry. Playback time, pass selection, comparison view, open windows, and temporary evaluated animation frames are workspace state rather than authored operations, so they do not flood document history.

The document has one **global base** and a bottom-to-top **render-pass stack**. Global geometry, camera, sampling, lighting, color, output, model perturbation, and texture choices feed every pass. A pass stores only deliberate local differences plus its inherently local enabled/output/composite state. Use **Pass Properties** to switch editing scope, author pass perturbations, and manage inheritance. Every open pipeline tool follows that shared scope. Editing a local value creates an override; returning it to the global value restores inheritance. **Clear pass overrides** removes every static local deviation without deleting that pass's animation tracks.

Press **Duplicate pass** to copy one compact set of deviations, then change only what should disagree. Later passes combine sequentially with explicit A and B operands. Either operand can read the accumulated result, the current raw pass, any other raw render pass, a fixed color, or the previous completed frame. Named pass operands use stable identities, so reordering the stack does not silently redirect them. Per-channel operations include difference, multiply, screen, exclusion, minimum/maximum, additive and subtractive hardware color math, half-add, quarter-add, signed color offset, and quantized bitwise XOR. Previous-frame inputs expose decay and UV transformation for controlled feedback; reset their persistent buffer with **View → Reset Frame History**. Each step also exposes opacity, gain, bias, encoded-RGB versus linear-light arithmetic, clamp/preserve/wrap range behavior, and optional luminance/depth/edge masks.

**Display Reconstruction** is deliberately downstream of the render stack. It can leave the final image as direct RGB or approximate composite-NTSC chroma bandwidth and luma/chroma crosstalk, then model scanlines, an aperture grille, and display bloom. These controls affect presentation only: raw pass textures and all A/B arithmetic remain unchanged and inspectable.

Its RGB observer modes reinterpret the completed image through approximate L, M, S cone and scotopic rod responses. Outputs include the LMS triplet, individual receptor channels, rod vision, a mesopic rod/cone mixture, L-minus-M and blue/yellow opponent channels, rod/cone absolute difference, and quantized rod/cone XOR. Exposure, dark adaptation, rod sensitivity, opponent gain, and XOR precision remain explicit. This is an RGB observer approximation rather than spectral rendering, so it cannot separate metameric spectra that already collapsed to the same RGB value.

The **Animation dope sheet** stores a separate sparse track for each animated property. Global tracks move the shared base for all inheriting passes; local tracks move only one pass and take precedence over its static override and the evaluated global value. Track rows use the same technical names as the inspector and show every keyed time as a diamond. Click a diamond to select it, scrub by clicking a track, and edit the selected key's exact time, value, and step/linear/smooth-step interpolation at the right. **All passes** shows the global tracks and every pass's local tracks together.

Animatable inspector controls have a right-aligned diamond: gray outline means unanimated, blue means the property has a track, and gold means it has a key at the playhead. Click the diamond to add or remove that exact key. Once a property is animated, changing its ordinary control writes that property at the playhead; **Auto Key** allows an ordinary control edit to create its first track. The control displays the evaluated playhead value, so editing does not require copying a sampled pose into the pass first.

The animation catalog covers eligible state across every pipeline category. Continuous numbers, vectors, colors, and angles support step, linear, or smooth-step interpolation. Booleans, integers, algorithm selections, filtering modes, depth/stencil modes, texture sources, composite operations, and N64 fixed-function selections use step tracks because intermediate values have no meaning. Settings that reallocate large GPU resources during playback—MSAA count, shadow-map resolution, internal resolution, and N64 tile dimensions—remain visible in the catalog but deliberately non-keyable. Playback evaluates a temporary render stack and does not overwrite authored base values.

In the dope sheet, click a diamond to select it and drag horizontally to retime it. Keys snap to the chosen frame rate; hold Alt while dragging to bypass snapping. Double-click an empty track position to insert a sampled key there, and press Delete or Backspace to remove the selected key. Exact time, value, and interpolation remain editable in the Selected Key pane.

Open **Window > Pass Differences** after duplicating a pass to audit effective disagreements against any reference pass. Each property is labelled inherited or overridden, alongside its local track state. Matching adopts the reference pass's override/inheritance choice and local track; if the reference inherits, the selected pass resumes tracking the global base. Imported texture resources are compared separately from their sampling state.

The Scene window's **Hardware Target** selector defaults to **Unrestricted**. **PlayStation (PS1)** and **Nintendo 64** normalize every pass to target-representable state, remove unavailable categories and controls, narrow shared controls to supported choices, and display important forced values or labelled emulation substitutes as profile facts. Switching back to Unrestricted unlocks the controls but does not restore values discarded during normalization. Reset buttons are also normalized by the active target.

The Nintendo 64 target exposes the standard RSP/RDP/VI model: one- or two-cycle `(A - B) x C + D` color combiners, named combiner sources, primitive/environment registers, RDP surface and alpha-compare modes, point/three-point/box texture filters, mip/trilinear/sharpen/detail modes, nine texture formats, tile addressing and calculated 4096-byte TMEM use, RSP texture generation and vertex fog, Z compare/update, coverage antialiasing, RGBA16/RGBA32 framebuffer state, color dithering, and VI reconstruction/divot filters. Coverage and VI behavior are explicitly labelled approximations; the combiner, format quantization, three-point filter, alpha comparison, and TMEM accounting are modeled directly.

The unrestricted **Field** tool can produce either wave interference or a genuine signed-distance field. SDF producers currently include sphere, box, and torus primitives with union, intersection, A-minus-B, and smooth-union operations. The pass field attachment stores signed world-unit distance in `R16F`; preview colors are only a display mapping, while named pass-field operands retain negative and positive values for composite arithmetic. A proximity mapping lets the same field drive mesh-vertex displacement, fragment discard, surface color, emission, and pass masks. **Ray-march iso-surface** renders the selected level as implicit geometry and writes fragment depth, so it participates in ordinary occlusion with rasterized meshes. Producer transforms, dimensions, combination, smoothing, iso level, and iso color are animatable.

Use **File > Copy Stack JSON** to place the same human-readable `graphics-lab.render-stack.v8` document on the clipboard. It records the global base, sparse local overrides, global and local property tracks, effective time-zero renderers, texture mapping, pass perturbations, selected output buffers, composite equations, masks, color spaces, range behavior, scene, camera, and display/observer state. Evaluation order is explicit: global base, global track, local override, then local track.

Use **Help > Graphics Handbook** to open the built-in technical reference. Start with **From mesh to pixel**, then follow its related-concept links or browse the knowledge map. Every article begins with a plain causal **Quick read** before preserving the precise definition, pipeline location, visible results, interactions, engine vocabulary, and technical diagram. The map groups the pipeline, shading, visibility and output, engine systems, animation, and ray tracing. Reading never changes renderer state; example buttons apply configurations deliberately.

The Scene window provides purpose-built geometry for texture minification, depth precision, transparency ordering, and lighting interpolation experiments in addition to the default torus.

Changing the selected scene preserves the current renderer state and camera. **Reset scene setup** explicitly applies that scene's recommended starting state and camera framing:

- Torus: conventional neutral rendering
- Texture minification: trilinear mipmapping and 8x anisotropy
- Depth precision: 16-bit depth with a near plane at 0.01 units
- Transparency: straight-alpha blending, depth testing, and disabled depth writes
- Lighting comparison: linear-light Blinn-Phong, mipmapping, and filtered directional shadows
- Stencil mask: unlit two-pass equal-reference masking
- Field interference: two world-space wave sources sampled by coarse and dense meshes
- SDF iso-surface: smooth sphere/torus union above a rasterized depth-test floor

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
