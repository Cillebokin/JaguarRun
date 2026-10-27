cmake_minimum_required(VERSION 3.16)

project(SparrowWasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(SPARROW_FP_COMPILE_OPTIONS /fp:strict)
else()
    set(SPARROW_FP_COMPILE_OPTIONS -fno-fast-math -ffp-contract=off)
endif()

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
        target_compile_options(SparrowCoreWasm PRIVATE -pthread ${SPARROW_FP_COMPILE_OPTIONS})
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
        target_compile_options(JaguarCoreWasm PRIVATE -pthread ${SPARROW_FP_COMPILE_OPTIONS})
        set(JAGUARCORE_LIBRARY JaguarCoreWasm)
    endif()

    add_executable(SparrowWasm SparrowWasm.cpp)
    target_compile_options(SparrowWasm PRIVATE -pthread ${SPARROW_FP_COMPILE_OPTIONS})
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

    get_filename_component(JAGUA_RS_ASSETS
        "${CMAKE_CURRENT_LIST_DIR}/../../../CodeResource/jagua-rs/assets" ABSOLUTE)
    add_executable(LbfWasm
        LbfWasm.cpp
        "${SPARROWCORE_ROOT}/Tools/Lbf/LbfProductSupport.cpp")
    target_include_directories(LbfWasm PRIVATE
        "${SPARROWCORE_ROOT}/Tools/Lbf")
    target_compile_options(LbfWasm PRIVATE
        -pthread -fexceptions ${SPARROW_FP_COMPILE_OPTIONS})
    target_link_options(LbfWasm PRIVATE
        "--bind"
        "--no-entry"
        "-fexceptions"
        "-pthread"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=LbfModule"
        "--post-js=${CMAKE_CURRENT_LIST_DIR}/LbfWasmPost.js"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sPTHREAD_POOL_SIZE=3"
        "-sFORCE_FILESYSTEM=1"
        "--preload-file=${JAGUA_RS_ASSETS}/swim.json@/lbf_assets/swim.json"
        "--preload-file=${JAGUA_RS_ASSETS}/baldacci1.json@/lbf_assets/baldacci1.json")
    set_property(TARGET LbfWasm APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/LbfWasmPost.js")
    target_link_libraries(LbfWasm PRIVATE
        "${SPARROWCORE_LIBRARY}"
        "${JAGUARCORE_LIBRARY}")
    set_target_properties(LbfWasm PROPERTIES OUTPUT_NAME "lbf")
else()
    add_library(SparrowWasm STATIC SparrowWasm.cpp)
    target_compile_options(SparrowWasm PRIVATE ${SPARROW_FP_COMPILE_OPTIONS})
endif()
target_include_directories(SparrowWasm PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../Include"
    "${CMAKE_CURRENT_LIST_DIR}/../../../JaguarCore/Include")

set_target_properties(SparrowWasm PROPERTIES OUTPUT_NAME "sparrow_wasm")
