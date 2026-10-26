QT += core gui widgets sql testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = knowledge_tests
INCLUDEPATH += ../src
SOURCES += knowledge_tests.cpp ../src/notestore.cpp ../src/noteeditor.cpp ../src/codehighlighter.cpp ../src/markdownimporter.cpp ../src/markdownexporter.cpp ../third_party/md4c/md4c.c
HEADERS += ../src/notestore.h ../src/noteeditor.h ../src/codehighlighter.h ../src/markdownimporter.h ../src/markdownexporter.h
win32-g++:QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
