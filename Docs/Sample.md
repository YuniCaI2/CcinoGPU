# Sample

### 图形
```cpp
Context c(createInfo); //其中有一些特性指定比如飞行帧
auto rt = Ccino::CreateRenderTarget(width, height, format); //尽可能简单，因为Texture其实还有Mipmap Array 然后Depth（第三个维度）

std::array<Ccino::RenderTarget, 1> renderTargets = {rt}; //
std::vector<Vertex> vertexArray; //顶点例子 这个Vertex是有限的
std::vector<uint32_t> indexArray; //索引

//类型自动推导
Ccino::Buffer<BufferType::Device> vertexBuffer = vertexArray; //触发上传
Ccino::Buffer<BufferType::Device> indexBuffer = indexArray;
//Uniform

struct UniformData {
    uint32_t x;
    uint32_t y;
};

//Host不需要刷新
Ccino::Buffer<UniformData, BufferType::Host> uniformBuffer{};

//这里可以等待下
//vertexBuffer.flush(); //等待上传完毕
//indexBuffer.flush();
Ccino::FlushAll(vertexArray, indexArray);

c.AddPass(
[&](Builder& builder, RGPass& pass) {
    //声明一些常量或者资源句柄
    auto mvp = GetMVP(); //常量 会被打包到UniformBuffer
    //Buffer也可以在这里Flush.这个区域是每帧的RenderGraph的录制阶段
    Ccino::Accessor indexAc = builder.Read(pass, indexBuffer);
    Ccino::Accessor vertexAc = builder.Read(pass, vertexBuffer);

    Ccino::Vec4 color = {1.0, 0.0, 0.0, 1.0}; //这种常量会被直接优化内联，在我的编译阶段

    //这里忽略飞行帧数
    builder.AddVSAndPS(vertexNums ,renderTargets, [=](uint32_t frameIndex, uint32_t vertexIndex, VertexOut& out){
        auto index = indexAc[vertexIO];
        auto vertex = vertexAc[index];
        //MVP....
        out = Ccino::Dot(mvp, Ccino::Vec4{vertex, 1.0f});
        //return  ;//out 可以返回多个，会变成Tuple
    },

    //VS后必须接一个PS 不然报错
    //这里需要处理上游的Out,类型一定要对齐不然报错
    [=](uint32_t frameIndex, /*Out*/) {
        //渲染到renderTarget
        return {color}; //数量和RenderTarget 对齐
    }
    );
}
);

//直接释放，使用Delete才放到释放队列
vertexBuffer.Destroy();
indexBuffer.Destroy();

//AddPass 中可以Transient 对象 RDG托管

Ccino::Loop(
    Ccino::Run(c.GetFrameIndex());
    Ccino::Display(rt);
);
```

