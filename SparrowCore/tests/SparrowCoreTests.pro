TEMPLATE = app
TARGET = SparrowCoreTests
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
QT -= core gui

win32-msvc: QMAKE_CXXFLAGS += /fp:strict

INCLUDEPATH += $$PWD/../Include
INCLUDEPATH += $$PWD/../../JaguarCore/Include
INCLUDEPATH += $$PWD/../Tools/SparrowWasm
INCLUDEPATH += $$PWD/../Tools/SparrowCli
INCLUDEPATH += $$PWD/../Tools/Lbf

HEADERS += \
    $$PWD/../Source/Optimizer/ExplorationNormalSampler.h \
    $$PWD/../Source/Optimizer/ExplorationTrace.h \
    $$PWD/../Source/Optimizer/LbfSearchTrace.h \
    $$PWD/../Source/Optimizer/LbfOptimizerTrace.h \
    $$PWD/../Source/Optimizer/CompressionTrace.h \
    $$PWD/../Source/Optimizer/WorkerLossSelector.h \
    $$PWD/../Source/Optimizer/WorkerRoundCollector.h

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
    $$PWD/TestSupport.cpp \
    $$PWD/TestRng.cpp \
    $$PWD/TestSampling.cpp \
    $$PWD/TestLbfLoss.cpp \
    $$PWD/TestSppLbf.cpp \
    $$PWD/TestBppLbf.cpp \
    $$PWD/TestLbfEntry.cpp \
    $$PWD/../Tools/Lbf/LbfProductSupport.cpp \
    $$PWD/TestQuantify.cpp \
    $$PWD/TestSeparator.cpp \
    $$PWD/TestExploration.cpp \
    $$PWD/TestWindowsConsoleCtrlC.cpp \
    $$PWD/../Tools/SparrowCli/OptimizationSignalHandler.cpp \
    $$PWD/TestWasm.cpp \
    $$PWD/../Tools/SparrowWasm/SparrowWasm.cpp
