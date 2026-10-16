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
        default = (pkgs.mkShell.override { stdenv = pkgs.clangStdenv; }) {
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            clang-tools
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

            # libTooling: CMake 配置和头文件在 dev 输出里
            llvmPackages.llvm
            llvmPackages.clang-unwrapped
          ];

          shellHook = ''
            export CC=clang
            export CXX=clang++
            export CMAKE_GENERATOR=Ninja
            export CMAKE_EXPORT_COMPILE_COMMANDS=ON
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
              llvmPackages.llvm
              llvmPackages.clang-unwrapped
            ])}:$LD_LIBRARY_PATH"

            echo "CcinoGPU Vulkan dev environment loaded (Clang via Flake)."
          '';
        };
      });
    };
}