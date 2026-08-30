cmake_minimum_required(VERSION 3.20)
project(StairfaxTemperatures C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The game freely stores pointers in u32 fields (e.g. gResourceFileBuffers[id] =
# (u32)mmAlloc(...)), so the port MUST be built 32-bit or every stored pointer is
# silently truncated. Fail loudly rather than miscompile.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "Stairfax port must be built 32-bit (x86): the game stores pointers in u32 "
        "fields. Configure with an x86 toolchain (vcvarsall x64_x86) or -A Win32.")
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
target_link_libraries(rhi_d3d11 PRIVATE d3d11 dxgi d3dcompiler)

# D3D12 (real)
add_library(rhi_d3d12 STATIC src/renderer/rhi_d3d12.cpp)
target_include_directories(rhi_d3d12 PUBLIC ${RHI_INCLUDE})
target_link_libraries(rhi_d3d12 PRIVATE d3d12 dxgi d3dcompiler)

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

# --- GX shim (GX -> RHI draw path) -----------------------------------------
add_library(stairfax_gx STATIC src/gx_shim.c src/tex_decode.c)
target_include_directories(stairfax_gx PUBLIC ${RHI_INCLUDE})
target_link_libraries(stairfax_gx PUBLIC rhi_common)

# --- DVD/fileio shim (disc reads -> host filesystem) -----------------------
add_library(stairfax_dvd STATIC src/dvd_shim.c)
target_include_directories(stairfax_dvd PUBLIC ${RHI_INCLUDE})

# Include layout + defines for compiling GAME code (and shims that shadow dolphin
# headers). Order is deliberate:
#   1. src/port/include  - host shadows (dolphin/os.h etc.) win first
#   2. MSVC system dirs  - so <string.h>/<stddef.h>/<stdlib.h> resolve to the real
#                          libc, NOT the repo's freestanding stubs (meant for MWCC)
#   3. repo include/     - provides dolphin/*, main/*, types.h (non-libc)
#   4. repo root
# Without (2) the repo's freestanding libc headers shadow the system ones and break
# size_t / malloc declarations. This only affects the host build; MWCC is untouched.
set(MSVC_SYS_INCLUDES "")
if(MSVC AND DEFINED ENV{INCLUDE})
    file(TO_CMAKE_PATH "$ENV{INCLUDE}" MSVC_SYS_INCLUDES)
endif()
set(GAME_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/include        # src/port/include - shadows dolphin/*
    ${MSVC_SYS_INCLUDES}                         # real libc before repo stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include   # repo include/
    ${CMAKE_CURRENT_SOURCE_DIR}/../..           # repo root
)
set(GAME_DEFS TARGET_PC=1 STAIRFAX_PORT=1 _CRT_SECURE_NO_WARNINGS=1)

# --- OS + mm shims (Dolphin OS API + game memory manager) ------------------
add_library(stairfax_os STATIC src/os_shim.c src/mm_shim.c)
target_include_directories(stairfax_os PUBLIC ${GAME_INCLUDES})
target_compile_definitions(stairfax_os PUBLIC ${GAME_DEFS})

# --- First real game TU compiled on host (milestone / regression guard) -----
# OBJECT library: compiles objanim.c through the shadow headers. Not linked into an
# exe yet (it references sibling game TUs); compiling clean is the milestone.
add_library(game_objanim OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/../../src/main/objanim.c)
target_include_directories(game_objanim PRIVATE ${GAME_INCLUDES})
target_compile_definitions(game_objanim PRIVATE ${GAME_DEFS})

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

add_executable(disc_ls src/tools/disc_ls.c)
target_include_directories(disc_ls PRIVATE ${RHI_INCLUDE})
target_link_libraries(disc_ls PRIVATE stairfax_dvd)

add_executable(os_mm_smoketest src/tools/os_mm_smoketest.c)
target_include_directories(os_mm_smoketest PRIVATE ${GAME_INCLUDES})
target_compile_definitions(os_mm_smoketest PRIVATE ${GAME_DEFS})
target_link_libraries(os_mm_smoketest PRIVATE stairfax_os)

add_executable(gx_smoketest src/tools/gx_smoketest.c)
target_include_directories(gx_smoketest PRIVATE ${RHI_INCLUDE})
target_link_libraries(gx_smoketest PRIVATE stairfax_gx stairfax_platform)

add_executable(gx_tex_smoketest src/tools/gx_tex_smoketest.c)
target_include_directories(gx_tex_smoketest PRIVATE ${RHI_INCLUDE})
target_link_libraries(gx_tex_smoketest PRIVATE stairfax_gx stairfax_platform)

if(MSVC)
    target_compile_options(rhi_d3d11 PRIVATE /W3)
    target_compile_options(rhi_common PRIVATE /W3)
    target_compile_options(rhi_smoketest PRIVATE /W3)
endif()

message(STATUS "StairfaxTemperatures RHI configured")
message(STATUS "  backends: VK=${STAIRFAX_RHI_VULKAN} D3D12=${STAIRFAX_RHI_D3D12} D3D11=${STAIRFAX_RHI_D3D11} GL=${STAIRFAX_RHI_GL}")
message(STATUS "  target:   rhi_smoketest  (run: rhi_smoketest --gfx d3d11|d3d12|vk)")
