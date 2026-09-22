TEMPLATE = lib
TARGET = JaguarCore
CONFIG += dll c++17 warn_on skip_target_version_ext
QT -= core gui

# Define output directories before JaguarCorePackage.pri expands its install
# source paths. This keeps clean qmake shadow builds installable.
DESTDIR = $$OUT_PWD/Bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
RCC_DIR = $$OUT_PWD/rcc

include($$PWD/Package/JaguarCorePackage.pri)

# Keep the PE file version numeric and stable for deployment checks. The
# public C++ version string may carry a development suffix independently.
VERSION = $$JAGUARCORE_BINARY_VERSION
QMAKE_TARGET_PRODUCT = JaguarCore
QMAKE_TARGET_DESCRIPTION = JaguarCore geometry and nesting core

# AVX2 is compiled as an isolated source file. Use CONFIG+=jaguarcore_no_avx2
# to build the same module with the scalar implementation only.
CONFIG += simd
!contains(CONFIG, jaguarcore_no_avx2) {
    CONFIG += avx2
    DEFINES += JAGUARCORE_AVX2_COMPILED
    AVX2_SOURCES += $$PWD/Source/Collision/SoaGeometryAvx2.cpp
} else {
    SOURCES += $$PWD/Source/Collision/SoaGeometryAvx2Fallback.cpp
}

CONFIG(debug, debug|release) {
    TARGET = JaguarCored
}

DEFINES += JAGUARCORE_BUILD

INCLUDEPATH += $$PWD/Include

HEADERS += \
    $$PWD/Include/JaguarCore/Export.h \
    $$PWD/Include/JaguarCore/Version.h \
    $$PWD/Include/JaguarCore/Result.h \
    $$PWD/Include/JaguarCore/NumericPolicy.h \
    $$PWD/Include/JaguarCore/Geometry/Point.h \
    $$PWD/Include/JaguarCore/Geometry/Rect.h \
    $$PWD/Include/JaguarCore/Geometry/Edge.h \
    $$PWD/Include/JaguarCore/Geometry/Circle.h \
    $$PWD/Include/JaguarCore/Geometry/Transformation.h \
    $$PWD/Include/JaguarCore/Geometry/ConvexHull.h \
    $$PWD/Include/JaguarCore/Geometry/SimplePolygon.h \
    $$PWD/Include/JaguarCore/Geometry/ShapeModification.h \
    $$PWD/Include/JaguarCore/Geometry/OriginalShape.h \
    $$PWD/Include/JaguarCore/Entities/Item.h \
    $$PWD/Include/JaguarCore/Entities/Container.h \
    $$PWD/Include/JaguarCore/Entities/PlacedItem.h \
    $$PWD/Include/JaguarCore/Entities/Layout.h \
    $$PWD/Include/JaguarCore/IO/ExternalRepr.h \
    $$PWD/Include/JaguarCore/IO/Importer.h \
    $$PWD/Include/JaguarCore/IO/JsonCodec.h \
    $$PWD/Include/JaguarCore/IO/SvgExport.h \
    $$PWD/Include/JaguarCore/Problems/Spp.h \
    $$PWD/Include/JaguarCore/Problems/Bpp.h \
    $$PWD/Include/JaguarCore/Problems/MSpp.h \
    $$PWD/Include/JaguarCore/Assertions.h \
    $$PWD/Include/JaguarCore/FailFast/Surrogate.h \
    $$PWD/Include/JaguarCore/Collision/Hazard.h \
    $$PWD/Include/JaguarCore/Collision/HazardFilter.h \
    $$PWD/Include/JaguarCore/Collision/HazardCollector.h \
    $$PWD/Include/JaguarCore/Collision/Quadtree.h \
    $$PWD/Include/JaguarCore/Collision/CollisionEngine.h

SOURCES += \
    $$PWD/Source/Support/Version.cpp \
    $$PWD/Source/Support/Result.cpp \
    $$PWD/Source/Geometry/Primitives/Edge.cpp \
    $$PWD/Source/Geometry/Primitives/Circle.cpp \
    $$PWD/Source/Geometry/Transformation/Transformation.cpp \
    $$PWD/Source/Geometry/Primitives/SimplePolygon.cpp \
    $$PWD/Source/Geometry/ConvexHull/ConvexHull.cpp \
    $$PWD/Source/Geometry/OriginalShape/OriginalShape.cpp \
    $$PWD/Source/Geometry/ShapeModification/Simplify.cpp \
    $$PWD/Source/Geometry/ShapeModification/Offset.cpp \
    $$PWD/Source/Geometry/ShapeModification/StraightSkeleton.cpp \
    $$PWD/Source/Geometry/ShapeModification/NarrowConcavity.cpp \
    $$PWD/Source/Entities/Item.cpp \
    $$PWD/Source/Entities/Container.cpp \
    $$PWD/Source/Entities/PlacedItem.cpp \
    $$PWD/Source/Entities/Layout.cpp \
    $$PWD/Source/IO/ExternalRepr.cpp \
    $$PWD/Source/IO/Importer.cpp \
    $$PWD/Source/IO/JsonCodec.cpp \
    $$PWD/Source/IO/SvgExport.cpp \
    $$PWD/Source/Problems/SPP/Strip.cpp \
    $$PWD/Source/Problems/SPP/Instance.cpp \
    $$PWD/Source/Problems/SPP/Problem.cpp \
    $$PWD/Source/Problems/SPP/Solution.cpp \
    $$PWD/Source/Problems/BPP/BppBin.cpp \
    $$PWD/Source/Problems/BPP/BppInstance.cpp \
    $$PWD/Source/Problems/BPP/BppProblem.cpp \
    $$PWD/Source/Problems/BPP/BppSolution.cpp \
    $$PWD/Source/Problems/MSPP/MspStrip.cpp \
    $$PWD/Source/Problems/MSPP/MspInstance.cpp \
    $$PWD/Source/Problems/MSPP/MspProblem.cpp \
    $$PWD/Source/Problems/MSPP/MspSolution.cpp \
    $$PWD/Source/Support/Assertions.cpp \
    $$PWD/Source/FailFast/Surrogate.cpp \
    $$PWD/Source/Collision/Hazards/Collector.cpp \
    $$PWD/Source/Collision/Quadtree/Quadtree.cpp \
    $$PWD/Source/Collision/ThreadPool.cpp \
    $$PWD/Source/Collision/SoaGeometry.cpp \
    $$PWD/Source/Collision/CollisionEngine.cpp

CONFIG(debug, debug|release) {
    JAGUARCORE_PACKAGE_CONFIGURATION = Debug
} else {
    JAGUARCORE_PACKAGE_CONFIGURATION = Release
}

contains(CONFIG, jaguarcore_no_avx2) {
    JAGUARCORE_PACKAGE_VARIANT = Scalar
} else {
    JAGUARCORE_PACKAGE_VARIANT = AVX2
}

isEmpty(JAGUARCORE_PACKAGE_ROOT) {
    JAGUARCORE_PACKAGE_ROOT = $$OUT_PWD/Package/$${JAGUARCORE_PACKAGE_DIRNAME}
}

JAGUARCORE_PACKAGE_CONFIG_ROOT = \
    $$JAGUARCORE_PACKAGE_ROOT/$${JAGUARCORE_PACKAGE_CONFIGURATION}/$${JAGUARCORE_PACKAGE_VARIANT}

jaguarcore_runtime.files = $$DESTDIR/$${TARGET}.dll
jaguarcore_runtime.path = $$JAGUARCORE_PACKAGE_CONFIG_ROOT/Bin
jaguarcore_runtime.CONFIG += no_check_exist

jaguarcore_import_library.files = $$DESTDIR/$${TARGET}.lib
jaguarcore_import_library.path = $$JAGUARCORE_PACKAGE_CONFIG_ROOT/Lib
jaguarcore_import_library.CONFIG += no_check_exist

jaguarcore_public_headers.files = $$PWD/Include/JaguarCore
jaguarcore_public_headers.path = $$JAGUARCORE_PACKAGE_ROOT/Include

jaguarcore_package_readme.files = $$PWD/Package/README.md
jaguarcore_package_readme.path = $$JAGUARCORE_PACKAGE_ROOT

jaguarcore_package_verifier.files = $$PWD/Package/VerifyPackage.ps1
jaguarcore_package_verifier.path = $$JAGUARCORE_PACKAGE_ROOT

jaguarcore_deployment_guide.files = $$PWD/../docs/deployment.md
jaguarcore_deployment_guide.path = $$JAGUARCORE_PACKAGE_ROOT/Docs

INSTALLS += \
    jaguarcore_runtime \
    jaguarcore_import_library \
    jaguarcore_public_headers \
    jaguarcore_package_readme \
    jaguarcore_package_verifier \
    jaguarcore_deployment_guide
