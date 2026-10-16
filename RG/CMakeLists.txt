file(GLOB_RECURSE RGSources "*.h" "*.cpp")

add_library(RG STATIC ${RGSources})

target_include_directories(RG PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(RG PUBLIC RHI Core)

target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})