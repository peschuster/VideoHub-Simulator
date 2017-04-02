#include "videohubserver.h"

#define OUTPUT_COUNT  40
#define INPUT_COUNT   40

VideoHubServer::VideoHubServer(QObject *parent) : QObject(parent), m_inputLabels(INPUT_COUNT), m_outputLabels(OUTPUT_COUNT), m_routing(OUTPUT_COUNT), m_outputLocks(OUTPUT_COUNT)
{
    connect(&m_server, SIGNAL(newConnection()), this, SLOT(onNewConnection()));

    m_inputCount = INPUT_COUNT;
    m_outputCount = OUTPUT_COUNT;

    m_version = "2.3";

    for (int i = 0; i < m_outputCount; i++) {
        m_outputLabels.replace(i, QString("Output %1").arg(i + 1).toLatin1());
        m_routing.replace(i, i);
        m_outputLocks.replace(i, false);
    }

    for (int i = 0; i < m_inputCount; i++) {
        m_inputLabels.replace(i, QString("Input %1").arg(i + 1).toLatin1());
    }
}

void VideoHubServer::start()
{
    m_server.listen(QHostAddress::Any, 9990);

    m_zeroConf.startServicePublish("_videohub", "_blackmagic._tcp", "local", 9990);
}

void VideoHubServer::onNewConnection()
{
    QTcpSocket* client = m_server.nextPendingConnection();

    connect(client, SIGNAL(disconnected()), client, SLOT(deleteLater()));
    connect(client, SIGNAL(disconnected()), this, SLOT(onClientConnectionClosed()));

    connect(client, SIGNAL(readyRead()), this, SLOT(onClientData()));

    m_clients.append(client);

    sendProtocolPreamble(client);
    sendDeviceInformation(client);
    sendInputLabels(client, false);
    sendOutputLabels(client, false);
    sendRouting(client, false);
    sendOutputLocks(client, false);
}

void VideoHubServer::onClientConnectionClosed()
{
    QTcpSocket* obj = (QTcpSocket*)sender();
    Q_ASSERT(obj != NULL);

    int index = m_clients.indexOf(obj);
    if (index > -1) {
        m_clients.removeAt(index);
    }
}

void VideoHubServer::onClientData()
{
    QTcpSocket* client = (QTcpSocket*)sender();
    Q_ASSERT(client != NULL);

    QByteArray input = client->readAll();
    qDebug("VideoHub received: %s", input.data());

    QList<QByteArray> message;
    QList<QByteArray> lines = input.split('\n');
    Q_FOREACH(QByteArray line, lines)
    {
        if ((line.length() == 1 && line.at(0) == '\r') || line.length() == 0) {
            processMessage(message);
            message.clear();
        } else {
            message.append(line);
        }
    }

    ProcessStatus result = processMessage(message);

    if (result == PS_Error) {
        client->write("NAK\n\n");
    } else {
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
            switch (result)
            {
                case PS_InputChanged:
                    sendInputLabels(c, true);
                    break;
                case PS_OutputChanged:
                    sendOutputLabels(c, true);
                    break;
                case PS_RoutingChanged:
                    sendRouting(c, true);
                    break;
                case PS_LockChanged:
                    sendOutputLocks(c, true);
                    break;
                default:
                    // NOP
                    break;
            }
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
}

void VideoHubServer::sendDeviceInformation(QTcpSocket* client) {
    Q_ASSERT(client != NULL);

    QList<QString> lines;
    lines.append(QString("Device present: true"));
    lines.append(QString("Model name: Blackmagic Videohub %1x%2").arg(m_inputCount).arg(m_outputCount));
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
}
