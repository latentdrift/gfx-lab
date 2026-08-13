{
  description = "Graphics Lab - a small native realtime rendering instrument";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
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

            nativeBuildInputs = with pkgs; [ cmake ninja pkg-config wrapGAppsHook3 ];
            buildInputs = with pkgs; [
              glew
              glfw
              glm
              imgui
              assimp
              nativefiledialog-extended
              stb
              zlib
              gtk3
              gsettings-desktop-schemas
              libGL
              libice
              libsm
              libx11
              libxext
            ];

            cmakeFlags = [ "-DGRAPHICS_LAB_WARNINGS_AS_ERRORS=OFF" ];
          };
        });

      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.mkShell {
            packages = with pkgs; [
              cmake
              ninja
              pkg-config
              clang-tools
              gdb
              glew
              glfw
              glm
              imgui
              assimp
              nativefiledialog-extended
              stb
              zlib
              gtk3
              gsettings-desktop-schemas
              libGL
            ];
          };
        });
    };
}
