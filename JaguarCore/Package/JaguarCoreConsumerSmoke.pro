TEMPLATE = app
TARGET = JaguarCoreConsumerSmoke
CONFIG += console c++17 warn_on
win32-msvc: QMAKE_CXXFLAGS += /utf-8
CONFIG -= app_bundle
QT -= core gui

isEmpty(JAGUARCORE_PACKAGE_ROOT): error("JAGUARCORE_PACKAGE_ROOT is required")
isEmpty(JAGUARCORE_PACKAGE_CONFIGURATION): error("JAGUARCORE_PACKAGE_CONFIGURATION is required")
isEmpty(JAGUARCORE_PACKAGE_VARIANT): error("JAGUARCORE_PACKAGE_VARIANT is required")

INCLUDEPATH += $$JAGUARCORE_PACKAGE_ROOT/Include
win32:LIBS += -L$$JAGUARCORE_PACKAGE_ROOT/$$JAGUARCORE_PACKAGE_CONFIGURATION/$$JAGUARCORE_PACKAGE_VARIANT/Lib

CONFIG(debug, debug|release) {
    LIBS += -lJaguarCored
} else {
    LIBS += -lJaguarCore
}

SOURCES += $$PWD/JaguarCoreConsumerSmoke.cpp
DESTDIR = $$OUT_PWD/Bin
OBJECTS_DIR = $$OUT_PWD/obj
