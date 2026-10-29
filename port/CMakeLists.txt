cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "")
endif()

project(sh4vita C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Silent Hill 4: The Room")
set(VITA_TITLEID  "SILH00004")
set(VITA_VERSION "00.01" CACHE STRING "VPK application version (keep the title ID unchanged)")

# C code generated from the player's own exe (port/gen/, never versioned): run recomp/x86rec.py first.
file(GLOB GEN_SRC ${CMAKE_SOURCE_DIR}/gen/*.c)
file(GLOB RT_SRC
  src/rt/*.c src/win32/*.c src/d3d8/*.c src/dx/*.c src/vita/*.c src/third_party/tlsf/tlsf.c)

# Vita RAM is tight: guest memory (20 MB image + game heap) and host heap.
if(NOT DEFINED SH4_GUEST_MB)
  set(SH4_GUEST_MB 200)
endif()
# Host heap: the malloc arena only uses ~5 MB in game (measured); the rest of the reserved block was lost to the guest heap.
set(SH4_HOST_HEAP_MB 24)
if(NOT DEFINED SH4_VGL_RAM_MB)
  set(SH4_VGL_RAM_MB 32) # RAM taken by vitaGL; SH4_SPARE_MB stays free for SDL and the threads
endif()
set(SH4_SPARE_MB 12)

add_executable(sh4 ${GEN_SRC} ${RT_SRC})
option(SH4_TEST_OPTIMIZATIONS "Enable qualified experimental optimizations in a tester VPK" OFF)
if(SH4_TEST_OPTIMIZATIONS)
  target_compile_definitions(sh4 PRIVATE SH4_TEST_OPTIMIZATIONS=1)
endif()
option(SH4_TEST_QUALITY_512 "Enable combined 512 texture qualification profile (requires corrected isolated vitaGL)" OFF)
if(SH4_TEST_QUALITY_512)
  if(NOT SH4_TEST_OPTIMIZATIONS)
    message(FATAL_ERROR "The combined quality profile requires SH4_TEST_OPTIMIZATIONS")
  endif()
  target_compile_definitions(sh4 PRIVATE SH4_TEST_QUALITY_512=1)
endif()
target_include_directories(sh4 PRIVATE src src/rt src/d3d8 gen "$ENV{VITASDK}/arm-vita-eabi/include/SDL2")
target_compile_definitions(sh4 PRIVATE
  SH4_GUEST_MB=${SH4_GUEST_MB} SH4_HOST_HEAP_MB=${SH4_HOST_HEAP_MB} SH4_VGL_RAM_MB=${SH4_VGL_RAM_MB} SH4_SPARE_MB=${SH4_SPARE_MB}
  SH4_LOG_PATH="ux0:data/sh4/sh4log.txt")
target_compile_options(sh4 PRIVATE -O2
  -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable -Wno-int-conversion)
# Generated code: -Os by default (measured -3 ms/frame in the hospital vs -O2; the guest code is huge -> I-cache footprint). -DSH4_GEN_FLAGS="" for -O2.
set(SH4_GEN_FLAGS "-Os" CACHE STRING "extra compile flags for generated guest code")
# Hot functions (tools/hotlist.py -> gen/x86_hot.h): __attribute__((hot)) -> contiguous. Measured: no gain (hospital 37.5 ms with or without) -> only with -DSH4_HOT=ON.
if(EXISTS ${CMAKE_SOURCE_DIR}/gen/x86_hot.h AND SH4_HOT)
  set_property(SOURCE ${GEN_SRC} APPEND PROPERTY COMPILE_OPTIONS -include ${CMAKE_SOURCE_DIR}/gen/x86_hot.h)
endif()
if(SH4_GEN_FLAGS)
  separate_arguments(SH4_GEN_FLAGS_LIST UNIX_COMMAND "${SH4_GEN_FLAGS}")
  set_property(SOURCE ${GEN_SRC} APPEND PROPERTY COMPILE_OPTIONS ${SH4_GEN_FLAGS_LIST})
endif()
# Movie (pl_mpeg + NEON conversion): hot decoding loop, -O3.
set_source_files_properties(src/dx/movie.c PROPERTIES COMPILE_OPTIONS "-O3")

set(SH4_VITAGL_LIBRARY "vitaGL" CACHE STRING "vitaGL library (build with NO_DEBUG=1 STORE_DEPTH_STENCIL=1)")
target_link_libraries(sh4
  ${SH4_VITAGL_LIBRARY} vitashark SceShaccCgExt mathneon taihen_stub
  SDL2
  SceDisplay_stub SceCtrl_stub SceAudio_stub SceAudioIn_stub SceSysmodule_stub SceGxm_stub
  SceCommonDialog_stub SceAppMgr_stub SceTouch_stub SceHid_stub SceMotion_stub SceKernelDmacMgr_stub
  SceShaccCg_stub SceIofilemgr_stub SceLibKernel_stub SceKernelThreadMgr_stub SceKernelModulemgr_stub
  ScePower_stub SceProcessmgr_stub SceRtc_stub SceIme_stub SceJpeg_stub
  stdc++ m z)

target_link_options(sh4 PRIVATE -Wl,--allow-multiple-definition)
set(SH4_SCE_PADDING "${CMAKE_CURRENT_SOURCE_DIR}/src/vita/sce-padding.ld")
target_link_options(sh4 PRIVATE "LINKER:-T,${SH4_SCE_PADDING}")
set_property(TARGET sh4 APPEND PROPERTY LINK_DEPENDS "${SH4_SCE_PADDING}")
vita_create_self(eboot.bin sh4 UNSAFE)
set(SH4_SHADER_CACHE_DIR "${CMAKE_SOURCE_DIR}/../build/shader-cache" CACHE PATH "validated compiled GPU cache bundle")
option(SH4_REQUIRE_SHADER_CACHE "require a compiled cache bundle for a release package" OFF)
set(SH4_CACHE_VPK_FILES)
if(EXISTS "${SH4_SHADER_CACHE_DIR}/manifest.json")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(SH4_CACHE_TOOL "${CMAKE_SOURCE_DIR}/../tools/shader_cache_bundle.py")
  set(SH4_CACHE_HEADER "${CMAKE_SOURCE_DIR}/src/d3d8/shader_cache_version.h")
  file(STRINGS "${SH4_CACHE_HEADER}" SH4_CACHE_ABI_LINE REGEX "^#define SH4_SHADER_CACHE_ABI")
  string(REGEX REPLACE ".*\"([a-z0-9]+)\".*" "\\1" SH4_CACHE_ABI "${SH4_CACHE_ABI_LINE}")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${SH4_CACHE_TOOL}" validate "${SH4_SHADER_CACHE_DIR}"
    --cmake-list "${CMAKE_BINARY_DIR}/shader-cache-files.cmake" RESULT_VARIABLE SH4_CACHE_RESULT)
  if(NOT SH4_CACHE_RESULT EQUAL 0)
    message(FATAL_ERROR "Compiled shader bundle validation failed")
  endif()
  include("${CMAKE_BINARY_DIR}/shader-cache-files.cmake")
  foreach(SH4_CACHE_FILE IN LISTS SH4_BUNDLED_CACHE_FILES)
    list(APPEND SH4_CACHE_VPK_FILES FILE "${SH4_SHADER_CACHE_DIR}/${SH4_CACHE_FILE}" "shadercache/${SH4_CACHE_ABI}/${SH4_CACHE_FILE}")
  endforeach()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SH4_SHADER_CACHE_DIR}/manifest.json" "${SH4_CACHE_TOOL}" "${SH4_CACHE_HEADER}")
  add_custom_target(sh4-validate-shader-cache
    COMMAND "${Python3_EXECUTABLE}" "${SH4_CACHE_TOOL}" validate "${SH4_SHADER_CACHE_DIR}")
  add_dependencies(sh4 sh4-validate-shader-cache)
elseif(SH4_REQUIRE_SHADER_CACHE)
  message(FATAL_ERROR "A release package requires SH4_SHADER_CACHE_DIR with a validated cache bundle")
endif()
vita_create_vpk(sh4.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE livearea/icon0.png sce_sys/icon0.png
  FILE livearea/pic0.png sce_sys/pic0.png
  FILE livearea/bg.png sce_sys/livearea/contents/bg.png
  FILE livearea/startup.png sce_sys/livearea/contents/startup.png
  FILE livearea/template.xml sce_sys/livearea/contents/template.xml
  FILE assets/controls/controls.tga ui/controls.tga
  FILE assets/controls/README.md ui/CONTROLLER-ART-CREDITS.md
  FILE assets/controls/Kenney-License.txt ui/Kenney-License.txt
  ${SH4_CACHE_VPK_FILES})

# Diagnostics: ring of the last guest functions (cmake -DSH4_TRACE_CALLS=ON), printed before a FATAL.
option(SH4_TRACE_CALLS "ring buffer of recent guest function entries" OFF)
if(SH4_TRACE_CALLS)
  set_property(SOURCE ${GEN_SRC} APPEND PROPERTY COMPILE_OPTIONS
    -finstrument-functions -finstrument-functions-exclude-file-list=rt.h)
  target_compile_definitions(sh4 PRIVATE SH4_TRACE_CALLS=1)
endif()

# Sampled profile of the guest code (cmake -DSH4_PROFILE=ON, env SH4_PROFILE=start,end); see src/rt/calltrace.c.
option(SH4_PROFILE "sampling profiler of guest functions" OFF)
if(SH4_PROFILE)
  # Preserve the release guest optimization/layout flags when profiling.
  set_property(SOURCE ${GEN_SRC} APPEND PROPERTY COMPILE_OPTIONS
    -finstrument-functions -finstrument-functions-exclude-file-list=rt.h)
  target_compile_definitions(sh4 PRIVATE SH4_PROFILE=1)
endif()
