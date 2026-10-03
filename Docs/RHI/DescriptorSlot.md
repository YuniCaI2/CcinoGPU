# Descriptor Slot 设计思路


### 简介
DescriptorSlot 是 Ccino 绑定层的消费端，基于 `VK_EXT_descriptor_heap` 实现 Bindless。

- 所有 Buffer / Texture 的 Descriptor 写在一个全局的 ResourceHeap，Sampler 写在 SamplerHeap。每个资源（RID）在堆里只占一个槽位，所有 Binding 共享。
- 一次 Dispatch / Draw 要用哪些资源，由一个 `DescriptorSlotBinding` 描述。它把 "name → 资源" 翻译成 "字段 → 堆数组索引"，写进 SlotBuffer 里的一个块。
- PushData 只推送这个块的设备地址（8 字节）。管线不需要知道布局，Shader 自己从块里读索引，再去访问堆。

```
vkCmdPushDataEXT: PushData[0..8) = Slot 块的设备地址
        │
        ▼
SlotBuffer 中的一个块（256 字节）
  [0] params = 0  ──►  heapParams[0]（ResourceHeap 中的 UBO Descriptor）  ──►  UBO
  [1] data   = 1  ──►  heapData[1]  （ResourceHeap 中的 SSBO Descriptor） ──►  SSBO
```


### 内存布局
三块内存都在 `Init` 时按上限一次分配好，之后只在里面分配槽位。

ResourceHeap（起点按 `resourceHeapAlignment` 对齐）：
```
0                          m_textureOffset             reservedRangeOffset
| Buffer 段                 | Image 段                   | 驱动保留区                     |
| 1024 × bufferDescriptorSize | 1024 × imageDescriptorSize | minResourceHeapReservedRange |
```
- Image 段的起点取整到 `imageDescriptorSize` 的整数倍，保证索引能落在数组元素上。
- 保留区的偏移按 Buffer / Image 两种 Descriptor 的对齐取整，应用侧不能写入。

SamplerHeap（起点按 `samplerHeapAlignment` 对齐）：所有 `SamplerType` 在 `Init` 时预先创建，后面是 `minSamplerHeapReservedRange` 大小的保留区。

SlotBuffer：`DESCRIPTOR_MAX_SLOT_COUNT`（1024）个 `DESCRIPTOR_SLOT_BLOCK_SIZE`（256 字节）的块，每个块最多 64 个 `uint32_t` 字段。

Shader 里每个 `descriptor_heap` 数组都从堆起点开始，步长是对应类型的 DescriptorSize，所以块里存的是**索引**，不是字节偏移：
- Buffer：Buffer 段内的下标（Buffer 段从 0 开始）。
- Texture：`m_textureOffset / imageDescriptorSize` 加上 Image 段内的下标。
- Sampler：`SamplerType` 的枚举值。


### 布局
```cpp
struct Descriptor {
    VkDescriptorType type;
};

using DescriptorSlotLayout = std::vector<std::pair<std::string, Descriptor>>;
```

- 第 i 个字段位于块内 `i * sizeof(uint32_t)`，对应 Shader 里 Slot 结构体的第 i 个 `uint` 成员。顺序是 CPU 和 Shader 之间唯一的约定，由 Ccino 编译后同时生成两边，不是用户声明的样子。
- 支持的类型：`UNIFORM_BUFFER`、`STORAGE_BUFFER`、`SAMPLED_IMAGE`、`STORAGE_IMAGE`、`SAMPLER`。不支持 `COMBINED_IMAGE_SAMPLER`，纹理和 Sampler 要拆成两个字段。
- 最多 64 个字段。
- 布局不和资源挂钩，只是一个类似 Template 的结构。Binding 创建时会拷贝一份。


### Shader 侧约定
```glsl
#version 460
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

layout(descriptor_heap) uniform Params {
    uint multiplier;
} heapParams[];

layout(descriptor_heap) buffer Data {
    uint values[];
} heapData[];

//和DescriptorSlotLayout 的字段顺序一致, 每个成员是对应 descriptor_heap 数组的索引
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer Slot {
    uint params;
    uint data;
};

layout(push_constant) uniform PushData {
    Slot slot;
} pushData;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    Slot slot = pushData.slot;
    heapData[slot.data].values[idx] = idx * heapParams[slot.params].multiplier;
}
```

- Slot 结构体用 std430，成员写成独立的 `uint`。不要写成 std140 下的 `uint` 数组，std140 会把数组步长抬到 16 字节。
- 块按 256 字节排列，`buffer_reference_align` 写 16 即可。
- `push_constant` 的第一个成员必须是 Slot 指针。自己的常量放在偏移 8 之后，用另一次 `vkCmdPushDataEXT` 推送。
- 设备需要开启 `VK_EXT_descriptor_heap` 和 `VK_KHR_shader_untyped_pointers`（`Core` 已经开启）。管线要用 `VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT` 创建，不带 PipelineLayout（`ComputePipeline` 已经处理）。
- 完整示例见 `Tests/Shaders/compute_pipeline_test.comp`，修改后用 `glslc --target-env=vulkan1.4` 重新生成同目录下的 spv。


### 使用流程
```cpp
DescriptorSlot descriptorSlot;
descriptorSlot.Init(&context);

const DescriptorSlotLayout layout = {
    {"params", {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}},
    {"data", {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
};

DescriptorSlotBinding binding = descriptorSlot.CreateSlotBinding(layout);
binding.BindBuffer("params", ubo).BindBuffer("data", ssbo);

//录制
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
descriptorSlot.BindHeaps(cmd); //每个命令缓冲绑定一次
binding.Push(cmd);             //之后的Dispatch 使用这个Binding
vkCmdDispatch(cmd, 8, 1, 1);

//每帧开始、等完这一帧的fence 之后
descriptorSlot.Update();

//资源销毁前
descriptorSlot.ReleaseBuffer(ssbo);
context.DestroyBuffer(ssbo);

//所有Binding 析构之后
descriptorSlot.Destroy();
```

同一个命令缓冲里可以先后 `Push` 多个 Binding，每次 `Push` 之后的 Dispatch 使用对应的块。完整示例见 `Tests/test_compute_pipeline.cpp`。


### 生命周期与延迟释放
- **堆槽位（跟着资源走）**：资源第一次被 `Bind*` 时写入 Descriptor，之后所有 Binding 复用同一个索引。`ReleaseBuffer` / `ReleaseTexture` 把槽位放进当前飞行帧的待释放桶。
- **Slot 块（跟着 Binding 走）**：`Bind*` 只修改 CPU 端的副本并标记 dirty。`Push` 时如果 dirty，就分配一个新块写入，旧块放进待释放桶。已经 Push 过的块不会再被改写，所以 GPU 正在读的内容不会变化。Binding 析构时，当前块也进入待释放桶。
- **延迟释放**：待释放的块和槽位按飞行帧分桶，`Update()` 切到下一帧时，把轮到的那个桶放回空闲队列。第 F 帧释放的东西要经过 N 次 `Update`（N 为飞行帧数）才会被复用；飞行帧为 1 时，也要等到下一次 `Update`。前提是 `Update` 在等完对应 fence 之后才调用。
- **Binding**：只能移动不能拷贝，自己持有一份布局。`DescriptorSlot::Destroy` 时如果还有 Binding 存活，会抛出异常。


### 线程安全
- `CreateSlotBinding`、`Bind*`、`Push`、`ReleaseBuffer` / `ReleaseTexture` 可以在多个线程同时调用：块池由 `m_slotMutex` 保护，堆槽位的登记由 `m_heapMutex` 保护。
- 同一个 Binding 同一时间只能被一个线程使用。
- `Update` 必须在帧边界调用，不能和录制并发。


### 容量与限制
- Buffer 和 Texture 各最多 1024 个（`DESCRIPTOR_MAX_BUFFER_COUNT` / `DESCRIPTOR_MAX_IMAGE_COUNT`），Slot 块最多 1024 个，包括等待释放的块。
- 纹理 Descriptor 的写入还是 TODO，要等 Context 支持创建纹理；不同的 `subresourceRange` 需要各自的 Descriptor。
- `ReleaseBuffer` / `ReleaseTexture` 要由销毁资源的一方调用，`Context::DestroyBuffer` 不会自动通知 DescriptorSlot。资源释放之后，不能再 Push 还引用它的 Binding。
