cmake_minimum_required(VERSION 3.16)

project(Vix LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

file(GLOB_RECURSE VIX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

option(VIX_BUILD_CORE "Build core sources under src/" ON)

if(VIX_BUILD_CORE)
    if(NOT VIX_SOURCES)
        message(FATAL_ERROR "No source files found under src/")
    endif()

    list(LENGTH VIX_SOURCES VIX_SOURCE_COUNT)
    message(STATUS "VIX_BUILD_CORE=ON: building ${VIX_SOURCE_COUNT} source files from src/")

    add_library(vix_core STATIC ${VIX_SOURCES})

    target_include_directories(vix_core
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    if(MSVC)
        target_compile_options(vix_core PRIVATE /W4)
    else()
        target_compile_options(vix_core PRIVATE -Wall -Wextra -Wpedantic)
    endif()
else()
    message(STATUS "VIX_BUILD_CORE=OFF: configured without compiling src/ (use -DVIX_BUILD_CORE=ON)")
endif()
