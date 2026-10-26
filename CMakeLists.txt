cmake_minimum_required(VERSION 3.16)
project(Shiye VERSION 0.4.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql Multimedia MultimediaWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql Multimedia MultimediaWidgets)
add_executable(Shiye WIN32
    src/main.cpp src/mainwindow.cpp src/mainwindow.h
    src/notestore.cpp src/notestore.h
    src/noteeditor.cpp src/noteeditor.h
    src/codehighlighter.cpp src/codehighlighter.h
    src/mediaplayerdialog.cpp src/mediaplayerdialog.h
    src/apptheme.cpp src/apptheme.h
    src/appupdater.cpp src/appupdater.h
    src/markdownimporter.cpp src/markdownimporter.h src/markdownexporter.cpp src/markdownexporter.h third_party/md4c/md4c.c)
target_link_libraries(Shiye PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Multimedia Qt${QT_VERSION_MAJOR}::MultimediaWidgets)
if(WIN32)
    target_link_libraries(Shiye PRIVATE winhttp)
endif()
if(MSVC)
    target_compile_options(Shiye PRIVATE /utf-8)
elseif(MINGW)
    target_compile_options(Shiye PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

