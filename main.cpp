#include "webserver.h"

int main()
{
    WebServer server(7878);
    server.start();

    return 0;
}
