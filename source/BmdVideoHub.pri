INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD

QT += network

# Enable very detailed debug messages when compiling the debug version
CONFIG(debug, debug|release) {
    DEFINES += SUPERVERBOSE
}

HEADERS += $$PWD/videohubserver.h

SOURCES += $$PWD/videohubserver.cpp
