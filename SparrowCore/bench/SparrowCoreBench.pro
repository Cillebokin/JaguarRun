TEMPLATE = app
TARGET = SparrowCoreBench
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
QT -= core gui

INCLUDEPATH += $$PWD/../include

isEmpty(SPARROWCORE_BUILD_DIR): SPARROWCORE_BUILD_DIR = $$PWD/../../build-SparrowCoreBench-MSVC2019x64-Debug
LIBS += -L$$SPARROWCORE_BUILD_DIR/bin -lSparrowCore
PRE_TARGETDEPS += $$SPARROWCORE_BUILD_DIR/bin/SparrowCore.lib

SOURCES += \
    $$PWD/bench_main.cpp
