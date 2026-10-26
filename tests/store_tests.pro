QT += core gui widgets sql testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = store_tests
INCLUDEPATH += ../src
SOURCES += store_tests.cpp ../src/notestore.cpp
HEADERS += ../src/notestore.h
win32-g++:QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
