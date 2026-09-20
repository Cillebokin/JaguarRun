TEMPLATE = lib
TARGET = JaguarCore
CONFIG += dll c++17 warn_on
QT -= core gui

# AVX2 is compiled as an isolated source file. Use CONFIG+=jaguarcore_no_avx2
# to build the same module with the scalar implementation only.
CONFIG += simd
!contains(CONFIG, jaguarcore_no_avx2) {
    CONFIG += avx2
    DEFINES += JAGUARCORE_AVX2_COMPILED
    AVX2_SOURCES += $$PWD/src/collision/soa_geometry_avx2.cpp
} else {
    SOURCES += $$PWD/src/collision/soa_geometry_avx2_fallback.cpp
}

CONFIG(debug, debug|release) {
    TARGET = JaguarCored
}

DEFINES += JAGUARCORE_BUILD

INCLUDEPATH += $$PWD/include

HEADERS += \
    $$PWD/include/JaguarCore/export.h \
    $$PWD/include/JaguarCore/version.h \
    $$PWD/include/JaguarCore/result.h \
    $$PWD/include/JaguarCore/numeric_policy.h \
    $$PWD/include/JaguarCore/geometry/point.h \
    $$PWD/include/JaguarCore/geometry/rect.h \
    $$PWD/include/JaguarCore/geometry/edge.h \
    $$PWD/include/JaguarCore/geometry/circle.h \
    $$PWD/include/JaguarCore/geometry/transformation.h \
    $$PWD/include/JaguarCore/geometry/convex_hull.h \
    $$PWD/include/JaguarCore/geometry/simple_polygon.h \
    $$PWD/include/JaguarCore/geometry/shape_modification.h \
    $$PWD/include/JaguarCore/geometry/original_shape.h \
    $$PWD/include/JaguarCore/entities/item.h \
    $$PWD/include/JaguarCore/entities/container.h \
    $$PWD/include/JaguarCore/entities/placed_item.h \
    $$PWD/include/JaguarCore/entities/layout.h \
    $$PWD/include/JaguarCore/io/external_repr.h \
    $$PWD/include/JaguarCore/io/importer.h \
    $$PWD/include/JaguarCore/io/json_codec.h \
    $$PWD/include/JaguarCore/io/svg_export.h \
    $$PWD/include/JaguarCore/problems/spp.h \
    $$PWD/include/JaguarCore/problems/bpp.h \
    $$PWD/include/JaguarCore/problems/mspp.h \
    $$PWD/include/JaguarCore/assertions.h \
    $$PWD/include/JaguarCore/fail_fast/surrogate.h \
    $$PWD/include/JaguarCore/collision/hazard.h \
    $$PWD/include/JaguarCore/collision/hazard_filter.h \
    $$PWD/include/JaguarCore/collision/hazard_collector.h \
    $$PWD/include/JaguarCore/collision/quadtree.h \
    $$PWD/include/JaguarCore/collision/collision_engine.h

SOURCES += \
    $$PWD/src/support/version.cpp \
    $$PWD/src/support/result.cpp \
    $$PWD/src/geometry/primitives/edge.cpp \
    $$PWD/src/geometry/primitives/circle.cpp \
    $$PWD/src/geometry/transformation/transformation.cpp \
    $$PWD/src/geometry/primitives/simple_polygon.cpp \
    $$PWD/src/geometry/convex_hull/convex_hull.cpp \
    $$PWD/src/geometry/original_shape/original_shape.cpp \
    $$PWD/src/geometry/shape_modification/simplify.cpp \
    $$PWD/src/geometry/shape_modification/offset.cpp \
    $$PWD/src/geometry/shape_modification/straight_skeleton.cpp \
    $$PWD/src/geometry/shape_modification/narrow_concavity.cpp \
    $$PWD/src/entities/item.cpp \
    $$PWD/src/entities/container.cpp \
    $$PWD/src/entities/placed_item.cpp \
    $$PWD/src/entities/layout.cpp \
    $$PWD/src/io/external_repr.cpp \
    $$PWD/src/io/importer.cpp \
    $$PWD/src/io/json_codec.cpp \
    $$PWD/src/io/svg_export.cpp \
    $$PWD/src/problems/spp/strip.cpp \
    $$PWD/src/problems/spp/instance.cpp \
    $$PWD/src/problems/spp/problem.cpp \
    $$PWD/src/problems/spp/solution.cpp \
    $$PWD/src/problems/bpp/bpp_bin.cpp \
    $$PWD/src/problems/bpp/bpp_instance.cpp \
    $$PWD/src/problems/bpp/bpp_problem.cpp \
    $$PWD/src/problems/bpp/bpp_solution.cpp \
    $$PWD/src/problems/mspp/msp_strip.cpp \
    $$PWD/src/problems/mspp/msp_instance.cpp \
    $$PWD/src/problems/mspp/msp_problem.cpp \
    $$PWD/src/problems/mspp/msp_solution.cpp \
    $$PWD/src/support/assertions.cpp \
    $$PWD/src/fail_fast/surrogate.cpp \
    $$PWD/src/collision/hazards/collector.cpp \
    $$PWD/src/collision/quadtree/quadtree.cpp \
    $$PWD/src/collision/thread_pool.cpp \
    $$PWD/src/collision/soa_geometry.cpp \
    $$PWD/src/collision/collision_engine.cpp

DESTDIR = $$OUT_PWD/bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
RCC_DIR = $$OUT_PWD/rcc
