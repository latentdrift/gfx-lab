# Graphics Lab

A native, graph-based 3D compositing instrument. Render operations create different live views of a scene;
typed effect, analysis, mask, and composite operations reinterpret and combine their signals.

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

Open **Window → Signal Inspector** to inspect the selected operation, first Render, or final stack target.
The inspector provides cursor-centered zoom, panning, texel coordinates, and raw floating-point RGBA values;
this makes clipped, negative, and above-white composite results visible as data even when the viewport cannot
display their full numerical range.

The Timeline can switch between its dope sheet and a focused **Curve view**. Select a key first,
then inspect or drag the chosen vector/color component in time and value; the exact key editor remains beside it
for precise entry and interpolation selection.

## Source architecture

The executable is divided by rendering responsibility:

```text
src/
  main.cpp                    process entry point only
  app/
    Application.cpp           GLFW/ImGui lifecycle and workspace orchestration
    Animation.cpp             renderer-property keyframe capture and interpolation
    HardwareProfile.cpp       target capabilities and state normalization
    RenderOperationState.cpp  transient renderer-property materialization vocabulary
    State.cpp                 explicit renderer state, scene setups, JSON export
    Validation.cpp            opt-in renderer, document, import, and UI validation suite
  assets/
    ModelAsset.cpp            mesh/material import, image decoding, validation, and normalization
  document/
    Document.cpp              authoritative scene, operation, automation, and presentation state
    Operations.cpp            typed operation factories and stable signal outputs
    Identifiers.hpp           typed owners and operation/port signal addresses
    Persistence.cpp           native typed-v12 JSON save/load and earlier typed-document migration
  evaluation/
    Compiler.cpp              dependency validation and typed evaluation planning
    SignalRegistry.cpp        named GPU/scalar resources produced by evaluation
  editor/
    Commands.cpp              validated mutations and typed undo/redo history
  renderer/
    Renderer.cpp              direct EvaluationPlan execution and OpenGL resources
    Shaders.hpp               scene, output, shadow, and analysis GLSL programs
    TestGeometry.cpp          procedural diagnostic meshes
  ui/
    AnimationControls.cpp     inspector diamonds and property-key editing
    Inspector.cpp             pipeline-category controls and visual styling
    DocumentInspector.cpp     typed Essentials, Changes, All Properties, and operation editing
    DocumentTimeline.cpp      typed track, playhead, and selected-key editing
    Workspace.cpp             document navigator, viewport, menu, and docking layout
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

Use **File → Save Document…** (`Ctrl+S`) to save the complete `graphics-lab.document.v12` document, and
**File → Open Document…** (`Ctrl+O`) to restore it. Stable operations and signal references, sparse overrides,
animation and modulation, camera, scene, hardware target, presentation, and asset references are preserved.

The [`examples`](examples) directory contains loadable graph documents. Start with
[`rod-cone-xor-sdf.json`](examples/rod-cone-xor-sdf.json): it XORs two related SDF render passes, then sends
the result through the quantized rod/cone observer comparison. Its parameters remain ordinary editable lab
state after loading.

[`binocular-disparity-difference.json`](examples/binocular-disparity-difference.json) is the introductory
human-stereo experiment. Two Render operations use parallel left- and right-eye cameras with opposite lateral
offsets and asymmetric off-axis frusta that meet at one convergence distance; the cameras are translated, never
toed in. A separate **Stereo analysis** operation reconstructs visible points from the eye depth buffers and
reprojects them between cameras. Its outputs are red/cyan anaglyph, signed or absolute geometric disparity,
correspondence confidence, and monocular occlusion. In the latter, red is visible only to the left eye and cyan
only to the right. Use **Stereo pair** in the viewport to inspect what the two eyes receive without analysis.
Change the eye separation to change stereo strength, or convergence distance to move the zero-disparity plane.
This is distinct from ordinary `|leftColor-rightColor|`, which remains available as an artistic image operator
but mixes geometry with lighting, texture, and shading disagreement.

[`single-world-cone-rod-xor.json`](examples/single-world-cone-rod-xor.json) demonstrates observer inputs at the
composite boundary. Both operands read the exact same raw render pass; A measures approximate cone luminance,
B measures approximate rod response, and the compositor XORs their five-bit measurements. Change either
operand interpretation to L, M, S, rod, cone luminance, or centered opponent response without duplicating or
rerendering the scene. These are explicit RGB-derived observer approximations, not spectral measurements.

[`spectral-metamer-observer.json`](examples/spectral-metamer-observer.json) uses the dedicated Spectral
metamers scene. Its two objects have different sixteen-band reflectance spectra but matching LMS responses
under the reference daylight/human condition. Its Composite operation reads the Render operation's spectral attachment twice
and displays human-versus-shifted-observer disagreement. Disable that pass to see the reference match; use the
Spectral section in Render Settings to switch to tungsten or tri-band illumination and watch the metameric match fail.

Graphics Lab uses one persistent document-editing workspace: a typed operation graph on the left, an
always-visible canvas in the center, and contextual **Properties** on the right. Selecting an operation does not
change the viewed output. Drag typed ports to make unusual connections; contextual add commands connect routine
operations automatically. Render properties use **Essentials**, **Changes**, and **All Properties**;
Interpret, Composite, Stereo, Measure, and Final Output expose only the state they own.

Render operations expose literal Color, Device depth, World normal, Field, and Spectrum16 outputs. Every signal
separates where it lives (for example Screen2D), its storage shape (Scalar, Vector3, Spectrum16), and its semantic
meaning (depth, normal, luminance, coverage, signed distance, and so on), with encoding, coordinate space, units,
and known range carried as metadata. Luminance, Remap, Edge, Blur, Threshold, Gradient Map, and Warp transform those
signals explicitly. A Composite mask is a screen-scalar socket role—not a nominal Mask type or hidden mask-mode
preset. The graph shows each meaningful GPU allocation and execution boundary while the Add menu automatically
connects a new operation to the selected output. Numerically legal operations may strip a semantic they cannot
preserve; the compiler reports that as a warning rather than banning experimental math.

Canvas previews are view adapters, not graph operations: device depth is linearized, signed fields receive a
divergent palette, raw signed normals are encoded for display, Vector2 fields use direction hue and magnitude,
and Spectrum16 is reconstructed to RGB. None of those mappings modify the resource consumed by another node.
Edge publishes separate scalar strength and RG direction textures; Warp consumes a screen-space Vector2 direction
and samples a Color image along it.

The renderer implementation catalog remains available as filtered, collapsed groups under **All Properties**
without dictating workspace navigation. **Changes from Render Defaults** stays beside a variant, reports the
actual local values and animation tracks, and resets individual overrides in place.

Observer parameters used while an Interpret or Composite operation evaluates are stored on that operation. Final Output keeps a separate presentation observer.

Use **View → UI Scale** to resize text, controls, spacing, tabs, and interaction targets together from 75% to
200%. `Ctrl+-` and `Ctrl+=` step between the supplied sizes; `Ctrl+0` returns to 100%. Every font size is loaded
once at startup, so enlarged text remains crisp and repeatedly changing scale does not rebuild live GPU resources.

Floating tools are checked against the current monitor work areas. If a saved position belongs to a disconnected
or rearranged display, the tool is resized if necessary and recovered to the center of the main display. The
Timeline also changes between side-by-side and stacked panes as its available width changes.

Use **File > Import Model** or the graph toolbar's overflow menu to load OBJ, glTF, or GLB geometry through the native file chooser. The importer applies scene-node transforms, triangulates faces, generates missing smooth normals and usable tangents, preserves UV0, vertex color 0, submesh material assignments, base-color factors, and external or embedded base-color images. It centers the result and scales its longest bounds extent to exactly 3.0 units so camera distance, fog, quantization, and operation perturbations remain comparable between unrelated assets. Missing image references remain explicit warnings. Unloading a model is undoable.

The **Texture source** control can be authored on the global base and inherited by every pass. A selected pass stores a local override only when it differs. **Scene material** uses imported submesh materials, **Built-in checker** substitutes the diagnostic texture, **White texel** removes image variation while retaining material factors, and **Imported override** applies a separately imported PNG, JPEG, TGA, or BMP to any scene. A locally overridden texture is copied when its pass is duplicated, so correlated renders can perturb its UVs, sampling, color precision, and compositing independently. The inspector reports dimensions and alpha and exposes sRGB-color versus linear-data interpretation.

This first material boundary intentionally imports only base color and alpha. Normal, emissive, metallic-roughness, occlusion, multiple UV sets, skeletal animation, morph targets, cameras, and lights remain separate future systems. Copying stack JSON records source paths, content hashes, dimensions, material facts, and normalization scale rather than embedding image pixels or vertex buffers.

Render **Essentials** concentrates the changes most useful while making a variant: object transform, normal
inflation, camera offsets, UV scale/offset, and local procedural time. Image assignment, exact mapping,
sampling, hardware behavior, and the rest of the renderer remain available under the collapsed **All Properties**
groups. Every keyable Essential retains its animation control.

Undo and redo operate on typed document commands, covering renderer and composite settings, operation order and
names, animation tracks, scene state, hardware target, and presentation. Continuous property and gizmo edits
collapse into one history entry; selection, comparison view, and open windows remain workspace state.

The document is a validated dependency graph ending at **Output**. A new document begins as `Render → Output`. A **Render** applies inherited renderer state plus sparse local overrides and produces named Color, Depth, Normal, Field, and Spectrum16 resources. An **Interpret** converts a spectral resource through a selected observer without rerendering the scene. A **Composite** combines two named signals with explicit arithmetic. A **Measure** reduces an upstream image or field to a scalar—mean, RMS, peak, threshold coverage, or one mean color channel—and maps that signal onto a continuous property. The compiler performs a stable topological sort, so canvas position and document display order do not dictate execution; invalid cycles are rejected before mutation. The Inspector shows only controls owned by the selected object.

Use **+ Add** to create literal Render, Composite, Interpret, or Measure operations. The selected operation or final output supplies the obvious compatible input automatically. **Duplicate + Compare**, available beside the selected operation in Properties, from its node-title menu, or through searchable Commands, atomically creates an exact variant and a Compare operation, lays out the branch, and makes absolute difference visible in one undo step. Right-click any output socket to continue directly with compatible signal operations such as Luminance, Edge, Threshold, or an explicit Remap to Mask conversion. Input rows show their producer and provide direct **View** and **Go** actions. Named ports use stable operation/port addresses. Per-channel operations include Normal, difference, multiply, screen, exclusion, minimum/maximum, additive and subtractive hardware color math, half-add, quarter-add, signed color offset, and quantized bitwise XOR.

**Output** is deliberately downstream of the operation graph. Its Inspector can leave the final image as direct RGB or approximate composite-NTSC chroma bandwidth and luma/chroma crosstalk, then model scanlines, an aperture grille, and display bloom. These controls affect presentation only: raw operation textures and all A/B arithmetic remain unchanged and inspectable.

Its RGB observer modes reinterpret the completed image through approximate L, M, S cone and scotopic rod responses. Outputs include the LMS triplet, individual receptor channels, rod vision, a mesopic rod/cone mixture, L-minus-M and blue/yellow opponent channels, rod/cone absolute difference, and quantized rod/cone XOR. Exposure, dark adaptation, rod sensitivity, opponent gain, and XOR precision remain explicit. This is an RGB observer approximation rather than spectral rendering, so it cannot separate metameric spectra that already collapsed to the same RGB value.

The **Timeline** stores a sparse typed track for every animated property. Render-default tracks affect inheriting
operations; operation tracks take final precedence. Track rows show key times as diamonds and expose exact time,
value, deletion, and step/linear/smooth-step interpolation editing.

Animatable inspector controls have a right-aligned diamond: gray outline means unanimated, blue means the property has a track, and gold means it has a key at the playhead. Click the diamond to add or remove that exact key. Once a property is animated, changing its ordinary control writes that property at the playhead; **Auto Key** allows an ordinary control edit to create its first track. The control displays the evaluated playhead value, so editing does not require copying a sampled pose into the pass first.

The animation catalog covers eligible state across every pipeline category. Continuous numbers, vectors, colors,
and angles support step, linear, or smooth-step interpolation. Discrete settings use step tracks. Settings that
reallocate large GPU resources during playback remain deliberately non-keyable. Playback evaluates typed operation
values without overwriting authored state.

The top bar's **Target** selector defaults to **Unrestricted**. **PlayStation (PS1)** and **Nintendo 64** normalize every Render to target-representable state, remove unavailable sections and controls, narrow shared controls to supported choices, and display important forced values or labelled emulation substitutes as profile facts. Switching back to Unrestricted unlocks the controls but does not restore values discarded during normalization. Reset buttons are also normalized by the active target.

The Nintendo 64 target exposes the standard RSP/RDP/VI model: one- or two-cycle `(A - B) x C + D` color combiners, named combiner sources, primitive/environment registers, RDP surface and alpha-compare modes, point/three-point/box texture filters, mip/trilinear/sharpen/detail modes, nine texture formats, tile addressing and calculated 4096-byte TMEM use, RSP texture generation and vertex fog, Z compare/update, coverage antialiasing, RGBA16/RGBA32 framebuffer state, color dithering, and VI reconstruction/divot filters. Coverage and VI behavior are explicitly labelled approximations; the combiner, format quantization, three-point filter, alpha comparison, and TMEM accounting are modeled directly.

The unrestricted **Field** tool can produce either wave interference or a genuine signed-distance field. SDF producers include sphere, box, torus, a rotating 4D hypersphere slice, and a pulsating sphere, with union, intersection, A-minus-B, and smooth-union operations. Every Render owns a procedural time transform: `local = timeline × scale + offset`; negative scale reverses time and zero freezes it. Both values are keyable and drivable. The pass field attachment stores signed world-unit distance in `R16F`; preview colors are only a display mapping, while named pass-field operands retain negative and positive values for composite arithmetic. A proximity mapping lets the same field drive mesh-vertex displacement, fragment discard, surface color, emission, and pass masks. **Ray-march iso-surface** renders the selected level as implicit geometry and writes fragment depth, so it participates in ordinary occlusion with rasterized meshes.

Use **File > Copy Stack JSON** to place the human-readable `graphics-lab.document.v11` document on the clipboard.
It records typed operations and signal references directly, together with sparse overrides, property tracks,
modulation routes, texture mapping, perturbations, masks, scene, camera, and presentation state.

Use **Help > Graphics Handbook** to open the built-in technical reference. Start with **From mesh to pixel**, then follow its related-concept links or browse the knowledge map. Every article begins with a plain causal **Quick read** before preserving the precise definition, pipeline location, visible results, interactions, engine vocabulary, and technical diagram. The map groups the pipeline, shading, visibility and output, engine systems, animation, and ray tracing. Reading never changes renderer state; example buttons apply configurations deliberately.

The Operation Graph's **Scene** menu provides purpose-built scenes for texture minification, depth precision, transparency ordering, and lighting interpolation studies in addition to the default torus.

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
