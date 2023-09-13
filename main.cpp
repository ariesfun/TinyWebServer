#include "webserver.h"

int main()
{
    WebServer server(7878, 8, 100000);
    server.start();

    return 0;
}
