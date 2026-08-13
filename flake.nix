{
  description = "Graphics Lab - a small native realtime rendering instrument";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.imguizmo = {
    url = "github:CedricGuillemet/ImGuizmo";
    flake = false;
  };
  outputs = { self, nixpkgs, imguizmo }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
      imguiDockingFor = pkgs: pkgs.imgui.overrideAttrs (_: {
        version = "1.92.8-docking";
        src = pkgs.fetchFromGitHub {
          owner = "ocornut";
          repo = "imgui";
          tag = "v1.92.8-docking";
          hash = "sha256-zUTQaQ9SifwoHl7Z/+tkdpHMXpehCDGomkyF7Kj9LzE=";
        };
      });
    in {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          imguiDocking = imguiDockingFor pkgs;
        in {
          default = pkgs.stdenv.mkDerivation {
            pname = "graphics-lab";
            version = "0.1.0";
            src = pkgs.lib.cleanSourceWith {
              src = ./.;
              filter = path: type:
                let name = baseNameOf path;
                in !(
                  (type == "directory" && (name == "build" || name == ".direnv" || name == ".git"))
                  || name == "result"
                );
            };

            nativeBuildInputs = with pkgs; [ cmake ninja pkg-config wrapGAppsHook3 ffmpeg ];
            buildInputs = with pkgs; [
              glew
              glfw
              glm
              imguiDocking
              assimp
              nativefiledialog-extended
              stb
              zlib
              nlohmann_json
              gtk3
              gsettings-desktop-schemas
              libGL
              ffmpeg
              libice
              libsm
              libx11
              libxext
            ];

            cmakeFlags = [
              "-DGRAPHICS_LAB_WARNINGS_AS_ERRORS=OFF"
              "-DGRAPHICS_LAB_IMGUIZMO_SOURCE=${imguizmo}"
            ];
          };
        });

      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          imguiDocking = imguiDockingFor pkgs;
        in {
          default = pkgs.mkShell {
            GRAPHICS_LAB_IMGUIZMO_SOURCE = "${imguizmo}";
            packages = with pkgs; [
              cmake
              ninja
              pkg-config
              clang-tools
              gdb
              glew
              glfw
              glm
              imguiDocking
              assimp
              nativefiledialog-extended
              stb
              zlib
              nlohmann_json
              gtk3
              gsettings-desktop-schemas
              libGL
              ffmpeg
            ];
          };
        });
    };
}
