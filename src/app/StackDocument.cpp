#include "app/StackDocument.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace gfxlab {
namespace {

using Json = nlohmann::json;

template <typename Enum, std::size_t Size>
Enum enumFromId(const std::string& id, const std::array<const char*, Size>& ids, const Enum fallback) {
  const auto found = std::find_if(ids.begin(), ids.end(), [&](const char* candidate) { return id == candidate; });
  return found == ids.end() ? fallback : static_cast<Enum>(std::distance(ids.begin(), found));
}

std::optional<AnimationProperty> propertyFromId(const std::string& id) {
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    if (id == animationPropertyInfo(property).id) return property;
  }
  return std::nullopt;
}

glm::vec4 vectorValue(const Json& value, const int components) {
  glm::vec4 result(0.0f);
  if (!value.is_array()) return result;
  for (int component = 0; component < components && component < static_cast<int>(value.size()); ++component)
    result[component] = value[static_cast<std::size_t>(component)].get<float>();
  return result;
}

void parseProperties(const Json& properties, RenderPass& pass) {
  if (!properties.is_object()) return;
  for (auto item = properties.begin(); item != properties.end(); ++item) {
    const std::optional<AnimationProperty> property = propertyFromId(item.key());
    if (!property.has_value()) continue;
    const AnimationPropertyInfo& info = animationPropertyInfo(*property);
    setAnimationPropertyValue(pass, *property, vectorValue(item.value(), info.components));
  }
}

void parseTracks(const Json& tracks, PassAnimation& animation) {
  if (!tracks.is_array()) return;
  constexpr std::array interpolationIds = {"step", "linear", "smooth_step"};
  animation.tracks.clear();
  for (const Json& sourceTrack : tracks) {
    if (!sourceTrack.is_object() || !sourceTrack.contains("property")) continue;
    const std::optional<AnimationProperty> property = propertyFromId(sourceTrack.at("property").get<std::string>());
    if (!property.has_value() || !animationPropertyIsAnimatable(*property)) continue;
    PropertyAnimationTrack track;
    track.property = *property;
    track.interpolation = enumFromId(sourceTrack.value("interpolation", "linear"), interpolationIds,
      KeyframeInterpolation::Linear);
    const AnimationPropertyInfo& info = animationPropertyInfo(*property);
    if (sourceTrack.contains("keys") && sourceTrack.at("keys").is_array()) {
      for (const Json& sourceKey : sourceTrack.at("keys")) {
        if (!sourceKey.is_object() || !sourceKey.contains("value")) continue;
        track.keyframes.push_back({sourceKey.value("time_seconds", 0.0f),
          vectorValue(sourceKey.at("value"), info.components)});
      }
    }
    std::sort(track.keyframes.begin(), track.keyframes.end(),
      [](const PropertyKeyframe& a, const PropertyKeyframe& b) { return a.timeSeconds < b.timeSeconds; });
    animation.tracks.push_back(std::move(track));
  }
}

TestScene sceneFromId(const std::string& id) {
  constexpr std::array sceneIds = {"torus", "texture_minification", "depth_precision", "transparency",
    "lighting_comparison", "stencil_mask", "field_interference", "sdf_iso_surface", "imported_model"};
  return enumFromId(id, sceneIds, TestScene::Torus);
}

HardwareProfile profileFromId(const std::string& id) {
  if (id == "sony_playstation_ps1") return HardwareProfile::PlayStation;
  if (id == "nintendo_64") return HardwareProfile::Nintendo64;
  return HardwareProfile::Unrestricted;
}

void parseCamera(const Json& root, CameraOrbit& camera) {
  const Json* view = nullptr;
  if (root.contains("view")) view = &root.at("view");
  else if (root.contains("global_base") && root.at("global_base").contains("renderer") &&
      root.at("global_base").at("renderer").contains("view"))
    view = &root.at("global_base").at("renderer").at("view");
  if (view == nullptr || !view->is_object()) return;
  camera.yaw = view->value("orbit_yaw_radians", camera.yaw);
  camera.pitch = view->value("orbit_pitch_radians", camera.pitch);
  camera.distance = view->value("distance_units", camera.distance);
  if (view->contains("target")) camera.target = glm::vec3(vectorValue(view->at("target"), 3));
}

void parseTimeline(const Json& root, AnimationTimeline& timeline) {
  if (!root.contains("animation_timeline") || !root.at("animation_timeline").is_object()) return;
  const Json& source = root.at("animation_timeline");
  timeline.durationSeconds = std::max(source.value("duration_seconds", timeline.durationSeconds), 0.001f);
  timeline.playbackRate = source.value("playback_rate", timeline.playbackRate);
  timeline.loop = source.value("loop", timeline.loop);
  timeline.autoKey = source.value("auto_key", timeline.autoKey);
  timeline.showAllPasses = source.value("show_all_passes", timeline.showAllPasses);
  timeline.snapToFrames = source.value("snap_to_frames", timeline.snapToFrames);
  timeline.framesPerSecond = std::clamp(source.value("frames_per_second", timeline.framesPerSecond), 1, 240);
  timeline.timeSeconds = std::clamp(source.value("current_time_seconds", 0.0f), 0.0f, timeline.durationSeconds);
  timeline.playing = false;
}

void parseDisplay(const Json& root, DisplayReconstructionState& display) {
  if (!root.contains("display_reconstruction") || !root.at("display_reconstruction").is_object()) return;
  const Json& source = root.at("display_reconstruction");
  constexpr std::array displayIds = {"direct_rgb", "composite_ntsc", "lms_receptor_triplet", "rod_response",
    "mesopic_mix", "l_cone_response", "m_cone_response", "s_cone_response", "l_minus_m_opponent",
    "s_minus_lm_half_opponent", "rod_cone_absolute_difference", "rod_cone_quantized_xor"};
  display.enabled = source.value("enabled", display.enabled);
  display.signal = enumFromId(source.value("signal", "direct_rgb"), displayIds, DisplaySignal::DirectRgb);
  display.chromaBleed = source.value("chroma_bleed", display.chromaBleed);
  display.lumaChromaCrosstalk = source.value("luma_chroma_crosstalk", display.lumaChromaCrosstalk);
  display.scanlineStrength = source.value("scanline_strength", display.scanlineStrength);
  display.phosphorMaskStrength = source.value("phosphor_mask_strength", display.phosphorMaskStrength);
  display.bloomStrength = source.value("bloom_strength", display.bloomStrength);
  display.bloomRadiusPixels = source.value("bloom_radius_pixels", display.bloomRadiusPixels);
  if (source.contains("rgb_observer_approximation") && source.at("rgb_observer_approximation").is_object()) {
    const Json& observer = source.at("rgb_observer_approximation");
    display.observerExposureStops = observer.value("exposure_stops", display.observerExposureStops);
    display.darkAdaptation = observer.value("dark_adaptation_rod_fraction", display.darkAdaptation);
    display.rodSensitivity = observer.value("rod_sensitivity", display.rodSensitivity);
    display.opponentGain = observer.value("opponent_gain", display.opponentGain);
    display.receptorXorBits = std::clamp(observer.value("xor_bit_depth", display.receptorXorBits), 1, 8);
  }
}

void parsePerturbation(const Json& source, PassPerturbation& perturbation) {
  if (!source.is_object()) return;
  constexpr std::array uvIds = {"mesh_uv0", "planar_xy", "planar_xz", "planar_yz"};
  if (source.contains("model_translation_units"))
    perturbation.modelTranslation = glm::vec3(vectorValue(source.at("model_translation_units"), 3));
  perturbation.modelScale = source.value("model_scale", perturbation.modelScale);
  perturbation.normalInflation = source.value("normal_inflation_units", perturbation.normalInflation);
  if (source.contains("uv_offset")) perturbation.uvOffset = glm::vec2(vectorValue(source.at("uv_offset"), 2));
  if (source.contains("uv_scale")) perturbation.uvScale = glm::vec2(vectorValue(source.at("uv_scale"), 2));
  perturbation.uvRotation = source.value("uv_rotation_radians", perturbation.uvRotation);
  if (source.contains("uv_pivot")) perturbation.uvPivot = glm::vec2(vectorValue(source.at("uv_pivot"), 2));
  perturbation.uvMapping = enumFromId(source.value("coordinate_source", "mesh_uv0"), uvIds, UvMapping::MeshUv0);
  perturbation.cameraYaw = source.value("camera_yaw_offset_radians", perturbation.cameraYaw);
  perturbation.cameraPitch = source.value("camera_pitch_offset_radians", perturbation.cameraPitch);
  perturbation.cameraDistance = source.value("camera_distance_offset_units", perturbation.cameraDistance);
  perturbation.fieldOfView = source.value("field_of_view_offset_degrees", perturbation.fieldOfView);
}

void parseComposite(const Json& source, CompositeStep& composite) {
  if (!source.is_object()) return;
  constexpr std::array sourceIds = {"accumulated_result", "current_pass", "render_pass", "fixed_color",
    "previous_frame", "render_pass_field"};
  constexpr std::array colorSpaceIds = {"encoded_rgb", "linear_light"};
  constexpr std::array rangeIds = {"clamp_0_to_1", "preserve_signed_hdr", "wrap_fractional_part"};
  constexpr std::array maskIds = {"none", "pass_luminance", "pass_depth_0_to_10_units", "pass_image_edges",
    "pass_field"};
  const std::string operationId = source.value("operation", "absolute_difference");
  for (int operation = 0; operation <= static_cast<int>(RelationOperator::BitwiseXor); ++operation)
    if (operationId == relationOperatorId(static_cast<RelationOperator>(operation)))
      composite.operation = static_cast<RelationOperator>(operation);
  if (source.contains("source_a")) {
    const Json& operand = source.at("source_a");
    composite.sourceA = enumFromId(operand.value("type", "accumulated_result"), sourceIds,
      CompositeSource::Accumulator);
    composite.sourceAPassId = operand.value("pass_id", composite.sourceAPassId);
  }
  if (source.contains("source_b")) {
    const Json& operand = source.at("source_b");
    composite.sourceB = enumFromId(operand.value("type", "current_pass"), sourceIds,
      CompositeSource::CurrentPass);
    composite.sourceBPassId = operand.value("pass_id", composite.sourceBPassId);
  }
  if (source.contains("fixed_color_rgba")) composite.fixedColor = vectorValue(source.at("fixed_color_rgba"), 4);
  composite.bitDepth = std::clamp(source.value("bit_depth", composite.bitDepth), 1, 8);
  if (source.contains("previous_frame") && source.at("previous_frame").is_object()) {
    const Json& history = source.at("previous_frame");
    composite.historyDecay = history.value("decay", composite.historyDecay);
    if (history.contains("uv_offset")) composite.historyUvOffset = glm::vec2(vectorValue(history.at("uv_offset"), 2));
    if (history.contains("uv_scale")) composite.historyUvScale = glm::vec2(vectorValue(history.at("uv_scale"), 2));
  }
  composite.gain = source.value("gain", composite.gain);
  composite.bias = source.value("bias", composite.bias);
  composite.opacity = source.value("opacity", composite.opacity);
  composite.colorSpace = enumFromId(source.value("arithmetic_color_space", "encoded_rgb"), colorSpaceIds,
    CompositeColorSpace::EncodedRgb);
  composite.range = enumFromId(source.value("range_behavior", "clamp_0_to_1"), rangeIds, CompositeRange::Clamp);
  composite.mask = enumFromId(source.value("mask", "none"), maskIds, CompositeMask::None);
  composite.invertMask = source.value("invert_mask", composite.invertMask);
}

std::filesystem::path resolveAssetPath(const std::filesystem::path& documentPath, const std::string& sourcePath) {
  const std::filesystem::path path(sourcePath);
  return path.is_absolute() ? path : documentPath.parent_path() / path;
}

} // namespace

StackDocumentLoadResult loadStackDocumentFile(const std::string& path) {
  try {
    std::ifstream input(path);
    if (!input) return {std::nullopt, "Could not open stack document: " + path};
    Json root;
    input >> root;
    if (!root.is_object() || root.value("schema", "") != "graphics-lab.render-stack.v8")
      return {std::nullopt, "Unsupported document schema; expected graphics-lab.render-stack.v8"};

    StackDocument document;
    document.scene = sceneFromId(root.value("test_scene", "torus"));
    const Json* nestedRenderer = nullptr;
    if (root.contains("global_base") && root.at("global_base").contains("renderer"))
      nestedRenderer = &root.at("global_base").at("renderer");
    document.hardwareProfile = profileFromId(root.value("hardware_target",
      nestedRenderer != nullptr ? nestedRenderer->value("hardware_target", "unrestricted") : "unrestricted"));
    parseCamera(root, document.camera);
    parseTimeline(root, document.timeline);
    parseDisplay(root, document.renderStack.display());

    if (root.contains("global_base") && root.at("global_base").is_object()) {
      const Json& global = root.at("global_base");
      if (global.contains("properties")) parseProperties(global.at("properties"), document.renderStack.global());
      if (global.contains("property_tracks")) parseTracks(global.at("property_tracks"),
        document.renderStack.global().animation);
    }

    const std::filesystem::path documentPath(path);
    if (root.contains("imported_model") && root.at("imported_model").is_object()) {
      const std::string sourcePath = root.at("imported_model").value("source_path", "");
      if (!sourcePath.empty()) {
        const ModelImportResult imported = importModelAsset(resolveAssetPath(documentPath, sourcePath).string());
        if (!imported) return {std::nullopt, "Could not restore imported model: " + imported.error};
        document.importedModel = imported.asset;
      }
    }

    if (!root.contains("passes") || !root.at("passes").is_array() || root.at("passes").empty())
      return {std::nullopt, "Stack document contains no render passes"};
    if (root.at("passes").size() > RenderStack::maximumPasses)
      return {std::nullopt, "Stack document exceeds the maximum of eight render passes"};

    constexpr std::array outputIds = {"color", "linear_depth_0_to_10_units", "normals", "vertex_colors",
      "field_signal"};
    std::vector<RenderPass> passes;
    std::unordered_set<int> passIds;
    std::shared_ptr<const TextureAsset> inheritedTexture;
    std::string inheritedTexturePath;
    for (const Json& sourcePass : root.at("passes")) {
      RenderPass pass;
      pass.id = sourcePass.value("id", static_cast<int>(passes.size() + 1));
      if (pass.id <= 0) return {std::nullopt, "Render-pass IDs must be positive integers"};
      if (!passIds.insert(pass.id).second)
        return {std::nullopt, "Stack document contains duplicate render-pass ID " + std::to_string(pass.id)};
      pass.name = sourcePass.value("name", "Pass " + std::to_string(passes.size() + 1));
      pass.enabled = sourcePass.value("enabled", true);
      pass.output = enumFromId(sourcePass.value("output_buffer", "color"), outputIds, PassOutput::Color);
      if (sourcePass.contains("overrides") && sourcePass.at("overrides").is_array()) {
        for (const Json& sourceOverride : sourcePass.at("overrides")) {
          if (!sourceOverride.contains("property") || !sourceOverride.contains("value")) continue;
          const std::optional<AnimationProperty> property = propertyFromId(sourceOverride.at("property").get<std::string>());
          if (!property.has_value() || animationPropertyIsPassLocal(*property)) continue;
          setRenderPassOverride(pass, *property,
            vectorValue(sourceOverride.at("value"), animationPropertyInfo(*property).components));
        }
      }
      if (sourcePass.contains("perturbation")) parsePerturbation(sourcePass.at("perturbation"), pass.perturbation);
      if (sourcePass.contains("composite_into_previous"))
        parseComposite(sourcePass.at("composite_into_previous"), pass.composite);
      if (sourcePass.contains("animation") && sourcePass.at("animation").is_object()) {
        const Json& animation = sourcePass.at("animation");
        pass.animation.enabled = animation.value("enabled", true);
        if (animation.contains("property_tracks")) parseTracks(animation.at("property_tracks"), pass.animation);
      }
      if (sourcePass.contains("imported_texture") && sourcePass.at("imported_texture").is_object()) {
        const Json& texture = sourcePass.at("imported_texture");
        const std::string sourcePath = texture.value("source_path", "");
        if (!sourcePath.empty()) {
          const std::string resolvedPath = resolveAssetPath(documentPath, sourcePath).string();
          if (inheritedTexture == nullptr && document.renderStack.global().textureSource == TextureSource::ImportedOverride) {
            const TextureImportResult imported = importTextureAsset(resolvedPath);
            if (!imported) return {std::nullopt, "Could not restore imported texture: " + imported.error};
            inheritedTexture = imported.asset;
            inheritedTexturePath = resolvedPath;
            document.renderStack.global().importedTexture = inheritedTexture;
          }
          if (inheritedTexture == nullptr || resolvedPath != inheritedTexturePath) {
            const TextureImportResult imported = importTextureAsset(resolvedPath);
            if (!imported) return {std::nullopt, "Could not restore imported texture: " + imported.error};
            pass.importedTexture = imported.asset;
            pass.importedTextureOverride = true;
          }
        }
      }
      passes.push_back(std::move(pass));
    }
    const auto validateNamedOperand = [&](const CompositeSource source, const int referencedPassId,
                                          const std::string& passName, const char* operandName) {
      if ((source == CompositeSource::RenderPass || source == CompositeSource::RenderPassField) &&
          !passIds.contains(referencedPassId))
        throw std::runtime_error(passName + " " + operandName + " references missing render-pass ID " +
          std::to_string(referencedPassId));
    };
    for (const RenderPass& pass : passes) {
      validateNamedOperand(pass.composite.sourceA, pass.composite.sourceAPassId, pass.name, "source A");
      validateNamedOperand(pass.composite.sourceB, pass.composite.sourceBPassId, pass.name, "source B");
    }
    document.renderStack.replacePasses(std::move(passes));
    return {std::move(document), {}};
  } catch (const std::exception& exception) {
    return {std::nullopt, std::string("Could not load stack document: ") + exception.what()};
  }
}

bool saveStackDocumentFile(const std::string& path, const std::string& json, std::string& error) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "Could not open stack document for writing: " + path;
    return false;
  }
  output.write(json.data(), static_cast<std::streamsize>(json.size()));
  if (!output) {
    error = "Could not finish writing stack document: " + path;
    return false;
  }
  error.clear();
  return true;
}

} // namespace gfxlab
