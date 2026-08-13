#include "app/FileDialog.hpp"

#include <nfd.h>

namespace gfxlab {

namespace {

FileDialogResult openFileDialog(const nfdu8filteritem_t* filters, const nfdfiltersize_t filterCount) {
  if (NFD_Init() != NFD_OKAY) return {std::nullopt, NFD_GetError()};
  nfdu8char_t* selectedPath = nullptr;
  const nfdresult_t result = NFD_OpenDialogU8(&selectedPath, filters, filterCount, nullptr);
  FileDialogResult dialog;
  if (result == NFD_OKAY) {
    dialog.path = std::string(selectedPath);
    NFD_FreePathU8(selectedPath);
  } else if (result == NFD_ERROR) {
    dialog.error = NFD_GetError();
  }
  NFD_Quit();
  return dialog;
}

} // namespace

FileDialogResult openModelFileDialog() {
  const nfdu8filteritem_t filters[] = {
    {"3D models", "obj,gltf,glb"}, {"Wavefront OBJ", "obj"}, {"glTF", "gltf,glb"}
  };
  return openFileDialog(filters, 3);
}

FileDialogResult openTextureFileDialog() {
  const nfdu8filteritem_t filters[] = {
    {"Images", "png,jpg,jpeg,tga,bmp"}, {"PNG", "png"}, {"JPEG", "jpg,jpeg"}, {"Targa", "tga"},
    {"Bitmap", "bmp"}
  };
  return openFileDialog(filters, 5);
}

FileDialogResult openStackDocumentDialog() {
  const nfdu8filteritem_t filters[] = {{"Graphics Lab stack", "json"}};
  return openFileDialog(filters, 1);
}

FileDialogResult saveStackDocumentDialog() {
  if (NFD_Init() != NFD_OKAY) return {std::nullopt, NFD_GetError()};
  const nfdu8filteritem_t filters[] = {{"Graphics Lab stack", "json"}};
  nfdu8char_t* selectedPath = nullptr;
  const nfdresult_t result = NFD_SaveDialogU8(&selectedPath, filters, 1, nullptr,
    "graphics-lab-stack.json");
  FileDialogResult dialog;
  if (result == NFD_OKAY) {
    dialog.path = std::string(selectedPath);
    if (dialog.path->size() < 5 || dialog.path->substr(dialog.path->size() - 5) != ".json")
      *dialog.path += ".json";
    NFD_FreePathU8(selectedPath);
  } else if (result == NFD_ERROR) {
    dialog.error = NFD_GetError();
  }
  NFD_Quit();
  return dialog;
}

FileDialogResult saveViewportRecordingDialog() {
  if (NFD_Init() != NFD_OKAY) return {std::nullopt, NFD_GetError()};
  const nfdu8filteritem_t filters[] = {{"MP4 video", "mp4"}};
  nfdu8char_t* selectedPath = nullptr;
  const nfdresult_t result = NFD_SaveDialogU8(&selectedPath, filters, 1, nullptr,
    "graphics-lab-recording.mp4");
  FileDialogResult dialog;
  if (result == NFD_OKAY) {
    dialog.path = std::string(selectedPath);
    if (dialog.path->size() < 4 || dialog.path->substr(dialog.path->size() - 4) != ".mp4")
      *dialog.path += ".mp4";
    NFD_FreePathU8(selectedPath);
  } else if (result == NFD_ERROR) {
    dialog.error = NFD_GetError();
  }
  NFD_Quit();
  return dialog;
}

} // namespace gfxlab
