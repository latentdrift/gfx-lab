#include "document/Persistence.hpp"

#include "assets/ModelAsset.hpp"
#include "document/Properties.hpp"
#include "evaluation/Compiler.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace gfxlab::document {
namespace {

using Json = nlohmann::json;

Json vector(const glm::vec4 value, const int components) {
  Json result = Json::array();
  for (int component = 0; component < components; ++component) result.push_back(value[component]);
  return result;
}

glm::vec4 vector(const Json& source, const int components) {
  glm::vec4 result(0.0f);
  if (!source.is_array()) return result;
  for (int component = 0; component < components && component < static_cast<int>(source.size()); ++component)
    result[component] = source[static_cast<std::size_t>(component)].get<float>();
  return result;
}

Json signal(const SignalRef value) {
  return {{"operation", value.id.producer.value}, {"port", value.id.port},
    {"frame_offset", value.frameOffset}};
}

SignalRef signal(const Json& value) {
  return {{{value.value("operation", std::uint64_t{0})}, value.value("port", "")},
    value.value("frame_offset", 0)};
}

Json object(const ObjectId value) {
  const char* kind = "none";
  if (value.kind == ObjectKind::Scene) kind = "scene";
  else if (value.kind == ObjectKind::RenderDefaults) kind = "render_defaults";
  else if (value.kind == ObjectKind::Presentation) kind = "presentation";
  else if (value.kind == ObjectKind::Operation) kind = "operation";
  return {{"kind", kind}, {"id", value.value}};
}

ObjectId object(const Json& value) {
  const std::string kind = value.value("kind", "none");
  const ObjectKind type = kind == "scene" ? ObjectKind::Scene
    : kind == "render_defaults" ? ObjectKind::RenderDefaults
    : kind == "presentation" ? ObjectKind::Presentation
    : kind == "operation" ? ObjectKind::Operation : ObjectKind::None;
  return {type, value.value("id", std::uint64_t{0})};
}

Json texture(const TextureBinding& binding) {
  return {{"source", static_cast<int>(binding.source)}, {"srgb", binding.srgb},
    {"asset", binding.imported != nullptr ? binding.imported->sourcePath : ""}};
}

TextureBinding texture(const Json& source, const std::filesystem::path& documentPath) {
  TextureBinding result;
  result.source = static_cast<TextureSource>(source.value("source", static_cast<int>(result.source)));
  result.srgb = source.value("srgb", result.srgb);
  const std::string asset = source.value("asset", "");
  if (!asset.empty()) {
    const std::filesystem::path path(asset);
    const TextureImportResult imported = importTextureAsset(
      (path.is_absolute() ? path : documentPath.parent_path() / path).string());
    if (!imported) throw std::runtime_error("Could not restore texture: " + imported.error);
    result.imported = imported.asset;
  }
  return result;
}

Json rendererProperties(const RenderDefaults& defaults) {
  RenderPass carrier;
  carrier.renderer = defaults.renderer;
  carrier.textureSource = defaults.texture.source;
  carrier.importedTextureSrgb = defaults.texture.srgb;
  Json result = Json::object();
  for (const PropertyDescriptor& descriptor : propertyDescriptors()) {
    if (descriptor.rendererProperty == AnimationProperty::Count) continue;
    result[descriptor.stableName] = vector(animationPropertyValue(carrier,
      descriptor.rendererProperty), descriptor.components);
  }
  return result;
}

void parseRendererProperties(const Json& source, RenderDefaults& defaults) {
  RenderPass carrier;
  carrier.renderer = defaults.renderer;
  carrier.textureSource = defaults.texture.source;
  carrier.importedTextureSrgb = defaults.texture.srgb;
  for (const PropertyDescriptor& descriptor : propertyDescriptors()) {
    if (descriptor.rendererProperty == AnimationProperty::Count) continue;
    if (!source.contains(descriptor.stableName)) continue;
    setAnimationPropertyValue(carrier, descriptor.rendererProperty,
      vector(source.at(descriptor.stableName), descriptor.components));
  }
  defaults.renderer = carrier.renderer;
  defaults.texture.source = carrier.textureSource;
  defaults.texture.srgb = carrier.importedTextureSrgb;
}

Json overrides(const std::vector<PropertyOverride>& values) {
  Json result = Json::array();
  for (const PropertyOverride& value : values) {
    const PropertyDescriptor* descriptor = propertyDescriptor(propertyId(value.property));
    if (descriptor == nullptr) continue;
    result.push_back({{"property", descriptor->stableName},
      {"value", vector(value.value, descriptor->components)}});
  }
  return result;
}

std::vector<PropertyOverride> overrides(const Json& values) {
  std::vector<PropertyOverride> result;
  if (!values.is_array()) return result;
  for (const Json& value : values) {
    const std::string name = value.value("property", "");
    const auto descriptor = std::find_if(propertyDescriptors().begin(), propertyDescriptors().end(),
      [&](const PropertyDescriptor& candidate) { return candidate.stableName == name; });
    if (descriptor == propertyDescriptors().end() || !value.contains("value")) continue;
    result.push_back({descriptor->rendererProperty,
      vector(value.at("value"), descriptor->components)});
  }
  return result;
}

Json perturbation(const PassPerturbation& value) {
  return {{"translation", {value.modelTranslation.x, value.modelTranslation.y, value.modelTranslation.z}},
    {"scale", value.modelScale}, {"normal_inflation", value.normalInflation},
    {"uv_offset", {value.uvOffset.x, value.uvOffset.y}},
    {"uv_scale", {value.uvScale.x, value.uvScale.y}}, {"uv_rotation", value.uvRotation},
    {"uv_pivot", {value.uvPivot.x, value.uvPivot.y}}, {"uv_mapping", static_cast<int>(value.uvMapping)},
    {"camera_yaw", value.cameraYaw}, {"camera_pitch", value.cameraPitch},
    {"camera_distance", value.cameraDistance}, {"camera_lateral", value.cameraLateral},
    {"stereo_convergence", value.stereoConvergence}, {"fov_offset", value.fieldOfView}};
}

PassPerturbation perturbation(const Json& source) {
  PassPerturbation result;
  if (source.contains("translation")) result.modelTranslation = glm::vec3(vector(source.at("translation"), 3));
  result.modelScale = source.value("scale", result.modelScale);
  result.normalInflation = source.value("normal_inflation", result.normalInflation);
  if (source.contains("uv_offset")) result.uvOffset = glm::vec2(vector(source.at("uv_offset"), 2));
  if (source.contains("uv_scale")) result.uvScale = glm::vec2(vector(source.at("uv_scale"), 2));
  result.uvRotation = source.value("uv_rotation", result.uvRotation);
  if (source.contains("uv_pivot")) result.uvPivot = glm::vec2(vector(source.at("uv_pivot"), 2));
  result.uvMapping = static_cast<UvMapping>(source.value("uv_mapping", static_cast<int>(result.uvMapping)));
  result.cameraYaw = source.value("camera_yaw", result.cameraYaw);
  result.cameraPitch = source.value("camera_pitch", result.cameraPitch);
  result.cameraDistance = source.value("camera_distance", result.cameraDistance);
  result.cameraLateral = source.value("camera_lateral", result.cameraLateral);
  result.stereoConvergence = source.value("stereo_convergence", result.stereoConvergence);
  result.fieldOfView = source.value("fov_offset", result.fieldOfView);
  return result;
}

Json display(const DisplayReconstructionState& value) {
  return {{"enabled", value.enabled}, {"signal", static_cast<int>(value.signal)},
    {"chroma_bleed", value.chromaBleed}, {"crosstalk", value.lumaChromaCrosstalk},
    {"scanlines", value.scanlineStrength}, {"phosphor_mask", value.phosphorMaskStrength},
    {"bloom", value.bloomStrength}, {"bloom_radius", value.bloomRadiusPixels},
    {"exposure", value.observerExposureStops}, {"dark_adaptation", value.darkAdaptation},
    {"rod_sensitivity", value.rodSensitivity}, {"opponent_gain", value.opponentGain},
    {"xor_bits", value.receptorXorBits}};
}

DisplayReconstructionState display(const Json& source) {
  DisplayReconstructionState result;
  result.enabled = source.value("enabled", result.enabled);
  result.signal = static_cast<DisplaySignal>(source.value("signal", static_cast<int>(result.signal)));
  result.chromaBleed = source.value("chroma_bleed", result.chromaBleed);
  result.lumaChromaCrosstalk = source.value("crosstalk", result.lumaChromaCrosstalk);
  result.scanlineStrength = source.value("scanlines", result.scanlineStrength);
  result.phosphorMaskStrength = source.value("phosphor_mask", result.phosphorMaskStrength);
  result.bloomStrength = source.value("bloom", result.bloomStrength);
  result.bloomRadiusPixels = source.value("bloom_radius", result.bloomRadiusPixels);
  result.observerExposureStops = source.value("exposure", result.observerExposureStops);
  result.darkAdaptation = source.value("dark_adaptation", result.darkAdaptation);
  result.rodSensitivity = source.value("rod_sensitivity", result.rodSensitivity);
  result.opponentGain = source.value("opponent_gain", result.opponentGain);
  result.receptorXorBits = source.value("xor_bits", result.receptorXorBits);
  return result;
}

Json operation(const Operation& value) {
  Json result{{"id", value.id.value}, {"name", value.name}, {"enabled", value.enabled}};
  std::visit([&](const auto& data) {
    using Type = std::decay_t<decltype(data)>;
    if constexpr (std::is_same_v<Type, RenderOperation>) {
      result["type"] = "render"; result["overrides"] = overrides(data.overrides);
      result["perturbation"] = perturbation(data.perturbation);
      result["output"] = static_cast<int>(data.presentedOutput); result["texture"] = texture(data.texture);
      result["time"] = {{"scale", data.time.scale}, {"offset_seconds", data.time.offsetSeconds}};
    } else if constexpr (std::is_same_v<Type, InterpretOperation>) {
      result["type"] = "interpret"; result["input"] = signal(data.spectrum);
      result["observer"] = static_cast<int>(data.observer); result["exposure"] = data.exposureStops;
      result["gain"] = data.gain; result["bias"] = data.bias;
    } else if constexpr (std::is_same_v<Type, CompositeOperation>) {
      result["type"] = "composite"; result["a"] = signal(data.a); result["b"] = signal(data.b);
      result["interpretation_a"] = static_cast<int>(data.interpretationA);
      result["interpretation_b"] = static_cast<int>(data.interpretationB);
      result["observer"] = {{"exposure", data.observer.exposureStops},
        {"rod_sensitivity", data.observer.rodSensitivity}, {"opponent_gain", data.observer.opponentGain}};
      result["arithmetic"] = {{"operation", static_cast<int>(data.arithmetic.operation)},
        {"gain", data.arithmetic.gain}, {"bias", data.arithmetic.bias},
        {"opacity", data.arithmetic.opacity}, {"bit_depth", data.arithmetic.bitDepth},
        {"color_space", static_cast<int>(data.arithmetic.colorSpace)},
        {"range", static_cast<int>(data.arithmetic.range)}};
      result["mask"] = static_cast<int>(data.mask); result["invert_mask"] = data.invertMask;
      if (data.feedback.has_value()) result["feedback"] = {{"decay", data.feedback->decay},
        {"offset", {data.feedback->uvOffset.x, data.feedback->uvOffset.y}},
        {"scale", {data.feedback->uvScale.x, data.feedback->uvScale.y}}};
    } else if constexpr (std::is_same_v<Type, ConstantOperation>) {
      result["type"] = "constant"; result["value"] = vector(data.value, 4);
      result["kind"] = static_cast<int>(data.kind);
    } else if constexpr (std::is_same_v<Type, StereoOperation>) {
      result["type"] = "stereo"; result["left"] = signal(data.left); result["right"] = signal(data.right);
      result["mode"] = static_cast<int>(data.mode); result["maximum_disparity"] = data.maximumDisparityPixels;
      result["occlusion_tolerance"] = data.occlusionTolerance;
    } else if constexpr (std::is_same_v<Type, MeasureOperation>) {
      result["type"] = "measure"; result["input"] = signal(data.input);
      result["metric"] = static_cast<int>(data.metric); result["threshold"] = data.threshold;
      result["absolute"] = data.absoluteMagnitude;
    }
  }, value.data);
  result["outputs"] = Json::array();
  for (const SignalDescriptor& output : value.outputs)
    result["outputs"].push_back({{"port", output.key}, {"kind", static_cast<int>(output.kind)},
      {"name", output.name}, {"domain", static_cast<int>(output.metadata.domain)},
      {"encoding", static_cast<int>(output.metadata.encoding)},
      {"extent", {output.metadata.extent.x, output.metadata.extent.y, output.metadata.extent.z}},
      {"units", output.metadata.units}});
  return result;
}

Operation operation(const Json& source, const std::filesystem::path& path) {
  const OperationId id{source.at("id").get<std::uint64_t>()};
  const std::string name = source.value("name", "Operation");
  const std::string type = source.at("type").get<std::string>();
  Operation result;
  if (type == "render") {
    result = makeRenderOperation(id, name);
    auto& data = std::get<RenderOperation>(result.data);
    data.overrides = overrides(source.value("overrides", Json::array()));
    data.perturbation = perturbation(source.value("perturbation", Json::object()));
    data.presentedOutput = static_cast<PassOutput>(source.value("output", 0));
    data.texture = texture(source.value("texture", Json::object()), path);
    const Json time = source.value("time", Json::object());
    data.time.scale = time.value("scale", data.time.scale);
    data.time.offsetSeconds = time.value("offset_seconds", data.time.offsetSeconds);
  } else if (type == "interpret") {
    result = makeInterpretOperation(id, name, signal(source.at("input")));
    auto& data = std::get<InterpretOperation>(result.data);
    data.observer = static_cast<CompositeInterpretation>(source.value("observer", 0));
    data.exposureStops = source.value("exposure", data.exposureStops);
    data.gain = source.value("gain", data.gain); data.bias = source.value("bias", data.bias);
  } else if (type == "composite") {
    result = makeCompositeOperation(id, name, signal(source.at("a")), signal(source.at("b")));
    auto& data = std::get<CompositeOperation>(result.data);
    data.interpretationA = static_cast<CompositeInterpretation>(source.value("interpretation_a", 0));
    data.interpretationB = static_cast<CompositeInterpretation>(source.value("interpretation_b", 0));
    const Json observer = source.value("observer", Json::object());
    data.observer.exposureStops = observer.value("exposure", data.observer.exposureStops);
    data.observer.rodSensitivity = observer.value("rod_sensitivity", data.observer.rodSensitivity);
    data.observer.opponentGain = observer.value("opponent_gain", data.observer.opponentGain);
    const Json arithmetic = source.value("arithmetic", Json::object());
    data.arithmetic.operation = static_cast<RelationOperator>(arithmetic.value("operation",
      static_cast<int>(data.arithmetic.operation)));
    data.arithmetic.gain = arithmetic.value("gain", data.arithmetic.gain);
    data.arithmetic.bias = arithmetic.value("bias", data.arithmetic.bias);
    data.arithmetic.opacity = arithmetic.value("opacity", data.arithmetic.opacity);
    data.arithmetic.bitDepth = arithmetic.value("bit_depth", data.arithmetic.bitDepth);
    data.arithmetic.colorSpace = static_cast<CompositeColorSpace>(arithmetic.value("color_space", 0));
    data.arithmetic.range = static_cast<CompositeRange>(arithmetic.value("range", 0));
    data.mask = static_cast<CompositeMask>(source.value("mask", 0));
    data.invertMask = source.value("invert_mask", false);
    if (source.contains("feedback")) {
      const Json& feedback = source.at("feedback");
      FeedbackSettings settings;
      settings.decay = feedback.value("decay", settings.decay);
      if (feedback.contains("offset")) settings.uvOffset = glm::vec2(vector(feedback.at("offset"), 2));
      if (feedback.contains("scale")) settings.uvScale = glm::vec2(vector(feedback.at("scale"), 2));
      data.feedback = settings;
    }
  } else if (type == "constant") {
    result = makeConstantOperation(id, name, vector(source.at("value"), 4),
      static_cast<SignalKind>(source.value("kind", 0)));
  } else if (type == "stereo") {
    result = makeStereoOperation(id, name, signal(source.at("left")), signal(source.at("right")));
    auto& data = std::get<StereoOperation>(result.data);
    data.mode = static_cast<StereoAnalysisMode>(source.value("mode", 0));
    data.maximumDisparityPixels = source.value("maximum_disparity", data.maximumDisparityPixels);
    data.occlusionTolerance = source.value("occlusion_tolerance", data.occlusionTolerance);
  } else if (type == "measure") {
    result = makeMeasureOperation(id, name, signal(source.at("input")));
    auto& data = std::get<MeasureOperation>(result.data);
    data.metric = static_cast<MeasurementMetric>(source.value("metric", 0));
    data.threshold = source.value("threshold", data.threshold);
    data.absoluteMagnitude = source.value("absolute", data.absoluteMagnitude);
  } else throw std::runtime_error("Unknown typed operation: " + type);
  result.enabled = source.value("enabled", true);
  if (source.contains("outputs")) {
    result.outputs.clear();
    for (const Json& output : source.at("outputs")) {
      SignalDescriptor descriptor;
      descriptor.producer = id;
      descriptor.key = output.at("port").get<std::string>();
      descriptor.id = operationSignal(id, descriptor.key);
      descriptor.kind = static_cast<SignalKind>(output.value("kind", 0));
      descriptor.name = output.value("name", descriptor.key);
      descriptor.metadata.domain = static_cast<SignalDomain>(output.value("domain", 1));
      descriptor.metadata.encoding = static_cast<SignalEncoding>(output.value("encoding", 0));
      if (output.contains("extent"))
        descriptor.metadata.extent = glm::ivec3(vector(output.at("extent"), 3));
      descriptor.metadata.units = output.value("units", "");
      result.outputs.push_back(std::move(descriptor));
    }
  }
  return result;
}

} // namespace

std::string documentJson(const Document& document) {
  Json root;
  root["schema"] = "graphics-lab.document.v10";
  root["next_operation_id"] = document.nextOperationIdentity;
  root["scene"] = {{"type", static_cast<int>(document.scene.testScene)},
    {"model", document.scene.importedModel != nullptr ? document.scene.importedModel->sourcePath : ""},
    {"camera", {{"yaw", document.scene.authoredCamera.yaw}, {"pitch", document.scene.authoredCamera.pitch},
      {"distance", document.scene.authoredCamera.distance}, {"target", {
        document.scene.authoredCamera.target.x, document.scene.authoredCamera.target.y,
        document.scene.authoredCamera.target.z}}}}};
  root["hardware_profile"] = static_cast<int>(document.hardwareProfile);
  root["render_defaults"] = {{"properties", rendererProperties(document.renderDefaults)},
    {"texture", texture(document.renderDefaults.texture)}};
  root["operations"] = Json::array();
  for (const Operation& value : document.operations) root["operations"].push_back(operation(value));
  const Automation::Timeline& timeline = document.automation.timeline;
  root["timeline"] = {{"current", timeline.currentTimeSeconds}, {"duration", timeline.durationSeconds},
    {"rate", timeline.playbackRate}, {"loop", timeline.loop}, {"auto_key", timeline.autoKey},
    {"show_all", timeline.showAllOperations}, {"snap", timeline.snapToFrames}, {"fps", timeline.framesPerSecond}};
  root["animation"] = Json::array();
  for (const AnimationTrack& track : document.automation.animation) {
    Json keys = Json::array();
    const PropertyDescriptor* property = propertyDescriptor(track.target.property);
    if (property == nullptr) throw std::logic_error("Cannot serialize an animation track with an unknown property key.");
    for (const PropertyKeyframe& key : track.keyframes)
      keys.push_back({{"time", key.timeSeconds}, {"value", vector(key.value, property->components)}});
    root["animation"].push_back({{"owner", object(track.target.owner)},
      {"property", property->stableName}, {"interpolation", static_cast<int>(track.interpolation)},
      {"keys", std::move(keys)}});
  }
  root["modulation"] = Json::array();
  for (const ModulationRoute& route : document.automation.modulation) {
    const PropertyDescriptor* property = propertyDescriptor(route.target.property);
    if (property == nullptr)
      throw std::logic_error("Cannot serialize a modulation route with an unknown property key.");
    root["modulation"].push_back({{"source", signal(route.source)},
      {"owner", object(route.target.owner)},
      {"property", property->stableName},
      {"input", {route.inputRange.x, route.inputRange.y}},
      {"output", {route.outputRange.x, route.outputRange.y}}, {"clamp", route.clamp},
      {"smoothing", route.smoothingSeconds}});
  }
  root["presentation"] = {{"input", signal(document.presentation.input)},
    {"display", display(document.presentation.reconstruction)}};
  return root.dump(2) + "\n";
}

bool saveDocumentFile(const std::string& path, const Document& document, std::string& error) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) { error = "Could not open document for writing: " + path; return false; }
  output << documentJson(document);
  if (!output) { error = "Could not finish writing document: " + path; return false; }
  error.clear();
  return true;
}

DocumentLoadResult loadDocumentFile(const std::string& path) {
  try {
    std::ifstream input(path);
    if (!input) return {std::nullopt, "Could not open document: " + path};
    Json root; input >> root;
    if (root.value("schema", "") != "graphics-lab.document.v10")
      return {std::nullopt, "Unsupported typed document schema."};
    Document result;
    result.nextOperationIdentity = root.value("next_operation_id", std::uint64_t{1});
    const std::filesystem::path documentPath(path);
    const Json& scene = root.at("scene");
    result.scene.testScene = static_cast<TestScene>(scene.value("type", 0));
    const Json camera = scene.value("camera", Json::object());
    result.scene.authoredCamera.yaw = camera.value("yaw", result.scene.authoredCamera.yaw);
    result.scene.authoredCamera.pitch = camera.value("pitch", result.scene.authoredCamera.pitch);
    result.scene.authoredCamera.distance = camera.value("distance", result.scene.authoredCamera.distance);
    if (camera.contains("target")) result.scene.authoredCamera.target = glm::vec3(vector(camera.at("target"), 3));
    const std::string model = scene.value("model", "");
    if (!model.empty()) {
      const std::filesystem::path modelPath(model);
      const ModelImportResult imported = importModelAsset(
        (modelPath.is_absolute() ? modelPath : documentPath.parent_path() / modelPath).string());
      if (!imported) throw std::runtime_error("Could not restore model: " + imported.error);
      result.scene.importedModel = imported.asset;
    }
    result.hardwareProfile = static_cast<HardwareProfile>(root.value("hardware_profile", 0));
    const Json& defaults = root.at("render_defaults");
    parseRendererProperties(defaults.at("properties"), result.renderDefaults);
    result.renderDefaults.texture = texture(defaults.value("texture", Json::object()), documentPath);
    for (const Json& source : root.at("operations")) result.operations.push_back(operation(source, documentPath));
    const Json timeline = root.value("timeline", Json::object());
    result.automation.timeline.currentTimeSeconds = timeline.value("current", 0.0f);
    result.automation.timeline.durationSeconds = timeline.value("duration", 4.0f);
    result.automation.timeline.playbackRate = timeline.value("rate", 1.0f);
    result.automation.timeline.loop = timeline.value("loop", true);
    result.automation.timeline.autoKey = timeline.value("auto_key", false);
    result.automation.timeline.showAllOperations = timeline.value("show_all", false);
    result.automation.timeline.snapToFrames = timeline.value("snap", true);
    result.automation.timeline.framesPerSecond = timeline.value("fps", 24);
    for (const Json& source : root.value("animation", Json::array())) {
      const std::string name = source.value("property", "");
      const auto property = std::find_if(propertyDescriptors().begin(), propertyDescriptors().end(),
        [&](const PropertyDescriptor& candidate) { return candidate.stableName == name; });
      if (property == propertyDescriptors().end())
        throw std::runtime_error("Animation references an unknown property: " + name);
      AnimationTrack track;
      track.target = {object(source.at("owner")), property->id};
      track.interpolation = static_cast<KeyframeInterpolation>(source.value("interpolation", 1));
      for (const Json& key : source.value("keys", Json::array()))
        track.keyframes.push_back({key.value("time", 0.0f), vector(key.at("value"), property->components)});
      result.automation.animation.push_back(std::move(track));
    }
    for (const Json& source : root.value("modulation", Json::array())) {
      ModulationRoute route;
      route.source = signal(source.at("source"));
      const Json& propertyValue = source.at("property");
      const PropertyDescriptor* property = nullptr;
      const std::string name = propertyValue.get<std::string>();
      const auto found = std::find_if(propertyDescriptors().begin(), propertyDescriptors().end(),
        [&](const PropertyDescriptor& candidate) { return candidate.stableName == name; });
      if (found != propertyDescriptors().end()) property = &*found;
      if (property == nullptr)
        throw std::runtime_error("Modulation references an unknown property: " + name);
      route.target = {object(source.at("owner")), property->id};
      if (source.contains("input")) route.inputRange = glm::vec2(vector(source.at("input"), 2));
      if (source.contains("output")) route.outputRange = glm::vec2(vector(source.at("output"), 2));
      route.clamp = source.value("clamp", true); route.smoothingSeconds = source.value("smoothing", 0.15f);
      result.automation.modulation.push_back(route);
    }
    const Json& presentation = root.at("presentation");
    result.presentation.input = signal(presentation.at("input"));
    result.presentation.reconstruction = display(presentation.value("display", Json::object()));
    for (const Operation& operation : result.operations)
      result.nextOperationIdentity = std::max(result.nextOperationIdentity, operation.id.value + 1);
    const evaluation::EvaluationPlan plan = evaluation::compileDocument(result);
    if (!plan.valid()) return {std::nullopt, "Typed document contains an invalid operation graph."};
    return {std::move(result), {}};
  } catch (const std::exception& exception) {
    return {std::nullopt, std::string("Could not load typed document: ") + exception.what()};
  }
}

} // namespace gfxlab::document
