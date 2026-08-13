#include "app/FileDialog.hpp"

#include <nfd.h>

namespace gfxlab {

FileDialogResult openModelFileDialog() {
  if (NFD_Init() != NFD_OKAY) return {std::nullopt, NFD_GetError()};
  nfdu8char_t* selectedPath = nullptr;
  const nfdu8filteritem_t filters[] = {
    {"3D models", "obj,gltf,glb"}, {"Wavefront OBJ", "obj"}, {"glTF", "gltf,glb"}
  };
  const nfdresult_t result = NFD_OpenDialogU8(&selectedPath, filters, 3, nullptr);
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

} // namespace gfxlab
