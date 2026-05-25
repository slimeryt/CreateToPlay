#pragma once
#include "NetProtocol.h"
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <vector>
#include <cstdint>

// Platform socket type forward-declared as opaque integer
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
using NativeSocket = SOCKET;
static constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
static constexpr NativeSocket kInvalidSocket = -1;
#endif

struct RemotePlayer {
    bool      active = false;
    uint8_t   id     = 0;
    glm::vec3 pos    = {};
    float     yaw    = 0.f;
};

class NetClient {
public:
    // Returns true if TCP connection succeeds.
    bool Connect(const std::string& host, uint16_t port = NET_DEFAULT_PORT);
    void Disconnect();

    // Call once per frame: sends local pos, drains incoming snapshots.
    void Update(const glm::vec3& localPos, float localYaw);

    bool    IsConnected() const { return m_sock != kInvalidSocket; }
    uint8_t MyId()        const { return m_myId; }

    const std::array<RemotePlayer, NET_MAX_PLAYERS>& GetRemotePlayers() const {
        return m_remote;
    }

private:
    bool SendRaw(const void* data, int len);
    void Poll();
    void ProcessPayload(const uint8_t* buf, int len);

    NativeSocket         m_sock  = kInvalidSocket;
    uint8_t              m_myId  = 0;
    std::vector<uint8_t> m_recvBuf;
    std::array<RemotePlayer, NET_MAX_PLAYERS> m_remote{};
};
