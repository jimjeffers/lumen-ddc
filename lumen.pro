QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = lumen
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/ddc.h \
    src/monitor.h \
    src/profile.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/ddc.cpp \
    src/monitor.cpp \
    src/profile.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
