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

#include "aquariumvalve.h"

AquariumValve::AquariumValve(const QHostAddress& host, const quint16 port, QObject *parent) : QMQTT::Client(host, port, parent)
{
    m_missedWaterLevelMessage = 0;
    m_waitForTomorrow = false;
    
    connect(this, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(this, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
    connect(this, &AquariumValve::error, this, &AquariumValve::onError);
    connect(this, &AquariumValve::received, this, &AquariumValve::onReceived);
    connect(this, &AquariumValve::subscribed, this, &AquariumValve::onSubscribed);

    setClientId("aquariumvalve");
    setAutoReconnect(true);
    connectToHost();
    
    m_guard = new QTimer();
    m_guard->setInterval(1500);
    connect(m_guard, SIGNAL(timeout()), this, SLOT(missedWaterLevelMessage()));
    m_heartbeat = new QTimer();
    connect(m_heartbeat, SIGNAL(timeout()), this, SLOT(sendHeartBeat()));
    m_heartbeat->setInterval(ONE_SECOND);
    m_heartbeat->start();
}

AquariumValve::~AquariumValve()
{
}

void AquariumValve::waterValveShutoff()
{
    qDebug() << "Shutting off water";
    digitalWrite(RELAY_PIN, LOW);
    m_valveOpen = false;
    m_waitForTomorrow = true;
    QTimer::singleShot(TWENTYFOUR_HOURS, this, SLOT(itIsTomorrow()));
}

/*
 * Check to see that we aren't off in the weeds on missed messages
 * before we start.
 */
void AquariumValve::waterValveTurnon()
{
    if (m_missedWaterLevelMessage >= 3)
        return;

    if (m_waitForTomorrow)
        return;

    qDebug() << "Turning on water";
    digitalWrite(RELAY_PIN, HIGH);
    m_valveOpen = true;
    m_missedWaterLevelMessage = 0;
    QTimer::singleShot(MAX_RUNTIME, this, SLOT(waterValveShutoff()));
}

void AquariumValve::itIsTomorrow()
{
    m_waitForTomorrow = false;
}

void AquariumValve::sendHeartBeat()
{
    nlohmann::json hb;
    if (digitalRead(RELAY_PIN) == HIGH)
        hb["state"] = "on";
    else
        hb["state"] = "off";
        
    hb["missed"] = m_missedWaterLevelMessage;
        
    QByteArray payload = QByteArray::fromStdString(hb.dump());
    QMQTT::Message message(0, "aquarium/valve/heartbeat", payload);
    publish(message);
}

void AquariumValve::onDisconnected()
{
}

void AquariumValve::onConnected()
{
    subscribe("aquarium/base/#");
    subscribe("aquarium/valve/turnon");
    subscribe("aquarium/valve/turnoff");
}

void AquariumValve::onError(const QMQTT::ClientError error)
{
    qDebug() << "MQTT error: " << error;
}

void AquariumValve::onSubscribed(const QString& topic)
{
    Q_UNUSED(topic)
}

void AquariumValve::onReceived(const QMQTT::Message& message)
{
    QString topic = message.topic();
    QByteArray payload = message.payload();
    
    if (topic == "aquarium/base") {
        nlohmann::json json;
        int level = 0;

        // Missed message timer is 1100ms, so if we miss a message, it should
        // happen a bit later than the incoming base message
        // Stop the old one, to avoid a timeout, and then restart it to make sure
        // we get the next message
        m_guard->stop();
        m_guard->start();

        if (m_missedWaterLevelMessage > 0) {
            m_missedWaterLevelMessage--;
        }

        if (m_waitForTomorrow) {
            return;
        }
        // Check our sensors and levels to make sure it's safe
        // If the water level says it's high enough, or both bob sensrs
        // are showing low, PLUS, we got a trusted water level, go ahead and fill.
        try {
            json = nlohmann::json::parse(payload.toStdString());
        }
        catch (nlohmann::json::exception& e) {
            qDebug() << __FUNCTION__ << ":" << __LINE__ << ":" << e.what();
            waterValveShutoff();
            return;
        }
        try {
            level = json["data"]["waterlevel"].get<int>();
        
            if ((json["sensor"]["1"].get<int>() == 1) || (json["sensor"]["2"].get<int>() == 1)) {
                qDebug() << "Bob sensors indicate full, turning off water";
                waterValveShutoff();
                return;
            }
        }
        catch (nlohmann::json::exception& e) {
            waterValveShutoff();
            qDebug() << __FUNCTION__ << ":" << __LINE__ << ":" << e.what();
            return;
        }

        if (level >= FULL_THRESHOLD) {
            qDebug() << "Waterlevel indicates full, turning off water";
            waterValveShutoff();
            return;
        }

        try {
            if (json["trusted"].get<bool>() == false) {
                waterValveShutoff();
                return;
            }
        }
        catch (nlohmann::json::exception& e) {
            waterValveShutoff();
            qDebug() << __FUNCTION__ << ":" << __LINE__ << ":" << e.what();
            return;
        } 

        // React if we get a full notice BEFORE we get the bob sensor showing we're full
        // The bob sensors register lower than the water level measurement, so this is
        // a failsafe.
        if (level >= FULL_THRESHOLD && m_valveOpen) {
            qDebug() << "Turning water off due to threshold";
            waterValveShutoff();

            nlohmann::json json;
            json["state"] = "off";
            json["reason"] = "level";

            QByteArray payload = QByteArray::fromStdString(json.dump());
            QMQTT::Message message(0, "aquarium/valve/state", payload);
            publish(message);
            // Don't fall through in the random case other things MAY happen to make the other statements true
            return;
        }
        if (level <= MIN_SAFE_THRESHOLD && !m_valveOpen) {
            qDebug() << "Turning water on due to threshold";
            waterValveTurnon();
            
            nlohmann::json json;
            json["state"] = "on";
            json["reason"] = "level";

            QByteArray payload = QByteArray::fromStdString(json.dump());
            QMQTT::Message message(0, "aquarium/valve/state", payload);
            publish(message);
            // Don't fall through in the random case other things MAY happen to make the other statements true
            return;
        }
    }
    else if (topic == "aquarium/valve/turnon") {
        std::cout << "Turning on water, will turn of in " << MAX_RUNTIME << "ms" << std::endl;
        waterValveTurnon();

        nlohmann::json json;
        json["state"] = "on";
        json["reason"] = "valve";

        QByteArray payload = QByteArray::fromStdString(json.dump());
        QMQTT::Message message(0, "aquarium/valve/state", payload);
        publish(message);
    }
    else if (topic == "aquarium/valve/turnoff") {
        std::cout << "Turning off water as someone called the valve turnoff" << std::endl;
        waterValveShutoff();

        nlohmann::json json;
        json["state"] = "off";
        json["reason"] = "valve";

        QByteArray payload = QByteArray::fromStdString(json.dump());
        QMQTT::Message message(0, "aquarium/valve/state", payload);
        publish(message);
    }
    else if (topic == "aquarium/base/turnoff") {
        std::cout << "Turning off water because the base said to" << std::endl;
        waterValveShutoff();

        nlohmann::json json;
        json["state"] = "off";
        json["reason"] = "base";

        QByteArray payload = QByteArray::fromStdString(json.dump());
        QMQTT::Message message(0, "aquarium/valve/state", payload);
        publish(message);
    }
}

/**
 * If we miss 3 or more messages, shutdown the water
 */
void AquariumValve::missedWaterLevelMessage()
{
    if (++m_missedWaterLevelMessage >= 3) {
        if (digitalRead(RELAY_PIN) == HIGH) {
            qDebug() << "Missed too many messages...";
            waterValveShutoff();
        }
    }
}

