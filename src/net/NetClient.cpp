#include "NetClient.h"
#include <cstdio>
#include <cstring>

// ── Platform socket abstraction ───────────────────────────────────────────────
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

static void SockInit()    { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
static void SockCleanup() { WSACleanup(); }
static void SockClose(SOCKET s) { closesocket(s); }
static void SockNonBlock(SOCKET s) {
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
}
static bool SockHasData(SOCKET s) {
    fd_set fds; FD_ZERO(&fds); FD_SET(s, &fds);
    timeval tv{0, 0};
    return select(0, &fds, nullptr, nullptr, &tv) > 0;
}
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
using SOCKET = int;

static void SockInit()    {}
static void SockCleanup() {}
static void SockClose(int s) { close(s); }
static void SockNonBlock(int s) { fcntl(s, F_SETFL, O_NONBLOCK); }
static bool SockHasData(int s) {
    fd_set fds; FD_ZERO(&fds); FD_SET(s, &fds);
    timeval tv{0, 0};
    return select(s + 1, &fds, nullptr, nullptr, &tv) > 0;
}
#endif

// ── Framing helpers ───────────────────────────────────────────────────────────
static void WriteU16BE(uint8_t* dst, uint16_t v) {
    dst[0] = (v >> 8) & 0xFF;
    dst[1] =  v       & 0xFF;
}
static uint16_t ReadU16BE(const uint8_t* src) {
    return (uint16_t(src[0]) << 8) | src[1];
}

// ── Connect / Disconnect ──────────────────────────────────────────────────────

bool NetClient::Connect(const std::string& host, uint16_t port) {
    if (m_sock != kInvalidSocket) Disconnect();

    SockInit();

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);

    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) {
        printf("[Net] Could not resolve '%s'\n", host.c_str());
        SockCleanup();
        return false;
    }

    NativeSocket s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == kInvalidSocket) {
        freeaddrinfo(res);
        SockCleanup();
        return false;
    }

    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        printf("[Net] connect() failed to %s:%u\n", host.c_str(), port);
        SockClose(s);
        freeaddrinfo(res);
        SockCleanup();
        return false;
    }
    freeaddrinfo(res);

    SockNonBlock(s);
    m_sock = s;

    // Send JOIN with actual name + avatar colours
    PktJoin pkt;
    std::strncpy(pkt.name, m_localName.c_str(), sizeof(pkt.name) - 1);
    auto F2B = [](float f) -> uint8_t { return (uint8_t)(f * 255.f + 0.5f); };
    pkt.skinR  = F2B(m_localSkin.r);  pkt.skinG  = F2B(m_localSkin.g);  pkt.skinB  = F2B(m_localSkin.b);
    pkt.shirtR = F2B(m_localShirt.r); pkt.shirtG = F2B(m_localShirt.g); pkt.shirtB = F2B(m_localShirt.b);
    pkt.pantsR = F2B(m_localPants.r); pkt.pantsG = F2B(m_localPants.g); pkt.pantsB = F2B(m_localPants.b);
    if (!SendRaw(&pkt, sizeof(pkt))) {
        Disconnect();
        return false;
    }

    printf("[Net] Connected to %s:%u\n", host.c_str(), port);
    return true;
}

void NetClient::Disconnect() {
    if (m_sock != kInvalidSocket) {
        SockClose(m_sock);
        m_sock = kInvalidSocket;
    }
    m_myId = 0;
    m_recvBuf.clear();
    for (auto& r : m_remote) r.active = false;
    SockCleanup();
    printf("[Net] Disconnected\n");
}

// ── Per-frame update ──────────────────────────────────────────────────────────

void NetClient::Update(const glm::vec3& localPos, float localYaw) {
    if (m_sock == kInvalidSocket) return;

    PktPosition pkt;
    pkt.x   = localPos.x;
    pkt.y   = localPos.y;
    pkt.z   = localPos.z;
    pkt.yaw = localYaw;
    SendRaw(&pkt, sizeof(pkt));

    Poll();
}

// ── Internal: send ────────────────────────────────────────────────────────────

bool NetClient::SendRaw(const void* data, int len) {
    uint8_t hdr[2];
    WriteU16BE(hdr, (uint16_t)len);
    send(m_sock, (const char*)hdr,  2,   0);
    send(m_sock, (const char*)data, len, 0);
    return true;
}

// ── Internal: receive + parse ─────────────────────────────────────────────────

void NetClient::Poll() {
    while (SockHasData(m_sock)) {
        uint8_t tmp[512];
#ifdef _WIN32
        int n = recv(m_sock, (char*)tmp, sizeof(tmp), 0);
#else
        int n = (int)recv(m_sock, tmp, sizeof(tmp), 0);
#endif
        if (n <= 0) {
            printf("[Net] Server closed connection\n");
            Disconnect();
            return;
        }
        m_recvBuf.insert(m_recvBuf.end(), tmp, tmp + n);
    }

    while (m_recvBuf.size() >= 2) {
        uint16_t payloadLen = ReadU16BE(m_recvBuf.data());
        if ((int)m_recvBuf.size() < 2 + payloadLen) break;
        ProcessPayload(m_recvBuf.data() + 2, payloadLen);
        m_recvBuf.erase(m_recvBuf.begin(), m_recvBuf.begin() + 2 + payloadLen);
    }
}

void NetClient::ProcessPayload(const uint8_t* buf, int len) {
    if (len < 1) return;
    uint8_t type = buf[0];

    if (type == PKT_WELCOME && len >= (int)sizeof(PktWelcome)) {
        m_myId = reinterpret_cast<const PktWelcome*>(buf)->myId;
        printf("[Net] Welcome! My ID = %d\n", m_myId);

    } else if (type == PKT_LEAVE && len >= (int)sizeof(PktLeave)) {
        uint8_t pid = reinterpret_cast<const PktLeave*>(buf)->id;
        if (pid < NET_MAX_PLAYERS) m_remote[pid].active = false;
        printf("[Net] Player %d left\n", pid);

    } else if (type == PKT_SNAPSHOT && len >= 2) {
        const auto* snap = reinterpret_cast<const PktSnapshot*>(buf);
        int count = snap->count;
        if (count > NET_MAX_PLAYERS) count = NET_MAX_PLAYERS;

        for (auto& r : m_remote) r.active = false;

        auto B2F = [](uint8_t b) -> float { return b / 255.f; };
        for (int i = 0; i < count; ++i) {
            const RemoteState& s = snap->players[i];
            if (s.id == m_myId)            continue;
            if (s.id >= NET_MAX_PLAYERS)   continue;
            auto& r  = m_remote[s.id];
            r.active = true;
            r.id     = s.id;
            r.pos    = {s.x, s.y, s.z};
            r.yaw    = s.yaw;
            r.skin   = {B2F(s.skinR),  B2F(s.skinG),  B2F(s.skinB)};
            r.shirt  = {B2F(s.shirtR), B2F(s.shirtG), B2F(s.shirtB)};
            r.pants  = {B2F(s.pantsR), B2F(s.pantsG), B2F(s.pantsB)};
        }
    }
}
