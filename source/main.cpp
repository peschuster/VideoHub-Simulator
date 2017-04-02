#include <QCoreApplication>
#include "videohubserver.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    VideoHubServer s;
    s.start();

    return a.exec();
}
