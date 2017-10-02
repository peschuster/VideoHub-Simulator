#include <QCoreApplication>
#include "videohubserver.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    VideoHubServer s(VideoHubServer::DeviceType_Compact_Videohub, 40, 40, VIDEOHUB_PORT);

    qDebug("Starting Videohub Server...");

    qDebug("Ctrl+C to exit application");
    s.start();

    return a.exec();
}
