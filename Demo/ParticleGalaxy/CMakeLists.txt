add_executable(ParticleGalaxy main.cpp)
# SDL3 用于窗口事件轮询; RHI 对 SDL3 是 PRIVATE 依赖, 头文件不会传递过来
target_link_libraries(ParticleGalaxy PRIVATE Context RHI RG SDL3::SDL3-static)
target_include_directories(ParticleGalaxy PRIVATE ${CMAKE_SOURCE_DIR})

# 着色器由 GeneratorMain 扫描本文件中的 AddCS / AddVSAndFS 生成, 运行前必须先生成
add_dependencies(ParticleGalaxy GenerateShaderIR)
