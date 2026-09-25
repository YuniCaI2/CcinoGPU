{
  description = "CcinoGPU Vulkan Development Environment";

  inputs = {
    nixpkgs.url = "git+https://github.com/NixOS/nixpkgs?ref=release-26.05&shallow=1";
  };

  outputs = { self, nixpkgs }:
    let
      # 支持的系统架构
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forEachSystem = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });
    in
    {
      devShells = forEachSystem ({ pkgs }: {
        default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            gcc
            gnumake
          ];

          buildInputs = with pkgs; [
            # Vulkan 核心依赖
            vulkan-headers
            vulkan-loader
            vulkan-validation-layers
            vulkan-tools

            # Python (用于 CodeGen / 脚本)
            python3
            python3Packages.pip
            python3Packages.virtualenv

            # GLFW / 窗口 / X11 & 图形依赖
            libGL
            libx11
            libxrandr
            libxinerama
            libxcursor
            libxi
            libxext
            libxfixes

            #Test
            gtest
          ];

          shellHook = ''
            export VULKAN_SDK="${pkgs.vulkan-headers}"
            export VK_LAYER_PATH="${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d"
            export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath (with pkgs; [
              vulkan-loader
              libGL
              libx11
              libxrandr
              libxinerama
              libxcursor
              libxi
              libxext
              libxfixes
            ])}:$LD_LIBRARY_PATH"

            echo "CaIEngine Vulkan dev environment loaded (via Flake)."
          '';
        };
      });
    };
}