cmake_minimum_required(VERSION 3.16)

project(SparrowWasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EMSCRIPTEN)
    add_executable(SparrowWasm SparrowWasm.cpp)
    target_link_options(SparrowWasm PRIVATE
        "--no-entry"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=SparrowWasmModule"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXPORTED_FUNCTIONS=['_SparrowOptimizeJson','_SparrowOptimizeSvg']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']")
else()
    add_library(SparrowWasm STATIC SparrowWasm.cpp)
endif()
target_include_directories(SparrowWasm PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../Include"
    "${CMAKE_CURRENT_LIST_DIR}/../../../JaguarCore/Include")

if(DEFINED SPARROWCORE_LIBRARY)
    target_link_libraries(SparrowWasm PRIVATE "${SPARROWCORE_LIBRARY}")
endif()
if(DEFINED JAGUARCORE_LIBRARY)
    target_link_libraries(SparrowWasm PRIVATE "${JAGUARCORE_LIBRARY}")
endif()

set_target_properties(SparrowWasm PROPERTIES OUTPUT_NAME "sparrow_wasm")
