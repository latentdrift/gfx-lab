#include "handbook/Handbook.hpp"

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
  Performance, Animation, RayTracing
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
  Chapter{"SHADING", "Shaders"}, Chapter{"SHADING", "Surface"},
  Chapter{"SHADING", "Materials"}, Chapter{"SHADING", "Texture"},
  Chapter{"SHADING", "Lighting"}, Chapter{"VISIBILITY & OUTPUT", "Visibility"},
  Chapter{"VISIBILITY & OUTPUT", "Color"}, Chapter{"VISIBILITY & OUTPUT", "Output"},
  Chapter{"ENGINE SYSTEMS", "Engine architecture"}, Chapter{"ENGINE SYSTEMS", "Performance"},
  Chapter{"BEYOND THIS LAB", "Animation"}, Chapter{"BEYOND THIS LAB", "Ray tracing"}
};

constexpr std::array articles = {
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
  Article{"Output", "Internal resolution and upscaling",
    "A renderer can draw the scene into a fixed-resolution offscreen framebuffer, then sample that completed image into the presentation viewport.",
    "Render targets and output pass", "Low internal resolution limits spatial detail; nearest upscaling preserves discrete pixels.",
    "Material texture filtering and framebuffer upscaling are different sampling operations.",
    "Specify internal resolution, presentation aspect ratio, and upscaling filter independently.", Diagram::Output,
    Example::InternalResolution, "640x480 internal rendering", "160x120 internal rendering with nearest upscaling"},
  Article{"Engine architecture", "Asset, scene, material, and renderer responsibilities",
    "Assets define authored data. The scene defines objects, transforms, cameras, and lights. Materials define surface inputs and shading. The renderer schedules passes and configures GPU state.",
    "Whole engine", "A coherent visual style emerges from constraints across all four responsibility areas.",
    "A shader cannot author low-poly silhouettes, schedule a shadow pass, or choose a camera composition by itself.",
    "Send agents separate asset constraints, scene constraints, renderer configuration, and architectural requirements.", Diagram::Pipeline},
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
  if (title == "The realtime rasterization pipeline") return {"What a shader program is", "Vertex attributes", "Render graphs and pass dependencies", nullptr};
  if (title == "Shaders and fixed-function state") return {"What a shader program is", "Material versus shader", "Depth testing and depth writes", nullptr};
  if (title == "What a shader program is") return {"Vertex shaders", "Fragment shaders", "Attributes, uniforms, varyings, and resources", "Compilation, linking, and shader variants"};
  if (title == "Vertex shaders") return {"Vertex attributes", "Skeletal animation and skinning", "Vertex position quantization", nullptr};
  if (title == "Fragment shaders") return {"Material versus shader", "Gouraud, Phong shading, and reflection models", "Transparency and compositing", nullptr};
  if (title == "Attributes, uniforms, varyings, and resources") return {"Material inputs and texture semantics", "Perspective-correct interpolation", "Compute and optional graphics stages", nullptr};
  if (title == "Material versus shader") return {"BRDFs and physically based materials", "Material inputs and texture semantics", "Forward, deferred, and forward+ rendering", nullptr};
  if (title == "Forward rendering and render passes") return {"Forward, deferred, and forward+ rendering", "Render graphs and pass dependencies", "Shadow mapping", nullptr};
  if (title == "Forward, deferred, and forward+ rendering") return {"Render graphs and pass dependencies", "GPU bottlenecks: geometry, fill rate, and bandwidth", "Rasterization versus ray tracing", nullptr};
  if (title == "Asset, scene, material, and renderer responsibilities") return {"Material versus shader", "Render graphs and pass dependencies", "CPU submission and draw calls", nullptr};
  if (title == "Skeletal animation and skinning") return {"Vertex shaders", "Morph targets and procedural deformation", nullptr, nullptr};
  if (title == "Rasterization versus ray tracing") return {"Acceleration structures and path tracing", "The realtime rasterization pipeline", nullptr, nullptr};
  return {nullptr, nullptr, nullptr, nullptr};
}

} // namespace

void Handbook::open() { open_ = true; }
bool Handbook::isOpen() const { return open_; }

Action Handbook::draw() {
  Action action;
  if (!open_) return action;

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.05f, io.DisplaySize.y * 0.05f), ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.90f, io.DisplaySize.y * 0.90f), ImGuiCond_Appearing);
  ImGui::SetNextWindowSizeConstraints(ImVec2(900, 600), io.DisplaySize);
  if (!ImGui::Begin("Graphics Handbook", &open_, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return action;
  }

  ImGui::TextUnformatted("GRAPHICS HANDBOOK");
  ImGui::SameLine();
  ImGui::TextDisabled("Mechanisms, pipeline locations, interactions, and engine vocabulary");
  ImGui::SameLine(ImGui::GetWindowWidth() - 285.0f);
  ImGui::SetNextItemWidth(260.0f);
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
      ImGui::TextDisabled("LIVE A/B EXAMPLE");
      ImGui::Text("A  %s", article.baseline);
      ImGui::Text("B  %s", article.alternative);
      ImGui::Spacing();
      if (ImGui::Button("Apply example to A")) action = {ActionType::ApplyToA, article.example};
      ImGui::SameLine();
      if (ImGui::Button("Apply comparison to B")) action = {ActionType::ApplyToB, article.example};
      ImGui::SameLine();
      if (ImGui::Button("Load split A/B")) action = {ActionType::LoadComparison, article.example};
      ImGui::TextDisabled("Applying is explicit. Opening or reading an article never changes the renderer.");
    }
  }
  ImGui::EndChild();
  ImGui::End();
  return action;
}

} // namespace handbook
