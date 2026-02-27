cmake_minimum_required(VERSION 3.12)
project(udp_quality)

# Include all mama dependencies via ${MAMA_INCLUDES} and ${MAMA_LIBS}
# For each dependency there will also be ${SomeLibrary_LIBS} (case-sensitive)
include(mama.cmake)
include_directories(${MAMA_INCLUDES})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clang-tidy
find_package(Threads REQUIRED)

if((CLANG OR GCC) AND (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64"))
    # -march=x86-64-v3: (close to Haswell) AVX, AVX2, BMI1, BMI2, F16C, FMA, LZCNT, MOVBE, XSAVE
    # -march=x86-64-v4: AVX512F, AVX512BW, AVX512CD, AVX512DQ, AVX512VL (generally not available on VM-s in 2023)
    # NOTE: on Alder Lake and later, AVX512 is no longer supported! So to support broken Intel CPU-s we cannot have AVX512
    add_compile_options(-march=x86-64-v3)
    message(STATUS "BUGFIX: Using -march=x86-64-v3 for backwards compatibility")
endif()

if(YOCTO_LINUX)
    set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--as-needed")
    set(CMAKE_EXE_LINKER_FLAGS "-Wl,--as-needed")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

if(OCLEA)
    include(/opt/oclea/1.0/oclea_yocto.cmake)
endif()

message(STATUS "BINARY_DIR: ${CMAKE_BINARY_DIR}")
add_executable(udp_quality main_udp_quality.cpp simple_udp.cpp)
target_link_libraries(udp_quality ${MAMA_LIBS} ${THIRDPARTY_LIBS} Threads::Threads)
install(TARGETS udp_quality DESTINATION bin)
