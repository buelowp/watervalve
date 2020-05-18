TEMPLATE = app

QT += qmqtt network
QT -= gui

CONFIG += debug

MOC_DIR = .moc
OBJECTS_DIR = .obj

QMAKE_CXXFLAGS += -fcompare-debug-second

LIBS += -lwiringPi
SOURCES = main.cpp aquariumvalve.cpp
HEADERS = aquariumvalve.h
