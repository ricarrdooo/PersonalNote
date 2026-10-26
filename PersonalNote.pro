QT += core gui widgets sql multimedia multimediawidgets
CONFIG += c++17
TEMPLATE = app
TARGET = Shiye
DEFINES += QT_DEPRECATED_WARNINGS
QMAKE_CXXFLAGS += -Wall -Wextra
win32:QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
INCLUDEPATH += src
SOURCES += src/main.cpp \
    src/mainwindow.cpp \
    src/notestore.cpp \
    src/noteeditor.cpp \
    src/codehighlighter.cpp \
    src/mediaplayerdialog.cpp \
    src/apptheme.cpp
SOURCES += src/markdownimporter.cpp src/markdownexporter.cpp third_party/md4c/md4c.c
SOURCES += src/appupdater.cpp
HEADERS += src/appupdater.h
win32:LIBS += -lwinhttp
HEADERS += src/markdownimporter.h src/markdownexporter.h third_party/md4c/md4c.h
HEADERS += src/mainwindow.h \
    src/notestore.h \
    src/noteeditor.h \
    src/codehighlighter.h \
    src/mediaplayerdialog.h \
    src/apptheme.h

