/*
 *	PSL1GHT Logger
 *				NZHawk
 *	A simple lib for you devs out there to include and setup
 *	network debugging quickly.
 *					  http://nzhawk.mezoka.com/pslightlogger
 */
#include "app/Log.h"

// Only a debug build (make LOG_HOST=<PC address>) sends its log, over UDP port 18194;
// a release package logs nothing. To read it on the PC: nc -u -l 18194
#ifdef LOG_HOST

#define PCPORT 18194

#include <net/net.h>

#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define INITSTRING "Logging Started\n"
#define BYESTRING "Logging Stopped\n"

static int s;
struct sockaddr_in server;
void debugInit()
{
    s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&server, 0, sizeof(server));
    server.sin_len = sizeof(server);
    server.sin_family = AF_INET;
    inet_pton(AF_INET, LOG_HOST, &server.sin_addr);
    server.sin_port = htons(PCPORT);

    sendto(s, INITSTRING, strlen(INITSTRING), 0, (struct sockaddr *)&server, sizeof(server));
}

void debugClose()
{
    sendto(s, BYESTRING, strlen(BYESTRING), 0, (struct sockaddr *)&server, sizeof(server));
    close(s);
}

void debugPrintf(const char *format, ...)
{
    if (s == -1)
        return;
    char logBuffer[1024];
    int max = sizeof(logBuffer);
    va_list va;
    va_start(va, format);

    int wrote = vsnprintf(logBuffer, max, format, va);
    if (wrote > max)
        wrote = max;
    va_end(va);
    sendto(s, logBuffer, wrote, 0, (struct sockaddr *)&server, sizeof(server));
}

#else

void debugInit() {}

void debugClose() {}

void debugPrintf(const char *, ...) {}

#endif
