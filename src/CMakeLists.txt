# Gather all source files in the current directory
file (GLOB_RECURSE SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
)

file (GLOB_RECURSE HEADER_FILES
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
)

add_executable (Sunaba ${SOURCE_FILES} ${HEADER_FILES})
source_group("Header Files" FILES ${HEADER_FILES})
source_group("Source Files" FILES ${SOURCE_FILES})
set_target_properties (Sunaba PROPERTIES FOLDER "Source")
target_include_directories (Sunaba PRIVATE ../${VMA_INCLUDE_DIR} ../${SDL_INCLUDE_DIR} ../${IMGUI_DIR} ../${IMGUI_DIR}/backends ../${VOLK_INCLUDE_DIR} ../${VKB_INCLUDE_DIR})
target_compile_definitions(Sunaba PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(Sunaba
    PRIVATE SDL3-static Volk SDL_uclibc Imgui glm Vkbootstrap)
