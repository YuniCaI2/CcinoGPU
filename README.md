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

以下汇总了除 `ThirdPart/` 外项目中所有的 `TODO` 标记及关联待实现项，按功能专题分类整理，方便后续逐项推进实现。

#### 4.1 纹理（Texture）全链路支持（最高频主线）
当前项目以计算管线 + Buffer BDA 为主，Texture 从 RHI 资源创建、Bindless 描述符索引管理到 RenderGraph 依赖分析与屏障均预留了 `TODO`：

- [ ] **RHI 纹理资源定义与创建**
  - `RHI/RHIStruct.h:160`：`//TODO: 纹理` —— 增加 Texture 相关结构体与创建参数定义。
  - `RHI/Context.h:77` & `RHI/Context.cpp:192`：实现 RHI 层 Texture 的创建、销毁，并完善 `GetImageDescriptorType` / `GetSamplerDescriptorType`（当前固定返回 `false`）。
- [ ] **Bindless 描述符槽位分配与延迟回收**
  - `RHI/DescriptorSlot.h:77`：`//TODO: Texture 在SampledImage / StorageImage 数组中的索引分配, 以及按飞行帧延迟回收`
  - `RHI/DescriptorSlot.cpp:379`：`//TODO: Texture 实现后, 在这里按飞行帧回收SampledImage / StorageImage 数组中的索引`（在 `DescriptorSlot::Update()` 中实现）。
  - `RHI/DescriptorSlotBinding.h:42`：`//TODO: BindTexture, 等RHI 支持Texture 创建后实现, 写入SampledImage / StorageImage 的数组索引`
  - `Docs/RHI/DescriptorSlot.md:156`：文档同步记录 Texture 绑定与回收设计。
- [ ] **RenderGraph 纹理视图、依赖分析与 Barrier**
  - `RG/Accessor.h:20`：`//TODO: 支持Texture` —— 扩展 `Accessor` 视图模板，支持 Texture 资源读写/采样接口及 AST 类型推导。
  - `RG/RenderGraph.h:48`：`//TODO:完善TextureBarrier这个结构体，其中还缺少很多对象` —— 为 `RGTextureBarrier` 补充 `resourceRID`、`srcPassType`、`dstPassType`、Image Layout 转换等字段。
  - `RG/RenderGraph.cpp:72` & `RG/RenderGraph.cpp:85`：`// TODO Texture` —— 在 `IsWritable()` 与 `IsReadable()` 中补全 `TextureAccessFlags`（如 `StorageWrite`、`RenderTarget`、`Sampled`、`StorageRead`）的读写判定。
  - `RG/RenderGraph.cpp:116`：`//TODO: Texture` —— 在 `RGBuilder::BuildGraph()` 中收集并生成 Texture 的依赖与 `VkImageMemoryBarrier2`。
  - `RG/RenderGraph.cpp:322`：`//TODO: Texture` —— 在 `RGBuilder::GenerateCommands()` 中处理 `UniformDataType` 为纹理类型时的 `DescriptorSlotBinding` 绑定映射。

#### 4.2 图形管线（Graphics Pass）与 Core 层管线封装
- [ ] **RenderGraph 图形 Pass 与外部 RenderTarget**
  - `RG/RenderGraph.h:73`：`//TODO: 定义外部RenderTarget 隐藏RHI RID` —— 实现 `RGBuilder::AddVSAndPS`，封装外部 RenderTarget 资源并完成 VS/PS 的翻译与绑定。
- [ ] **Core 层管线与 Pass 对象对接**
  - `RG/ComputePass.h:6`：`//TODO: 生成合适的Pipeline IR 并传入Core层`
  - `Core/ComputePipeline.h:24`：`//TODO` —— 实现 `ComputePipeline::CreateImpl` 与 `Delete`，打通 Core 层 `ComputePipeline` 对 RHI 层及 Pipeline IR 的消费。

#### 4.3 RHI 同步粒度、命令录制与配置扩展
- [ ] **Buffer 粒度的 Timeline Semaphore 同步**
  - `RHI/Context.cpp:262`：`//TODO: 增加更细粒度的timeline semaphore的设置，当前是等待整个管线的任务，后续增加Buffer粒度的可用性的标注` —— 优化 `Context::ReadbackBuffer`，按 Buffer 粒度追踪并等待 Timeline 值，避免粗粒度阻塞整个 Compute / Graphics 队列。
- [ ] **命令录制任务封装**
  - `RHI/Command.h:9`：`//TODO这里实现录制任务` —— 完善 `CommandContext` 的命令录制任务封装及多线程录制支持。
- [ ] **RHI 初始化配置扩展**
  - `RHI/RHIStruct.h:17`：`//TODO:增加别的特性` —— 扩展 `RHISettings` 中的设备特性与配置开关。

#### 4.4 性能优化与内存管理
- [ ] **`TRIDArray` 遍历性能优化**
  - `RHI/RHIStruct.h:133`：`//TODO: 这里的性能有点差，需要有一个更好的性能点` —— 优化 `TRIDArray::GetAllRIDs()` 每次全量遍历构造 `std::vector<RID>` 的开销。
- [ ] **RenderGraph 构建期容器栈分配优化（P2）**
  - `RG/RenderGraph.cpp:102`：`//TODO: (P2) 将这里的堆分配容器放到栈上` —— 优化 `RGBuilder::BuildGraph()` 中 `dependecyGraph`、`passDependencies`、`passInDegrees`、`producers`、`readers` 等临时哈希表/向量的堆内存分配。
- [ ] **RenderGraph 每帧 Pass 注册与绑定开销评估**
  - `RG/RenderGraph.h:85`：`//TODO:这里会在每帧调用不确定性能是否可靠！` —— 评估并优化 `RGBuilder::AddCS` 每帧调用时的查询与 Lambda 内存拷贝性能。

#### 4.5 CodeGenerator（C++ AST 转 GLSL）
- [ ] **默认构造初始化语义确认**
  - `CodeGenerator/CppParser.cpp:796`：`//TODO: 确认含义` —— 在 `CppParser::ChangeCppInitToGLSLInitializer` 中确认 `construct->getNumArgs() == 0 && type.find('[') == std::string::npos` 空参构造返回空初始化器的边界行为。

#### 4.6 其他关联未完成骨架（无显式 `TODO` 注释但留空处）
- [ ] **瞬态资源池实现**：`RG/TransientPool.h:19` 中 `TransientPool::Create` 尚未实现，且 `TransientPool::Init` 尚无定义（配合 RenderGraph 瞬态资源复用与内存别名）。

