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

if(OCLEA)
    set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--as-needed")
    set(CMAKE_EXE_LINKER_FLAGS "-Wl,--as-needed")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

    include(/opt/oclea/1.0/oclea_yocto.cmake)

    set(THIRDPARTY_LIBS
        ${LIBDW_DEPS} # required by ReCpp
    )
endif()

message(STATUS "BINARY_DIR: ${CMAKE_BINARY_DIR}")
add_executable(udp_quality main_udp_quality.cpp simple_udp.cpp)
target_link_libraries(udp_quality ${MAMA_LIBS} ${THIRDPARTY_LIBS} Threads::Threads)
install(TARGETS udp_quality DESTINATION bin)
