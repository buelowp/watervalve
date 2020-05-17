TEMPLATE = app

QT += qmqtt network
QT -= gui

CONFIG += debug

MOC_DIR = .moc
OBJECTS_DIR = .obj

LIBS += -lwiringPi
SOURCES = main.cpp aquariumvalve.cpp
HEADERS = aquariumvalve.h
