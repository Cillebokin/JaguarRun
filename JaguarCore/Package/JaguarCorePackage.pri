# The published package name is an ABI claim. Refuse to emit it from a
# different compiler generation or target architecture.
!win32-msvc {
    error("JaguarCore packages require the MSVC qmake spec")
}
!contains(QMAKE_TARGET.arch, x86_64) {
    error("JaguarCore packages require an x64 target")
}
lessThan(QMAKE_MSC_VER, 1920) {
    error("JaguarCore packages require the MSVC 2019 toolset")
}
greaterThan(QMAKE_MSC_VER, 1929) {
    error("JaguarCore packages require the MSVC 2019 toolset")
}

JAGUARCORE_BINARY_VERSION = 0.1.0
JAGUARCORE_PACKAGE_DIRNAME = JaguarCore-$${JAGUARCORE_BINARY_VERSION}-msvc2019-x64

win32-msvc: QMAKE_CXXFLAGS += /utf-8
