#include "AuthClient.h"
#include "net/NetProtocol.h"
#include <cstdio>
#include <cstring>

// ── Platform socket boilerplate ───────────────────────────────────────────────
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using NativeSock = SOCKET;
static constexpr NativeSock kBad = INVALID_SOCKET;
static void SockInit()         { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
static void SockCleanup()      { WSACleanup(); }
static void SockClose(SOCKET s){ closesocket(s); }
// 10-second recv timeout (generous for Railway cold-starts)
static void SetRecvTimeout(SOCKET s) {
    DWORD ms = 10000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
}
#else
#  include <sys/socket.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <sys/select.h>
using NativeSock = int;
static constexpr NativeSock kBad = -1;
static void SockInit()    {}
static void SockCleanup() {}
static void SockClose(int s) { close(s); }
static void SetRecvTimeout(int s) {
    timeval tv{10, 0};   // 10-second timeout
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}
#endif

// ── Framing helpers ───────────────────────────────────────────────────────────
static void WriteU16BE(uint8_t* dst, uint16_t v) {
    dst[0] = (v >> 8) & 0xFF;
    dst[1] =  v       & 0xFF;
}
static uint16_t ReadU16BE(const uint8_t* s) {
    return (uint16_t(s[0]) << 8) | s[1];
}

static bool SendFramed(NativeSock s, const void* data, int len) {
    uint8_t hdr[2];
    WriteU16BE(hdr, (uint16_t)len);
    send(s, (const char*)hdr,  2,   0);
    send(s, (const char*)data, len, 0);
    return true;
}

// Blocking recv that fills buf exactly 'need' bytes (or returns false on error/timeout).
static bool RecvExact(NativeSock s, uint8_t* buf, int need) {
    int got = 0;
    while (got < need) {
#ifdef _WIN32
        int n = recv(s, (char*)(buf + got), need - got, 0);
#else
        int n = (int)recv(s, buf + got, need - got, 0);
#endif
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

// ── AuthClient::Send ──────────────────────────────────────────────────────────
AuthResult AuthClient::Send(uint8_t type,
                             const std::string& host, uint16_t port,
                             const std::string& username, const std::string& password) {
    SockInit();

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);

    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;

    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) {
        SockCleanup();
        return {false, "Could not resolve server address"};
    }

    NativeSock s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == kBad) {
        freeaddrinfo(res);
        SockCleanup();
        return {false, "Socket creation failed"};
    }

    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        SockClose(s);
        freeaddrinfo(res);
        SockCleanup();
        return {false, "Server unavailable — try again later"};
    }
    freeaddrinfo(res);

    // 5-second receive timeout so we never block forever
    SetRecvTimeout(s);

    // Build + send auth packet
    PktAuthRequest pkt{};
    pkt.type = type;
    std::strncpy(pkt.username, username.c_str(), sizeof(pkt.username) - 1);
    pkt.passHash = NetHashPassword(username.c_str(), password.c_str());
    SendFramed(s, &pkt, sizeof(pkt));

    // Read 2-byte length prefix
    uint8_t hdrBuf[2];
    if (!RecvExact(s, hdrBuf, 2)) {
        SockClose(s);
        SockCleanup();
        return {false, "Server did not respond — try again in a moment"};
    }
    uint16_t payloadLen = ReadU16BE(hdrBuf);

    // Read payload
    uint8_t payBuf[256] = {};
    if (payloadLen > sizeof(payBuf)) payloadLen = sizeof(payBuf);
    if (!RecvExact(s, payBuf, payloadLen)) {
        SockClose(s);
        SockCleanup();
        return {false, "Incomplete response from server"};
    }

    SockClose(s);
    SockCleanup();

    if (payloadLen < 1)
        return {false, "Empty response"};

    if (payBuf[0] == PKT_AUTH_OK)
        return {true, ""};

    if (payBuf[0] == PKT_AUTH_FAIL) {
        // Parse reason if payload is large enough to contain it
        if (payloadLen >= (int)sizeof(PktAuthFail)) {
            const auto* f = reinterpret_cast<const PktAuthFail*>(payBuf);
            char reason[sizeof(f->reason) + 1] = {};
            std::strncpy(reason, f->reason, sizeof(f->reason));
            return {false, std::string(reason)};
        }
        return {false, "Login failed"};
    }

    char dbg[64];
    snprintf(dbg, sizeof(dbg), "Unknown response from server (type=%u)", (unsigned)payBuf[0]);
    return {false, std::string(dbg)};
}

AuthResult AuthClient::Login(const std::string& host, uint16_t port,
                              const std::string& username, const std::string& password) {
    return Send(PKT_LOGIN, host, port, username, password);
}

AuthResult AuthClient::Register(const std::string& host, uint16_t port,
                                 const std::string& username, const std::string& password) {
    return Send(PKT_REGISTER, host, port, username, password);
}
