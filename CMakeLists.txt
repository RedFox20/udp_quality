cmake_minimum_required(VERSION 3.12)
project(udp_quality)

# Include all mama dependencies via ${MAMA_INCLUDES} and ${MAMA_LIBS}
# For each dependency there will also be ${SomeLibrary_LIBS} (case-sensitive)
include(mama.cmake)
include_directories(${MAMA_INCLUDES})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clang-tidy
find_package(Threads REQUIRED)

if(OCLEA)
    set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--as-needed")
    set(CMAKE_EXE_LINKER_FLAGS "-Wl,--as-needed")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

    # TODO: Currently using DOCKER based hard-coded oclea.cmake
    # TODO: perhaps in the future we can have something modular
    include(/build/workspace-cv25/oclea.cmake)
    set(THIRDPARTY_LIBS
        ${LIBDW_DEPS} # required by ReCpp
    )
endif()

add_executable(udp_quality main_udp_quality.cpp simple_udp.cpp)
target_link_libraries(udp_quality ${MAMA_LIBS} ${THIRDPARTY_LIBS} Threads::Threads)
set_target_properties(udp_quality PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
install(TARGETS udp_quality DESTINATION bin)
