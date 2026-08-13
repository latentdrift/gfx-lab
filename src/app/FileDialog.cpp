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

} // namespace gfxlab
