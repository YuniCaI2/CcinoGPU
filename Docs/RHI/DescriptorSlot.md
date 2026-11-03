# Descriptor Slot 设计思路


### 简介
DescriptorSlot 是 Ccino 绑定层的消费端。它只依赖 Vulkan 1.2 核心的 `bufferDeviceAddress` 和 `descriptor_indexing`，不需要额外的设备扩展。

- **Buffer**：全部通过 BDA（Buffer Device Address）访问，不占用 Descriptor。
- **Texture / Sampler**：放在一个全局 DescriptorSet 的 `descriptor_indexing` 数组里，Shader 用数组索引访问。Sampler 在 `Init` 时整表写入；2D 的 SampledImage / StorageImage 在 `BindTexture2DSRV` / `BindTexture2DUAV` 时分配索引并写入。
- **Binding**：一次 Dispatch 要用的资源和常量由一个 `DescriptorSlotBinding` 描述。它有一组 UniformBuffer，数量和飞行帧一致，按 std140 存放 Buffer 的 BDA、Sampler / Texture 的数组索引和常量参数。
- **PushConstant**：只推送当前飞行帧那份 UniformBuffer 的 BDA（8 字节）。

```
vkCmdPushConstants: PushConstant[0..8) = 当前飞行帧 UniformBuffer 的 BDA
        │
        ▼
Binding 的 UniformBuffer（std140）
  params     : uint64_t BDA  ──►  Params Buffer
  data       : uint64_t BDA  ──►  Data Buffer
  multiplier : uint           常量直接存值
  sampler    : uint           ──►  globalSamplers[sampler]（binding 0）
  albedo     : uint           ──►  globalTextures[index]（binding 1，SampledImage）
  storage    : uint           ──►  globalImages[index]（binding 2，StorageImage）
```


### 全局对象
下面这些对象都是全局唯一的，由 `DescriptorSlot::InitGlobal` 创建（`Context::Init` 调用），由 `DescriptorSlot::DestroyGlobal` 销毁（`Context::Destroy` 调用）。

| 对象 | 内容 |
|---|---|
| DescriptorSetLayout | binding 0：`SAMPLER[16]`；binding 1：`SAMPLED_IMAGE[1024]`；binding 2：`STORAGE_IMAGE[1024]`。每个数组都带 `PARTIALLY_BOUND`、`UPDATE_AFTER_BIND`、`UPDATE_UNUSED_WHILE_PENDING` |
| PipelineLayout | set 0 是上面的 DescriptorSetLayout，PushConstant 是 `[0, 8)`，stage 是 `VK_SHADER_STAGE_COMPUTE_BIT` |
| DescriptorPool | `UPDATE_AFTER_BIND`，最多分配 `DESCRIPTOR_MAX_SET_COUNT`（8）个 DescriptorSet |
| Sampler | 每个 `SamplerType` 一个，在 Sampler 数组中的索引就是枚举值 |

- 所有 ComputePipeline 都用这个 PipelineLayout 创建，`Context::CreateComputePipeline` 已经自动处理。
- 每个 `DescriptorSlot` 从全局 Pool 分配一个 DescriptorSet，`Init` 时把所有 Sampler 写进去。
- binding 编号和数组容量由 `DescriptorSlot.h` 里的 `DESCRIPTOR_BINDING_*` / `DESCRIPTOR_MAX_*` 宏定义。要支持更多类型，在 `DescriptorSlot.cpp` 的 `BINDLESS_ARRAYS` 里追加一项即可。
- 目前只考虑计算。支持图形时，把 `DESCRIPTOR_SHADER_STAGES` 改成 `VK_SHADER_STAGE_ALL`，并让 `BindDescriptorSet` 也绑定到 Graphics 绑定点。


### 布局
```cpp
using DescriptorSlotLayout = std::vector<std::pair<std::string, UniformDataType>>;
```

- 字段顺序就是 UniformBuffer 中成员的顺序，对应 Shader 里 Slot 结构体的成员声明顺序。顺序是 CPU 和 Shader 之间唯一的约定，由 Ccino 编译后同时生成两边。
- 偏移按 **std140** 计算，Shader 里的 Slot 结构体必须声明为 `std140`：
  - 标量的大小和对齐都是 N（double 为 8）。
  - `vec2` 对齐 2N；`vec3` 和 `vec4` 对齐 4N。`vec3` 的大小是 12，后面可以紧跟一个标量。
  - `matCxR` 看成 C 个 `vecR`，列步长向上取整到 16。所以 `mat3` 是 48 字节，`mat3x2` 每列也占 16 字节。
  - 整个块的大小按 16 字节补齐。
- `Buffer` 字段占 8 字节，存的是 BDA。`SampledImage` / `StorageImage` / `Sampler` 字段占 4 字节，存的是全局 DescriptorSet 中的数组索引。
- 字段名不能重复，否则创建 Binding 时会抛异常。
- 布局只是一个类似模板的结构，不和资源挂钩。`DescriptorSlotBinding::GetOffsetsTable` 可以单独用来计算偏移。


### Shader 侧约定
```glsl
#version 460
#extension GL_EXT_buffer_reference : require

//全局DescriptorSet (set 0)。CodeGenerator 的 TextureBinding::EmitGLSL 会输出这些声明。
//layout(set = 0, binding = 0) uniform sampler globalSamplers[];
//layout(set = 0, binding = 1) uniform texture2D globalTextures[];
//layout(set = 0, binding = 1) uniform itexture2D globalITextures[];
//layout(set = 0, binding = 1) uniform utexture2D globalUTextures[];
//layout(set = 0, binding = 2) uniform image2D globalImages[];
//layout(set = 0, binding = 2) uniform iimage2D globalIImages[];
//layout(set = 0, binding = 2) uniform uimage2D globalUImages[];

layout(buffer_reference, std140, buffer_reference_align = 16) readonly buffer Params {
    uint multiplier;
};

layout(buffer_reference, std430, buffer_reference_align = 4) buffer Data {
    uint values[];
};

//和DescriptorSlotLayout 的字段顺序一致, 按std140 排布
layout(buffer_reference, std140, buffer_reference_align = 16) readonly buffer Slot {
    Params params; //UniformDataType::Buffer
    Data data;     //UniformDataType::Buffer
};

layout(push_constant) uniform PushData {
    Slot slot;
} pushData;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    Slot slot = pushData.slot;
    slot.data.values[idx] = idx * slot.params.multiplier;
}
```

- `push_constant` 里只有 Slot 指针，PipelineLayout 的 PushConstant 范围也只有这 8 字节。
- Slot 的 `buffer_reference_align` 写 16。UniformBuffer 的 BDA 会在创建时检查是否 16 字节对齐。
- 示例见 `Tests/Shaders/compute_pipeline_test.comp`（Buffer）和 `Tests/Shaders/bind_param_test.comp`（各种常量）。修改后用 `glslc --target-env=vulkan1.3` 重新生成同目录下的 spv。


### 使用流程
```cpp
//Context::Init 内部已经调用 DescriptorSlot::InitGlobal
DescriptorSlot descriptorSlot;
descriptorSlot.Init(&context);

const DescriptorSlotLayout layout = {
    {"params", UniformDataType::Buffer},
    {"data", UniformDataType::Buffer},
    {"scale", UniformDataType::Float},
    {"transform", UniformDataType::Mat4},
};

DescriptorSlotBinding binding = descriptorSlot.CreateSlotBinding(layout);
binding.BindBuffer("params", ubo)
       .BindBuffer("data", ssbo)
       .BindParam("scale", 2.0f)              //类型必须和布局一致, 1.0 是double, 会抛异常
       .BindParam("transform", glm::mat4(1.0f));

//录制
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
descriptorSlot.BindDescriptorSet(cmd); //每个命令缓冲绑定一次
binding.Push(cmd);                     //之后的Dispatch 使用这个Binding
vkCmdDispatch(cmd, 8, 1, 1);

//每帧开始、等完这一帧的fence 之后
descriptorSlot.Update();

//所有Binding 析构之后
descriptorSlot.Destroy();
```

`BindParam` 支持 `bool`、`int32_t`、`uint32_t`、`float`、`double`，以及 `Core/CcinoType.h` 中的全部向量和矩阵类型：
- `bool` 和 `bvec` 会展开成每个分量 4 字节的 `uint32_t`。
- 矩阵按列写入，跳过列之间的 padding。
- 不支持的 C++ 类型（如 `size_t`、`int64_t`）在编译期报错。
- 类型和布局不一致、字段名不存在时抛异常。
- `SampledImage` / `StorageImage` / `Sampler` 字段也可以直接用 `uint32_t` 写入索引。


### 生命周期与同步
- **Bind 只改 CPU 副本**：`BindBuffer` / `BindSampler` / `BindParam` 只修改 Binding 的 CPU 副本，并把所有飞行帧标记为需要上传。`BindTexture2DSRV` / `BindTexture2DUAV` 还会立刻创建 ImageView 并写入描述符数组；Uniform 里的索引同样等到 `Push` 再上传。
- **Push 才上传**：`Push` 时如果当前飞行帧的 UniformBuffer 落后于 CPU 副本，就先拷贝过去（非 coherent 内存会 flush），再推送它的 BDA。已经录制的命令不受之后 `Bind*` 的影响。
- **同一帧多次 Push 的限制**：同一飞行帧内的多次 `Push` 共用这一帧的 UniformBuffer，GPU 读到的是这一帧最后一次 `Push` 时的内容。需要不同的值，请用多个 Binding。
- **飞行帧轮换**：第 F 帧的 UniformBuffer 只在第 F 帧被改写。前提是 `Context::Tick` 推进帧序号之前，已经等完这一帧的 fence。
- **析构**：Binding 析构时，UniformBuffer 交给 `Context::DeleteResource` 按飞行帧延迟释放。已绑定的 ImageView 交给 `DescriptorSlot` 按飞行帧回收。
- **Buffer 不需要登记**：Buffer 通过 BDA 访问，DescriptorSlot 不登记 Buffer。资源销毁后，不能再 Push 还引用它的 Binding。
- **只能移动**：Binding 只能移动不能拷贝。`DescriptorSlot::Destroy` 时如果还有 Binding 存活，会抛出异常。


### 线程安全
- `CreateSlotBinding` 可以在多个线程同时调用。
- 同一个 Binding 同一时间只能被一个线程使用。
- `Init` / `Destroy` 对全局 DescriptorPool 的分配和释放由全局锁保护。
- `Update` 必须在帧边界调用，不能和录制并发。


### 容量与限制
- Sampler 数组 16 个，SampledImage / StorageImage 数组各 1024 个，最多 8 个 DescriptorSlot。`InitGlobal` 会检查设备的 update-after-bind 上限，不够时抛异常。
- 全局对象只支持一个 Context。
- 纹理绑定目前是 2D。`BindTexture2D` 会用纹理自身的 format 覆盖 `VkImageViewCreateInfo::format`，多 format 视图还没做。


### 纹理绑定与回收
`BindTexture2DSRV` 对应布局里的 `SampledImage`，`BindTexture2DUAV` 对应 `StorageImage`。两者都接收纹理 RID 和 `VkImageViewCreateInfo`：`image` 从 `Context::GetVkImage` 填入，`format` 从 `Context::GetFormat` 填入，然后 `Context::CreateImageView` 创建 View。

```cpp
VkImageViewCreateInfo viewInfo{};
viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
viewInfo.subresourceRange.baseMipLevel = 0;
viewInfo.subresourceRange.levelCount = 1;
viewInfo.subresourceRange.baseArrayLayer = 0;
viewInfo.subresourceRange.layerCount = 1;

binding.BindTexture2DSRV("albedo", albedoRID, viewInfo)
       .BindTexture2DUAV("storage", storageRID, viewInfo);
```

- **分配索引**：`DescriptorSlot::WriteTextureDescriptor` 先复用数组里的空位，没有空位就追加。SampledImage 写入 binding 1，layout 为 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`；StorageImage 写入 binding 2，layout 为 `VK_IMAGE_LAYOUT_GENERAL`。数组满时抛异常。返回的索引写入该字段的 Uniform 副本。
- **描述符立即更新**：数组带 `UPDATE_AFTER_BIND`，`Bind*` 里直接 `vkUpdateDescriptorSets`。Shader 要读到新索引，仍要等这次 `Push` 把 UniformBuffer 上传。
- **重绑**：同一字段再次绑定时，旧 View 进入当前飞行帧的回收桶。飞行中的命令仍可通过旧索引访问它。
- **回收**：`DescriptorSlot::Update` 在这一帧的 fence 已经等到之后调用，清掉当前飞行帧桶里的槽位，索引可以立刻复用，View 再交给 `Context::DeleteResource`。Binding 释放时，名下还绑着的 View 同样进桶。`Destroy` 时数组里仍占用的 View 也交给 `Context::DeleteResource`。
