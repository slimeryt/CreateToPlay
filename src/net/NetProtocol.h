#pragma once
#include <cstdint>

// ── Wire framing ──────────────────────────────────────────────────────────────
// Every message on the wire:
//   [uint16_t payloadLen  — big-endian, does NOT include these 2 bytes]
//   [uint8_t  type        — PacketType enum                          ]
//   [remaining bytes      — packet-specific fields                   ]

static constexpr uint16_t NET_DEFAULT_PORT = 7777;
static constexpr int      NET_MAX_PLAYERS  = 10;

enum PacketType : uint8_t {
    PKT_JOIN     = 1,   // client → server
    PKT_WELCOME  = 2,   // server → client
    PKT_LEAVE    = 3,   // server → all
    PKT_POSITION = 4,   // client → server (every frame)
    PKT_SNAPSHOT = 5,   // server → all   (every server tick)
};

// ── Packet bodies (after the 2-byte length prefix) ────────────────────────────
#pragma pack(push, 1)

// PKT_JOIN  payload = 1 + 20 = 21 bytes
struct PktJoin {
    uint8_t type = PKT_JOIN;
    char    name[20] = {};
};

// PKT_WELCOME  payload = 1 + 1 = 2 bytes
struct PktWelcome {
    uint8_t type = PKT_WELCOME;
    uint8_t myId;
};

// PKT_LEAVE  payload = 1 + 1 = 2 bytes
struct PktLeave {
    uint8_t type = PKT_LEAVE;
    uint8_t id;
};

// PKT_POSITION  payload = 1 + 16 = 17 bytes
struct PktPosition {
    uint8_t type = PKT_POSITION;
    float   x, y, z, yaw;
};

// One slot inside PKT_SNAPSHOT
struct RemoteState {
    uint8_t id;
    float   x, y, z, yaw;   // 17 bytes per slot
};

// PKT_SNAPSHOT  payload = 1 + 1 + count*17  (max 172 bytes)
struct PktSnapshot {
    uint8_t     type = PKT_SNAPSHOT;
    uint8_t     count;
    RemoteState players[NET_MAX_PLAYERS];
};

#pragma pack(pop)
