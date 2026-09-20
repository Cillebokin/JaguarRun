TEMPLATE = app
TARGET = SparrowCoreTests
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
QT -= core gui

INCLUDEPATH += $$PWD/../include
INCLUDEPATH += $$PWD/../../JaguarCore/include

isEmpty(SPARROWCORE_BUILD_DIR): SPARROWCORE_BUILD_DIR = $$PWD/../../build-SparrowCore-MSVC2019x64-Debug

CONFIG(debug, debug|release) {
    isEmpty(JAGUARCORE_BUILD_DIR): JAGUARCORE_BUILD_DIR = $$PWD/../../build-JaguarCore-MSVC2019x64-Debug
    LIBS += -L$$SPARROWCORE_BUILD_DIR/bin -lSparrowCore
    LIBS += -L$$JAGUARCORE_BUILD_DIR/bin -lJaguarCored
    PRE_TARGETDEPS += $$SPARROWCORE_BUILD_DIR/bin/SparrowCore.lib
    PRE_TARGETDEPS += $$JAGUARCORE_BUILD_DIR/bin/JaguarCored.lib
} else {
    isEmpty(JAGUARCORE_BUILD_DIR): JAGUARCORE_BUILD_DIR = $$PWD/../../build-JaguarCore-MSVC2019x64-Release
    LIBS += -L$$SPARROWCORE_BUILD_DIR/bin -lSparrowCore
    LIBS += -L$$JAGUARCORE_BUILD_DIR/bin -lJaguarCore
    PRE_TARGETDEPS += $$SPARROWCORE_BUILD_DIR/bin/SparrowCore.lib
    PRE_TARGETDEPS += $$JAGUARCORE_BUILD_DIR/bin/JaguarCore.lib
}

SOURCES += \
    $$PWD/test_support.cpp \
    $$PWD/test_rng.cpp \
    $$PWD/test_sampling.cpp \
    $$PWD/test_lbf_loss.cpp \
    $$PWD/test_spp_lbf.cpp \
    $$PWD/test_bpp_lbf.cpp
