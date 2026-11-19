/*
    Copyright 2019 natinusala
    Copyright 2019 WerWolv
    Copyright 2019 p-sam

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <switch.h>
#include <unistd.h>

static int nxlink_sock = -1;

// The default config caps a UDP socket's receive buffer at 0xA500 bytes,
// far below what a streamed 4K keyframe needs. The service may accept a
// larger cap and still be unable to back it, so a size is only kept if a
// stream's worth of UDP sockets can then be opened with it and receive.
#define UDP_PROBE_SOCKETS 4
#define UDP_PROBE_WAIT_MS 100

static const u32 udp_rx_buf_sizes[] = { 1 << 20, 512 << 10, 256 << 10 };

static int udpLoopback(int fd)
{
    struct sockaddr_in local;
    socklen_t length = sizeof(local);
    if (getsockname(fd, (struct sockaddr*)&local, &length) != 0)
        return errno;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    unsigned char byte = 0x5a;
    if (sendto(fd, &byte, 1, 0, (const struct sockaddr*)&local, sizeof(local)) != 1)
        return errno == ENETUNREACH || errno == EHOSTUNREACH || errno == EADDRNOTAVAIL ? 0 : errno;

    struct pollfd ready = { .fd = fd, .events = POLLIN };
    if (poll(&ready, 1, UDP_PROBE_WAIT_MS) != 1)
        return ETIMEDOUT;
    return recv(fd, &byte, 1, 0) == 1 ? 0 : errno;
}

static int udpProbe(int size)
{
    int fds[UDP_PROBE_SOCKETS];
    int count = 0;
    int error = 0;
    while (count < UDP_PROBE_SOCKETS && error == 0)
    {
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0)
        {
            error = errno;
            break;
        }
        fds[count++] = fd;

        struct sockaddr_in local = { .sin_len = sizeof(local), .sin_family = AF_INET };
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) != 0
            || bind(fd, (const struct sockaddr*)&local, sizeof(local)) != 0)
            error = errno;
    }
    if (error == 0)
        error = udpLoopback(fds[0]);
    while (count > 0)
        close(fds[--count]);
    return error;
}

static void socketInitializeLargeUdp(SocketInitConfig cfg)
{
    for (size_t i = 0; i < sizeof(udp_rx_buf_sizes) / sizeof(udp_rx_buf_sizes[0]); i++)
    {
        cfg.udp_rx_buf_size = udp_rx_buf_sizes[i];
        Result rc           = socketInitialize(&cfg);
        if (R_FAILED(rc))
        {
            printf("UDP receive buffer %u: rc=0x%x\n", (unsigned)cfg.udp_rx_buf_size, (unsigned)rc);
            continue;
        }
        int error = udpProbe((int)cfg.udp_rx_buf_size);
        if (error == 0)
        {
            printf("UDP receive buffer %u: ok\n", (unsigned)cfg.udp_rx_buf_size);
            return;
        }
        printf("UDP receive buffer %u: errno=%d\n", (unsigned)cfg.udp_rx_buf_size, error);
        socketExit();
    }
    cfg.udp_rx_buf_size = socketGetDefaultInitConfig()->udp_rx_buf_size;
    socketInitialize(&cfg);
}

void userAppInit()
{
    printf("userAppInit\n");
    appletLockExit();

    // Init network
    SocketInitConfig cfg = *(socketGetDefaultInitConfig());
    AppletType at        = appletGetAppletType();
    if (at == AppletType_Application || at == AppletType_SystemApplication)
    {
        cfg.num_bsd_sessions = 12; // default is 3
        cfg.sb_efficiency    = 8; // default is 4
        socketInitializeLargeUdp(cfg);
    }
    else
    {
        cfg.num_bsd_sessions = 2;
        cfg.sb_efficiency    = 1;
        socketInitialize(&cfg);
    }

#ifdef DEBUG
    nxlink_sock = nxlinkStdio();
#endif

    romfsInit();
    hidsysInitialize();
    inssInitialize();
    plInitialize(PlServiceType_User);
    setsysInitialize();
    setInitialize();
    psmInitialize();
    nifmInitialize(NifmServiceType_User);
    lblInitialize();
}

void userAppExit()
{
    printf("userAppExit\n");

    // backlight
    lblExit();
    // network state
    nifmExit();
    // power state
    psmExit();
    // system language...
    setExit();
    // system theme, system version...
    setsysExit();
    // system font
    plExit();

    inssExit();
    hidsysExit();
    romfsExit();

    if (nxlink_sock != -1)
        close(nxlink_sock);

    socketExit();

    appletUnlockExit();
}
