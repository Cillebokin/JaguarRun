TEMPLATE = lib
TARGET = SparrowCore
CONFIG += staticlib c++17 warn_on
QT -= core gui

INCLUDEPATH += $$PWD/include
INCLUDEPATH += $$PWD/../JaguarCore/include

HEADERS += \
    $$PWD/include/SparrowCore/export.h \
    $$PWD/include/SparrowCore/result_alias.h \
    $$PWD/include/SparrowCore/version.h \
    $$PWD/include/SparrowCore/lbf_config.h \
    $$PWD/include/SparrowCore/lbf_metrics.h \
    $$PWD/include/SparrowCore/random/sparrow_rng.h \
    $$PWD/include/SparrowCore/sampling/rotation_distribution.h \
    $$PWD/include/SparrowCore/sampling/uniform_rect_sampler.h \
    $$PWD/include/SparrowCore/optimizer/sample_eval.h \
    $$PWD/include/SparrowCore/optimizer/lbf_loss.h \
    $$PWD/include/SparrowCore/optimizer/lbf_search.h \
    $$PWD/include/SparrowCore/optimizer/spp_lbf_optimizer.h \
    $$PWD/include/SparrowCore/optimizer/bpp_lbf_optimizer.h \
    $$PWD/include/SparrowCore/eval/lbf_evaluator.h \
    $$PWD/include/SparrowCore/sampling/local_search_sampler.h \
    $$PWD/include/SparrowCore/sampling/coordinate_descent.h \
    $$PWD/include/SparrowCore/sampling/best_samples.h

SOURCES += \
    $$PWD/src/support/version.cpp \
    $$PWD/src/random/sparrow_rng.cpp \
    $$PWD/src/sampling/rotation_distribution.cpp \
    $$PWD/src/sampling/uniform_rect_sampler.cpp \
    $$PWD/src/sampling/local_search_sampler.cpp \
    $$PWD/src/sampling/coordinate_descent.cpp \
    $$PWD/src/optimizer/sample_eval.cpp \
    $$PWD/src/optimizer/lbf_loss.cpp \
    $$PWD/src/optimizer/lbf_search.cpp \
    $$PWD/src/optimizer/spp_lbf_optimizer.cpp \
    $$PWD/src/optimizer/bpp_lbf_optimizer.cpp \
    $$PWD/src/sampling/best_samples.cpp \
    $$PWD/src/eval/lbf_evaluator.cpp

CONFIG(debug, debug|release) {
    isEmpty(SPARROWCORE_BUILD_DIR): SPARROWCORE_BUILD_DIR = $$OUT_PWD
} else {
    isEmpty(SPARROWCORE_BUILD_DIR): SPARROWCORE_BUILD_DIR = $$OUT_PWD
}

DESTDIR = $$SPARROWCORE_BUILD_DIR/bin
OBJECTS_DIR = $$SPARROWCORE_BUILD_DIR/obj
MOC_DIR = $$SPARROWCORE_BUILD_DIR/moc
RCC_DIR = $$SPARROWCORE_BUILD_DIR/rcc
