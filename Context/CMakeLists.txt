
file(GLOB_RECURSE RGSources "*.h" "*.cpp")

add_library(Context STATIC ${RGSources})

target_include_directories(Context PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Context PUBLIC RHI Core RG)

target_include_directories(Context PUBLIC ${CMAKE_SOURCE_DIR})