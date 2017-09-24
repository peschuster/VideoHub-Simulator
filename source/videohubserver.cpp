#include "videohubserver.h"
#include <QNetworkInterface>

#define OUTPUT_COUNT  40
#define INPUT_COUNT   40

#define PORT   9990

VideoHubServer::VideoHubServer(QObject *parent) : QObject(parent), m_inputLabels(INPUT_COUNT), m_outputLabels(OUTPUT_COUNT), m_routing(OUTPUT_COUNT), m_outputLocks(OUTPUT_COUNT)
{
    connect(&m_server, SIGNAL(newConnection()), this, SLOT(onNewConnection()));

    m_inputCount = INPUT_COUNT;
    m_outputCount = OUTPUT_COUNT;

    m_version = "2.5";
    m_modelName = "Blackmagic Compact Videohub";
    m_friendlyName = "XP 40x40";

    for (int i = 0; i < m_outputCount; i++) {
        m_outputLabels.replace(i, QString("Output %1").arg(i + 1).toLatin1());
        m_routing.replace(i, i);
        m_outputLocks.replace(i, false);
    }

    for (int i = 0; i < m_inputCount; i++) {
        m_inputLabels.replace(i, QString("Input %1").arg(i + 1).toLatin1());
    }
}

QString VideoHubServer::getMacAddress()
{
    foreach(QNetworkInterface netInterface, QNetworkInterface::allInterfaces())
    {
        // Return only the first non-loopback MAC Address
        if (!(netInterface.flags() & QNetworkInterface::IsLoopBack))
            return netInterface.hardwareAddress();
    }
    return QString();
}

void VideoHubServer::start()
{
    m_server.listen(QHostAddress::Any, PORT);

    QString mac = getMacAddress();
    if (mac.length() == 0) {
        m_uniqueId = "a1b2c3d4e5f6";
    } else {
        QByteArray filtered;
        for (int i = 0; i < mac.length(); i++) {
            if (mac.at(i) != ':') {
                filtered.append(mac.at(i));
            }
        }
        m_uniqueId = QString(filtered.toLower());
    }

    publish();
}

void VideoHubServer::publish() {

    m_zeroConf.clearServiceTxtRecords();

    m_zeroConf.addServiceTxtRecord("txtvers", "1");
    m_zeroConf.addServiceTxtRecord("name", m_modelName);
    m_zeroConf.addServiceTxtRecord("class", "Videohub");
    m_zeroConf.addServiceTxtRecord("protocol version", m_version);
    m_zeroConf.addServiceTxtRecord("internal version", "FW:20-EM:6cab520c");
    m_zeroConf.addServiceTxtRecord("unqie id", m_uniqueId);

    m_zeroConf.startServicePublish(m_friendlyName.toLatin1().data(), "_blackmagic._tcp", "local", PORT);
}

void VideoHubServer::stop() {
    m_zeroConf.stopServicePublish();

    Q_FOREACH(QTcpSocket* c, m_clients) {
        c->close();
    }

    m_server.close();
}

void VideoHubServer::republish() {
    m_zeroConf.stopServicePublish();
    publish();
}

void VideoHubServer::onNewConnection()
{
    QTcpSocket* client = m_server.nextPendingConnection();

    connect(client, SIGNAL(disconnected()), client, SLOT(deleteLater()));
    connect(client, SIGNAL(disconnected()), this, SLOT(onClientConnectionClosed()));

    connect(client, SIGNAL(readyRead()), this, SLOT(onClientData()));

    m_clients.append(client);

    qDebug("Added client at %s", client->peerAddress().toString().toLatin1().data());
    qDebug("New client count: %i", m_clients.length());

    sendProtocolPreamble(client);
    sendDeviceInformation(client);
    sendInputLabels(client, false);
    sendOutputLabels(client, false);
    sendRouting(client, false);
    sendOutputLocks(client, false);
}

void VideoHubServer::onClientConnectionClosed()
{
    QTcpSocket* client = (QTcpSocket*)sender();
    Q_ASSERT(client != NULL);

    int index = m_clients.indexOf(client);
    if (index > -1) {
        m_clients.removeAt(index);
        qDebug("Removed client at %s", client->peerAddress().toString().toLatin1().data());
        qDebug("New client count: %i", m_clients.length());
    }
}

void VideoHubServer::onClientData()
{
    QTcpSocket* client = (QTcpSocket*)sender();
    Q_ASSERT(client != NULL);

    QByteArray input = client->readAll();
    qDebug("Command received \"%s\"", input.trimmed().data());
    qDebug("Message received from %s", client->peerAddress().toString().toLatin1().data());

    QList<QByteArray> message;
    QList<QByteArray> lines = input.split('\n');
    Q_FOREACH(QByteArray line, lines)
    {
        if ((line.length() == 1 && line.at(0) == '\r') || line.length() == 0) {
            if (!message.empty()) {
                ProcessStatus result = processMessage(message);
                processRequestResult(client, result);
                message.clear();
            }
        } else {
            message.append(line);
        }
    }

    if (!message.empty()) {
        ProcessStatus result = processMessage(message);
        processRequestResult(client, result);
    }
}

void VideoHubServer::processRequestResult(QTcpSocket* client, VideoHubServer::ProcessStatus result)
{
    if (result == PS_Error) {
        qDebug("Sending NAK...");
        client->write("NAK\n\n");
    } else {
        qDebug("Sending ACK...");
        client->write("ACK\n\n");

        switch (result)
        {
            case PS_InputDump:
                sendInputLabels(client, false);
                break;
            case PS_OutputDump:
                sendOutputLabels(client, false);
                break;
            case PS_RoutingDump:
                sendRouting(client, false);
                break;
            case PS_LockDump:
                sendOutputLocks(client, false);
                break;
            default:
                // NOP
                break;
        }

        Q_FOREACH(QTcpSocket* c, m_clients)
        {
            if (!m_pendingInputLabel.empty())
                sendInputLabels(c, true);

            if (!m_pendingOutputLabel.empty())
                sendOutputLabels(c, true);

            if (!m_pendingRouting.empty())
                sendRouting(c, true);

            if (!m_pendingOutputLocks.empty())
                sendOutputLocks(c, true);
        }

        m_pendingInputLabel.clear();
        m_pendingOutputLabel.clear();
        m_pendingRouting.clear();
        m_pendingOutputLocks.clear();
    }
}

VideoHubServer::ProcessStatus VideoHubServer::processMessage(QList<QByteArray> &message)
{
    if (message.length() < 1)
        return VideoHubServer::PS_Error;

    QByteArray header = message.first();
    message.pop_front();

    if (header.startsWith("PING:")) {
        return VideoHubServer::PS_Ok;
    } else if (header.startsWith("VIDEOHUB DEVICE:")) {

        if (message.length() > 0) {
            Q_FOREACH(QByteArray line, message) {
                int index = line.indexOf(':');
                if (index > 0 && index < (line.length() - 2)) {
                    QByteArray label = line.left(index + 1).trimmed();
                    QByteArray value = line.right(line.length() - index - 1).trimmed();

                    if (label == "Friendly name:") {
                        m_friendlyName = value;
                        republish();
                    }
                }
            }
        }
    } else if (header.startsWith("INPUT LABELS:")) {

        if (message.length() == 0)
            return VideoHubServer::PS_InputDump;

        Q_FOREACH(QByteArray line, message) {
            int index = line.indexOf(' ');
            QByteArray label = line.right(line.length() - index - 1).trimmed();
            int input = line.left(index).toInt();

            if (label != m_inputLabels.value(input)) {
                m_inputLabels.replace(input, label);
                m_pendingInputLabel.append(input);
            }
        }

        return VideoHubServer::PS_Ok;

    } else if (header.startsWith("OUTPUT LABELS:")) {

        if (message.length() == 0)
            return VideoHubServer::PS_OutputDump;

        Q_FOREACH(QByteArray line, message) {
            int index = line.indexOf(' ');
            QByteArray label = line.right(line.length() - index - 1).trimmed();
            int output = line.left(index).toInt();

            if (label != m_outputLabels.value(output)) {
                m_outputLabels.replace(output, label);
                m_pendingOutputLabel.append(output);
            }
        }

        return VideoHubServer::PS_Ok;

    } else if (header.startsWith("VIDEO OUTPUT ROUTING:")) {

        if (message.length() == 0)
            return VideoHubServer::PS_RoutingDump;

        Q_FOREACH(QByteArray line, message) {
            int index = line.indexOf(' ');
            int output = line.left(index).trimmed().toInt();
            int input  = line.right(line.length() - index - 1).trimmed().toInt();

            if (input != m_routing.value(output))
            {
                m_routing.replace(output, input);
                m_pendingRouting.append(output);
            }
        }

        return VideoHubServer::PS_Ok;

    } else if (header.startsWith("VIDEO OUTPUT LOCKS:")) {

        if (message.length() == 0)
            return VideoHubServer::PS_RoutingDump;

        Q_FOREACH(QByteArray line, message) {
            int index = line.indexOf(' ');
            int output = line.left(index).trimmed().toInt();
            QByteArray lock  = line.right(line.length() - index - 1).trimmed();

            bool currentLock = m_outputLocks.value(output);
            if ((currentLock && lock == "U") || (!currentLock && (lock == "O" || lock == "L" || lock == "F"))) {
                m_outputLocks.replace(output, !(lock == "U"));
                m_pendingOutputLocks.append(output);
            }
        }

        return VideoHubServer::PS_Ok;
    }

    return VideoHubServer::PS_Error;
}

void VideoHubServer::sendProtocolPreamble(QTcpSocket* client) {
    Q_ASSERT(client != NULL);

    QString message = QString("PROTOCOL PREAMBLE:\nVersion: %1\n\n").arg(m_version);
    client->write(message.toLatin1());
    qDebug("SEND: %s", message.toLatin1().data());
}

void VideoHubServer::sendDeviceInformation(QTcpSocket* client) {
    Q_ASSERT(client != NULL);

    QList<QString> lines;
    lines.append(QString("Device present: true"));
    lines.append(QString("Model name: %1").arg(m_modelName));
    lines.append(QString("Friendly name: %1").arg(m_friendlyName));
    lines.append(QString("Unique ID: %1").arg(m_uniqueId));
    lines.append(QString("Video inputs: %1").arg(m_inputCount));
    lines.append(QString("Video processing units: 0"));
    lines.append(QString("Video outputs: %1").arg(m_outputCount));
    lines.append(QString("Video monitoring outputs: 0"));
    lines.append(QString("Serial ports: 0"));

    QString header = "VIDEOHUB DEVICE";
    send(client, header, lines);
}

void VideoHubServer::sendInputLabels(QTcpSocket* client, bool pending)
{
    Q_ASSERT(client != NULL);

    QList<QString> lines;

    if (pending) {
        Q_FOREACH(int input, m_pendingInputLabel) {
            lines.append(QString("%1 %2").arg(input).arg(QString(m_inputLabels.value(input))));
        }
    } else {
        for(int input = 0; input < m_inputCount; input++) {
            lines.append(QString("%1 %2").arg(input).arg(QString(m_inputLabels.value(input))));
        }
    }

    QString header = "INPUT LABELS";
    send(client, header, lines);
}

void VideoHubServer::sendOutputLabels(QTcpSocket* client, bool pending)
{
    Q_ASSERT(client != NULL);

    QList<QString> lines;

    if (pending) {
        Q_FOREACH(int output, m_pendingOutputLabel) {
            lines.append(QString("%1 %2").arg(output).arg(QString(m_outputLabels.value(output))));
        }
    } else {
        for(int output = 0; output < m_outputCount; output++) {
            lines.append(QString("%1 %2").arg(output).arg(QString(m_outputLabels.value(output))));
        }
    }

    QString header = "OUTPUT LABELS";
    send(client, header, lines);
}

void VideoHubServer::sendRouting(QTcpSocket* client, bool pending)
{
    Q_ASSERT(client != NULL);

    QList<QString> lines;

    if (pending) {
        Q_FOREACH(int output, m_pendingRouting) {
            lines.append(QString("%1 %2").arg(output).arg(m_routing.value(output)));
        }
    } else {
        for(int output = 0; output < m_outputCount; output++) {
            lines.append(QString("%1 %2").arg(output).arg(m_routing.value(output)));
        }
    }

    QString header = "VIDEO OUTPUT ROUTING";
    send(client, header, lines);
}

void VideoHubServer::sendOutputLocks(QTcpSocket* client, bool pending)
{
    Q_ASSERT(client != NULL);

    QList<QString> lines;

    if (pending) {
        Q_FOREACH(int output, m_pendingOutputLocks) {
            lines.append(QString("%1 %2").arg(output).arg(m_outputLocks.value(output) ? "L" : "U"));
        }
    } else {
        for(int output = 0; output < m_outputCount; output++) {
            lines.append(QString("%1 %2").arg(output).arg(m_outputLocks.value(output) ? "L" : "U"));
        }
    }

    QString header = "VIDEO OUTPUT LOCKS";
    send(client, header, lines);
}

void VideoHubServer::send(QTcpSocket* client, QString &header, QList<QString> &data)
{
    Q_ASSERT(client != NULL);

    QByteArray raw;

    raw.append(header.toLatin1());
    raw.append(":\n");

    Q_FOREACH(QString line, data)
    {
        raw.append(line.toLatin1());
        raw.append("\n");
    }

    raw.append("\n");

    client->write(raw);
    qDebug("SEND: %s", raw.data());
}
