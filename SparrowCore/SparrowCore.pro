TEMPLATE = lib
TARGET = SparrowCore
CONFIG += staticlib c++17 warn_on
QT -= core gui

INCLUDEPATH += $$PWD/Include
INCLUDEPATH += $$PWD/../JaguarCore/Include

HEADERS += \
    $$PWD/Include/SparrowCore/Export.h \
    $$PWD/Include/SparrowCore/ResultAlias.h \
    $$PWD/Include/SparrowCore/Version.h \
    $$PWD/Include/SparrowCore/LbfConfig.h \
    $$PWD/Include/SparrowCore/LbfMetrics.h \
    $$PWD/Include/SparrowCore/Random/SparrowRng.h \
    $$PWD/Include/SparrowCore/Sampling/RotationDistribution.h \
    $$PWD/Include/SparrowCore/Sampling/UniformRectSampler.h \
    $$PWD/Include/SparrowCore/Optimizer/SampleEval.h \
    $$PWD/Include/SparrowCore/Optimizer/LbfLoss.h \
    $$PWD/Include/SparrowCore/Optimizer/LbfSearch.h \
    $$PWD/Include/SparrowCore/Optimizer/SppLbfOptimizer.h \
    $$PWD/Include/SparrowCore/Optimizer/BppLbfOptimizer.h \
    $$PWD/Include/SparrowCore/Optimizer/OptimizationTerminator.h \
    $$PWD/Include/SparrowCore/Optimizer/OptimizationListener.h \
    $$PWD/Include/SparrowCore/Optimizer/OptimizationObserver.h \
    $$PWD/Include/SparrowCore/Optimizer/SparrowConfig.h \
    $$PWD/Include/SparrowCore/Optimizer/Exploration.h \
    $$PWD/Source/Optimizer/ExplorationNormalSampler.h \
    $$PWD/Source/Optimizer/ExplorationTrace.h \
    $$PWD/Source/Optimizer/CompressionTrace.h \
    $$PWD/Include/SparrowCore/Optimizer/Compression.h \
    $$PWD/Include/SparrowCore/Optimizer/SeparatorWorker.h \
    $$PWD/Include/SparrowCore/Optimizer/Separator.h \
    $$PWD/Include/SparrowCore/Optimizer/SparrowOptimizer.h \
    $$PWD/Include/SparrowCore/Eval/LbfEvaluator.h \
    $$PWD/Include/SparrowCore/Eval/SparrowLbfEvaluator.h \
    $$PWD/Include/SparrowCore/Eval/SeparationEvaluator.h \
    $$PWD/Include/SparrowCore/Sampling/LocalSearchSampler.h \
    $$PWD/Include/SparrowCore/Sampling/CoordinateDescent.h \
    $$PWD/Include/SparrowCore/Sampling/BestSamples.h \
    $$PWD/Include/SparrowCore/Optimizer/SparrowLbfBuilder.h \
    $$PWD/Include/SparrowCore/Quantify/OverlapProxy.h \
    $$PWD/Include/SparrowCore/Quantify/PairMatrix.h \
    $$PWD/Include/SparrowCore/Quantify/CollisionTracker.h

SOURCES += \
    $$PWD/Source/Support/Version.cpp \
    $$PWD/Source/Random/SparrowRng.cpp \
    $$PWD/Source/Sampling/RotationDistribution.cpp \
    $$PWD/Source/Sampling/UniformRectSampler.cpp \
    $$PWD/Source/Sampling/LocalSearchSampler.cpp \
    $$PWD/Source/Sampling/CoordinateDescent.cpp \
    $$PWD/Source/Optimizer/SampleEval.cpp \
    $$PWD/Source/Optimizer/LbfLoss.cpp \
    $$PWD/Source/Optimizer/LbfSearch.cpp \
    $$PWD/Source/Optimizer/SppLbfOptimizer.cpp \
    $$PWD/Source/Optimizer/BppLbfOptimizer.cpp \
    $$PWD/Source/Optimizer/OptimizationTerminator.cpp \
    $$PWD/Source/Optimizer/OptimizationListener.cpp \
    $$PWD/Source/Optimizer/SparrowConfig.cpp \
    $$PWD/Source/Optimizer/Exploration.cpp \
    $$PWD/Source/Optimizer/Compression.cpp \
    $$PWD/Source/Optimizer/SeparatorWorker.cpp \
    $$PWD/Source/Optimizer/Separator.cpp \
    $$PWD/Source/Optimizer/SparrowOptimizer.cpp \
    $$PWD/Source/Sampling/BestSamples.cpp \
    $$PWD/Source/Eval/LbfEvaluator.cpp \
    $$PWD/Source/Eval/SparrowLbfEvaluator.cpp \
    $$PWD/Source/Eval/SeparationEvaluator.cpp \
    $$PWD/Source/Optimizer/SparrowLbfBuilder.cpp \
    $$PWD/Source/Quantify/OverlapProxy.cpp \
    $$PWD/Source/Quantify/PairMatrix.cpp \
    $$PWD/Source/Quantify/CollisionTracker.cpp

CONFIG(debug, debug|release) {
    isEmpty(SPARROWCORE_BUILD_DIR): SPARROWCORE_BUILD_DIR = $$OUT_PWD
} else {
    isEmpty(SPARROWCORE_BUILD_DIR): SPARROWCORE_BUILD_DIR = $$OUT_PWD
}

DESTDIR = $$SPARROWCORE_BUILD_DIR/bin
OBJECTS_DIR = $$SPARROWCORE_BUILD_DIR/obj
MOC_DIR = $$SPARROWCORE_BUILD_DIR/moc
RCC_DIR = $$SPARROWCORE_BUILD_DIR/rcc
