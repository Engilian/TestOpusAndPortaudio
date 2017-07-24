QT += core
QT -= gui

CONFIG += c++11

TARGET = TestOpusCodec
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

DESTDIR = ../../dist/test

SOURCES += main.cpp

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0


#PortAudio lib



unix {

LIBS += -lportaudio

INCLUDEPATH += $$PWD/external_libs/portaudio/include
DEPENDPATH += $$PWD/external_libs/portaudio/include

}


win32 {

LIBS += -L$$PWD/external_libs/portaudio/lib/win32/ -lportaudio_x86

INCLUDEPATH += $$PWD/external_libs/portaudio/include
DEPENDPATH += $$PWD/external_libs/portaudio/include

}

# Opus

unix {

LIBS += -lopus

INCLUDEPATH += $$PWD/external_libs/opus/include
DEPENDPATH  += $$PWD/external_libs/opus/include

}

win32 {

LIBS += -L$$PWD/external_libs/opus/lib/win32 -lopus

INCLUDEPATH += $$PWD/external_libs/opus/include
DEPENDPATH += $$PWD/external_libs/opus/include

}
