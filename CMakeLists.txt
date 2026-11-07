cmake_minimum_required(VERSION 3.16)
project(mini-container-runtime C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # we need GNU extensions for clone(), pivot_root, etc.

add_compile_options(-Wall -Wextra -Wpedantic -Wno-gnu-zero-variadic-macro-arguments)

include_directories(${CMAKE_SOURCE_DIR}/include)

# Everything except main.c goes into a static library so tests can link it.
file(GLOB MCR_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM MCR_SOURCES ${CMAKE_SOURCE_DIR}/src/main.c)

add_executable(mcr src/main.c)

if(MCR_SOURCES)
    add_library(mcr_core STATIC ${MCR_SOURCES})
    target_link_libraries(mcr mcr_core)
endif()

# Unit tests (run with: ctest --test-dir build --output-on-failure)
enable_testing()
foreach(t test_cli test_util test_cgroup)
    if(EXISTS ${CMAKE_SOURCE_DIR}/tests/${t}.c)
        add_executable(${t} tests/${t}.c)
        target_link_libraries(${t} mcr_core)
        target_include_directories(${t} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
        add_test(NAME ${t} COMMAND ${t})
    endif()
endforeach()
