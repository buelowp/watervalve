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

    setClientId("aquariumvalve");
    connectToHost();
    
    m_guard = new QTimer();
    connect(m_guard, SIGNAL(timeout()), this, SLOT(missedWaterLevelMessage()));
    m_heartbeat = new QTimer();
    connect(m_heartbeat, SIGNAL(timeout()), this, SLOT(sendHeartBeat()));
    m_heartbeat->setInterval(ONE_SECOND);
    m_heartbeat->start();
    m_tomorrow = new QTimer();
    connect(m_tomorrow, SIGNAL(timeout()), this, SLOT(itIsTomorrow()));
}

AquariumValve::~AquariumValve()
{
}

void AquariumValve::waterValveShutoff()
{
    digitalWrite(RELAY_PIN, LOW);
    m_valveOpen = false;
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
    qDebug() << payload;
}

void AquariumValve::onDisconnected()
{
}

void AquariumValve::onConnected()
{
    qDebug() << __FUNCTION__;
    subscribe("aquarium/base/#", 0);
    subscribe("aquarium/valve/#", 0);
}

void AquariumValve::onError(const QMQTT::ClientError error)
{
    qDebug() << "MQTT error: " << error;
}

void AquariumValve::onReceived(const QMQTT::Message& message)
{
    QString topic = message.topic();
    QByteArray payload = message.payload();
    
    if (topic == "aquarium/base") {
        if (m_waitForTomorrow)
            return;
        
        auto json = nlohmann::json::parse(payload.toStdString());
        int level = json["data"]["waterlevel"].get<int>();
        
        if (json["trusted"].get<bool>() == false)
            return;
        
        if (m_missedWaterLevelMessage > 0) {
            m_missedWaterLevelMessage--;
        }
        
        if (level >= FULL_THRESHOLD && m_valveOpen) {
            digitalWrite(RELAY_PIN, LOW);
            m_guard->stop();
            m_valveOpen = false;
            nlohmann::json json;
            json["state"] = "off";
            json["reason"] = "level";
            QByteArray payload = QByteArray::fromStdString(json.dump());
            QMQTT::Message message(0, "aquarium/valve/state", payload);
            publish(message);
        }
        if (level <= MIN_SAFE_THRESHOLD && !m_valveOpen) {
            m_missedWaterLevelMessage = 0;
            digitalWrite(RELAY_PIN, HIGH);
            m_valveOpen = true;
            QTimer::singleShot(MAX_RUNTIME, this, SLOT(waterValveShutoff()));
            
            m_guard->stop();
            m_guard->setInterval(ONE_SECOND);
            
            nlohmann::json json;
            json["state"] = "off";
            json["reason"] = "level";

            QByteArray payload = QByteArray::fromStdString(json.dump());
            QMQTT::Message message(0, "aquarium/valve/state", payload);
            publish(message);
        }
    }
    else if (topic == "aquarium/valve/turnon") {
        if (m_waitForTomorrow)
            return;
        
        std::cout << "Turning on water, will turn of in " << MAX_RUNTIME << "ms" << std::endl;
        digitalWrite(RELAY_PIN, HIGH);
        QTimer::singleShot(MAX_RUNTIME, this, SLOT(waterValveShutoff()));
        nlohmann::json json;
        json["state"] = "on";
        json["reason"] = "valve";

        QByteArray payload = QByteArray::fromStdString(json.dump());
        QMQTT::Message message(0, "aquarium/valve/state", payload);
        publish(message);
    }
    else if (topic == "aquarium/valve/turnoff") {
        std::cout << "Turning off water" << std::endl;
        digitalWrite(RELAY_PIN, LOW);
        m_guard->stop();
        QTimer::singleShot(TWENTYFOUR_HOURS, this, SLOT(itIsTomorrow()));
        m_waitForTomorrow = true;
        nlohmann::json json;
        json["state"] = "off";
        json["reason"] = "valve";

        QByteArray payload = QByteArray::fromStdString(json.dump());
        QMQTT::Message message(0, "aquarium/valve/state", payload);
        publish(message);
    }
    else if (topic == "aquarium/base/turnoff") {
        std::cout << "Turning off water" << std::endl;
        digitalWrite(RELAY_PIN, LOW);
        QTimer::singleShot(TWENTYFOUR_HOURS, this, SLOT(itIsTomorrow()));
        m_waitForTomorrow = true;
        nlohmann::json json;
        json["state"] = "off";
        json["reason"] = "base";

        QByteArray payload = QByteArray::fromStdString(json.dump());
        QMQTT::Message message(0, "aquarium/valve/state", payload);
        publish(message);
    }
}

void AquariumValve::missedWaterLevelMessage()
{
    m_missedWaterLevelMessage++;
    
    if (m_missedWaterLevelMessage >= 3) {
        digitalWrite(RELAY_PIN, LOW);
    }
    m_guard->stop();
    m_waitForTomorrow = true;
}

