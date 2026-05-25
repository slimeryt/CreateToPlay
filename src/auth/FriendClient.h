#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ── Result types ─────────────────────────────────────────────────────────────

struct FriendEntry {
    std::string username;
    bool        online     = false;
    bool        inGame     = false;
    std::string gameServer; // "host:port" when in a game, else empty
};

struct FriendReqEntry {
    std::string fromUser;   // server uses fromUser as the implicit ID
};

struct FriendOpResult {
    bool        ok    = false;
    std::string error;
};

struct FriendListResult {
    bool                     ok = false;
    std::vector<FriendEntry> friends;
    std::string              error;
};

struct FriendReqsResult {
    bool                         ok = false;
    std::vector<FriendReqEntry>  requests;
    std::string                  error;
};

struct JoinFriendResult {
    bool        ok         = false;
    std::string serverAddr; // "host:port" on success
    std::string error;
};

struct UserProfileResult {
    bool        ok           = false;
    // Avatar colours (0-1 float range)
    float       skinR  = 0.976f, skinG  = 0.820f, skinB  = 0.173f;
    float       shirtR = 0.059f, shirtG = 0.420f, shirtB = 0.690f;
    float       pantsR = 0.110f, pantsG = 0.529f, pantsB = 0.047f;
    int         friendCount = 0;
    std::string bio;
    std::string error;
};

// ── FriendClient ─────────────────────────────────────────────────────────────
// All methods are synchronous / blocking — call from a background thread.
// Text-line protocol over a short-lived TCP connection:
//   Client sends:   "COMMAND arg1 arg2\n"
//   Server replies: "OK [data]\n" | "OK\nline1\nline2\nEND\n" | "FAIL reason\n"
//
// The server differentiates text connections from binary game connections by
// checking whether the first byte is >= 0x20 (printable ASCII).

class FriendClient {
public:
    FriendOpResult   SendRequest   (const std::string& host, uint16_t port,
                                    const std::string& user, const std::string& target);
    FriendOpResult   AcceptRequest (const std::string& host, uint16_t port,
                                    const std::string& user, const std::string& fromUser);
    FriendOpResult   DeclineRequest(const std::string& host, uint16_t port,
                                    const std::string& user, const std::string& fromUser);
    FriendOpResult   RemoveFriend  (const std::string& host, uint16_t port,
                                    const std::string& user, const std::string& target);
    FriendListResult GetFriends    (const std::string& host, uint16_t port,
                                    const std::string& user);
    FriendReqsResult GetRequests   (const std::string& host, uint16_t port,
                                    const std::string& user);
    JoinFriendResult  JoinFriend      (const std::string& host, uint16_t port,
                                     const std::string& user, const std::string& target);
    UserProfileResult GetUserProfile (const std::string& host, uint16_t port,
                                     const std::string& targetUser);
    FriendOpResult    SetUserProfile (const std::string& host, uint16_t port,
                                     const std::string& user,
                                     const float skin[3], const float shirt[3],
                                     const float pants[3], const std::string& bio);
};
