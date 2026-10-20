cmake_minimum_required(VERSION 3.16)

project(SparrowWasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EMSCRIPTEN)
    get_filename_component(SPARROWCORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    get_filename_component(JAGUARCORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../JaguarCore" ABSOLUTE)

    if(NOT SPARROWCORE_LIBRARY)
        file(GLOB_RECURSE SPARROWCORE_SOURCES CONFIGURE_DEPENDS
            "${SPARROWCORE_ROOT}/Source/*.cpp")
        add_library(SparrowCoreWasm STATIC ${SPARROWCORE_SOURCES})
        target_include_directories(SparrowCoreWasm PUBLIC
            "${SPARROWCORE_ROOT}/Include"
            "${JAGUARCORE_ROOT}/Include")
        target_compile_options(SparrowCoreWasm PRIVATE -pthread)
        set(SPARROWCORE_LIBRARY SparrowCoreWasm)
    endif()

    if(NOT JAGUARCORE_LIBRARY)
        file(GLOB_RECURSE JAGUARCORE_SOURCES CONFIGURE_DEPENDS
            "${JAGUARCORE_ROOT}/Source/*.cpp")
        list(FILTER JAGUARCORE_SOURCES EXCLUDE REGEX "SoaGeometryAvx2\\.cpp$")
        list(APPEND JAGUARCORE_SOURCES
            "${JAGUARCORE_ROOT}/Source/Collision/SoaGeometryAvx2Fallback.cpp")
        add_library(JaguarCoreWasm STATIC ${JAGUARCORE_SOURCES})
        target_include_directories(JaguarCoreWasm PUBLIC
            "${JAGUARCORE_ROOT}/Include")
        target_compile_options(JaguarCoreWasm PRIVATE -pthread)
        set(JAGUARCORE_LIBRARY JaguarCoreWasm)
    endif()

    add_executable(SparrowWasm SparrowWasm.cpp)
    target_compile_options(SparrowWasm PRIVATE -pthread)
    target_link_options(SparrowWasm PRIVATE
        "--no-entry"
        "-pthread"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=SparrowWasmModule"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sPTHREAD_POOL_SIZE=3"
        "-sEXPORTED_FUNCTIONS=['_SparrowOptimizeJson','_SparrowOptimizeSvg','_SparrowOptimizeWithSvg','_SparrowFreeOutput','_malloc','_free']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAPU32']")
    target_link_libraries(SparrowWasm PRIVATE
        "${SPARROWCORE_LIBRARY}"
        "${JAGUARCORE_LIBRARY}")
else()
    add_library(SparrowWasm STATIC SparrowWasm.cpp)
endif()
target_include_directories(SparrowWasm PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../Include"
    "${CMAKE_CURRENT_LIST_DIR}/../../../JaguarCore/Include")

set_target_properties(SparrowWasm PROPERTIES OUTPUT_NAME "sparrow_wasm")
