add_executable(OfflineTriangle main.cpp)
target_link_libraries(OfflineTriangle PRIVATE Context RHI RG)
target_include_directories(OfflineTriangle PRIVATE ${CMAKE_SOURCE_DIR})

# 着色器由 GeneratorMain 扫描本文件中的 AddVSAndFS 生成, 运行前必须先生成
add_dependencies(OfflineTriangle GenerateShaderIR)

target_include_directories(OfflineTriangle PUBLIC ${CMAKE_SOURCE_DIR})
