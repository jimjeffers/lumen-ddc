QT += core gui quick quickcontrols2 testlib
CONFIG += c++17 testcase
TARGET = lumen_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/ddc.h ../src/monitor.h ../src/theme.h ../src/translator.h
SOURCES += lumen_tests.cpp ../src/backend.cpp ../src/ddc.cpp ../src/monitor.cpp ../src/theme.cpp ../src/translator.cpp
