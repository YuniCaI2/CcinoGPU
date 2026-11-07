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
            pkg-config
            wayland-scanner
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

            # Wayland & 图形依赖
            libGL
            wayland
            wayland-protocols
            libxkbcommon
            libdecor

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

            # 让 LibTooling (GeneratorMain / Tests) 能找到 Nix 注入的 C/C++ 标准库与第三方头文件
            c_inc=""
            c_after=""
            prev=""
            for flag in $NIX_CFLAGS_COMPILE $(cat ${pkgs.clangStdenv.cc}/nix-support/libc-cflags); do
              if [ "$prev" = "-isystem" ]; then c_inc="$c_inc:$flag"; fi
              if [ "$prev" = "-idirafter" ]; then c_after="$c_after:$flag"; fi
              prev="$flag"
            done
            export C_INCLUDE_PATH="''${c_inc#:}:''${c_after#:}"

            cxx_inc=""
            cxx_after=""
            prev=""
            for flag in $NIX_CFLAGS_COMPILE $(cat ${pkgs.clangStdenv.cc}/nix-support/libcxx-cxxflags) $(cat ${pkgs.clangStdenv.cc}/nix-support/libc-cflags); do
              if [ "$prev" = "-isystem" ] || [ "$prev" = "-cxx-isystem" ]; then cxx_inc="$cxx_inc:$flag"; fi
              if [ "$prev" = "-idirafter" ]; then cxx_after="$cxx_after:$flag"; fi
              prev="$flag"
            done
            export CPLUS_INCLUDE_PATH="''${cxx_inc#:}:''${cxx_after#:}"

            export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath (with pkgs; [
              vulkan-loader
              libGL
              wayland
              libxkbcommon
              libdecor
              llvmPackages.llvm
              llvmPackages.clang-unwrapped
            ])}:$LD_LIBRARY_PATH"

            echo "CcinoGPU Vulkan dev environment loaded (Clang via Flake)."
          '';
        };
      });
    };
}