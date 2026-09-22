TEMPLATE = app
TARGET = SparrowCoreBench
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
QT -= core gui

INCLUDEPATH += $$PWD/../Include
INCLUDEPATH += $$PWD/../../JaguarCore/Include

isEmpty(SPARROWCORE_BUILD_DIR): SPARROWCORE_BUILD_DIR = $$PWD/../../build-SparrowCoreBench-MSVC2019x64-Debug
isEmpty(JAGUARCORE_BUILD_DIR): JAGUARCORE_BUILD_DIR = $$PWD/../../build-JaguarCore-current
LIBS += -L$$SPARROWCORE_BUILD_DIR/bin -lSparrowCore
LIBS += -L$$JAGUARCORE_BUILD_DIR/Bin -lJaguarCore
PRE_TARGETDEPS += $$SPARROWCORE_BUILD_DIR/bin/SparrowCore.lib
PRE_TARGETDEPS += $$JAGUARCORE_BUILD_DIR/Bin/JaguarCore.lib

SOURCES += \
    $$PWD/BenchMain.cpp
