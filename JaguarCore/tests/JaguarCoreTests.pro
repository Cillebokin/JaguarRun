TEMPLATE = app
TARGET = JaguarCoreTests
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
QT -= core gui

# Keep test assertions active in Release so assert(expr) does not remove the
# expression and silently skip the behavior under test. The DLL itself keeps
# the normal Release configuration.
CONFIG(release, debug|release) {
    DEFINES -= NDEBUG
    QMAKE_CXXFLAGS_RELEASE += /UNDEBUG
    DEFINES += JAGUARCORE_TEST_RELEASE
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

CONFIG(debug, debug|release) {
    isEmpty(JAGUARCORE_BUILD_DIR): JAGUARCORE_BUILD_DIR = $$PWD/../../build-JaguarCore-MSVC2019x64-Debug
    LIBS += -L$$JAGUARCORE_BUILD_DIR/Bin -lJaguarCored
    PRE_TARGETDEPS += $$JAGUARCORE_BUILD_DIR/Bin/JaguarCored.lib
} else {
    isEmpty(JAGUARCORE_BUILD_DIR): JAGUARCORE_BUILD_DIR = $$PWD/../../build-JaguarCore-MSVC2019x64-Release
    LIBS += -L$$JAGUARCORE_BUILD_DIR/Bin -lJaguarCore
    PRE_TARGETDEPS += $$JAGUARCORE_BUILD_DIR/Bin/JaguarCore.lib
}

SOURCES += \
    $$PWD/TestSupport.cpp \
    $$PWD/TestGeometry.cpp \
    $$PWD/TestPolygon.cpp \
    $$PWD/TestShapeModification.cpp \
    $$PWD/TestSurrogate.cpp \
    $$PWD/TestCollision.cpp \
    $$PWD/TestLayout.cpp \
    $$PWD/TestIo.cpp \
    $$PWD/TestJsonCodec.cpp \
    $$PWD/TestSvgExport.cpp \
    $$PWD/TestSoa.cpp \
    $$PWD/../Source/Collision/SoaGeometry.cpp \
    $$PWD/Differential/CompareGeometry.cpp \
    $$PWD/Differential/CompareProblem.cpp \
    $$PWD/Differential/CompareMatrix.cpp \
    $$PWD/Differential/CompareTrajectory.cpp \
    $$PWD/Differential/CompareOptimizer.cpp \
    $$PWD/Differential/CompareBoundary.cpp \
    $$PWD/TestSpp.cpp \
    $$PWD/TestBpp.cpp \
    $$PWD/TestMspp.cpp \
    $$PWD/TestProblemRandom.cpp \
    $$PWD/TestBoundary.cpp
