#ifndef VIDEOHUBSERVER_H
#define VIDEOHUBSERVER_H

#include <QObject>
#include <QList>
#include <QTcpSocket>
#include <QtNetwork/QTcpServer>
#include "qzeroconf.h"

class VideoHubServer : public QObject
{
    Q_OBJECT
public:
    enum ProcessStatus {
        PS_Error = -1,
        PS_Ok = 0,
        PS_InputDump,
        PS_OutputDump,
        PS_RoutingDump,
        PS_LockDump,
    };

    enum InOutType {
        Input = 1,
        Output = 2
    };

    enum VideoHubDeviceType {
        DeviceType_Videohub_Server,
        DeviceType_Local_USB_Videohub,
        DeviceType_Videohub,
        DeviceType_Workgroup_Videohub,
        DeviceType_Broadcast_Videohub,
        DeviceType_Studio_Videohub,
        DeviceType_Enterprise_Videohub,
        DeviceType_Micro_Videohub,
        DeviceType_Smart_Videohub,
        DeviceType_Compact_Videohub,
        DeviceType_Universal_Videohub,
        DeviceType_Universal_Videohub_72,
        DeviceType_Universal_Videohub_288,
        DeviceType_Smart_Videohub_12_x_12,
        DeviceType_Smart_Videohub_20_x_20,
        DeviceType_Smart_Videohub_40_x_40,
        DeviceType_MultiView_16,
        DeviceType_MultiView_4
    };

private:
    QTcpServer m_server;
    QZeroConf m_zeroConf;

    QList<QTcpSocket*> m_clients;

    VideoHubDeviceType m_deviceType;
    QString m_modelName;
    QString m_friendlyName;
    QString m_uniqueId;
    QString m_version;
    int m_inputCount;
    int m_outputCount;

    QVector<QByteArray> m_inputLabels;
    QVector<QByteArray> m_outputLabels;
    QVector<int> m_routing;
    QVector<bool> m_outputLocks;

    QList<int> m_pendingInputLabel;
    QList<int> m_pendingOutputLabel;
    QList<int> m_pendingRouting;
    QList<int> m_pendingOutputLocks;
public:
    explicit VideoHubServer(VideoHubDeviceType deviceType, QObject *parent = 0);
    void start();
    void stop();
    void republish();
protected:
    void publish();
    ProcessStatus processMessage(QList<QByteArray> &message);
    void processRequestResult(QTcpSocket* client, ProcessStatus status);
    void sendProtocolPreamble(QTcpSocket* client);
    void sendDeviceInformation(QTcpSocket* client);
    void sendInputLabels(QTcpSocket* client, bool pending);
    void sendOutputLabels(QTcpSocket* client, bool pending);
    void sendRouting(QTcpSocket* client, bool pending);
    void sendOutputLocks(QTcpSocket* client, bool pending);
    void send(QTcpSocket* client, QString &header, QList<QString> &data);
    QString getMacAddress();
    QString getName(VideoHubDeviceType deviceType);
signals:
    void nameChanged(QString &newName, QString &oldName);
    void routingChanged(int output, int newInput, int oldInput);
    void labelChanged(InOutType type, int number, QString &newLabel, QString &oldLabel);
    void lockChanged(int output, bool newState);

protected slots:
    void onNewConnection();
    void onClientData();
    void onClientConnectionClosed();
};

#endif // VIDEOHUBSERVER_H
