cmake_minimum_required(VERSION 3.16)

project(EmbeddedCCompiler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF) # strict -std=c11, no GNU extensions

# Build with AddressSanitizer + UndefinedBehaviorSanitizer:
#   cmake -S . -B build -DENABLE_SANITIZERS=ON
option(ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Compiler library: every src/*.c except the HTTP server.
# Teammates just drop their files into src/ - no edit needed here.
# ---------------------------------------------------------------------------
file(GLOB COMPILER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
list(FILTER COMPILER_SOURCES EXCLUDE REGEX ".*/server\\.c$")

add_library(compiler STATIC ${COMPILER_SOURCES})
target_include_directories(compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(compiler PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Command-line demo: prints tokens / AST / JSON for a source file.
# ---------------------------------------------------------------------------
add_executable(frontend_demo tools/frontend_demo.c)
target_link_libraries(frontend_demo PRIVATE compiler)
target_compile_options(frontend_demo PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Tests: every tests/*_tests.c becomes its own executable and a CTest test.
#   cd build && ctest --output-on-failure
# ---------------------------------------------------------------------------
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*_tests.c)
foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE compiler)
    target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# ---------------------------------------------------------------------------
# REST server (Person 3): enabled automatically once src/server.c exists and
# cJSON + Mongoose have been added. Left as a hook so this file keeps working.
# ---------------------------------------------------------------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/server.c)
    message(STATUS "src/server.c found - add the cJSON/Mongoose setup for compiler_server here")
endif()
