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

### 4. 示例：离屏三角形（`Demo/OfflineTriangle`）

不创建窗口和交换链，用一个图形 Pass 把三角形画到外部 `Texture2D` 上，读回后写成 BMP。
三角形不使用顶点缓冲，VS 按 `VertexIndex` 生成三个顶点和颜色，FS 直接输出插值后的颜色：

```cpp
builder->AddPass<"OfflineTrianglePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
    auto vs = [=](Ccino::Vec4& color) { /* 写 Ccino::Position 和 color */ };
    auto fs = [=](const Ccino::Vec4& color, Ccino::Vec4& outColor) { outColor = color; };

    auto renderTarget = rg.RTView(targetRID, passRID, Ccino::LoadOp::Clear, kClearColor);
    rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{.drawCounts = 3}, {renderTarget});
});
ctx.Run("OfflineTriangle");
target->Readback(pixels.data());
```

VS 参数按值传入表示 `in`，按引用传入表示 `out`；FS 的 `out` 按顺序对应 `renderTargets` 的 location。
着色器由 `GenerateShaderIR` 在构建期扫描 `main.cpp` 生成，Demo 目标已依赖它，跟随项目构建即可：

```powershell
cmake --build build/release --target OfflineTriangle
build/release/Demo/OfflineTriangle/OfflineTriangle.exe triangle.bmp
```

省略参数时输出到当前目录的 `triangle.bmp`。程序会粗略检查角落是清屏色、中心像素被画到，不满足时返回非 0。

### 5. 示例：交换链三角形（`Demo/SwapchainTriangle`）

打开窗口，每帧把三角形画到离屏 `RGBA8_sRGB` 纹理上，再 `Blit` 到交换链。VS 用捕获的 `angle` 旋转三个顶点，这个值在 `LoopRun` 里每帧重跑 `AddPass` 时更新：

```cpp
builder->AddPass<"SwapchainTrianglePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
    auto vs = [=](Ccino::Vec4& color) { /* 按 VertexIndex 生成顶点，用 angle 旋转后写入 Ccino::Position */ };
    auto fs = [=](const Ccino::Vec4& color, Ccino::Vec4& outColor) { outColor = color; };

    auto renderTarget = rg.RTView(targetRID, passRID, Ccino::LoadOp::Clear, kClearColor);
    rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{.drawCounts = 3}, {renderTarget});
});
builder->LoopRun(targetRID, [&]() { /* 返回 true 退出 */ });
```

`RHISettings::bUseSwapChain` 为 true 时才会创建窗口和交换链。一帧的顺序是 `PrepareFrame`、录制、`BlitToSwapchain`、`SubmitFrame`、`PresentFrame`，帧尾 `Tick` 推进飞行帧。省略参数时一直运行到关窗或按 Esc；传入帧数则跑完该帧数后退出。窗口尺寸变化时交换链会重建，离屏纹理保持创建时的大小，由 Blit 缩放。

```powershell
cmake --build build/release --target SwapchainTriangle
build/release/Demo/SwapchainTriangle/SwapchainTriangle.exe 300
```

---

### 6. 待办事项（TODO 汇总与推进路线）

以下汇总了除 `ThirdPart/` 外项目中的 `TODO` 标记及关联待实现项，行号对应当前工作区（2026-11-09）。已经落地的条目标成勾选。

#### 6.1 纹理（Texture）

计算管线侧的 2D 纹理已经打通创建、Bindless 索引、`StorageImage` / `SampledImage` 绑定和 RenderGraph barrier，并由 `Tests/test_context_integration.cpp` 的 `StorageImageReadWriteClosedLoop` 覆盖。

- [x] **RHI 纹理资源创建与销毁**
  - `Context::CreateTexture` / `DestroyTexture` 与 `TextureCreateInfo`（`RHI/Context.h`），以及 `Core/Texture2D` 的创建封装已经可用。
  - `VkImageView` 由 `Context::CreateTextureView` 包装成 `TextureView` 并交给 RID 管理，销毁走 `DeleteResource`。
- [x] **Bindless 描述符槽位分配与延迟回收**
  - `DescriptorSlot::WriteTextureDescriptor` / `DeferReleaseTexture` / `RecycleTextureBucket` 已在 `Update()` 中按飞行帧回收 SampledImage / StorageImage 索引。
  - `BindTexture2DSRV` / `BindTexture2DUAV` 已把数组索引写入 Uniform。`RHI/DescriptorSlotBinding.h:39` 上的 `BindTexture` 注释还留着。
  - `Docs/RHI/DescriptorSlot.md` 已记下 2D 纹理的索引分配、描述符写入和按飞行帧回收。
- [x] **RenderGraph 纹理视图、读写判定与 Barrier**
  - 2D 纹理视图由 `RG/Viewer.h` 的 `Load` / `Store` / `Sample` 提供，`Accessor` 继续表示 Buffer。
  - `RGTextureBarrier` 已包含 `resourceRID`、`srcPassType`、`dstPassType` 和访问标志。
  - `StorageWrite`、`StorageRead`、`Sampled` 和 `RenderTarget` 都已进入读写判定和 layout / access 转换。`BuildGraph()` 会收集纹理依赖并生成 `VkImageMemoryBarrier2`。`RenderTarget` 的依赖由 `RenderGraphTimeline.RenderTargetWriteChain` / `IndependentRenderTargetsJoin` 覆盖。
- [ ] **纹理链路上还没做完的部分**
  - `RHI/Context.cpp:489`：`GetImageDescriptorType` / `GetSamplerDescriptorType` 仍固定返回 `false`。
  - `Context::CreateTextureView`（`RHI/Context.cpp:1033`）已经按 `ViewRange` 选择 mip / array 范围和 `VkImageViewType`，但 format 仍取纹理自身（`RHI/Context.cpp:1066`），还不能按视图重新解释格式。
  - `RHI/Context.h:211`：`//TODO：完善对多格式和布局的TextureCopy的支持`。`CopyTex*` 会把整张图转到传输 layout，并拒绝 `mipLevels > 1` 和多重采样；实际只拷 mip 0（array layer 全拷）。`CopyTex` 还要求两边 extent、format、array layer 一致，没有格式转换和区域拷贝。
  - `RG/Viewer.h:97`：`//TODO: 支持Array` —— 着色器侧的纹理数组访问。RHI 的 ImageView 已经能按 `arrayCounts > 1` 建成 array view。
  - `RG/Accessor.h:20`：`//TODO: 支持Texture` —— 注释还在；2D 读写/采样已经改由 `Viewer` 承担。

#### 6.2 图形管线（Graphics Pass）与 Core 层管线封装
- [x] **RenderGraph 单轮图形 Pass**
  - `RGBuilder::AddVSAndFS`（`RG/RenderGraph.h:82`）接收 VS / FS lambda、`GraphicsSetting` 和由 `RTView` 创建的 `RenderTarget`。`RTView` 在创建时登记写边，附件的 View 在 `GenerateCommands()` 里按次创建、`Run()` 结束后释放。
  - CodeGenerator 识别 `AddVSAndFS`，分别编译出 `<Pass>.vs.spv` / `<Pass>.fs.spv` 并写进 ShaderIR JSON；`CcinoContext::Init` 按 `PassType` 加载 cs 或 vs/fs 后注册。
  - RHI 侧用 `GraphicsShaderObject`（`VK_EXT_shader_object`）绑定 VS / FS，光栅化、深度、混合和颜色写掩码都作为动态状态在 `Bind` 时录制；`GenerateCommands()` 用 dynamic rendering 录制 Draw。
  - 由 `Demo/OfflineTriangle`、`CcinoIntegrationTest.GraphicsPassRegisteredAndCreatesShaderObject`、`ProcessGraphicsShaderTest.*` 和 `ParseFilePassShadersTest.GraphicsPassCollectsAndCompilesVertexAndFragment` 覆盖。
- [ ] **图形 Pass 还没做完的部分**
  - 深度附件：`RHI::RenderDesc::depthAttachment` 已由 `Context::BeginRendering`（`RHI/Context.cpp:1609`）消费，但 `RTView` 只产出颜色附件，`GenerateCommands()`（`RG/RenderGraph.cpp:584`）只填 `colorAttachments`。`GraphicsSetting` 里的 `depthTestEnable` / `depthWriteEnable` 目前没有可用的深度目标。
  - `RG/RenderGraph.cpp:601`：`//TODO:更加灵活的派发，可以将这些配置放到Setting` —— 目前只有 `vkCmdDraw(drawCounts, 1, 0, 0)`，没有顶点 / 索引缓冲、实例化和 indirect。
  - `RG/RenderGraph.cpp:478`：`//TODO: 不全部绑定` —— 每次 `GenerateCommands()` 都同时绑定 Compute 和 Graphics 两个 bind point 的描述符集。
- [ ] **Core 层管线与 Pass 对象对接**
  - `RG/ComputePass.h:6`：`//TODO: 生成合适的Pipeline IR 并传入Core层`
  - `Core/ComputePipeline.h:24`：`//TODO` —— 实现 `ComputePipeline::CreateImpl` 与 `Delete`，打通 Core 层 `ComputePipeline` 对 RHI 层及 Pipeline IR 的消费。

#### 6.3 交换链与帧循环
- [x] **窗口、交换链和呈现**
  - `RHI/Swapchain` 负责创建、重建、Acquire 和 Present。`Context` 把交换链图像登记成 `isSwapchain` 的纹理，不走 VMA 销毁。
  - 帧顺序是 `PrepareFrame` → 录制 → `BlitToSwapchain` → `SubmitFrame` → `PresentFrame`。`PrepareFrame` 比较窗口像素尺寸，`OUT_OF_DATE` / `SUBOPTIMAL` 时重建；最小化时本帧跳过。
  - `RGBuilder::LoopRun`（`RG/RenderGraph.h:242`）每帧 `RecordPasses()` 后重新 `BuildGraph()`，再把 `presentTexture` Blit 到当前交换链图像。`Demo/SwapchainTriangle` 用它画旋转三角形。
  - `Tick()` 在离开一帧时记下各 timeline 的目标值，下次进入同一飞行帧槽位前等待，再复用命令缓冲和延迟销毁队列。
- [ ] **帧循环还没做完的部分**
  - `RG/RenderGraph.h:246`：`CreateGPUResource()` 在 `LoopRun` 循环外只调用一次，瞬态资源没有池化或别名。注释里“没有每帧调用 AddPass、没法实现动态效果”已经过时：`RecordPasses()` 在循环内，uniform 可以逐帧更新。
  - 窗口变大变小时只重建交换链。调用方的离屏纹理保持创建时的尺寸，`BlitToSwapchain` 做缩放，不会重建或通知这块纹理。
  - `SubmitFrame` / `ExecuteCommand` 都不把本次 timeline 值写回 Pass 用过的资源。`BlitToSwapchain` 会改源纹理的 layout，但也不写 `lastQueue` / `lastTimelineValue`。

#### 6.4 RHI Buffer、同步粒度与命令录制
- [x] **Buffer 的 CPU 访问方式显式声明**
  - `RHI::BufferCreateInfo::access` 用 `HostAccess::None` / `Write` / `Read` / `ReadWrite` 决定内存类型和 VMA 的访问提示，不再从 usage 推断。`requireHostCoherent` 要求分到 coherent 内存，分不到时创建失败。
  - `MapBuffer` 的 invalidate 和 `UnmapBuffer` 的 flush 按实际分到的内存是否 coherent 决定。
  - `Core/Buffer::CreateArgs` 改成 `size` + `BufferUsage` + `RHI::HostAccess`，删掉了 `BufferType`。`Copy` 在带 `Write` 时直接映射写入，否则走 staging 上传；`Readback` 在带 `Read` 时直接映射读取，否则走回读 buffer 拷贝。
- [ ] **资源粒度的 Timeline 同步（部分完成）**
  - `Context::WaitResource` 按资源记录的 `lastQueue` / `lastTimelineValue` 等待。`ReadbackBuffer`（`RHI/Context.cpp:563`）和 `Buffer::Readback` 的直接映射路径已经只用它。`CopyTex*` 提交后会把传输队列的 signal 值写回参与拷贝的纹理和 buffer。
  - `RGBuilder::Run()` 走 `ExecuteCommand`，`LoopRun` 走 `SubmitFrame`。两条路径都只 signal 队列 timeline，不回写 Pass 用到的资源。之后如果只 `WaitResource`，等不到这次图形提交。
  - `CopyTexToBuffer`（`RHI/Context.cpp:1315`）因此仍额外等待 Compute / Graphics 队列的 `ReadValue()`。同函数注释写“和 ReadbackBuffer 一样”，但 `ReadbackBuffer` 已经不再整队列等待。`CopyTexFromBuffer` 和 `CopyTex` 没有这层等待，只看资源自己的 timeline，和 RenderGraph 写过的纹理之间有竞争。
- [ ] **Timeline 提交失败时的值回退**
  - `TimelineSemaphore::BeginSignal`（`RHI/TimelineSemaphore.cpp:34`）在 `vkQueueSubmit2` 之前就递增了 `m_value`。如果提交失败，这个值永远不会被 signal，之后等待 `ReadValue()` 或 `previous` 的地方都会卡住。可以让 `BeginSignal` 只算出目标值，提交成功后再提交到 `m_value`。
- [ ] **命令录制任务封装**
  - `RHI/Command.h:9`：`//TODO这里实现录制任务` —— 完善 `CommandContext` 的命令录制任务封装及多线程录制支持。
- [ ] **RHI 初始化配置扩展**
  - `RHI/RHIStruct.h:17`：`//TODO:增加别的特性` —— 扩展 `RHISettings` 中的设备特性与配置开关。

#### 6.5 性能优化与内存管理
- [ ] **`TRIDArray` 遍历性能优化**
  - `RHI/RHIStruct.h:137`：`//TODO: 这里的性能有点差，需要有一个更好的性能点` —— 优化 `TRIDArray::GetAllRIDs()` 每次全量遍历构造 `std::vector<RID>` 的开销。
- [ ] **RenderGraph 构建期容器栈分配优化（P2）**
  - `RG/RenderGraph.cpp:241`：`//TODO: (P2) 将这里的堆分配容器放到栈上` —— 优化 `RGBuilder::BuildGraph()` 中 `dependecyGraph`、`passDependencies`、`passInDegrees`、`producers`、`readers` 等临时哈希表/向量的堆内存分配。
- [ ] **RenderGraph 每帧 Pass 注册与绑定开销评估**
  - `RG/RenderGraph.h:117`：`//TODO:这里会在每帧调用不确定性能是否可靠！` —— 评估并优化 `RGBuilder::AddCS` 每帧调用时的查询与 Lambda 内存拷贝性能。`AddVSAndFS` 和 `LoopRun` 里的 `RecordPasses()` 走的是同一套逻辑。
- [ ] **`RGResource` 句柄的 RTTI**
  - `RG/RGResource.h:133`：`//TODO:有RTTI，可能需要优化` —— `GetHandle()` 目前用 `dynamic_cast` 取回具体资源句柄。

#### 6.6 CodeGenerator（C++ AST 转 GLSL）
- [x] **默认构造初始化语义**
  - 空参构造的判定已放到 `CodeGenerator/ASTGenerator_Stmt.cpp` 的 `IsTrivialEmptyInit`。原 `CppParser::ChangeCppInitToGLSLInitializer` 里的 `//TODO: 确认含义` 已随 AST 重构去掉。
- [x] **全局 Texture descriptor indexing 声明**
  - `TextureBinding::EmitGLSL` 已输出 `globalSamplers` / `globalTextures` / `globalImages`（含有符号/无符号变体）。`CodeGenerator/GLSLAST.h:181` 的 `//TODO: 全局的Texture 的descriptor indexing 绑定` 还留在声明后面。
- [x] **图形 Pass 的 VS / FS 生成**
  - VS / FS 的捕获合并成同一个 UniformData 块，字段类型冲突时抛 `std::invalid_argument`；没有捕获时不输出 PushConstant；`out` 参数按顺序生成 location，并插入用到的自定义结构体。
- [ ] **VS / FS 合并后的布局对齐**
  - `CodeGenerator/CppParser.cpp:385`：`//TODO:修复当前的DescriptorSlotLayout的布局和UniformReference 对齐` —— CPU 侧 `DescriptorSlotLayout` 的偏移和生成的 GLSL UniformData 布局需要保证一致。

#### 6.7 其他关联未完成骨架
- [ ] **瞬态资源池实现**：`RG/TransientPool.h:19` 中 `TransientPool::Create` 仍是空函数，`TransientPool::Init`（`RG/TransientPool.h:17`）尚无定义（配合 RenderGraph 瞬态资源复用与内存别名）。
- [ ] **`Docs/Sample.md` 过时**：仍是更早的模板写法 `Buffer<BufferType::Device>` / `Buffer<UniformData, BufferType::Host>`，需要按当前的 `Buffer::CreateArgs` 和 RenderGraph 接口重写。
