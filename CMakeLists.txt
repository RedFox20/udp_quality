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

add_executable(udp_quality udp_quality.cpp simple_udp.cpp)
target_link_libraries(udp_quality ${MAMA_LIBS} Threads::Threads)
if(OCLEA)
    set(EXE_OUT bin/cv25)
elseif(MIPS)
    set(EXE_OUT bin/mips)
else()
    set(EXE_OUT bin)
endif()
install(TARGETS udp_quality DESTINATION bin)
