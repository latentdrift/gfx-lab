#include "handbook/Handbook.hpp"
#include "app/HardwareProfile.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace handbook {
namespace {

enum class Diagram {
  Pipeline, Attributes, Projection, Triangle, Mipmaps, Tbn, Lighting, Depth, Blend,
  Stencil, Color, Shadow, Output, Overdraw, ShaderProgram, ShaderData, RenderGraph,
  Comparison, Performance, Animation, RayTracing
};

struct Chapter {
  const char* domain;
  const char* name;
};

struct Article {
  const char* chapter;
  const char* title;
  const char* definition;
  const char* pipelineLocation;
  const char* visualResult;
  const char* interactions;
  const char* engineVocabulary;
  Diagram diagram;
  Example example = Example::None;
  const char* baseline = nullptr;
  const char* alternative = nullptr;
};

constexpr std::array chapters = {
  Chapter{"PIPELINE", "Foundations"}, Chapter{"PIPELINE", "Geometry"},
  Chapter{"PIPELINE", "Camera"}, Chapter{"PIPELINE", "Rasterization"},
  Chapter{"SHADING", "Shaders"}, Chapter{"SHADING", "Fields & implicit surfaces"},
  Chapter{"SHADING", "Surface"},
  Chapter{"SHADING", "Materials"}, Chapter{"SHADING", "Texture"},
  Chapter{"SHADING", "Lighting"}, Chapter{"VISIBILITY & OUTPUT", "Visibility"},
  Chapter{"VISIBILITY & OUTPUT", "Color"}, Chapter{"VISIBILITY & OUTPUT", "Output"},
  Chapter{"ENGINE SYSTEMS", "Engine architecture"}, Chapter{"ENGINE SYSTEMS", "Performance"},
  Chapter{"BEYOND THIS LAB", "Animation"}, Chapter{"BEYOND THIS LAB", "Ray tracing"}
};

constexpr std::array articles = {
  Article{"Foundations", "Start here: from mesh to pixel",
    "A mesh is a list of vertices connected into triangles. The camera and vertex shader place those vertices on screen, the rasterizer finds the pixel samples covered by each triangle, the fragment shader proposes colors, and framebuffer tests decide which results are stored.",
    "Whole renderer, in execution order", "Together these steps turn model data into the image in the viewport.",
    "Each step has a narrow job. Most graphics techniques change one step, its inputs, or the handoff between two steps.",
    "Start engine conversations by locating a feature in geometry, camera, rasterization, shading, visibility, or output.", Diagram::Pipeline},
  Article{"Foundations", "The realtime rasterization pipeline",
    "A realtime rasterizer transforms mesh vertices, assembles triangles, generates fragments, shades them, tests visibility, and writes pixels to framebuffer attachments.",
    "Whole renderer", "The final image is the accumulated result of several independent operations, not one shader.",
    "Every later stage consumes assumptions and data produced by earlier stages.",
    "Ask which pipeline stage or render pass owns each requested behavior.", Diagram::Pipeline},
  Article{"Foundations", "Shaders and fixed-function state",
    "Shaders are programmable GPU stages. Depth testing, stencil testing, blending, face culling, viewport mapping, and sampler configuration are GPU state surrounding those programs.",
    "Shader stages and output-merger state", "Two identical shaders can produce different images when their depth, blend, cull, or sampler state differs.",
    "Material systems usually bundle shader programs with required render state.",
    "Specify shader logic and render state separately; do not call the entire pipeline a shader.", Diagram::Pipeline},
  Article{"Foundations", "Nintendo 64: RSP, RDP, and VI",
    "The N64 CPU builds display lists; graphics microcode on the Reality Signal Processor transforms and lights vertices; the Reality Display Processor rasterizes, textures, combines, tests, and blends pixels; the Video Interface reconstructs the framebuffer for display.",
    "Whole N64 graphics architecture", "The visible image reflects three distinct processing domains rather than one programmable shader.",
    "RSP microcode choice, RDP cycle state, memory traffic, framebuffer format, and VI state impose different constraints.",
    "Separate RSP geometry work, RDP pixel work, and VI output processing when specifying an N64 target.", Diagram::Pipeline},
  Article{"Shaders", "What a shader program is",
    "A shader is a small GPU program for one programmable pipeline stage. A linked graphics program combines compatible stages; it does not include the mesh, textures, framebuffer, or most fixed-function state.",
    "Programmable GPU stages", "Changing shader code can move vertices, calculate surface color, write auxiliary values, or discard fragments.",
    "The program's declared inputs must match vertex layouts, resource bindings, stage interfaces, and render-target formats.",
    "Name the stage and responsibility: vertex transformation, fragment material evaluation, compute work, or another specific stage.", Diagram::ShaderProgram},
  Article{"Shaders", "Vertex shaders",
    "A vertex shader runs once for each submitted vertex. It must produce a clip-space position and commonly transforms or forwards attributes needed by later stages.",
    "After vertex fetch, before primitive assembly", "It can deform geometry or change projected positions, but cannot directly create a final pixel color.",
    "Shared triangle vertices run independently; neighboring vertices only become connected when primitives are assembled afterward.",
    "Discuss model, world, view, clip, and normalized-device coordinate spaces explicitly.", Diagram::ShaderProgram},
  Article{"Shaders", "Fragment shaders",
    "A fragment shader runs for rasterizer-generated samples covered by primitives. It evaluates material and lighting inputs, then writes color or other render-target values; a fragment is only a candidate pixel until tests and blending finish.",
    "After rasterization, before per-fragment output operations", "It determines surface appearance and can discard coverage, but depth, stencil, and blending still govern the final framebuffer update.",
    "Invocation count depends on coverage, overdraw, sample count, early tests, and helper invocations used for derivatives.",
    "Use fragment shader in OpenGL/Vulkan terminology and pixel shader in Direct3D terminology.", Diagram::ShaderProgram},
  Article{"Shaders", "Attributes, uniforms, varyings, and resources",
    "Attributes vary per vertex. Uniform or constant data stays fixed across a draw. Varyings carry vertex-stage outputs into interpolated fragment-stage inputs. Textures, samplers, storage buffers, and images are bound resources.",
    "Stage interfaces and resource binding", "These data paths control what a shader can know; they are not visual effects by themselves.",
    "Interpolation qualifiers apply to varyings, sampler state is distinct from a texture, and resource layouts must match the program interface.",
    "Describe vertex attributes, per-draw constants, interpolants, descriptors/bind groups, textures, samplers, and buffers separately.", Diagram::ShaderData},
  Article{"Shaders", "Compilation, linking, and shader variants",
    "Shader source compiles into stage code, compatible stages link into a program or pipeline, and reflection can expose its inputs. Variants are separately compiled combinations for materially different features.",
    "Asset build and runtime pipeline creation", "Compilation does not normally change a frame, but variant selection determines the executed code and supported features.",
    "Unbounded feature switches cause variant explosion; dynamic branches avoid some variants but can increase GPU work or divergence.",
    "Ask whether a feature is a compile-time define, specialization constant, uniform branch, separate material, or separate pass.", Diagram::ShaderData},
  Article{"Shaders", "Compute and optional graphics stages",
    "Compute shaders launch general workgroups outside the draw pipeline. Tessellation stages subdivide patches, geometry shaders process whole primitives, and mesh/task shaders reorganize geometry work on supported hardware.",
    "Separate compute dispatches or optional pre-raster stages", "They enable simulation, culling, image processing, generated geometry, and specialized subdivision.",
    "They require explicit synchronization and are not automatically faster than vertex/fragment processing.",
    "Name the workload first; only prescribe a stage when its execution and data model fit that workload.", Diagram::ShaderProgram},
  Article{"Fields & implicit surfaces", "Signed distance fields",
    "A signed distance field maps a position to the shortest distance from a surface. Values are negative inside, zero on the surface, and positive outside; the sign and world-unit magnitude are meaningful data, not display color.",
    "Analytic field evaluation or a sampled scalar resource", "A field is invisible until a consumer maps it to geometry, color, masking, simulation, or another operation.",
    "Distance remains geometrically valid only under compatible transforms and distance-preserving combination rules.",
    "Name the producer, coordinate space, units, sign convention, and consumer separately.", Diagram::RenderGraph},
  Article{"Fields & implicit surfaces", "Constructive solid geometry with SDFs",
    "Minimum forms a union, maximum forms an intersection, and max(A, -B) subtracts B from A. Smooth minimum replaces the hard union seam with a controllable transition region.",
    "Field-production stage", "Boolean operations create new implicit shapes without editing triangle topology.",
    "Smooth operations alter the exact distance property near the blend; subtraction also reverses B's boundary orientation.",
    "Use SDF union, intersection, difference, smooth union, and smoothing radius rather than generic shape blending.", Diagram::Attributes},
  Article{"Fields & implicit surfaces", "Iso-surfaces and sphere tracing",
    "An iso-surface is the set of positions where a scalar field equals a chosen level. Sphere tracing advances a ray by the current signed-distance estimate until it reaches that level or exhausts its step and distance limits.",
    "Implicit-geometry render pass", "The zero crossing becomes a visible surface with normals estimated from the field gradient.",
    "Hit epsilon controls precision, maximum steps bound cost, and writing fragment depth lets implicit and rasterized geometry occlude each other.",
    "Specify iso level, hit epsilon, maximum steps, maximum ray distance, normal method, and depth-write behavior.", Diagram::RayTracing},
  Article{"Geometry", "Vertex attributes",
    "A vertex can carry position, normal, texture coordinates, color, tangent, skin weights, and other attributes. Rasterization interpolates selected attributes across each triangle.",
    "Asset data and vertex input", "Different attributes reveal different descriptions of the same surface.",
    "Shared vertices require shared attribute values; UV seams and hard normals often require duplicated vertices.",
    "Describe required mesh attributes explicitly: positions, normals, UV0, tangents, vertex colors.", Diagram::Attributes},
  Article{"Geometry", "Vertex position quantization",
    "Quantization rounds continuous vertex positions to a fixed grid. Applying it before projection causes silhouettes and internal edges to move in discrete increments.",
    "Vertex processing", "Geometry appears to snap or wobble as the object or camera moves.",
    "Grid space matters: model-space, world-space, view-space, and screen-space quantization behave differently.",
    "Request a quantization space and step, such as model-space positions at 1/16 unit.", Diagram::Attributes,
    Example::VertexQuantization, "Full-precision model-space positions", "Model-space positions quantized to 1/8 unit"},
  Article{"Camera", "Perspective and orthographic projection",
    "Perspective projection divides coordinates by depth, making distant objects appear smaller. Orthographic projection preserves apparent size with distance.",
    "Projection transform", "Perspective creates convergence and foreshortening; orthographic views appear dimensionally flat.",
    "Field of view applies to perspective; view height applies to orthographic projection.",
    "Specify projection type, vertical field of view or orthographic height, and clipping planes.", Diagram::Projection,
    Example::Projection, "Perspective projection at 45 degrees", "Orthographic projection with a 4-unit view height"},
  Article{"Camera", "Near plane and depth distribution",
    "Perspective depth is nonlinear: much of the depth buffer's precision is concentrated near the camera. Moving the near plane unnecessarily close wastes precision over the rest of the view.",
    "Projection and depth buffer", "Poor precision produces unstable comparisons and z-fighting on nearby surfaces.",
    "Near plane, far plane, depth format, and projection type jointly determine useful precision.",
    "Treat near/far planes as precision parameters, not merely clipping distances.", Diagram::Depth,
    Example::DepthPrecision, "24-bit depth with a 0.1-unit near plane", "16-bit depth with a 0.01-unit near plane"},
  Article{"Rasterization", "Triangle winding and face culling",
    "Projected vertex order classifies triangles as front- or back-facing. Face culling discards one orientation before fragment shading.",
    "Primitive assembly and rasterization", "Incorrect winding can make an object's exterior disappear and expose its interior.",
    "Mirrored transforms can reverse winding; two-sided materials normally disable culling or handle both orientations.",
    "Specify front-face convention and cull mode: none, front, or back.", Diagram::Triangle},
  Article{"Rasterization", "Perspective-correct interpolation",
    "Perspective-correct interpolation accounts for clip-space w while interpolating attributes. Affine interpolation changes linearly in screen space and causes textures to warp with depth.",
    "Rasterizer interpolation", "Affine UVs visibly swim and shear across triangles under perspective.",
    "The difference disappears under orthographic projection because w is effectively constant.",
    "Request perspective-correct or noperspective/affine interpolation for specific varyings.", Diagram::Triangle,
    Example::AffineMapping, "Perspective-correct UV interpolation", "Affine screen-space UV interpolation"},
  Article{"Rasterization", "Multisample anti-aliasing",
    "MSAA stores multiple coverage and depth samples per pixel while usually shading less frequently than full supersampling. A resolve operation combines samples into a displayable pixel.",
    "Rasterization, framebuffer storage, resolve pass", "Silhouette edges become smoother without increasing the logical output dimensions.",
    "MSAA does not solve texture aliasing; mipmaps and anisotropy address texture minification.",
    "Specify sample count, attachment compatibility, and when the multisample target resolves.", Diagram::Triangle},
  Article{"Rasterization", "N64 coverage antialiasing",
    "The RDP tracks subpixel coverage for rasterized pixels, the blender conditionally updates framebuffer color and coverage, and the VI uses that information during final reconstruction.",
    "RDP rasterizer/blender plus Video Interface", "Polygon silhouettes become smoother, with a different signal path and artifact profile than conventional MSAA.",
    "Coverage storage, Z mode, surface submission order, framebuffer format, and VI filtering interact; the lab uses four-sample coverage as an approximation.",
    "Call it RDP coverage antialiasing, not N64 MSAA, and identify whether VI reconstruction is also enabled.", Diagram::Triangle,
    Example::N64Coverage, "One raster sample; coverage AA disabled", "Four-sample approximation of RDP coverage AA"},
  Article{"Surface", "Normals and shading interpolation",
    "A geometric face normal is constant per triangle. Smooth shading interpolates authored vertex normals, while flat shading derives or selects one normal for the face.",
    "Mesh attributes and fragment shading", "Smooth normals make coarse geometry appear curved without changing its silhouette.",
    "Normal interpolation, vertex splitting, and the lighting model are distinct decisions.",
    "Use the terms flat normals, smooth vertex normals, Gouraud lighting, and Phong normal interpolation precisely.", Diagram::Tbn,
    Example::LightingInterpolation, "Gouraud per-vertex Lambert lighting", "Phong-shaded per-fragment Lambert lighting"},
  Article{"Surface", "Tangent-space normal mapping",
    "A normal map stores directions relative to a surface basis. Tangent, bitangent, and normal vectors form the TBN matrix that transforms sampled directions into world or view space.",
    "Asset attributes, texture sampling, fragment shading", "Fine lighting detail appears without modifying the mesh silhouette.",
    "Incorrect tangents, handedness, normal-map convention, or nonuniform scale can rotate lighting incorrectly.",
    "Specify tangent-space normal mapping, tangent generation convention, map handedness, and strength.", Diagram::Tbn,
    Example::NormalMapping, "Interpolated geometric normals", "Tangent-space normal map at strength 1.5"},
  Article{"Surface", "Transparency and compositing",
    "Alpha testing discards fragments. Alpha blending combines source and destination colors using explicit factors. Most conventional transparency is order-dependent.",
    "Fragment shader, depth state, framebuffer blending", "Overlapping transparent surfaces change when draw order or depth writes change.",
    "Straight and premultiplied alpha require different source blend factors.",
    "Specify alpha representation, blend equation/factors, sort order, depth test, and depth writes.", Diagram::Blend,
    Example::Transparency, "Opaque surfaces with depth writes", "Straight-alpha blending, back-to-front order, depth writes disabled"},
  Article{"Surface", "PlayStation semitransparency equations",
    "The original PlayStation selected one of four fixed equations for marked semitransparent pixels: B/2 + F/2, B + F, B - F, or B + F/4, where B is the framebuffer background and F is the incoming foreground.",
    "GPU framebuffer read-modify-write operation", "Average produces even translucency, addition produces strong light, subtraction darkens, and quarter-add produces a weaker glow.",
    "The operation is order-dependent, saturates at the framebuffer range, and is not equivalent to general source-alpha blending.",
    "Name the exact PS1 equation instead of requesting generic alpha blending.", Diagram::Blend,
    Example::Ps1Semitransparency, "Opaque intersecting surfaces", "PS1 average equation: background/2 + foreground/2"},
  Article{"Surface", "N64 RDP color combiner and cycle types",
    "Each RDP color-combiner cycle evaluates a restricted expression of the form (A - B) x C + D using sources such as TEXEL0, TEXEL1, SHADE, PRIMITIVE, ENVIRONMENT, COMBINED, and LOD fraction.",
    "RDP color/alpha combiner before the framebuffer blender", "It constructs textured, vertex-lit, tinted, detail, environment, and two-layer materials without arbitrary shader code.",
    "Two-cycle mode permits a second expression but reduces nominal pixel throughput; trilinear and detail modes consume parts of the same two-cycle machinery.",
    "Export cycle type, all four operands per cycle, primitive/environment registers, and separate blender/Z state.", Diagram::ShaderData,
    Example::N64Combiner, "TEXEL0 x ONE: texture without vertex lighting", "TEXEL0 x SHADE: Gouraud-lit texture modulation"},
  Article{"Materials", "Material versus shader",
    "A shader defines executable GPU logic. A material selects that logic and supplies surface parameters, textures, keywords, and required render state for a particular kind of surface.",
    "Engine asset layer over shader programs and GPU state", "Many materials can look different while executing the same shader program.",
    "A material may need multiple pass-specific programs for depth, shadows, and camera rendering while retaining one conceptual surface definition.",
    "Request a material model and its parameters; identify custom shader code only where the model requires new computation.", Diagram::ShaderData},
  Article{"Materials", "BRDFs and physically based materials",
    "A BRDF describes how opaque surface reflection varies with incoming and outgoing direction. Common PBR workflows parameterize diffuse/base color, metalness, roughness, and a microfacet specular model.",
    "Material evaluation inside lighting", "Roughness broadens highlights; metalness changes the relationship between colored specular reflection and diffuse response.",
    "Physically based does not mean photorealistic: lighting units, tone mapping, authored values, and artistic constraints still shape the image.",
    "Specify the BRDF and workflow, such as metallic-roughness Cook-Torrance with GGX distribution.", Diagram::Lighting},
  Article{"Materials", "Material inputs and texture semantics",
    "A texture's meaning determines decoding and use: base color is normally color data, while normals, roughness, metalness, occlusion, and masks are numeric data.",
    "Asset import, material binding, and shader evaluation", "Incorrect semantics can distort normals, roughness, masks, or brightness even when the source image looks plausible.",
    "Channel packing, UV set, scale/bias, color space, normal convention, and sampler state are independent metadata.",
    "List each material input with semantic, channel, UV set, encoding, range, and fallback value.", Diagram::ShaderData},
  Article{"Texture", "Filtering and reconstruction",
    "Nearest filtering selects one texel. Bilinear filtering combines four neighboring texels. These describe reconstruction within one mip level.",
    "Texture sampler state", "Nearest sampling preserves hard texel boundaries; bilinear sampling softens them.",
    "Magnification and minification filters can be configured independently.",
    "Name magnification filter, minification filter, address mode, and color-space interpretation.", Diagram::Mipmaps},
  Article{"Texture", "Mipmaps, trilinear, and anisotropic filtering",
    "Mipmaps are prefiltered lower-resolution copies. Trilinear filtering blends adjacent mip levels. Anisotropic filtering takes additional samples for elongated footprints at grazing angles.",
    "Texture asset and sampler state", "Receding textures become stable instead of shimmering or blurring excessively.",
    "Mipmaps reduce minification aliasing; anisotropy preserves directional detail; neither changes magnification.",
    "Request mip generation, mip-level filtering, and a target anisotropy value separately.", Diagram::Mipmaps,
    Example::TextureMinification, "Bilinear sampling without mipmaps", "Trilinear mipmapping with 8x anisotropy"},
  Article{"Texture", "Indexed textures and CLUT palettes",
    "An indexed texture stores a palette entry number at each texel rather than RGB. A color lookup table, or CLUT, converts that index into the displayed color; PS1 texture pages supported 4-bit, 8-bit, and direct-color texels.",
    "Texture storage and sampling", "Changing one palette can recolor many texels, while 4-bit indices restrict a texture to 16 addressable palette entries at once.",
    "Index precision, palette contents, transparency flags, texture-window addressing, and filtering are separate concerns.",
    "Specify index depth and CLUT size: 4-bit indices with 16 entries, or 8-bit indices with 256 entries.", Diagram::Mipmaps,
    Example::ClutTextures, "Direct-color texture sampling", "4-bit indices sampling a 16-entry CLUT"},
  Article{"Texture", "N64 TMEM, tiles, and texture formats",
    "The RDP samples active texture tiles from 4096 bytes of on-chip TMEM. Tile descriptors define format and addressing; CI textures also reserve TMEM for a texture lookup table.",
    "RDP texture engine and display-list texture uploads", "Smaller or indexed formats permit larger active tiles and more simultaneous texture data.",
    "TMEM is a working set, not a maximum asset size. Oversized images require tiled uploads, subdivision, or another format; mip and second-texture data share the same budget.",
    "Specify RGBA16/32, CI4/CI8, IA, or I format; tile dimensions; TLUT format; calculated TMEM bytes; and upload boundaries.", Diagram::Mipmaps,
    Example::N64TextureFormats, "RGBA32 direct-color tile", "CI4 indices with a 16-entry RGBA16 TLUT"},
  Article{"Texture", "N64 three-point filtering and mip modes",
    "The usual RDP filtered mode interpolates the three nearest texels rather than performing true four-sample bilinear filtering. The RDP can also point-sample, select mip levels, interpolate levels, sharpen, or apply detail texture behavior.",
    "RDP texture engine, filter, and two-cycle combiner", "Three-point filtering softens texels with a subtle diagonal bias; mip modes stabilize or reshape receding texture detail.",
    "Trilinear, sharpen, and detail modes require two-cycle resources, and every active level must fit the TMEM working set.",
    "Request point, N64 three-point, or box filtering separately from nearest-mip, trilinear, sharpen, or detail mode.", Diagram::Mipmaps,
    Example::N64ThreePoint, "RDP point sampling", "RDP three-point approximate bilinear filtering"},
  Article{"Texture", "N64 trilinear mipmapping",
    "Two-cycle RDP operation can sample adjacent mip levels and interpolate them using the fractional level-of-detail value before subsequent color combination.",
    "RDP TX0/TX1 texture paths and color combiner", "Transitions between mip levels become gradual and distant surfaces shimmer less.",
    "Both levels occupy TMEM, two-cycle mode halves nominal pixel throughput, and combiner inputs compete with other two-texture effects.",
    "State mip count, TMEM layout, LOD range, two-cycle combiner configuration, and whether interpolation, detail, or sharpen mode is active.", Diagram::Mipmaps,
    Example::N64Mipmap, "Three-point filtering at base level only", "Two-cycle trilinear mip interpolation"},
  Article{"Lighting", "Gouraud, Phong shading, and reflection models",
    "Gouraud computes lighting at vertices and interpolates color. Phong shading interpolates normals and computes lighting per fragment. Phong and Blinn-Phong also name related specular reflection formulas.",
    "Vertex or fragment shader", "Per-fragment lighting preserves smaller highlights and reduces interpolation artifacts.",
    "Phong shading is an interpolation strategy; Phong reflection is a specular model. They are related but not identical terms.",
    "State lighting frequency and reflection model independently.", Diagram::Lighting,
    Example::LightingInterpolation, "Gouraud per-vertex Lambert lighting", "Per-fragment Blinn-Phong reflection"},
  Article{"Lighting", "Shadow mapping",
    "A shadow map stores scene depth from the light's view. Camera-visible fragments transform into light space and compare their depth against the stored value.",
    "Separate light-depth pass plus camera shading pass", "Objects prevent direct light from reaching receivers, creating cast shadows.",
    "Resolution, projection bounds, bias, filtering, and light type determine quality and artifacts.",
    "Specify shadow-map resolution, light projection, comparison bias, and PCF kernel.", Diagram::Shadow,
    Example::ShadowMapping, "Direct lighting without shadows", "1024px directional shadow map with 3x3 PCF"},
  Article{"Lighting", "Vertex depth cueing",
    "Depth cueing computes a blend factor from camera depth at each vertex, then interpolates it across the polygon and moves color toward a configured far color.",
    "Geometry Transformation Engine color calculation and Gouraud interpolation", "Distant geometry fades toward fog, darkness, or an atmospheric color with changes aligned to polygon interpolation.",
    "It is not volumetric fog and can reveal large triangles because the factor is evaluated only at vertices.",
    "Request vertex depth cueing with near/far cue distances and an explicit far color.", Diagram::Lighting,
    Example::VertexDepthCue, "No depth cue; distance does not alter color", "Vertex depth cue from 4 to 12 units toward blue-gray"},
  Article{"Visibility", "Depth testing and depth writes",
    "The depth test compares an incoming fragment depth with stored depth. A separate write mask determines whether a passing fragment replaces the stored value.",
    "Per-fragment framebuffer operations", "Depth testing makes nearer surfaces occlude farther ones independent of opaque draw order.",
    "Transparent rendering often keeps depth testing but disables depth writes.",
    "Specify comparison function, write mask, clear value, format, and projection range.", Diagram::Depth,
    Example::DepthPrecision, "24-bit depth with conventional near plane", "16-bit stressed depth precision"},
  Article{"Visibility", "Stencil testing",
    "The stencil buffer stores a small integer per pixel. Tests and operations can conditionally update it and conditionally permit later rendering.",
    "Per-fragment framebuffer operations across multiple passes", "It creates exact screen-space masks for portals, outlines, mirrors, and cutaways.",
    "Stencil normally shares a packed depth-stencil attachment and requires deliberate pass ordering.",
    "Describe stencil reference, read/write masks, comparison, and fail/depth-fail/pass operations.", Diagram::Stencil,
    Example::Stencil, "Unmasked textured quad", "Two-pass sphere mask using Always/Replace then Equal/Keep"},
  Article{"Visibility", "Overdraw",
    "Overdraw is the number of fragments processed for pixels that may already have been covered. It measures hidden or repeatedly blended rasterization work.",
    "Performance analysis pass", "A heat map reveals overlapping transparent geometry and hidden surface work.",
    "Early depth rejection can reduce shading cost even when geometric overdraw remains.",
    "Ask for overdraw, shader-cost, draw-call, and bandwidth analysis separately.", Diagram::Overdraw,
    Example::Overdraw, "Normally shaded scene", "Additive fragment-count heat map with depth disabled"},
  Article{"Visibility", "Ordering tables and painter submission",
    "Without a depth buffer, software can assign primitives or objects to buckets by camera depth and submit farther buckets before nearer buckets. Later drawing then covers or blends over earlier drawing.",
    "CPU/GTE submission order before GPU rasterization", "Correct ordering creates plausible occlusion and transparency; intersections and items sharing a bucket can still be wrong.",
    "Object sorting cannot resolve intersecting polygons, bucket quantization creates ties, and semitransparency makes every ordering error visible.",
    "State ordering direction, bucket count, depth key, and whether granularity is object or polygon.", Diagram::Depth,
    Example::OrderingTable, "Deliberately reversed transparent-object submission", "32-bucket far-to-near object ordering table"},
  Article{"Visibility", "N64 surface and Z modes",
    "RDP render modes combine Z comparison/update, coverage behavior, and blender rules for opaque, translucent, decal, and interpenetrating surfaces.",
    "RDP blender, memory interface, and Z buffer", "The selected mode determines whether surfaces occlude, blend, sit coplanar, or preserve intersecting coverage.",
    "Translucency remains order-dependent; decal behavior uses Z and delta-Z semantics rather than a generic modern polygon-offset slider.",
    "Specify surface class, Z compare, Z update, primitive versus interpolated depth, and alpha/coverage behavior together.", Diagram::Depth},
  Article{"Color", "Linear light and encoded RGB",
    "Display-oriented RGB encodings are nonlinear. Lighting and most blending should operate on decoded linear-light values, followed by output encoding for display.",
    "Texture decode, shading, framebuffer format, output encoding", "Incorrect encoded-space lighting often produces unnaturally dark gradients and blends.",
    "Color textures are usually encoded; normal maps and data textures are normally linear data.",
    "Specify texture color spaces, linear render targets, tone mapping, and output transfer function.", Diagram::Color,
    Example::LinearLight, "Lighting directly in encoded RGB values", "Decode, light in linear space, then encode for display"},
  Article{"Color", "Color quantization and dithering",
    "Color quantization restricts channel values to discrete levels. Dithering offsets nearby pixels before quantization to trade structured noise for reduced banding.",
    "Output or post-processing pass", "Gradients become stepped; ordered dithering creates a stable threshold pattern between levels.",
    "Dither amplitude must match the quantization step and should occur in the intended color space.",
    "Specify channel bit depth and the exact dither method, such as a 4x4 Bayer matrix.", Diagram::Color,
    Example::ColorQuantization, "8 bits per RGB channel without dithering", "5 bits per channel with 4x4 Bayer dithering"},
  Article{"Color", "Cone, rod, and opponent-response approximations",
    "Human photopic vision begins with overlapping long-, medium-, and short-wavelength cone responses; rods contribute a highly sensitive largely achromatic signal in dim conditions. Early opponent channels compare responses, including an L-minus-M axis and an S-versus-L-plus-M axis.",
    "Observer transform after scene rendering", "The same RGB image can become cone reconstruction, rod luminance, a mesopic mixture, individual receptor responses, or signed chromatic disagreement.",
    "An RGB-to-LMS matrix approximates receptor response but cannot recover the original spectrum: metameric spectra that produced the same RGB remain identical. Rod/cone XOR is expressive quantized arithmetic, not a biological model.",
    "Distinguish spectral radiance, RGB tristimulus values, LMS cone response, scotopic rod response, adaptation, and opponent channels.", Diagram::Color},
  Article{"Output", "Internal resolution and upscaling",
    "A renderer can draw the scene into a fixed-resolution offscreen framebuffer, then sample that completed image into the presentation viewport.",
    "Render targets and output pass", "Low internal resolution limits spatial detail; nearest upscaling preserves discrete pixels.",
    "Material texture filtering and framebuffer upscaling are different sampling operations.",
    "Specify internal resolution, presentation aspect ratio, and upscaling filter independently.", Diagram::Output,
    Example::InternalResolution, "640x480 internal rendering", "160x120 internal rendering with nearest upscaling"},
  Article{"Output", "N64 Video Interface filtering",
    "The Video Interface reads the completed framebuffer, uses coverage and neighboring samples to reconstruct output, optionally removes single-pixel divots, and scales the signal to video timing.",
    "After RDP framebuffer rendering", "Reconstruction and divot filtering soften or stabilize the final signal independently of material texture filtering.",
    "VI filtering is not bilinear texture sampling and should be evaluated after framebuffer precision, dithering, and coverage behavior.",
    "Specify framebuffer resolution/format, RDP coverage AA, VI reconstruction, divot filtering, and final display scaling separately.", Diagram::Output,
    Example::N64VideoInterface, "VI reconstruction and divot filters disabled", "Approximate VI reconstruction plus horizontal divot filter"},
  Article{"Output", "Render algebra between completed images",
    "Render algebra evaluates correlated render passes, then computes a per-pixel, usually per-channel relationship F(accumulator, pass). Absolute difference preserves disagreement magnitude; signed difference also preserves which input produced the larger value. Repeating the operation forms a bottom-to-top pass stack.",
    "Separate fullscreen passes after each selected renderer pipeline has produced an image",
    "Agreement, opposition, and partial channel disagreement become the subject of the image. Clamped extrema can appear black, white, or intensely saturated without representing physical darkness or emitted light.",
    "Camera, geometry, resolution, and sample alignment must match if the goal is pipeline comparison. Deliberate perturbation makes those mismatches expressive instead. Encoded-RGB versus linear-light arithmetic changes the result; gain reveals small residuals, bias displays signed values, and clamp, preserve, or wrap policies treat extrema differently.",
    "Call this comparative rendering, image-space arithmetic, render algebra, or difference compositing. State the operator and whether it runs before or after quantization, dithering, framebuffer blending, and display reconstruction. Do not confuse it with an RDP material combiner or framebuffer alpha blending.", Diagram::Comparison},
  Article{"Engine architecture", "Render-pass stacks and controlled perturbation",
    "A render-pass stack rerenders a shared scene several ways, then combines the output images sequentially. Keep common renderer choices in one global base and let each pass store only its intentional local deviations. Duplicating a pass preserves both spatial correlation and a readable list of what makes that interpretation different.",
    "CPU pass scheduling, per-pass render targets, and fullscreen compositing passes",
    "Near-duplicates reveal silhouettes, parallax, topology, texture parameterization, depth boundaries, sampling thresholds, and color extrema as controllable interference structures.",
    "A duplicated framebuffer can only receive image-space edits; a duplicated render pass can alter the 3D interpretation and rerasterize it. Copying every global value into every pass makes later global edits ambiguous. Inheritance keeps the common intent singular, while a sparse override says exactly where one pass stops following it. More passes still increase render-target memory, scene submission, fill cost, and attribution difficulty.",
    "Describe the global base once. For each pass, state its sparse overrides, local animation tracks, output buffer, render-target format, and composite step. Effective property precedence is global base, global animation, local override, then local animation. This resembles a layer stack, but each layer may be a rerender rather than a stored bitmap.", Diagram::Comparison},
  Article{"Animation", "Keyframes and parameter animation",
    "A typed property track contains keyframes for one named parameter. A global track changes the shared input seen by every inheriting pass; a local track changes only one pass and wins over its static override. Continuous values may use step, linear, or smooth-step interpolation. Booleans, integers, and algorithm selections always step because values between their legal states have no meaning. Graphics Lab evaluates tracks into a temporary render-pass stack, leaving authored values intact.",
    "Animation timeline evaluation before render-pass submission",
    "Camera parallax, UV drift, geometry inflation, changing light, fog boundaries, quantization thresholds, and composite extrema can move coherently through the rendered image.",
    "A whole-pass snapshot hides which values are intentionally animated and couples unrelated edits. Independent typed tracks make scope, key ownership, deletion, and legal interpolation explicit. A local track intentionally shields that property from later global animation, so its scope must be visible. Resource allocation, topology, pass creation, and pass ordering stay outside ordinary parameter animation because changing them can rebuild GPU storage or restructure the document. Angle interpolation may need explicit wrap handling for large rotations.",
    "A dope sheet shows global and local property tracks vertically and time horizontally; diamonds mark keys. State the property type, scope, units, key times, legal interpolation, looping behavior, and whether evaluation changes source data or produces a temporary frame state. A timeline animates renderer parameters; skeletal animation separately deforms a mesh through a bone hierarchy.", Diagram::Animation},
  Article{"Engine architecture", "Undo, redo, and editor transactions",
    "Undo stores a recoverable authored document state before an operation. Redo stores the state displaced by undo. A transaction groups many intermediate updates from one continuous gesture, such as every frame of a slider drag, into one meaningful history step.",
    "Editor and document layer above rendering",
    "Experiments become reversible without treating every mouse-motion sample or animation playback tick as a separate command.",
    "Snapshots are simple and comprehensive but consume memory proportional to document size. Command histories can be smaller and more descriptive but every operation needs a correct inverse. Transient workspace state should be separated from authored data so selection changes, window layout, and playback evaluation do not pollute history.",
    "Ask what constitutes the editor document, which actions begin and end a transaction, whether restoration is exact after destructive normalization, the history bound, and which state is deliberately transient.", Diagram::Pipeline},
  Article{"Engine architecture", "Asset, scene, material, and renderer responsibilities",
    "Assets define authored data. The scene defines objects, transforms, cameras, and lights. Materials define surface inputs and shading. The renderer schedules passes and configures GPU state.",
    "Whole engine", "A coherent visual style emerges from constraints across all four responsibility areas.",
    "A shader cannot author low-poly silhouettes, schedule a shadow pass, or choose a camera composition by itself.",
    "Send agents separate asset constraints, scene constraints, renderer configuration, and architectural requirements.", Diagram::Pipeline},
  Article{"Engine architecture", "Model import and mesh preprocessing",
    "A model importer translates an interchange file into engine-owned geometry, submesh, material, and image data. Graphics Lab applies node transforms, triangulates faces, validates indices and finite values, supplies missing smooth normals and tangents, preserves UV0, vertex color 0, and material assignments, then expands indexed triangles into the barycentric vertex format required by wireframe analysis.",
    "Asset-loading boundary before GPU buffer upload",
    "Imported OBJ, glTF, and GLB geometry and base-color textures can pass through the same sampling, shading, depth, fog, quantization, wireframe, render-pass, compositing, and animation experiments as supplied test geometry.",
    "An image asset is decoded pixel data; a material assigns that image and numeric factors to a submesh; renderer state determines filtering, addressing, color interpretation, and shading. Graphics Lab preserves this separation. Base color and alpha are supported, while normal, emissive, metallic-roughness, occlusion, multiple UV sets, skinning, morph targets, lights, cameras, and source animation remain distinct future systems.",
    "Specify supported formats, primitive types, coordinate and unit conversion, node-transform handling, index validation, normal/tangent policy, UV channels, material texture semantics, color space, missing-file behavior, size limits, and whether source bounds are preserved or normalized. This lab normalizes the longest centered bounds extent to 3.0 units and limits imports to two million triangles.", Diagram::Pipeline},
  Article{"Engine architecture", "Forward rendering and render passes",
    "A forward renderer shades geometry while rasterizing it into the camera target. Additional passes may generate shadows, masks, analysis buffers, or post-processed output.",
    "Render architecture", "Pass ordering determines which intermediate data exists and when it can be consumed.",
    "Deferred, forward+, tiled, and clustered renderers organize lighting data differently.",
    "Ask which passes exist, their inputs and outputs, attachment formats, and ordering constraints.", Diagram::Pipeline},
  Article{"Engine architecture", "Forward, deferred, and forward+ rendering",
    "Forward rendering evaluates lighting while drawing surfaces. Deferred rendering first stores surface attributes in a G-buffer, then lights screen-space samples. Forward+ keeps forward shading but builds per-tile or per-cluster light lists.",
    "Whole renderer architecture", "The choice affects transparency, material flexibility, anti-aliasing, memory traffic, and the practical number of lights.",
    "These architectures can coexist by pass; transparent objects commonly use forward shading even in a deferred renderer.",
    "Ask which path handles opaque, transparent, shadow, and special materials, and why.", Diagram::RenderGraph},
  Article{"Engine architecture", "Render graphs and pass dependencies",
    "A render graph represents passes, resources, and read/write dependencies. It can determine ordering, transitions, lifetimes, and when temporary images may safely share memory.",
    "CPU-side frame scheduling and GPU synchronization", "The graph itself is architectural; its passes collectively determine the visible frame.",
    "A render graph does not define the shading model. Incorrect dependencies cause hazards, stale data, or unnecessary synchronization.",
    "Describe each pass by inputs, outputs, formats, clear/load/store behavior, and dependency edges.", Diagram::RenderGraph},
  Article{"Engine architecture", "Hardware capability profiles",
    "A hardware profile describes which renderer operations a target can represent, which choices are restricted, and which unavailable values must be normalized to a supported equivalent.",
    "Renderer configuration, material validation, and tool UI", "A constrained profile prevents an image from quietly depending on features the target does not provide.",
    "Hiding a control is insufficient unless serialized state and rendering are normalized too. An approximation should be labelled when the lab's implementation differs from original hardware.",
    "Export a stable target identifier plus complete normalized state; distinguish hardware capability, engine policy, and emulation approximation.", Diagram::RenderGraph},
  Article{"Performance", "CPU submission and draw calls",
    "Before the GPU can render, the CPU builds command buffers, binds pipelines and resources, and submits draws or dispatches. Too many small state changes and draw calls can make the frame CPU-bound.",
    "Application and rendering API submission", "CPU bottlenecks reduce frame rate even when shader or pixel complexity is low.",
    "Draw count alone is not a universal budget; driver model, command recording, state sorting, and platform matter.",
    "Profile CPU frame time, render-thread time, draw/dispatch count, and pipeline changes.", Diagram::Performance},
  Article{"Performance", "GPU bottlenecks: geometry, fill rate, and bandwidth",
    "GPU time may be limited by vertex/primitive work, fragment arithmetic, raster fill rate, texture access, or memory bandwidth. Resolution mostly magnifies pixel and bandwidth costs, not CPU submission cost.",
    "GPU execution and memory system", "Different bottlenecks can produce the same low frame rate while requiring opposite fixes.",
    "Use timing queries and controlled experiments: change resolution, mesh density, shader complexity, or texture traffic one variable at a time.",
    "Ask whether a pass is vertex-bound, fragment-bound, bandwidth-bound, latency-bound, or occupancy-limited—and require measurement.", Diagram::Performance},
  Article{"Performance", "Batching, instancing, and culling",
    "Batching combines compatible work, instancing draws repeated geometry with per-instance data, and culling rejects work known not to contribute to the view.",
    "Scene traversal, command construction, and sometimes GPU compute", "These primarily improve performance; aggressive batching can constrain material or visibility choices.",
    "Frustum, occlusion, backface, and small-object culling reject different kinds of work. Instancing does not remove pixel overdraw.",
    "Specify batch compatibility, instance data, culling granularity, and whether decisions happen on CPU or GPU.", Diagram::Performance},
  Article{"Animation", "Skeletal animation and skinning",
    "A skeleton is a hierarchy of animated transforms. Skinning blends each vertex among one or more bone transforms using authored weights, usually before world and camera transforms.",
    "Animation evaluation followed by vertex deformation", "A shared mesh bends with an articulated pose while retaining its topology.",
    "Bind pose, inverse-bind matrices, weight normalization, blend count, and matrix versus dual-quaternion skinning affect results.",
    "Separate clip sampling, pose blending, inverse kinematics, skeleton transforms, and mesh skinning.", Diagram::Animation},
  Article{"Animation", "Morph targets and procedural deformation",
    "Morph targets store alternate vertex attributes and blend their deltas by weights. Procedural deformation computes positions or attributes from rules, simulation, or shader inputs instead of a bone hierarchy.",
    "CPU animation systems or vertex/compute processing", "They support facial shapes, corrective forms, waves, cloth, and other non-rigid changes.",
    "Morph targets require matching topology; deformed normals and tangents must be updated or transformed consistently.",
    "State whether deformation is skeletal, morph-based, simulated, or procedural, and where it is evaluated.", Diagram::Animation},
  Article{"Ray tracing", "Rasterization versus ray tracing",
    "Rasterization projects primitives onto pixels. Ray tracing starts from rays and finds scene intersections, making visibility queries such as reflections and shadows more direct but computationally different.",
    "Alternative or hybrid visibility architecture", "Ray tracing can produce accurate indirect visibility, reflections, refractions, and soft shadows, usually with sampling noise and denoising concerns.",
    "Modern renderers frequently mix rasterized primary visibility with selected ray-traced effects.",
    "Specify which effects use rays, ray count, bounce count, sampling strategy, denoiser, and fallback path.", Diagram::RayTracing},
  Article{"Ray tracing", "Acceleration structures and path tracing",
    "A bounding-volume hierarchy accelerates ray-scene intersection by rejecting large regions. Path tracing estimates the rendering equation by following randomized light transport paths across multiple bounces.",
    "Scene acceleration, intersection, shading, accumulation, and denoising", "More samples converge toward a stable image; too few produce variance seen as noise.",
    "Static and dynamic geometry have different build/update costs, while material branching and incoherent rays affect traversal efficiency.",
    "Discuss BLAS/TLAS structure, build/update policy, samples per pixel, maximum depth, importance sampling, accumulation, and denoising.", Diagram::RayTracing},
};

struct QuickRead {
  const char* title;
  const char* text;
};

constexpr std::array quickReads = {
  QuickRead{"Start here: from mesh to pixel", "A model is numbers. The GPU positions its vertices, fills the triangles between them, proposes colors for the covered spots, and keeps the results that pass the visibility rules."},
  QuickRead{"The realtime rasterization pipeline", "Pipeline means ordered handoffs: each stage does one kind of work and passes its result to the next stage."},
  QuickRead{"Shaders and fixed-function state", "Shaders are programs you write. Fixed-function state is the set of hardware rules—such as depth, blending, and culling—that runs around those programs."},
  QuickRead{"Nintendo 64: RSP, RDP, and VI", "The RSP prepares vertices, the RDP constructs pixels, and the VI turns the completed framebuffer into the displayed signal."},
  QuickRead{"What a shader program is", "A shader is code for one GPU stage, not a name for everything that makes the rendered image look a certain way."},
  QuickRead{"Vertex shaders", "For every submitted vertex, this program answers: where should this point end up, and what data should travel onward with it?"},
  QuickRead{"Fragment shaders", "For every covered sample of a triangle, this program answers: what values should this surface propose writing here?"},
  QuickRead{"Attributes, uniforms, varyings, and resources", "These names mostly answer how often data changes: per vertex, per draw, across a triangle, or through a separately bound resource."},
  QuickRead{"Compilation, linking, and shader variants", "The engine turns shader source into executable GPU stages, connects compatible stages, then chooses the version whose features match the draw."},
  QuickRead{"Compute and optional graphics stages", "Vertex and fragment stages are the common path; these stages exist for workloads that need a different unit of work."},
  QuickRead{"Vertex attributes", "A vertex is a bundle of facts about one mesh point, not merely a position."},
  QuickRead{"Vertex position quantization", "Round vertex positions to a grid and the geometry can only move in fixed spatial steps."},
  QuickRead{"Perspective and orthographic projection", "Perspective makes screen size depend on distance; orthographic projection does not."},
  QuickRead{"Near plane and depth distribution", "Moving the near plane extremely close spends most available depth precision near the camera."},
  QuickRead{"Triangle winding and face culling", "The order of a triangle's three projected points tells the renderer which side it is seeing; culling can skip the other side."},
  QuickRead{"Perspective-correct interpolation", "The rasterizer must account for depth while spreading UVs across a triangle, or the texture bends as perspective changes."},
  QuickRead{"Multisample anti-aliasing", "Store several coverage tests inside one pixel so triangle edges can be partially covered instead of only on or off."},
  QuickRead{"N64 coverage antialiasing", "The RDP records how much of a pixel a polygon covers, then its blender and video output use that coverage to soften edges."},
  QuickRead{"Normals and shading interpolation", "Normals tell lighting which way a surface points; interpolating them can make a coarse mesh light as though it were smooth."},
  QuickRead{"Tangent-space normal mapping", "A normal map changes the direction used for lighting at each texel without moving the actual surface."},
  QuickRead{"Transparency and compositing", "A transparent fragment combines with color already stored behind it, so order and depth-write choices matter."},
  QuickRead{"PlayStation semitransparency equations", "Instead of arbitrary alpha, the PS1 offered four specific arithmetic choices for combining an incoming color with the color already stored."},
  QuickRead{"N64 RDP color combiner and cycle types", "An N64 material selects named color sources and evaluates the small fixed equation (A - B) x C + D once or twice."},
  QuickRead{"Material versus shader", "The shader is reusable logic; the material is one configured surface that supplies values, textures, and render state to that logic."},
  QuickRead{"BRDFs and physically based materials", "A BRDF is the rule that decides how much incoming light leaves a surface toward the camera."},
  QuickRead{"Material inputs and texture semantics", "The engine must know what every texture channel means before it can decode and use those numbers correctly."},
  QuickRead{"Filtering and reconstruction", "When a sample lands between texels, filtering decides whether to choose one texel or combine nearby texels."},
  QuickRead{"Mipmaps, trilinear, and anisotropic filtering", "When many texels shrink into a pixel, prefiltered smaller copies help the sampler represent their average instead of flickering."},
  QuickRead{"Indexed textures and CLUT palettes", "The texture stores small palette numbers; a separate table says which actual color each number means."},
  QuickRead{"N64 TMEM, tiles, and texture formats", "Textures can live in main memory, but the RDP can only sample the tiles currently loaded into its 4096-byte working memory."},
  QuickRead{"N64 three-point filtering and mip modes", "The N64 normally blends three nearby texels, and can spend a second cycle blending texture levels or adding detail."},
  QuickRead{"N64 trilinear mipmapping", "Sample two neighboring mip levels and use the fractional LOD value to blend smoothly between them."},
  QuickRead{"Gouraud, Phong shading, and reflection models", "First ask where lighting is calculated—vertices or fragments—then ask which lighting formula is used there."},
  QuickRead{"Shadow mapping", "Render depth from the light first; later, anything farther than that stored depth is hidden from the light."},
  QuickRead{"Vertex depth cueing", "Calculate how far each vertex is, then interpolate how strongly the polygon should approach a chosen far color."},
  QuickRead{"Depth testing and depth writes", "For each covered sample, compare distance with the stored winner, then optionally replace that stored distance."},
  QuickRead{"Stencil testing", "Stencil is a small per-pixel integer you write in one pass and use as an exact mask in another."},
  QuickRead{"Overdraw", "Overdraw counts work spent on the same pixel more than once, including work whose result is later hidden."},
  QuickRead{"Ordering tables and painter submission", "When no depth buffer chooses the nearest fragment, draw farther things first and let nearer things cover them later."},
  QuickRead{"N64 surface and Z modes", "Opaque, translucent, decal, and intersecting surfaces need different combinations of depth, coverage, and framebuffer blending rules."},
  QuickRead{"Linear light and encoded RGB", "Image files often store brightness nonlinearly for display, but lighting math expects values proportional to actual light."},
  QuickRead{"Color quantization and dithering", "Fewer allowed colors create bands; dithering rearranges the rounding errors into a controlled spatial pattern."},
  QuickRead{"Internal resolution and upscaling", "Render the whole scene into a smaller image first, then enlarge that finished image for the window."},
  QuickRead{"N64 Video Interface filtering", "After rendering is finished, the VI reconstructs and scales the framebuffer signal; this is separate from filtering a material texture."},
  QuickRead{"Render algebra between completed images", "Start with one completed pass, then make each later pass change the accumulated image according to an explicit per-pixel equation."},
  QuickRead{"Render-pass stacks and controlled perturbation", "Set shared choices once, let each pass remember only how it differs, then composite those correlated interpretations."},
  QuickRead{"Keyframes and parameter animation", "Animate a shared property globally or give one pass a local track that takes control of that property."},
  QuickRead{"Undo, redo, and editor transactions", "Save the document before an edit, and treat an entire continuous drag as one reversible operation rather than hundreds of tiny changes."},
  QuickRead{"Asset, scene, material, and renderer responsibilities", "Assets provide data, the scene arranges it, materials describe surfaces, and the renderer schedules the work that produces a frame."},
  QuickRead{"Model import and mesh preprocessing", "Translate source geometry, material assignments, and images into separate engine-owned assets before uploading any of them to the GPU."},
  QuickRead{"Forward rendering and render passes", "A pass is one scheduled piece of rendering with declared inputs and outputs; a frame is usually made from several passes."},
  QuickRead{"Forward, deferred, and forward+ rendering", "These architectures mainly differ in when surface lighting happens and how visible surfaces find the lights that affect them."},
  QuickRead{"Render graphs and pass dependencies", "A render graph makes the frame's passes and intermediate images explicit so the engine can order them safely."},
  QuickRead{"Hardware capability profiles", "A target profile turns impossible settings into supported ones, then removes controls that can no longer change anything."},
  QuickRead{"CPU submission and draw calls", "The CPU must describe and submit GPU work; too many tiny submissions can become the bottleneck before the GPU is full."},
  QuickRead{"GPU bottlenecks: geometry, fill rate, and bandwidth", "A slow frame only says time was spent; profiling tells whether vertices, pixels, shader math, or moving data consumed it."},
  QuickRead{"Batching, instancing, and culling", "Send compatible work together, repeat shared geometry cheaply, and reject invisible work before paying to render it."},
  QuickRead{"Skeletal animation and skinning", "Animate a hierarchy of bones, then blend each vertex among the bone transforms it is weighted to follow."},
  QuickRead{"Morph targets and procedural deformation", "Instead of—or before—moving a mesh with bones, blend stored vertex shapes or calculate new vertex positions from a rule."},
  QuickRead{"Rasterization versus ray tracing", "Rasterization asks which pixels a triangle covers; ray tracing asks what surface a ray encounters."},
  QuickRead{"Acceleration structures and path tracing", "A BVH makes intersection searches practical; path tracing repeats those searches to estimate how light travels through the scene."},
};

const char* quickReadFor(std::string_view title) {
  const auto match = std::find_if(quickReads.begin(), quickReads.end(), [title](const QuickRead& quickRead) {
    return title == quickRead.title;
  });
  return match == quickReads.end() ? nullptr : match->text;
}

bool exampleAvailableForProfile(gfxlab::HardwareProfile profile, Example example) {
  if (profile == gfxlab::HardwareProfile::Unrestricted) return true;
  if (profile == gfxlab::HardwareProfile::Nintendo64) {
    switch (example) {
      case Example::None:
      case Example::VertexQuantization:
      case Example::Projection:
      case Example::AffineMapping:
      case Example::InternalResolution:
      case Example::VertexDepthCue:
      case Example::N64ThreePoint:
      case Example::N64Combiner:
      case Example::N64TextureFormats:
      case Example::N64Mipmap:
      case Example::N64Coverage:
      case Example::N64VideoInterface:
        return true;
      case Example::TextureMinification:
      case Example::NormalMapping:
      case Example::LightingInterpolation:
      case Example::DepthPrecision:
      case Example::Transparency:
      case Example::Stencil:
      case Example::LinearLight:
      case Example::ColorQuantization:
      case Example::ShadowMapping:
      case Example::Overdraw:
      case Example::ClutTextures:
      case Example::Ps1Semitransparency:
      case Example::OrderingTable:
        return false;
    }
  }
  switch (example) {
    case Example::None:
    case Example::VertexQuantization:
    case Example::InternalResolution:
    case Example::ClutTextures:
    case Example::VertexDepthCue:
    case Example::Ps1Semitransparency:
    case Example::OrderingTable:
      return true;
    case Example::Projection:
    case Example::AffineMapping:
    case Example::TextureMinification:
    case Example::NormalMapping:
    case Example::LightingInterpolation:
    case Example::DepthPrecision:
    case Example::Transparency:
    case Example::Stencil:
    case Example::LinearLight:
    case Example::ColorQuantization:
    case Example::ShadowMapping:
    case Example::Overdraw:
    case Example::N64ThreePoint:
    case Example::N64Combiner:
    case Example::N64TextureFormats:
    case Example::N64Mipmap:
    case Example::N64Coverage:
    case Example::N64VideoInterface:
      return false;
  }
  return false;
}

bool containsInsensitive(std::string_view text, std::string_view query) {
  if (query.empty()) return true;
  std::string lowerText(text), lowerQuery(query);
  std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowerText.find(lowerQuery) != std::string::npos;
}

void wrappedText(const char* text, ImVec4 color = ImVec4(0.86f, 0.87f, 0.88f, 1.0f)) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

void arrow(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color) {
  draw->AddLine(a, b, color, 2.0f);
  const ImVec2 d = ImVec2(b.x - a.x, b.y - a.y);
  const float length = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
  const ImVec2 n(d.x / length, d.y / length);
  const ImVec2 p(-n.y, n.x);
  draw->AddTriangleFilled(b, ImVec2(b.x - n.x * 9 + p.x * 4, b.y - n.y * 9 + p.y * 4),
    ImVec2(b.x - n.x * 9 - p.x * 4, b.y - n.y * 9 - p.y * 4), color);
}

void drawDiagram(Diagram diagram) {
  const ImVec2 size(std::max(420.0f, ImGui::GetContentRegionAvail().x), 180.0f);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("technical-diagram", size);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImU32 border = IM_COL32(70, 74, 77, 255);
  const ImU32 line = IM_COL32(125, 171, 175, 255);
  const ImU32 fill = IM_COL32(37, 42, 44, 255);
  const ImU32 text = IM_COL32(220, 223, 224, 255);
  draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(25, 28, 30, 255));
  draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), border);
  auto box = [&](float x, float y, float w, float h, const char* label) {
    const ImVec2 a(origin.x + x, origin.y + y), b(origin.x + x + w, origin.y + y + h);
    draw->AddRectFilled(a, b, fill); draw->AddRect(a, b, border);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(a.x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), text, label);
  };
  if (diagram == Diagram::Pipeline) {
    const char* labels[] = {"Vertices", "Projection", "Rasterize", "Shade", "Test / Blend", "Output"};
    const float w = (size.x - 100.0f) / 6.0f;
    for (int i = 0; i < 6; ++i) {
      box(18.0f + i * (w + 13.0f), 65.0f, w, 48.0f, labels[i]);
      if (i < 5) arrow(draw, ImVec2(origin.x + 18 + i * (w + 13) + w, origin.y + 89),
        ImVec2(origin.x + 18 + (i + 1) * (w + 13) - 3, origin.y + 89), line);
    }
  } else if (diagram == Diagram::Mipmaps) {
    for (int i = 0; i < 5; ++i) {
      const float s = 112.0f / static_cast<float>(1 << i);
      const float x = 25.0f + i * 105.0f;
      draw->AddRectFilled(ImVec2(origin.x + x, origin.y + 34 + (112 - s) * 0.5f),
        ImVec2(origin.x + x + s, origin.y + 34 + (112 + s) * 0.5f), i % 2 ? fill : IM_COL32(100, 125, 116, 255));
      draw->AddText(ImVec2(origin.x + x, origin.y + 151), text, i == 0 ? "LOD 0" : (i == 1 ? "LOD 1" : i == 2 ? "LOD 2" : i == 3 ? "LOD 3" : "LOD 4"));
      if (i < 4) arrow(draw, ImVec2(origin.x + x + s + 5, origin.y + 90), ImVec2(origin.x + x + 94, origin.y + 90), line);
    }
  } else if (diagram == Diagram::Projection) {
    draw->AddTriangle(ImVec2(origin.x + 40, origin.y + 90), ImVec2(origin.x + 250, origin.y + 28), ImVec2(origin.x + 250, origin.y + 152), line, 2.0f);
    draw->AddText(ImVec2(origin.x + 32, origin.y + 95), text, "eye");
    draw->AddText(ImVec2(origin.x + 118, origin.y + 44), text, "perspective");
    draw->AddLine(ImVec2(origin.x + 335, origin.y + 40), ImVec2(origin.x + 335, origin.y + 145), line, 2.0f);
    draw->AddLine(ImVec2(origin.x + 390, origin.y + 40), ImVec2(origin.x + 390, origin.y + 145), line, 2.0f);
    draw->AddLine(ImVec2(origin.x + 445, origin.y + 40), ImVec2(origin.x + 445, origin.y + 145), line, 2.0f);
    draw->AddText(ImVec2(origin.x + 345, origin.y + 151), text, "orthographic: parallel rays");
  } else if (diagram == Diagram::Tbn) {
    const ImVec2 center(origin.x + size.x * 0.48f, origin.y + 105.0f);
    draw->AddCircleFilled(center, 6, text);
    arrow(draw, center, ImVec2(center.x + 105, center.y), IM_COL32(220, 90, 80, 255));
    arrow(draw, center, ImVec2(center.x - 65, center.y + 55), IM_COL32(80, 190, 110, 255));
    arrow(draw, center, ImVec2(center.x, center.y - 75), IM_COL32(90, 140, 230, 255));
    draw->AddText(ImVec2(center.x + 108, center.y - 8), text, "T tangent");
    draw->AddText(ImVec2(center.x - 150, center.y + 55), text, "B bitangent");
    draw->AddText(ImVec2(center.x + 8, center.y - 90), text, "N normal");
  } else if (diagram == Diagram::Depth) {
    const float left = origin.x + 35, right = origin.x + size.x - 35;
    draw->AddLine(ImVec2(left, origin.y + 95), ImVec2(right, origin.y + 95), border, 2.0f);
    for (int i = 0; i < 14; ++i) {
      const float t = static_cast<float>(i) / 13.0f;
      const float x = left + (right - left) * t * t;
      draw->AddLine(ImVec2(x, origin.y + 79), ImVec2(x, origin.y + 111), line, 1.0f);
    }
    draw->AddText(ImVec2(left, origin.y + 120), text, "near: dense precision");
    draw->AddText(ImVec2(right - 125, origin.y + 120), text, "far: sparse");
  } else if (diagram == Diagram::Blend) {
    box(35, 48, 125, 52, "source RGBA"); box(35, 116, 125, 36, "source factor");
    box(size.x - 160, 48, 125, 52, "destination"); box(size.x - 160, 116, 125, 36, "destination factor");
    box(size.x * 0.5f - 62, 66, 124, 52, "blend equation");
    arrow(draw, ImVec2(origin.x + 165, origin.y + 75), ImVec2(origin.x + size.x * 0.5f - 68, origin.y + 86), line);
    arrow(draw, ImVec2(origin.x + size.x - 165, origin.y + 75), ImVec2(origin.x + size.x * 0.5f + 68, origin.y + 86), line);
  } else if (diagram == Diagram::Shadow) {
    box(28, 50, 120, 50, "light camera"); box(210, 50, 125, 50, "depth texture"); box(397, 50, 150, 50, "camera shading");
    arrow(draw, ImVec2(origin.x + 153, origin.y + 75), ImVec2(origin.x + 204, origin.y + 75), line);
    arrow(draw, ImVec2(origin.x + 340, origin.y + 75), ImVec2(origin.x + 391, origin.y + 75), line);
    draw->AddText(ImVec2(origin.x + 205, origin.y + 118), text, "compare light-space depths");
  } else if (diagram == Diagram::Color) {
    box(25, 65, 130, 48, "encoded texture"); box(210, 65, 110, 48, "decode"); box(375, 65, 120, 48, "linear light"); box(550, 65, 100, 48, "encode");
    arrow(draw, ImVec2(origin.x + 160, origin.y + 89), ImVec2(origin.x + 204, origin.y + 89), line);
    arrow(draw, ImVec2(origin.x + 325, origin.y + 89), ImVec2(origin.x + 369, origin.y + 89), line);
    arrow(draw, ImVec2(origin.x + 500, origin.y + 89), ImVec2(origin.x + 544, origin.y + 89), line);
  } else if (diagram == Diagram::Output) {
    box(30, 45, 150, 90, "320 x 240 target"); box(size.x - 230, 28, 200, 125, "4:3 presentation");
    arrow(draw, ImVec2(origin.x + 188, origin.y + 90), ImVec2(origin.x + size.x - 238, origin.y + 90), line);
    draw->AddText(ImVec2(origin.x + 220, origin.y + 105), text, "nearest or bilinear upscale");
  } else if (diagram == Diagram::Overdraw) {
    for (int i = 0; i < 5; ++i) {
      const ImU32 heat = IM_COL32(40 + i * 45, 150 - i * 18, 220 - i * 42, 150);
      draw->AddCircleFilled(ImVec2(origin.x + size.x * 0.38f + i * 30, origin.y + 90), 55, heat);
    }
    draw->AddText(ImVec2(origin.x + 25, origin.y + 145), text, "add 1 for every rasterized fragment; depth test disabled");
  } else if (diagram == Diagram::Stencil) {
    box(30, 42, 155, 55, "pass 1: Replace 1"); box(245, 42, 155, 55, "8-bit stencil mask"); box(460, 42, 160, 55, "pass 2: Equal 1");
    arrow(draw, ImVec2(origin.x + 190, origin.y + 70), ImVec2(origin.x + 239, origin.y + 70), line);
    arrow(draw, ImVec2(origin.x + 405, origin.y + 70), ImVec2(origin.x + 454, origin.y + 70), line);
    draw->AddText(ImVec2(origin.x + 210, origin.y + 125), text, "stored integer controls later coverage");
  } else if (diagram == Diagram::Lighting) {
    const ImVec2 center(origin.x + size.x * 0.5f, origin.y + 100);
    draw->AddCircle(center, 58, border, 0, 2.0f);
    arrow(draw, ImVec2(center.x - 150, center.y - 70), ImVec2(center.x - 50, center.y - 20), line);
    arrow(draw, center, ImVec2(center.x, center.y - 90), IM_COL32(90, 140, 230, 255));
    draw->AddText(ImVec2(center.x - 180, center.y - 95), text, "light direction L");
    draw->AddText(ImVec2(center.x + 10, center.y - 100), text, "surface normal N");
    draw->AddText(ImVec2(center.x - 72, center.y + 68), text, "diffuse = max(dot(N,L), 0)");
  } else if (diagram == Diagram::ShaderProgram) {
    box(22, 58, 105, 54, "vertices"); box(165, 58, 125, 54, "vertex shader");
    box(328, 58, 105, 54, "rasterizer"); box(471, 58, 135, 54, "fragment shader");
    for (float x : {132.0f, 295.0f, 438.0f})
      arrow(draw, ImVec2(origin.x + x, origin.y + 85), ImVec2(origin.x + x + 28, origin.y + 85), line);
    draw->AddText(ImVec2(origin.x + 165, origin.y + 125), text, "programmable");
    draw->AddText(ImVec2(origin.x + 328, origin.y + 125), text, "fixed operation");
  } else if (diagram == Diagram::ShaderData) {
    box(22, 28, 120, 42, "attributes"); box(22, 96, 120, 42, "uniforms");
    box(size.x * 0.5f - 72, 56, 144, 58, "shader stage");
    box(size.x - 150, 28, 125, 42, "varyings"); box(size.x - 150, 96, 125, 42, "resources");
    arrow(draw, ImVec2(origin.x + 147, origin.y + 49), ImVec2(origin.x + size.x * 0.5f - 78, origin.y + 73), line);
    arrow(draw, ImVec2(origin.x + 147, origin.y + 117), ImVec2(origin.x + size.x * 0.5f - 78, origin.y + 97), line);
    arrow(draw, ImVec2(origin.x + size.x * 0.5f + 77, origin.y + 73), ImVec2(origin.x + size.x - 155, origin.y + 49), line);
    arrow(draw, ImVec2(origin.x + size.x - 155, origin.y + 117), ImVec2(origin.x + size.x * 0.5f + 77, origin.y + 97), line);
  } else if (diagram == Diagram::RenderGraph) {
    box(24, 58, 115, 50, "shadow pass"); box(190, 28, 125, 50, "depth / G-buffer");
    box(190, 112, 125, 40, "light lists"); box(370, 58, 120, 50, "lighting pass");
    box(540, 58, 105, 50, "post / output");
    arrow(draw, ImVec2(origin.x + 144, origin.y + 83), ImVec2(origin.x + 365, origin.y + 83), line);
    arrow(draw, ImVec2(origin.x + 320, origin.y + 53), ImVec2(origin.x + 365, origin.y + 74), line);
    arrow(draw, ImVec2(origin.x + 320, origin.y + 132), ImVec2(origin.x + 365, origin.y + 98), line);
    arrow(draw, ImVec2(origin.x + 495, origin.y + 83), ImVec2(origin.x + 535, origin.y + 83), line);
  } else if (diagram == Diagram::Comparison) {
    box(22, 30, 112, 44, "renderer A"); box(22, 112, 112, 44, "renderer B");
    box(180, 30, 100, 44, "image A"); box(180, 112, 100, 44, "image B");
    box(340, 65, 150, 56, "per-pixel F(A, B)"); box(550, 65, 105, 56, "output");
    arrow(draw, ImVec2(origin.x + 139, origin.y + 52), ImVec2(origin.x + 175, origin.y + 52), line);
    arrow(draw, ImVec2(origin.x + 139, origin.y + 134), ImVec2(origin.x + 175, origin.y + 134), line);
    arrow(draw, ImVec2(origin.x + 285, origin.y + 52), ImVec2(origin.x + 335, origin.y + 80), line);
    arrow(draw, ImVec2(origin.x + 285, origin.y + 134), ImVec2(origin.x + 335, origin.y + 106), line);
    arrow(draw, ImVec2(origin.x + 495, origin.y + 93), ImVec2(origin.x + 545, origin.y + 93), line);
  } else if (diagram == Diagram::Performance) {
    box(24, 30, 125, 44, "CPU submission"); box(24, 106, 125, 44, "scene culling");
    box(205, 65, 120, 50, "GPU commands"); box(380, 25, 120, 44, "geometry work");
    box(380, 111, 120, 44, "fragment work"); box(555, 65, 105, 50, "memory");
    arrow(draw, ImVec2(origin.x + 154, origin.y + 52), ImVec2(origin.x + 200, origin.y + 82), line);
    arrow(draw, ImVec2(origin.x + 154, origin.y + 128), ImVec2(origin.x + 200, origin.y + 98), line);
    arrow(draw, ImVec2(origin.x + 330, origin.y + 90), ImVec2(origin.x + 375, origin.y + 47), line);
    arrow(draw, ImVec2(origin.x + 330, origin.y + 90), ImVec2(origin.x + 375, origin.y + 133), line);
  } else if (diagram == Diagram::Animation) {
    box(22, 65, 100, 48, "clip time"); box(165, 65, 105, 48, "local pose");
    box(313, 65, 115, 48, "world bones"); box(471, 65, 105, 48, "skinning");
    for (float x : {127.0f, 275.0f, 433.0f})
      arrow(draw, ImVec2(origin.x + x, origin.y + 89), ImVec2(origin.x + x + 33, origin.y + 89), line);
    draw->AddText(ImVec2(origin.x + 315, origin.y + 126), text, "hierarchy");
  } else if (diagram == Diagram::RayTracing) {
    box(22, 62, 95, 48, "camera ray"); box(162, 62, 105, 48, "BVH query");
    box(312, 62, 105, 48, "intersection"); box(462, 62, 105, 48, "shade / bounce");
    arrow(draw, ImVec2(origin.x + 122, origin.y + 86), ImVec2(origin.x + 157, origin.y + 86), line);
    arrow(draw, ImVec2(origin.x + 272, origin.y + 86), ImVec2(origin.x + 307, origin.y + 86), line);
    arrow(draw, ImVec2(origin.x + 422, origin.y + 86), ImVec2(origin.x + 457, origin.y + 86), line);
    draw->AddText(ImVec2(origin.x + 300, origin.y + 128), text, "repeat and accumulate samples");
  } else {
    const ImVec2 a(origin.x + size.x * 0.25f, origin.y + 140), b(origin.x + size.x * 0.50f, origin.y + 35), c(origin.x + size.x * 0.75f, origin.y + 140);
    draw->AddTriangleFilled(a, b, c, fill); draw->AddTriangle(a, b, c, line, 2.0f);
    draw->AddCircleFilled(a, 5, text); draw->AddCircleFilled(b, 5, text); draw->AddCircleFilled(c, 5, text);
    draw->AddText(ImVec2(a.x - 20, a.y + 8), text, "vertex 0"); draw->AddText(ImVec2(b.x - 20, b.y - 20), text, "vertex 1"); draw->AddText(ImVec2(c.x - 20, c.y + 8), text, "vertex 2");
  }
}

std::array<const char*, 4> branchesFor(std::string_view title) {
  if (title == "Start here: from mesh to pixel") return {"The realtime rasterization pipeline", "Vertex attributes", "Perspective and orthographic projection", "What a shader program is"};
  if (title == "The realtime rasterization pipeline") return {"What a shader program is", "Vertex attributes", "Render graphs and pass dependencies", nullptr};
  if (title == "Nintendo 64: RSP, RDP, and VI") return {"N64 RDP color combiner and cycle types", "N64 TMEM, tiles, and texture formats", "N64 coverage antialiasing", "N64 Video Interface filtering"};
  if (title == "Shaders and fixed-function state") return {"What a shader program is", "Material versus shader", "Depth testing and depth writes", nullptr};
  if (title == "What a shader program is") return {"Vertex shaders", "Fragment shaders", "Attributes, uniforms, varyings, and resources", "Compilation, linking, and shader variants"};
  if (title == "Vertex shaders") return {"Vertex attributes", "Skeletal animation and skinning", "Vertex position quantization", nullptr};
  if (title == "Fragment shaders") return {"Material versus shader", "Gouraud, Phong shading, and reflection models", "Transparency and compositing", nullptr};
  if (title == "Transparency and compositing") return {"PlayStation semitransparency equations", "Ordering tables and painter submission", "Depth testing and depth writes", nullptr};
  if (title == "Filtering and reconstruction") return {"Indexed textures and CLUT palettes", "Mipmaps, trilinear, and anisotropic filtering", nullptr, nullptr};
  if (title == "N64 TMEM, tiles, and texture formats") return {"N64 three-point filtering and mip modes", "N64 trilinear mipmapping", "N64 RDP color combiner and cycle types", nullptr};
  if (title == "N64 RDP color combiner and cycle types") return {"N64 surface and Z modes", "N64 trilinear mipmapping", "N64 Video Interface filtering", "Render algebra between completed images"};
  if (title == "Gouraud, Phong shading, and reflection models") return {"Vertex depth cueing", "BRDFs and physically based materials", nullptr, nullptr};
  if (title == "Attributes, uniforms, varyings, and resources") return {"Material inputs and texture semantics", "Perspective-correct interpolation", "Compute and optional graphics stages", nullptr};
  if (title == "Material versus shader") return {"BRDFs and physically based materials", "Material inputs and texture semantics", "Forward, deferred, and forward+ rendering", nullptr};
  if (title == "Forward rendering and render passes") return {"Forward, deferred, and forward+ rendering", "Render graphs and pass dependencies", "Shadow mapping", nullptr};
  if (title == "Forward, deferred, and forward+ rendering") return {"Render graphs and pass dependencies", "GPU bottlenecks: geometry, fill rate, and bandwidth", "Rasterization versus ray tracing", nullptr};
  if (title == "Asset, scene, material, and renderer responsibilities") return {"Model import and mesh preprocessing", "Material versus shader", "Render graphs and pass dependencies", "CPU submission and draw calls"};
  if (title == "Model import and mesh preprocessing") return {"Asset, scene, material, and renderer responsibilities", "Vertex attributes", "Material inputs and texture semantics", "Skeletal animation and skinning"};
  if (title == "Render graphs and pass dependencies") return {"Hardware capability profiles", "Forward, deferred, and forward+ rendering", nullptr, nullptr};
  if (title == "Render algebra between completed images") return {"Render-pass stacks and controlled perturbation", "Transparency and compositing", "Linear light and encoded RGB", "Color quantization and dithering"};
  if (title == "Render-pass stacks and controlled perturbation") return {"Render algebra between completed images", "Keyframes and parameter animation", "Forward rendering and render passes", "Render graphs and pass dependencies"};
  if (title == "Keyframes and parameter animation") return {"Render-pass stacks and controlled perturbation", "Skeletal animation and skinning", "Morph targets and procedural deformation", "Render graphs and pass dependencies"};
  if (title == "Undo, redo, and editor transactions") return {"Render-pass stacks and controlled perturbation", "Keyframes and parameter animation", "Asset, scene, material, and renderer responsibilities", "Render graphs and pass dependencies"};
  if (title == "Skeletal animation and skinning") return {"Vertex shaders", "Morph targets and procedural deformation", nullptr, nullptr};
  if (title == "Rasterization versus ray tracing") return {"Acceleration structures and path tracing", "The realtime rasterization pipeline", nullptr, nullptr};
  return {nullptr, nullptr, nullptr, nullptr};
}

} // namespace

void Handbook::open() {
  open_ = true;
  focusRequested_ = true;
}
bool Handbook::isOpen() const { return open_; }

Action Handbook::draw(gfxlab::HardwareProfile profile) {
  Action action;
  if (!open_) return action;

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.05f, io.DisplaySize.y * 0.05f), ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.90f, io.DisplaySize.y * 0.90f), ImGuiCond_Appearing);
  ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(900.0f, io.DisplaySize.x * 0.8f),
    std::min(600.0f, io.DisplaySize.y * 0.8f)), io.DisplaySize);
  if (focusRequested_) {
    ImGui::SetNextWindowFocus();
    focusRequested_ = false;
  }
  if (!ImGui::Begin("Graphics Handbook", &open_, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return action;
  }
  gfxlab::ui::keepCurrentWindowVisible();

  ImGui::TextUnformatted("GRAPHICS HANDBOOK");
  ImGui::SameLine();
  ImGui::TextDisabled("Mechanisms, pipeline locations, interactions, and engine vocabulary");
  if (ImGui::GetContentRegionAvail().x >= 280.0f) ImGui::SameLine(ImGui::GetWindowWidth() - 285.0f);
  ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x));
  ImGui::InputTextWithHint("##handbook-search", "Search terminology...", search_.data(), search_.size());
  ImGui::Separator();

  const float height = ImGui::GetContentRegionAvail().y;
  ImGui::BeginChild("handbook-chapters", ImVec2(190, height), true);
  ImGui::TextDisabled("KNOWLEDGE MAP");
  for (int i = 0; i < static_cast<int>(chapters.size()); ++i) {
    if (i == 0 || std::string_view(chapters[i].domain) != chapters[i - 1].domain) {
      ImGui::Spacing();
      ImGui::TextDisabled("%s", chapters[i].domain);
    }
    if (ImGui::Selectable(chapters[i].name, chapter_ == i, 0, ImVec2(0, 25))) {
      chapter_ = i;
      article_ = 0;
      search_[0] = '\0';
    }
  }
  ImGui::EndChild();
  ImGui::SameLine(0, 5);

  std::vector<const Article*> visible;
  const std::string_view query(search_.data());
  for (const Article& article : articles) {
    const bool chapterMatch = query.empty() && article.chapter == std::string_view(chapters[chapter_].name);
    const bool searchMatch = !query.empty() && (containsInsensitive(article.title, query) || containsInsensitive(article.definition, query) ||
      containsInsensitive(article.engineVocabulary, query) || containsInsensitive(article.chapter, query));
    if (chapterMatch || searchMatch) visible.push_back(&article);
  }
  if (visible.empty()) article_ = 0; else article_ = std::clamp(article_, 0, static_cast<int>(visible.size()) - 1);

  ImGui::BeginChild("handbook-articles", ImVec2(250, height), true);
  ImGui::TextDisabled(query.empty() ? "ARTICLES" : "SEARCH RESULTS");
  ImGui::Spacing();
  for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
    ImGui::PushID(i);
    if (ImGui::Selectable(visible[i]->title, article_ == i, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 42))) article_ = i;
    ImGui::PopID();
  }
  if (visible.empty()) wrappedText("No matching terms.", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::EndChild();
  ImGui::SameLine(0, 5);

  ImGui::BeginChild("handbook-article", ImVec2(0, height), true);
  if (!visible.empty()) {
    const Article& article = *visible[article_];
    ImGui::TextDisabled("%s", article.chapter);
    ImGui::TextUnformatted(article.title);
    ImGui::Separator();
    ImGui::Spacing();
    if (const char* quickRead = quickReadFor(article.title)) {
      ImGui::TextDisabled("QUICK READ");
      wrappedText(quickRead, ImVec4(0.78f, 0.89f, 0.89f, 1.0f));
      ImGui::Spacing();
      ImGui::TextDisabled("PRECISE DEFINITION");
    }
    wrappedText(article.definition);
    ImGui::Spacing();
    drawDiagram(article.diagram);
    ImGui::Spacing();
    ImGui::TextDisabled("WHERE IT HAPPENS");
    wrappedText(article.pipelineLocation);
    ImGui::Spacing();
    ImGui::TextDisabled("WHAT CHANGES VISUALLY");
    wrappedText(article.visualResult);
    ImGui::Spacing();
    ImGui::TextDisabled("INTERACTIONS AND PITFALLS");
    wrappedText(article.interactions);
    ImGui::Spacing();
    ImGui::TextDisabled("ENGINE VOCABULARY");
    wrappedText(article.engineVocabulary, ImVec4(0.66f, 0.82f, 0.83f, 1.0f));

    const auto branches = branchesFor(article.title);
    if (branches[0] != nullptr) {
      ImGui::Spacing();
      ImGui::TextDisabled("CONTINUE INTO");
      for (const char* target : branches) {
        if (target == nullptr) continue;
        ImGui::PushID(target);
        const std::string label = std::string("-> ") + target;
        if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0, 22))) {
          std::snprintf(search_.data(), search_.size(), "%s", target);
          article_ = 0;
        }
        ImGui::PopID();
      }
    }

    if (article.example != Example::None) {
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextDisabled("LIVE TWO-PASS EXAMPLE");
      ImGui::Text("Base pass      %s", article.baseline);
      ImGui::Text("Selected pass  %s", article.alternative);
      ImGui::Spacing();
      if (exampleAvailableForProfile(profile, article.example)) {
        if (ImGui::Button("Apply to base pass")) action = {ActionType::ApplyToA, article.example};
        ImGui::SameLine();
        if (ImGui::Button("Apply to selected pass")) action = {ActionType::ApplyToB, article.example};
        ImGui::SameLine();
        if (ImGui::Button("Load split comparison")) {
          action = {ActionType::LoadComparison, article.example};
          open_ = false;
        }
        ImGui::TextDisabled("Applying is explicit. Opening or reading an article never changes the renderer.");
      } else {
        ImGui::TextDisabled("Live example unavailable for the active hardware target. The article remains reference material.");
      }
    }
  }
  ImGui::EndChild();
  ImGui::End();
  return action;
}

} // namespace handbook
