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
    $$PWD/test_support.cpp \
    $$PWD/test_geometry.cpp \
    $$PWD/test_polygon.cpp \
    $$PWD/test_shape_modification.cpp \
    $$PWD/test_surrogate.cpp \
    $$PWD/test_collision.cpp \
    $$PWD/test_layout.cpp \
    $$PWD/test_io.cpp \
    $$PWD/test_json_codec.cpp \
    $$PWD/test_svg_export.cpp \
    $$PWD/test_soa.cpp \
    $$PWD/../src/collision/soa_geometry.cpp \
    $$PWD/differential/compare_geometry.cpp \
    $$PWD/differential/compare_problem.cpp \
    $$PWD/differential/compare_matrix.cpp \
    $$PWD/differential/compare_trajectory.cpp \
    $$PWD/differential/compare_optimizer.cpp \
    $$PWD/differential/compare_boundary.cpp \
    $$PWD/test_spp.cpp \
    $$PWD/test_bpp.cpp \
    $$PWD/test_mspp.cpp \
    $$PWD/test_problem_random.cpp
