#include "FriendClient.h"
#include <cstdio>
#include <cstring>
#include <sstream>

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
static void SockInit()          { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
static void SockCleanup()       { WSACleanup(); }
static void SockClose(SOCKET s) { closesocket(s); }
static void SetRecvTimeout(SOCKET s) {
    DWORD ms = 8000;
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
    timeval tv{8, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}
#endif

// ── Socket helpers ────────────────────────────────────────────────────────────

static NativeSock FC_Connect(const std::string& host, uint16_t port) {
    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res)
        return kBad;
    NativeSock s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == kBad) { freeaddrinfo(res); return kBad; }
    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        SockClose(s); freeaddrinfo(res); return kBad;
    }
    freeaddrinfo(res);
    SetRecvTimeout(s);
    return s;
}

static bool FC_SendLine(NativeSock s, const std::string& line) {
    std::string msg = line + "\n";
    int sent = 0, len = (int)msg.size();
    while (sent < len) {
#ifdef _WIN32
        int n = send(s, msg.c_str() + sent, len - sent, 0);
#else
        int n = (int)send(s, msg.c_str() + sent, len - sent, 0);
#endif
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static bool FC_RecvLine(NativeSock s, std::string& out) {
    out.clear();
    char c;
    while (true) {
#ifdef _WIN32
        int n = recv(s, &c, 1, 0);
#else
        int n = (int)recv(s, &c, 1, 0);
#endif
        if (n <= 0) return false;
        if (c == '\n') return true;
        if (c != '\r') out += c;
    }
}

// Simple op: send command, read one "OK" or "FAIL reason" line
static FriendOpResult FC_SimpleOp(const std::string& host, uint16_t port,
                                   const std::string& cmd) {
    SockInit();
    NativeSock s = FC_Connect(host, port);
    if (s == kBad) { SockCleanup(); return {false, "Could not connect to server"}; }

    if (!FC_SendLine(s, cmd)) {
        SockClose(s); SockCleanup(); return {false, "Send failed"};
    }

    std::string resp;
    if (!FC_RecvLine(s, resp)) {
        SockClose(s); SockCleanup(); return {false, "No response from server"};
    }

    SockClose(s); SockCleanup();

    if (resp == "OK" || resp.substr(0, 3) == "OK ") return {true, ""};
    if (resp.size() > 5 && resp.substr(0, 5) == "FAIL ")
        return {false, resp.substr(5)};
    return {false, resp.empty() ? "Unknown error" : resp};
}

// ── FriendClient methods ──────────────────────────────────────────────────────

FriendOpResult FriendClient::SendRequest(const std::string& host, uint16_t port,
                                          const std::string& user, const std::string& target) {
    return FC_SimpleOp(host, port, "FRIEND_REQUEST " + user + " " + target);
}

FriendOpResult FriendClient::AcceptRequest(const std::string& host, uint16_t port,
                                            const std::string& user, const std::string& fromUser) {
    return FC_SimpleOp(host, port, "FRIEND_ACCEPT " + user + " " + fromUser);
}

FriendOpResult FriendClient::DeclineRequest(const std::string& host, uint16_t port,
                                             const std::string& user, const std::string& fromUser) {
    return FC_SimpleOp(host, port, "FRIEND_DECLINE " + user + " " + fromUser);
}

FriendOpResult FriendClient::RemoveFriend(const std::string& host, uint16_t port,
                                           const std::string& user, const std::string& target) {
    return FC_SimpleOp(host, port, "FRIEND_REMOVE " + user + " " + target);
}

FriendListResult FriendClient::GetFriends(const std::string& host, uint16_t port,
                                           const std::string& user) {
    SockInit();
    NativeSock s = FC_Connect(host, port);
    if (s == kBad) { SockCleanup(); return {false, {}, "Could not connect to server"}; }

    if (!FC_SendLine(s, "FRIEND_LIST " + user)) {
        SockClose(s); SockCleanup(); return {false, {}, "Send failed"};
    }

    std::string first;
    if (!FC_RecvLine(s, first) || first != "OK") {
        SockClose(s); SockCleanup();
        return {false, {}, first.empty() ? "No response" : first};
    }

    FriendListResult res;
    res.ok = true;
    std::string line;
    while (FC_RecvLine(s, line) && line != "END") {
        // Format: "username online|offline [gameserver]"
        std::istringstream ss(line);
        std::string uname, status, gsrv;
        ss >> uname >> status >> gsrv;
        if (uname.empty()) continue;
        FriendEntry e;
        e.username   = uname;
        e.online     = (status == "online");
        e.inGame     = !gsrv.empty() && gsrv != "-";
        e.gameServer = e.inGame ? gsrv : "";
        res.friends.push_back(std::move(e));
    }

    SockClose(s); SockCleanup();
    return res;
}

FriendReqsResult FriendClient::GetRequests(const std::string& host, uint16_t port,
                                             const std::string& user) {
    SockInit();
    NativeSock s = FC_Connect(host, port);
    if (s == kBad) { SockCleanup(); return {false, {}, "Could not connect to server"}; }

    if (!FC_SendLine(s, "FRIEND_REQS " + user)) {
        SockClose(s); SockCleanup(); return {false, {}, "Send failed"};
    }

    std::string first;
    if (!FC_RecvLine(s, first) || first != "OK") {
        SockClose(s); SockCleanup();
        return {false, {}, first.empty() ? "No response" : first};
    }

    FriendReqsResult res;
    res.ok = true;
    std::string line;
    while (FC_RecvLine(s, line) && line != "END") {
        // Format: "fromUsername"
        if (!line.empty())
            res.requests.push_back({line});
    }

    SockClose(s); SockCleanup();
    return res;
}

JoinFriendResult FriendClient::JoinFriend(const std::string& host, uint16_t port,
                                           const std::string& user, const std::string& target) {
    SockInit();
    NativeSock s = FC_Connect(host, port);
    if (s == kBad) { SockCleanup(); return {false, "", "Could not connect to server"}; }

    if (!FC_SendLine(s, "FRIEND_JOIN " + user + " " + target)) {
        SockClose(s); SockCleanup(); return {false, "", "Send failed"};
    }

    std::string resp;
    if (!FC_RecvLine(s, resp)) {
        SockClose(s); SockCleanup(); return {false, "", "No response"};
    }

    SockClose(s); SockCleanup();

    // "OK host:port"
    if (resp.size() > 3 && resp.substr(0, 3) == "OK ") {
        std::string addr = resp.substr(3);
        if (!addr.empty()) return {true, addr, ""};
    }
    if (resp.size() > 5 && resp.substr(0, 5) == "FAIL ")
        return {false, "", resp.substr(5)};
    return {false, "", "Friend is not currently in a game"};
}

UserProfileResult FriendClient::GetUserProfile(const std::string& host, uint16_t port,
                                                const std::string& targetUser) {
    SockInit();
    NativeSock s = FC_Connect(host, port);
    if (s == kBad) { SockCleanup();
        UserProfileResult r{}; r.error = "Could not connect"; return r; }

    if (!FC_SendLine(s, "GET_PROFILE " + targetUser)) {
        SockClose(s); SockCleanup();
        UserProfileResult r{}; r.error = "Send failed"; return r;
    }

    std::string first;
    if (!FC_RecvLine(s, first) || first != "OK") {
        SockClose(s); SockCleanup();
        UserProfileResult r{}; r.error = first.empty() ? "No response" : first; return r;
    }

    UserProfileResult res;
    res.ok = true;
    // Defaults (noob colors)
    res.skinR  = 0.976f; res.skinG  = 0.820f; res.skinB  = 0.173f;
    res.shirtR = 0.059f; res.shirtG = 0.420f; res.shirtB = 0.690f;
    res.pantsR = 0.110f; res.pantsG = 0.529f; res.pantsB = 0.047f;

    std::string line;
    while (FC_RecvLine(s, line) && line != "END") {
        if (line.size() > 7 && line.compare(0, 7, "colors ") == 0) {
            int sR,sG,sB, shrR,shrG,shrB, pR,pG,pB;
            if (sscanf(line.c_str() + 7, "%d %d %d %d %d %d %d %d %d",
                       &sR,&sG,&sB,&shrR,&shrG,&shrB,&pR,&pG,&pB) == 9) {
                res.skinR  = sR   / 255.f;  res.skinG  = sG   / 255.f;  res.skinB  = sB   / 255.f;
                res.shirtR = shrR / 255.f;  res.shirtG = shrG / 255.f;  res.shirtB = shrB / 255.f;
                res.pantsR = pR   / 255.f;  res.pantsG = pG   / 255.f;  res.pantsB = pB   / 255.f;
            }
        } else if (line.size() > 8 && line.compare(0, 8, "friends ") == 0) {
            res.friendCount = atoi(line.c_str() + 8);
        } else if (line.size() >= 4 && line.compare(0, 4, "bio ") == 0) {
            res.bio = line.substr(4);
        } else if (line == "bio") {
            res.bio.clear();
        }
    }

    SockClose(s); SockCleanup();
    return res;
}

ServerStatusResult FriendClient::GetServerStatus(const std::string& host, uint16_t port) {
    SockInit();
    NativeSock s = FC_Connect(host, port);
    if (s == kBad) { SockCleanup(); return {false, 0, 0, "Could not connect to server"}; }

    if (!FC_SendLine(s, "SERVER_STATUS")) {
        SockClose(s); SockCleanup(); return {false, 0, 0, "Send failed"};
    }

    std::string first;
    if (!FC_RecvLine(s, first) || first != "OK") {
        SockClose(s); SockCleanup();
        return {false, 0, 0, first.empty() ? "No response" : first};
    }

    ServerStatusResult res;
    res.ok = true;
    std::string line;
    while (FC_RecvLine(s, line) && line != "END") {
        if (line.size() > 8 && line.compare(0, 8, "players ") == 0)
            res.playerCount = atoi(line.c_str() + 8);
        else if (line.size() > 4 && line.compare(0, 4, "max ") == 0)
            res.maxPlayers = atoi(line.c_str() + 4);
    }

    SockClose(s); SockCleanup();
    return res;
}

FriendOpResult FriendClient::SetUserProfile(const std::string& host, uint16_t port,
                                             const std::string& user,
                                             const float skin[3], const float shirt[3],
                                             const float pants[3], const std::string& bio) {
    auto clamp8 = [](float v) -> int {
        int i = (int)(v * 255.f + 0.5f);
        return i < 0 ? 0 : (i > 255 ? 255 : i);
    };
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "SET_PROFILE %s %d %d %d %d %d %d %d %d %d %s",
             user.c_str(),
             clamp8(skin[0]),  clamp8(skin[1]),  clamp8(skin[2]),
             clamp8(shirt[0]), clamp8(shirt[1]), clamp8(shirt[2]),
             clamp8(pants[0]), clamp8(pants[1]), clamp8(pants[2]),
             bio.c_str());
    return FC_SimpleOp(host, port, cmd);
}
