TEMPLATE = app
TARGET = JaguarCoreBench
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
QT -= core gui

# Benchmark correctness checks use assert(expr); keep them active in Release
# so brute-force/CDE/SoA comparisons are actually executed.
CONFIG(release, debug|release) {
    DEFINES -= NDEBUG
    QMAKE_CXXFLAGS_RELEASE += /UNDEBUG
    QMAKE_CXXFLAGS_RELEASE += /FI$$PWD/test_release_asserts.h
}

CONFIG += simd
!contains(CONFIG, jaguarcore_no_avx2) {
    CONFIG += avx2
    DEFINES += JAGUARCORE_AVX2_COMPILED
    AVX2_SOURCES += $$PWD/../src/collision/soa_geometry_avx2.cpp
} else {
    SOURCES += $$PWD/../src/collision/soa_geometry_avx2_fallback.cpp
}

INCLUDEPATH += $$PWD/../include

CONFIG(debug, debug|release) {
    isEmpty(JAGUARCORE_BUILD_DIR): JAGUARCORE_BUILD_DIR = $$PWD/../../build-JaguarCore-MSVC2019x64-Debug
    LIBS += -L$$JAGUARCORE_BUILD_DIR/bin -lJaguarCored
    PRE_TARGETDEPS += $$JAGUARCORE_BUILD_DIR/bin/JaguarCored.lib
} else {
    isEmpty(JAGUARCORE_BUILD_DIR): JAGUARCORE_BUILD_DIR = $$PWD/../../build-JaguarCore-MSVC2019x64-Release
    LIBS += -L$$JAGUARCORE_BUILD_DIR/bin -lJaguarCore
    PRE_TARGETDEPS += $$JAGUARCORE_BUILD_DIR/bin/JaguarCore.lib
}

SOURCES += \
    $$PWD/bench/bench_main.cpp \
    $$PWD/bench/bench_geometry.cpp \
    $$PWD/bench/bench_collision.cpp \
    $$PWD/bench/bench_layout.cpp \
    $$PWD/../src/collision/soa_geometry.cpp
