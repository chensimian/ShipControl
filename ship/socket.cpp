#include <iostream>
#include <chrono>
#include <memory>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "socket.h"
#include "misc.h"

using namespace std::chrono;

int connect_timeout = 3000;

#ifdef __CYGWIN32__
#undef _WIN32
#endif

#ifdef _WIN32
#ifndef ECONNRESET
#define ECONNRESET WSAECONNRESET
#endif	/* not ECONNRESET */
#endif /* _WI32 */

/* informations for SOCKS */
#define SOCKS5_REP_SUCCEEDED    0x00    /* succeeded */
#define SOCKS5_REP_FAIL         0x01    /* general SOCKS serer failure */
#define SOCKS5_REP_NALLOWED     0x02    /* connection not allowed by ruleset */
#define SOCKS5_REP_NUNREACH     0x03    /* Network unreachable */
#define SOCKS5_REP_HUNREACH     0x04    /* Host unreachable */
#define SOCKS5_REP_REFUSED      0x05    /* connection refused */
#define SOCKS5_REP_EXPIRED      0x06    /* TTL expired */
#define SOCKS5_REP_CNOTSUP      0x07    /* Command not supported */
#define SOCKS5_REP_ANOTSUP      0x08    /* Address not supported */
#define SOCKS5_REP_INVADDR      0x09    /* Invalid address */

/* SOCKS5 authentication methods */
#define SOCKS5_AUTH_REJECT      0xFF    /* No acceptable auth method */
#define SOCKS5_AUTH_NOAUTH      0x00    /* without authentication */
#define SOCKS5_AUTH_GSSAPI      0x01    /* GSSAPI */
#define SOCKS5_AUTH_USERPASS    0x02    /* User/Password */
#define SOCKS5_AUTH_CHAP        0x03    /* Challenge-Handshake Auth Proto. */
#define SOCKS5_AUTH_EAP         0x05    /* Extensible Authentication Proto. */
#define SOCKS5_AUTH_MAF         0x08    /* Multi-Authentication Framework */

/* socket related definitions */
#ifndef _WIN32
#define SOCKET int
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif /* WIN32 */

/* packet operation macro */
#define PUT_BYTE(ptr, data) (*(unsigned char* )(ptr) = (unsigned char)(data))

SOCKET initSocket(int af, int type, int protocol)
{
    SOCKET s = socket(af, type, protocol);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int));
#ifdef SO_NOSIGPIPE
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (char *)&one, sizeof(int));
#endif
    return s;
}

int Send(SOCKET sHost, const char* data, int len, int flags)
{
#ifdef _WIN32
    return send(sHost, data, len, flags);
#else
    return send(sHost, data, len, flags | MSG_NOSIGNAL);
#endif // _WIN32
}

int Recv(SOCKET sHost, char* data, int len, int flags)
{
#ifdef _WIN32
    return recv(sHost, data, len, flags);
#else
    return recv(sHost, data, len, flags | MSG_NOSIGNAL);
#endif // _WIN32
}

int getNetworkType(std::string addr)
{
    if(isIPv4(addr))
        return AF_INET;
    else if(isIPv6(addr))
        return AF_INET6;
    else
        return AF_UNSPEC;
}

int setTimeout(SOCKET s, int timeout)
{
    int ret = -1;
#ifdef _WIN32
    ret = setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(int));
    ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(int));
#else
    struct timeval timeo = {timeout / 1000, (timeout % 1000) * 1000};
    ret = setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeo, sizeof(timeo));
    ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeo, sizeof(timeo));
#endif
    return ret;
}

int setSocketBlocking(SOCKET s, bool blocking)
{
#ifdef _WIN32
    unsigned long ul = !blocking;
    return ioctlsocket(s, FIONBIO, &ul); //set to non-blocking mode
#else
    int flags = fcntl(s, F_GETFL, 0);
    if(flags == -1)
        return -1;
    return fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
#endif // _WIN32
}

int connect_adv(SOCKET sockfd, const struct sockaddr* addr, int addrsize)
{
    int ret = -1;
    int error = 1;
    struct timeval tm;
    fd_set set;

    int len = sizeof(int);
    if(setSocketBlocking(sockfd, false) == -1)
        return -1;
    if(connect(sockfd, addr, addrsize) == -1)
    {
        tm.tv_sec = connect_timeout / 1000;
        tm.tv_usec = (connect_timeout % 1000) * 1000;
        FD_ZERO(&set);
        FD_SET(sockfd, &set);
        if(select(sockfd + 1, NULL, &set, NULL, &tm) > 0)
        {
            getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, (socklen_t *)&len);
            if(error == 0)
                ret = 0;
            else
                ret = 1;
        }
        else
            ret = 1;
    }
    else
        ret = 0;

    if(setSocketBlocking(sockfd, true) == -1)
        return -1;
    return ret;
}

int startConnect(SOCKET sHost, std::string addr, int port)
{
    int retVal = -1;
    struct sockaddr_in servAddr = {};
    struct sockaddr_in6 servAddr6 = {};
    if(isIPv4(addr))
    {
        servAddr.sin_family = AF_INET;
        servAddr.sin_port = htons((short)port);
        inet_pton(AF_INET, addr.data(), (struct in_addr *)&servAddr.sin_addr.s_addr);
        retVal = connect_adv(sHost, reinterpret_cast<sockaddr *>(&servAddr), sizeof(servAddr));
    }
    else if(isIPv6(addr))
    {
        servAddr6.sin6_family = AF_INET6;
        servAddr6.sin6_port = htons((short)port);
        inet_pton(AF_INET6, addr.data(), (struct in_addr6 *)&servAddr6.sin6_addr);
        retVal = connect_adv(sHost, reinterpret_cast<sockaddr *>(&servAddr6), sizeof(servAddr6));
    }
    return retVal;
}

int send_simple(SOCKET sHost, std::string data)
{
    return Send(sHost, data.data(), data.size(), 0);
}

int simpleSend(std::string addr, int port, std::string data)
{
    SOCKET sHost = socket(getNetworkType(addr), SOCK_STREAM, IPPROTO_IP);
    if(sHost == INVALID_SOCKET)
        return SOCKET_ERROR;
    if(startConnect(sHost, addr, port) != 0)
        return SOCKET_ERROR;
    setTimeout(sHost, 3000);
    unsigned int retVal = send_simple(sHost, data);
    if(retVal == data.size())
    {
        closesocket(sHost);
#ifdef _WIN32
        return WSAGetLastError();
#else
        return 0;
#endif // _WIN32
    }
    else
    {
        closesocket(sHost);
        return SOCKET_ERROR;
    }
}

std::string sockaddrToIPAddr(sockaddr *addr)
{
    std::string retAddr;
    char cAddr[128] = {};
    struct sockaddr_in *target;
    struct sockaddr_in6 *target6;
    if(addr->sa_family == AF_INET)
    {
        target = reinterpret_cast<struct sockaddr_in *>(addr);
        inet_ntop(AF_INET, &target->sin_addr, cAddr, sizeof(cAddr));
    }
    else if(addr->sa_family == AF_INET6)
    {
        target6 = reinterpret_cast<struct sockaddr_in6 *>(addr);
        inet_ntop(AF_INET6, &target6->sin6_addr, cAddr, sizeof(cAddr));
    }
    retAddr.assign(cAddr);
    return retAddr;
}

std::string hostnameToIPAddr(std::string host)
{
    int retVal;
    std::string retstr;
    struct addrinfo hint = {}, *retAddrInfo = NULL, *cur;
    defer(if(retAddrInfo) freeaddrinfo(retAddrInfo));
    retVal = getaddrinfo(host.data(), NULL, &hint, &retAddrInfo);
    if(retVal != 0)
        return std::string();

    for(cur = retAddrInfo; cur != NULL; cur = cur->ai_next)
    {
        retstr = sockaddrToIPAddr(cur->ai_addr);
        if(!retstr.empty())
            break;
    }
    return retstr;
}
