cmake_minimum_required (VERSION 3.30)

project ("Sunaba")

set(CMAKE_CXX_STANDARD 20)

# skip creation of PACKAGE and INSTALL targets
set(CMAKE_SKIP_INSTALL_RULES True)
set(CMAKE_SKIP_PACKAGE_ALL_DEPENDENCY True)
set_property(GLOBAL PROPERTY USE_FOLDERS True)
# disables linking of Vulkan SDK through find_package; we want to use volk
set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan True)

# dependency directories
set (VOLK_DIR third_party/vulkan_deps/volk)
set (VKB_DIR third_party/vulkan_deps/vkbootstrap/src)
set (GLM_DIR third_party/glm) # glm adds itself as an include directory
set (SDL_DIR third_party/SDL)

# include directories
set (VOLK_INCLUDE_DIR third_party/vulkan_deps/volk)
set (VKB_INCLUDE_DIR third_party/vulkan_deps/vkbootstrap/src)
set (VMA_INCLUDE_DIR third_party/vulkan_deps/vma/include) # vma is a header only library
set (VULKAN_INCLUDE_DIR third_party/vulkan_deps/vulkan_headers/include) # volk does the pointing to GPU commands; we only need the vulkan headers
set (SDL_INCLUDE_DIR third_party/SDL/include)

add_subdirectory(${GLM_DIR})
add_subdirectory(${SDL_DIR})

# Volk setup
add_library(Volk STATIC ${VOLK_DIR}/volk.c)
target_include_directories(Volk PUBLIC ${VOLK_DIR} ${VULKAN_INCLUDE_DIR})
target_compile_definitions(Volk PRIVATE VK_KHR_win32_surface VK_USE_PLATFORM_WIN32_KHR)

# VKBootstrap setup
set (VKB_SOURCES
    ${VKB_DIR}/VkBootstrap.cpp)
add_library(Vkbootstrap ${VKB_SOURCES})
target_include_directories(Vkbootstrap PRIVATE ${VKB_DIR} ${VULKAN_INCLUDE_DIR}) # VkBootstrap uses vk_video

# IMGUI setup
set (IMGUI_DIR third_party/imgui)
set (IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
set (IMGUI_BACKEND_SOURCES
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
add_library (Imgui ${IMGUI_SOURCES} ${IMGUI_BACKEND_SOURCES})
target_include_directories(Imgui PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends ${VOLK_DIR} ${SDL_INCLUDE_DIR} ${VULKAN_INCLUDE_DIR})
target_compile_definitions(Imgui PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK VK_USE_PLATFORM_WIN32_KHR)

# Group targets under a folder
function(assign_folder_to_targets_in_dir folder_name)
    set(all_targets "")
    foreach(dir ${ARGN})
        get_property(dir_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
        list(APPEND all_targets ${dir_targets})
    endforeach()

    foreach(target ${all_targets})
        set_target_properties(${target} PROPERTIES FOLDER ${folder_name})
    endforeach()
endfunction()

# Capture all current targets (defined after adding the subdirectory)
assign_folder_to_targets_in_dir(Dependencies ${GLM_DIR}/glm ${SDL_DIR})
set_target_properties(Imgui PROPERTIES FOLDER Dependencies)
set_target_properties(Vkbootstrap PROPERTIES FOLDER Dependencies)
set_target_properties(Volk PROPERTIES FOLDER Dependencies)

add_subdirectory(src)

if (MSVC)
    set_property (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Sunaba)
endif()

