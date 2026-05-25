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
    glm::vec3 skin   = {0.976f, 0.820f, 0.173f};
    glm::vec3 shirt  = {0.059f, 0.420f, 0.690f};
    glm::vec3 pants  = {0.110f, 0.529f, 0.047f};
};

class NetClient {
public:
    // Returns true if TCP connection succeeds.
    bool Connect(const std::string& host, uint16_t port = NET_DEFAULT_PORT);
    void Disconnect();

    // Set name + avatar before Connect() so they are included in PKT_JOIN.
    void SetLocalName(const std::string& name) { m_localName = name; }
    void SetLocalAvatar(const glm::vec3& skin, const glm::vec3& shirt, const glm::vec3& pants) {
        m_localSkin = skin; m_localShirt = shirt; m_localPants = pants;
    }

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

    std::string m_localName  = "Player";
    glm::vec3   m_localSkin  = {0.976f, 0.820f, 0.173f};
    glm::vec3   m_localShirt = {0.059f, 0.420f, 0.690f};
    glm::vec3   m_localPants = {0.110f, 0.529f, 0.047f};
};
