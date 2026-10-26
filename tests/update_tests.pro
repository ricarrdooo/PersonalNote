QT += core testlib
QT -= gui
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = update_tests
INCLUDEPATH += ../src
SOURCES += update_tests.cpp ../src/appupdater.cpp
HEADERS += ../src/appupdater.h
win32:LIBS += -lwinhttp
win32-g++:QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
