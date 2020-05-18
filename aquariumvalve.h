/*
 * Copyright (c) 2020 <copyright holder> <email>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef AQUARIUMVALVE_H
#define AQUARIUMVALVE_H

#define FULL_THRESHOLD          2770
#define MIN_SAFE_THRESHOLD      2730
#define MAX_RUNTIME             (10 * 1000)
#define RELAY_PIN               7
#define ONE_SECOND              (1000)
#define ONE_MINUTE              (ONE_SECOND * 60)
#define ONE_HOUR                (ONE_MINUTE * 60)
#define TWENTYFOUR_HOURS        (ONE_HOUR * 24)

#include <iostream>
#include <QtCore/QtCore>
#include <QtNetwork/QtNetwork>
#include <QtQmqtt/QtQmqtt>
#include <wiringPi.h>
#include <nlohmann/json.hpp>

class AquariumValve : public QMQTT::Client
{
    Q_OBJECT
public:
    explicit AquariumValve(const QHostAddress& host, const quint16 port = 1883, QObject* parent = NULL);
    virtual ~AquariumValve();

public slots:
    void onReceived(const QMQTT::Message& message);
    void onConnected();
    void onDisconnected();
    void onSubscribed(const QString& topic);
    void onError(const QMQTT::ClientError error);
    void waterValveShutoff();
    void missedWaterLevelMessage();
    void sendHeartBeat();
    void itIsTomorrow();

private:
    QTimer *m_guard;
    QTimer *m_heartbeat;
    QTimer *m_tomorrow;
    QHostAddress m_host;
    quint16 m_port;
    int m_missedWaterLevelMessage;
    bool m_valveOpen;
    bool m_waitForTomorrow;
};

#endif // AQUARIUMVALVE_H
