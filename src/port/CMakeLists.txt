cmake_minimum_required(VERSION 3.20)
project(StairfaxTemperatures C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- Backend toggles -------------------------------------------------------
option(STAIRFAX_RHI_VULKAN "Enable Vulkan backend (primary)"                 ON)
option(STAIRFAX_RHI_D3D12  "Enable D3D12 backend (primary on Windows)"        ON)
option(STAIRFAX_RHI_D3D11  "Enable D3D11 backend (best-effort fallback)"      ON)
option(STAIRFAX_RHI_GL     "Enable GL/GLES backend (best-effort, Android)"    ON)
option(STAIRFAX_BUILD_GAME "Compile the game/shim TUs (off until shims land)" OFF)

set(RHI_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(WIN32)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX)
endif()

# --- Host platform layer (window/input glue) -------------------------------
add_library(stairfax_platform STATIC src/platform/plat_window_win32.c)
target_include_directories(stairfax_platform PUBLIC ${RHI_INCLUDE})

# --- Backend libraries -----------------------------------------------------
# Each renderer is its own static library. rhi_common (below) selects among them
# through the vtable; every backend provides rhi_<name>_available/create so the
# selector always links, even for a disabled/stubbed backend.

# D3D11 (real)
add_library(rhi_d3d11 STATIC src/renderer/rhi_d3d11.cpp)
target_include_directories(rhi_d3d11 PUBLIC ${RHI_INCLUDE})
target_link_libraries(rhi_d3d11 PRIVATE d3d11 dxgi)

# D3D12 (real)
add_library(rhi_d3d12 STATIC src/renderer/rhi_d3d12.cpp)
target_include_directories(rhi_d3d12 PUBLIC ${RHI_INCLUDE})
target_link_libraries(rhi_d3d12 PRIVATE d3d12 dxgi)

# Vulkan: real if the SDK headers are found, otherwise a link-satisfying stub.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_library(rhi_vulkan STATIC src/renderer/rhi_vulkan.cpp)
    target_compile_definitions(rhi_vulkan PRIVATE STAIRFAX_HAVE_VULKAN=1 VK_USE_PLATFORM_WIN32_KHR=1)
    target_include_directories(rhi_vulkan PRIVATE ${Vulkan_INCLUDE_DIRS})
    message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS} (dynamic loader, no link)")
else()
    add_library(rhi_vulkan STATIC src/renderer/rhi_vulkan.c)
    message(STATUS "Vulkan SDK NOT found - vulkan backend stubbed (install LunarG SDK to enable)")
endif()
target_include_directories(rhi_vulkan PUBLIC ${RHI_INCLUDE})

# GL (stub for now)
add_library(rhi_gl STATIC src/renderer/rhi_gl.c)
target_include_directories(rhi_gl PUBLIC ${RHI_INCLUDE})

# --- Common groundwork -----------------------------------------------------
# Owns backend selection + vtable dispatch; depends on the backend libs.
add_library(rhi_common STATIC src/renderer/rhi.c)
target_include_directories(rhi_common PUBLIC ${RHI_INCLUDE})
target_link_libraries(rhi_common PUBLIC rhi_d3d11 rhi_d3d12 rhi_vulkan rhi_gl)

# --- VI shim (video interface -> RHI + window) -----------------------------
add_library(stairfax_vi STATIC src/vi_shim.c)
target_include_directories(stairfax_vi PUBLIC ${RHI_INCLUDE})
target_link_libraries(stairfax_vi PUBLIC rhi_common stairfax_platform)

# --- DVD/fileio shim (disc reads -> host filesystem) -----------------------
add_library(stairfax_dvd STATIC src/dvd_shim.c)
target_include_directories(stairfax_dvd PUBLIC ${RHI_INCLUDE})

# --- Smoketests ------------------------------------------------------------
add_executable(rhi_smoketest src/tools/rhi_smoketest.c)
target_include_directories(rhi_smoketest PRIVATE ${RHI_INCLUDE})
target_link_libraries(rhi_smoketest PRIVATE rhi_common stairfax_platform)

add_executable(vi_smoketest src/tools/vi_smoketest.c)
target_include_directories(vi_smoketest PRIVATE ${RHI_INCLUDE})
target_link_libraries(vi_smoketest PRIVATE stairfax_vi)

add_executable(dvd_smoketest src/tools/dvd_smoketest.c)
target_include_directories(dvd_smoketest PRIVATE ${RHI_INCLUDE})
target_link_libraries(dvd_smoketest PRIVATE stairfax_dvd)

if(MSVC)
    target_compile_options(rhi_d3d11 PRIVATE /W3)
    target_compile_options(rhi_common PRIVATE /W3)
    target_compile_options(rhi_smoketest PRIVATE /W3)
endif()

message(STATUS "StairfaxTemperatures RHI configured")
message(STATUS "  backends: VK=${STAIRFAX_RHI_VULKAN} D3D12=${STAIRFAX_RHI_D3D12} D3D11=${STAIRFAX_RHI_D3D11} GL=${STAIRFAX_RHI_GL}")
message(STATUS "  target:   rhi_smoketest  (run: rhi_smoketest --gfx d3d11|d3d12|vk)")
