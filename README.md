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

---

### 4. 待办事项（TODO 汇总与推进路线）

以下汇总了除 `ThirdPart/` 外项目中的 `TODO` 标记及关联待实现项。自上次整理以来已经落地的条目标成勾选，未完成项的行号按当前代码更新。

#### 4.1 纹理（Texture）

计算管线侧的 2D 纹理已经打通创建、Bindless 索引、`StorageImage` / `SampledImage` 绑定和 RenderGraph barrier，并由 `Tests/test_context_integration.cpp` 的 `StorageImageReadWriteClosedLoop` 覆盖。

- [x] **RHI 纹理资源创建与销毁**
  - `Context::CreateTexture` / `DestroyTexture` 与 `TextureCreateInfo`（`RHI/Context.h`），以及 `Core/Texture2D` 的创建封装已经可用。
  - `RHI/RHIStruct.h:163` 的 `//TODO: 纹理` 还留在文件里，对应实现已经在 Context / `Texture2D`。
- [x] **Bindless 描述符槽位分配与延迟回收**
  - `DescriptorSlot::WriteTextureDescriptor` / `DeferReleaseTexture` / `RecycleTextureBucket` 已在 `Update()` 中按飞行帧回收 SampledImage / StorageImage 索引。
  - `BindTexture2DSRV` / `BindTexture2DUAV` 已把数组索引写入 Uniform。`RHI/DescriptorSlotBinding.h:39` 上的 `BindTexture` 注释还留着。
  - `Docs/RHI/DescriptorSlot.md` 已记下 2D 纹理的索引分配、描述符写入和按飞行帧回收。
- [x] **RenderGraph 纹理视图、读写判定与 Barrier**
  - 2D 纹理视图由 `RG/Viewer.h` 的 `Load` / `Store` / `Sample` 提供，`Accessor` 继续表示 Buffer。
  - `RGTextureBarrier` 已包含 `resourceRID`、`srcPassType`、`dstPassType` 和访问标志。
  - `IsWritable()` / `IsReadable()` 已覆盖 `StorageWrite`、`StorageRead`、`Sampled`。`BuildGraph()` 会收集纹理依赖并生成 `VkImageMemoryBarrier2`，`GenerateCommands()` 会把 `SampledImage` / `StorageImage` 绑到 `DescriptorSlotBinding`。
- [ ] **纹理链路上还没做完的部分**
  - `RHI/Context.cpp:446`：`GetImageDescriptorType` / `GetSamplerDescriptorType` 仍固定返回 `false`。
  - `RHI/DescriptorSlot.cpp:246`：`//TODO：支持多format` —— ImageView 目前直接使用纹理自身的 format。
  - `RHI/Context.h:107`：`//TODO：完善对多格式和布局的TextureCopy的支持` —— `CopyTex*` 还不支持多 mip、多格式和布局。
  - `RG/Viewer.h:108`：`//TODO: 支持Array` —— 纹理数组视图。
  - `RG/Accessor.h:20`：`//TODO: 支持Texture` —— 注释还在；2D 读写/采样已经改由 `Viewer` 承担。
  - `RG/RenderGraph.cpp:215`：`//TODO: Texture` —— `RenderTarget` 还没有进入 `IsWritable()` / `IsReadable()`，这类边在 `BuildGraph()` 里不会记成 producer 或 reader。`RG/RenderGraph.cpp:492` 的 `//TODO: Texture` 对 Storage / Sampled 绑定已经过时。

#### 4.2 图形管线（Graphics Pass）与 Core 层管线封装
- [ ] **RenderGraph 图形 Pass 与外部 RenderTarget**
  - `RG/RenderGraph.h:74`：`//TODO: 定义外部RenderTarget 隐藏RHI RID` —— 实现 `RGBuilder::AddVSAndPS`，封装外部 RenderTarget 资源并完成 VS/PS 的翻译与绑定。
- [ ] **Core 层管线与 Pass 对象对接**
  - `RG/ComputePass.h:6`：`//TODO: 生成合适的Pipeline IR 并传入Core层`
  - `Core/ComputePipeline.h:24`：`//TODO` —— 实现 `ComputePipeline::CreateImpl` 与 `Delete`，打通 Core 层 `ComputePipeline` 对 RHI 层及 Pipeline IR 的消费。

#### 4.3 RHI 同步粒度、命令录制与配置扩展
- [ ] **Buffer 粒度的 Timeline Semaphore 同步**
  - `RHI/Context.cpp:520`：`//TODO: 增加更细粒度的timeline semaphore的设置，当前是等待整个管线的任务，后续增加Buffer粒度的可用性的标注` —— 优化 `Context::ReadbackBuffer`，按 Buffer 粒度追踪并等待 Timeline 值，避免粗粒度阻塞整个 Compute / Graphics 队列。
- [ ] **命令录制任务封装**
  - `RHI/Command.h:9`：`//TODO这里实现录制任务` —— 完善 `CommandContext` 的命令录制任务封装及多线程录制支持。
- [ ] **RHI 初始化配置扩展**
  - `RHI/RHIStruct.h:17`：`//TODO:增加别的特性` —— 扩展 `RHISettings` 中的设备特性与配置开关。

#### 4.4 性能优化与内存管理
- [ ] **`TRIDArray` 遍历性能优化**
  - `RHI/RHIStruct.h:136`：`//TODO: 这里的性能有点差，需要有一个更好的性能点` —— 优化 `TRIDArray::GetAllRIDs()` 每次全量遍历构造 `std::vector<RID>` 的开销。
- [ ] **RenderGraph 构建期容器栈分配优化（P2）**
  - `RG/RenderGraph.cpp:201`：`//TODO: (P2) 将这里的堆分配容器放到栈上` —— 优化 `RGBuilder::BuildGraph()` 中 `dependecyGraph`、`passDependencies`、`passInDegrees`、`producers`、`readers` 等临时哈希表/向量的堆内存分配。
- [ ] **RenderGraph 每帧 Pass 注册与绑定开销评估**
  - `RG/RenderGraph.h:86`：`//TODO:这里会在每帧调用不确定性能是否可靠！` —— 评估并优化 `RGBuilder::AddCS` 每帧调用时的查询与 Lambda 内存拷贝性能。
- [ ] **`RGResource` 句柄的 RTTI**
  - `RG/RGResource.h:133`：`//TODO:有RTTI，可能需要优化` —— `GetHandle()` 目前用 `dynamic_cast` 取回具体资源句柄。

#### 4.5 CodeGenerator（C++ AST 转 GLSL）
- [x] **默认构造初始化语义**
  - 空参构造的判定已放到 `CodeGenerator/ASTGenerator_Stmt.cpp` 的 `IsTrivialEmptyInit`。原 `CppParser::ChangeCppInitToGLSLInitializer` 里的 `//TODO: 确认含义` 已随 AST 重构去掉。
- [x] **全局 Texture descriptor indexing 声明**
  - `TextureBinding::EmitGLSL` 已输出 `globalSamplers` / `globalTextures` / `globalImages`（含有符号/无符号变体）。`CodeGenerator/GLSLAST.h:174` 的 `//TODO: 全局的Texture 的descriptor indexing 绑定` 还留在声明后面。

#### 4.6 其他关联未完成骨架（无显式 `TODO` 注释但留空处）
- [ ] **瞬态资源池实现**：`RG/TransientPool.h:19` 中 `TransientPool::Create` 仍是空函数，`TransientPool::Init`（`RG/TransientPool.h:17`）尚无定义（配合 RenderGraph 瞬态资源复用与内存别名）。

