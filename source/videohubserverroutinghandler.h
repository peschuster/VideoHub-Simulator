#ifndef VIDEOHUBSERVERROUTINGHANDLER_H
#define VIDEOHUBSERVERROUTINGHANDLER_H

class VideoHubServerRoutingHandler
{
public:
    virtual bool routingChangeRequest(int output, int input) = 0;
};

#endif // VIDEOHUBSERVERROUTINGHANDLER_H
