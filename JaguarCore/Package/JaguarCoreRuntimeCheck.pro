TEMPLATE = app
TARGET = JaguarCoreRuntimeCheck
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
QT -= core gui

include($$PWD/JaguarCorePackage.pri)

SOURCES += $$PWD/JaguarCoreRuntimeCheck.cpp

DESTDIR = $$OUT_PWD/Bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
RCC_DIR = $$OUT_PWD/rcc

win32:LIBS += -lVersion

CONFIG(debug, debug|release) {
    JAGUARCORE_PACKAGE_CONFIGURATION = Debug
} else {
    JAGUARCORE_PACKAGE_CONFIGURATION = Release
}

isEmpty(JAGUARCORE_PACKAGE_ROOT) {
    JAGUARCORE_PACKAGE_ROOT = $$OUT_PWD/Package/$${JAGUARCORE_PACKAGE_DIRNAME}
}

target.path = $$JAGUARCORE_PACKAGE_ROOT/$${JAGUARCORE_PACKAGE_CONFIGURATION}/Tools
INSTALLS += target
