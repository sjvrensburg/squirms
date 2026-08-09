cmake_minimum_required(VERSION 3.13)
project(SquirmsWASM LANGUAGES CXX)

# CMAKE_TOOLCHAIN_FILE only takes effect if set *before* project(), so it can't
# be derived here. Configure with emcmake instead, which sets it on the command
# line ahead of time: `source emsdk/emsdk_env.sh && emcmake cmake -B build`.
# The Emscripten toolchain file defines the EMSCRIPTEN variable for us.
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This project must be configured with the Emscripten toolchain. Run:\n  source emsdk/emsdk_env.sh && emcmake cmake -B build")
endif()

# Local raylib (prebuilt for WASM)
set(RAYLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/vendor/raylib/include" CACHE PATH "Raylib include directory")
set(RAYLIB_LIBRARY "${CMAKE_SOURCE_DIR}/vendor/raylib/lib/libraylib.a" CACHE FILEPATH "Raylib static library")

# Local Box2D 2.4 (built for WASM) - uses pointer-based API
set(BOX2D_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/vendor/box2d-2.4/include" CACHE PATH "Box2D include directory")
set(BOX2D_LIBRARY "${CMAKE_SOURCE_DIR}/vendor/box2d-2.4/lib/libbox2d.a" CACHE FILEPATH "Box2D static library")

# Collect all source files
file(GLOB_RECURSE SOURCES 
    src/*.cpp
)

# Add executable
add_executable(squirms_wasm ${SOURCES})

# Include directories
target_include_directories(squirms_wasm PRIVATE 
    src
    ${RAYLIB_INCLUDE_DIR}
    ${BOX2D_INCLUDE_DIR}
)

# C++ standard
set_target_properties(squirms_wasm PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

set_target_properties(squirms_wasm PROPERTIES
    OUTPUT_NAME "squirms_wasm"
    SUFFIX ".js"
)

# Compiler flags
target_compile_options(squirms_wasm PRIVATE -O3)

# Link libraries (raylib + box2d + system libs)
target_link_libraries(squirms_wasm PRIVATE
    ${RAYLIB_LIBRARY}
    ${BOX2D_LIBRARY}
    -lGL
    -ldl
    -lm
)

# Link flags. The game drives its own main loop via emscripten_set_main_loop
# and raylib/GLFW3 owns the canvas (id="canvas") directly, so no JS glue
# (MODULARIZE, embind, cwrap/ccall) is needed to boot or drive it.
target_link_options(squirms_wasm PRIVATE
    -sENVIRONMENT=web
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=33554432
    -sSTACK_SIZE=524288
    -sUSE_WEBGPU=0
    -sUSE_GLFW=3
    -sSTRICT=0
)
