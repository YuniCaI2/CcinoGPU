file(GLOB_RECURSE CoreSources "*.h" "*.cpp")

add_library(Core STATIC ${CoreSources})

target_include_directories(Core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Core PUBLIC glm::glm)
target_link_libraries(Core PUBLIC RHI)

target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})