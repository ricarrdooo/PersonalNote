QT += core gui widgets testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = markdown_tests
INCLUDEPATH += ../src
SOURCES += markdown_tests.cpp ../src/noteeditor.cpp ../src/codehighlighter.cpp
HEADERS += ../src/noteeditor.h ../src/codehighlighter.h
win32-g++:QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
