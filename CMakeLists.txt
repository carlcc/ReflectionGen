cmake_minimum_required(VERSION 3.20)
project(ReflectionGen)

add_subdirectory(Thirdparty/lua)

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE REFLECTION_GEN_SOURCE_CODE ReflectionGen/include/* ReflectionGen/src/*)


if (MSVC)
    add_compile_options("/bigobj")
endif()

add_executable(ReflectionGen ${REFLECTION_GEN_SOURCE_CODE})
target_link_libraries(ReflectionGen
PUBLIC
    lua
    clang
)