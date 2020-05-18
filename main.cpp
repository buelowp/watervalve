#include <QtWidgets/QApplication>
#include <QtCore/QDebug>

#include <cstring>
#include <wiringPi.h>
#include <signal.h>

#include <iostream>

#include "aquariumvalve.h"

static void signalHandler(int sig)
{
    qDebug() << "Exiting";
    digitalWrite(RELAY_PIN, LOW);
    exit(0);
}

void usage(char *progname)
{
    std::cerr << progname << ": <mqttserver> <mqttport>" << std::endl;
}

int main(int argc, char **argv) 
{
    QHostAddress server(argv[1]);
    QCoreApplication app(argc, argv);
    AquariumValve aqv(server, 1883);
    struct sigaction sa;    
    
    if (argc != 3) {
        usage(argv[0]);
        exit(-1);
    }
    
    wiringPiSetup();
    piHiPri(99);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);

    return app.exec();
}
