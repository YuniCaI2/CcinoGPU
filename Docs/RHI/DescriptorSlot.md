# Descriptor Slot 设计思路


### 简介
DescriptorSlot 是对 Ccino的绑定层的进一步抽象。 它作为消费端。

name -> Offset / BDA (分别处理Texture 和 Image 并且其配套的Sample)
Offset 被打包成PushConstant推送到GPU。 Image的ResourceHeap 作为全局资源池。

### 布局
布局需要和着色器中的布局完全对齐。
其中布局决定了PushConstant的结构体形式。
当然此处的布局是Ccino编译后的结果，并不是用户所声明的样子。
布局不和资源挂钩，只是作为一个类似Template的结构。

```glsl
layout(push_constant) uniform PushConstants {
    UniformData uniformData; //UnfiromData的BDA; 
    //... 更多的BDA
    uint texture1; //Texture在Descriptor中的Offset
    //... 更多的offset
} pc;
```

同步到DescriptorSlot， 这里为了保证顺序的一致性,需要虚拟一个布局便于创建PushConstant:
其使用:
```cpp
struct Descriptor {
    VkDescriptorType type;
    uint64_t bda;
    uint32_t offset;
    //...更多meta信息
};

using DescriptorSlotLayout = std::vector<std::pair<std::string, Descriptor>>; 
```





