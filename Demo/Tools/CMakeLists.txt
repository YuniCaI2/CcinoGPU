file(GLOB DEMO_TOOLS_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

add_library(DEMO_TOOLS STATIC ${DEMO_TOOLS_SOURCES})

target_link_libraries(DEMO_TOOLS PRIVATE tinygltf)
target_link_libraries(DEMO_TOOLS PUBLIC glm::glm)
target_include_directories(DEMO_TOOLS PUBLIC ${CMAKE_SOURCE_DIR})
