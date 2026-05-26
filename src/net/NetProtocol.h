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
    PKT_JOIN      = 1,   // client → server
    PKT_WELCOME   = 2,   // server → client
    PKT_LEAVE     = 3,   // server → all
    PKT_POSITION  = 4,   // client → server (every frame)
    PKT_SNAPSHOT  = 5,   // server → all   (every server tick)
    PKT_REGISTER  = 6,   // client → server: create account
    PKT_LOGIN     = 7,   // client → server: login
    PKT_AUTH_OK   = 8,   // server → client: auth success
    PKT_AUTH_FAIL = 9,   // server → client: auth failure + reason
    PKT_CHAT      = 0x10, // client → server → all: chat message
};

// ── Packet bodies (after the 2-byte length prefix) ────────────────────────────
#pragma pack(push, 1)

// PKT_JOIN  payload = 1 + 20 + 9 = 30 bytes
struct PktJoin {
    uint8_t type = PKT_JOIN;
    char    name[20] = {};
    // Avatar colours (0-255 each)
    uint8_t skinR=249, skinG=209, skinB=44;    // noob yellow head/arms
    uint8_t shirtR=15, shirtG=107, shirtB=176; // noob blue torso
    uint8_t pantsR=28, pantsG=135, pantsB=12;  // noob green legs
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

// One slot inside PKT_SNAPSHOT (46 bytes each)
struct RemoteState {
    uint8_t id;
    float   x, y, z, yaw;
    // Avatar colours
    uint8_t skinR,  skinG,  skinB;
    uint8_t shirtR, shirtG, shirtB;
    uint8_t pantsR, pantsG, pantsB;
    // Display name (null-terminated, up to 19 chars)
    char    name[20];
};

// PKT_SNAPSHOT  payload = 1 + 1 + count*26  (max 262 bytes)
struct PktSnapshot {
    uint8_t     type = PKT_SNAPSHOT;
    uint8_t     count;
    RemoteState players[NET_MAX_PLAYERS];
};

// PKT_REGISTER / PKT_LOGIN  payload = 1 + 24 + 4 = 29 bytes
struct PktAuthRequest {
    uint8_t  type;           // PKT_REGISTER or PKT_LOGIN
    char     username[24] = {};
    uint32_t passHash = 0;   // NetHashPassword(username, password)
};

// PKT_AUTH_OK  payload = 1 byte
struct PktAuthOk {
    uint8_t type = PKT_AUTH_OK;
};

// PKT_AUTH_FAIL  payload = 1 + 48 = 49 bytes
struct PktAuthFail {
    uint8_t type = PKT_AUTH_FAIL;
    char    reason[48] = {};
};

// PKT_CHAT  payload = 1 + 20 + 80 = 101 bytes
struct PktChat {
    uint8_t type = PKT_CHAT;
    char    name[20] = {};   // sender's display name
    char    msg[80]  = {};   // message text (null-terminated)
};

#pragma pack(pop)

// ── Shared password hash (djb2 over "username::password") ────────────────────
// Must produce identical results on client and server.
inline uint32_t NetHashPassword(const char* username, const char* password) {
    uint32_t h = 5381;
    auto feed = [&](const char* s) {
        for (const unsigned char* p = (const unsigned char*)s; *p; ++p)
            h = (h * 33) ^ *p;
    };
    feed(username);
    feed("::");
    feed(password);
    return h;
}
