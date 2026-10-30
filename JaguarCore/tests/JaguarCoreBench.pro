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
    QMAKE_CXXFLAGS_RELEASE += /FI$$PWD/TestReleaseAsserts.h
}

CONFIG += simd
!contains(CONFIG, jaguarcore_no_avx2) {
    CONFIG += avx2
    DEFINES += JAGUARCORE_AVX2_COMPILED
    AVX2_SOURCES += $$PWD/../Source/Collision/SoaGeometryAvx2.cpp
} else {
    SOURCES += $$PWD/../Source/Collision/SoaGeometryAvx2Fallback.cpp
}

INCLUDEPATH += $$PWD/../Include
include($$PWD/../Package/JaguarCorePackage.pri)

CONFIG(debug, debug|release) {
    isEmpty(JAGUARCORE_BUILD_DIR): JAGUARCORE_BUILD_DIR = $$PWD/../../build-JaguarCore-MSVC2019x64-Debug
    LIBS += -L$$JAGUARCORE_BUILD_DIR/Bin -lJaguarCore
    PRE_TARGETDEPS += $$JAGUARCORE_BUILD_DIR/Bin/JaguarCore.lib
} else {
    isEmpty(JAGUARCORE_BUILD_DIR): JAGUARCORE_BUILD_DIR = $$PWD/../../build-JaguarCore-MSVC2019x64-Release
    LIBS += -L$$JAGUARCORE_BUILD_DIR/Bin -lJaguarCore
    PRE_TARGETDEPS += $$JAGUARCORE_BUILD_DIR/Bin/JaguarCore.lib
}

SOURCES += \
    $$PWD/Bench/BenchMain.cpp \
    $$PWD/Bench/BenchGeometry.cpp \
    $$PWD/Bench/BenchCollision.cpp \
    $$PWD/Bench/BenchLayout.cpp \
    $$PWD/../Source/Collision/SoaGeometry.cpp
