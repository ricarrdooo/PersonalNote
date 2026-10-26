QT += core gui widgets testlib multimedia multimediawidgets
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = editor_tests
INCLUDEPATH += ../src
SOURCES += editor_tests.cpp ../src/noteeditor.cpp ../src/codehighlighter.cpp ../src/mediaplayerdialog.cpp
HEADERS += ../src/noteeditor.h ../src/codehighlighter.h ../src/mediaplayerdialog.h
win32-g++:QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
