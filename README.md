# CcinoGPU

### 1. 简介
CcinoGPU是基于Vulkan实现的，以RenderGraph 配合GPU Driven Bindless特性实现的C++ 高性能GPU库。

### 2. LLVM / Clang 版本

`ThirdPart/llvm-project` 使用 LLVM 官方仓库，固定在 `llvmorg-23.1.4`
（提交 `7a941dfb2877a0b8cf30323fefa85ad019e0b0f5`），与当前使用的
llvm-mingw Clang / libc++ 23.1.4 匹配。项目通过 LibTooling 在运行时解析 C++，
因此不仅构建项目的编译器要支持标准库头文件，内嵌 Clang 也必须支持。
构建会同时准备 `clang-resource-headers`，解析时默认使用该版本的内置头文件目录；
编译数据库若已显式指定 `-resource-dir`，则保留其设置。

初始化依赖时使用父仓库记录的子模块提交，不要用 `--remote` 随意升级：

```sh
git submodule update --init --recursive
```

`.gitmodules` 中的 `release/23.x` 只用于显式的远端跟踪更新，实际可复现版本由父仓库的
submodule 提交记录固定。升级后需同时提交子模块指针及相关配置。

### 3. Windows Release 构建与测试

需要 CMake 3.24 或更高版本、Ninja、Python 3、Vulkan SDK，以及 llvm-mingw 23.1.4。
以下命令在项目根目录运行，路径按本机安装位置调整：

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\1.4.363.0'
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/Tools/llvm-mingw/bin/cc.exe -DCMAKE_CXX_COMPILER=C:/Tools/llvm-mingw/bin/c++.exe -DCMAKE_MAKE_PROGRAM=C:/Tools/ninja/ninja.exe -DLLVM_PARALLEL_COMPILE_JOBS=4 -DLLVM_PARALLEL_LINK_JOBS=1 -DLLVM_PARALLEL_TABLEGEN_JOBS=2
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure
```

LLVM / Clang 跨主版本升级或更换工具链时，建议使用新的构建目录；若复用旧目录，
需用 `cmake --fresh` 重新配置，并用 `cmake --build build/release --clean-first --parallel 4`
重新编译，避免混用旧的生成头文件和静态库。
