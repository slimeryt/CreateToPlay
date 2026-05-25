//
// CreateToPlay — TCP relay + auth + friend server (Linux/POSIX)
//
// Binary protocol  (first byte < 0x20):
//   2-byte BE length prefix + packet payload
//   PKT_REGISTER / PKT_LOGIN   → auth, close after reply
//   PKT_JOIN / PKT_POSITION    → game relay
//
// Text protocol  (first byte >= 0x20 — printable ASCII):
//   "COMMAND arg1 arg2\n"  →  "OK\n" | "OK\nlines\nEND\n" | "FAIL reason\n"
//   FRIEND_REQUEST from to
//   FRIEND_ACCEPT  user fromUser
//   FRIEND_DECLINE user fromUser
//   FRIEND_REMOVE  user target
//   FRIEND_LIST    user          → OK + "name online|offline [server]" lines + END
//   FRIEND_REQS    user          → OK + "fromName" lines + END
//   FRIEND_JOIN    user target   → OK host:port | FAIL reason
//
// Env vars:
//   DATA_DIR   - directory for accounts.dat / friends.dat / friendreqs.dat (default ".")
//   PORT       - listen port (default NET_DEFAULT_PORT)
//   GAME_ADDR  - public "host:port" returned by FRIEND_JOIN, e.g. "myserver.railway.app:7777"
//                If unset, FRIEND_JOIN returns FAIL.
//
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

#include "../src/net/NetProtocol.h"

// ── Global config ─────────────────────────────────────────────────────────────
static std::string g_gameAddr;   // "host:port" — returned by FRIEND_JOIN

// ── Account store ─────────────────────────────────────────────────────────────
static std::string g_accountsPath;

struct Account { char name[24]; uint32_t hash; };
static std::vector<Account> g_accounts;

static void LoadAccounts() {
    g_accounts.clear();
    FILE* f = fopen(g_accountsPath.c_str(), "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        for (char* p = line; *p; ++p) if (*p=='\n'||*p=='\r'){*p=0;break;}
        char* colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        Account a{};
        strncpy(a.name, line, sizeof(a.name)-1);
        a.hash = (uint32_t)strtoul(colon+1, nullptr, 10);
        g_accounts.push_back(a);
    }
    fclose(f);
}
static void AppendAccount(const char* name, uint32_t hash) {
    FILE* f = fopen(g_accountsPath.c_str(), "a");
    if (!f) return;
    fprintf(f, "%s:%u\n", name, hash);
    fclose(f);
    Account a{};
    strncpy(a.name, name, sizeof(a.name)-1);
    a.hash = hash;
    g_accounts.push_back(a);
}

// ── Friend store ──────────────────────────────────────────────────────────────
static std::string g_friendsPath;
static std::string g_freqsPath;

struct FriendPair { char a[24]; char b[24]; };   // a < b (lexicographic)
struct FriendReq  { char from[24]; char to[24]; };

static std::vector<FriendPair> g_friends;
static std::vector<FriendReq>  g_freqs;

static void LoadFriends() {
    g_friends.clear();
    FILE* f = fopen(g_friendsPath.c_str(), "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        for (char* p = line; *p; ++p) if (*p=='\n'||*p=='\r'){*p=0;break;}
        char* colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        FriendPair p{};
        strncpy(p.a, line, sizeof(p.a)-1);
        strncpy(p.b, colon+1, sizeof(p.b)-1);
        g_friends.push_back(p);
    }
    fclose(f);
}
static void SaveFriends() {
    FILE* f = fopen(g_friendsPath.c_str(), "w");
    if (!f) return;
    for (auto& p : g_friends) fprintf(f, "%s:%s\n", p.a, p.b);
    fclose(f);
}
static void LoadFriendReqs() {
    g_freqs.clear();
    FILE* f = fopen(g_freqsPath.c_str(), "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        for (char* p = line; *p; ++p) if (*p=='\n'||*p=='\r'){*p=0;break;}
        char* colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        FriendReq r{};
        strncpy(r.from, line, sizeof(r.from)-1);
        strncpy(r.to,   colon+1, sizeof(r.to)-1);
        g_freqs.push_back(r);
    }
    fclose(f);
}
static void SaveFriendReqs() {
    FILE* f = fopen(g_freqsPath.c_str(), "w");
    if (!f) return;
    for (auto& r : g_freqs) fprintf(f, "%s:%s\n", r.from, r.to);
    fclose(f);
}

// Normalise pair so a < b
static void NormPair(const char* u1, const char* u2, char* a, char* b) {
    if (strcmp(u1, u2) < 0) { strncpy(a, u1, 23); strncpy(b, u2, 23); }
    else                    { strncpy(a, u2, 23); strncpy(b, u1, 23); }
}
static bool AreFriends(const char* u1, const char* u2) {
    char a[24]={}, b[24]={};
    NormPair(u1, u2, a, b);
    for (auto& p : g_friends)
        if (strcmp(p.a,a)==0 && strcmp(p.b,b)==0) return true;
    return false;
}
static void AddFriendship(const char* u1, const char* u2) {
    if (AreFriends(u1, u2)) return;
    FriendPair p{};
    NormPair(u1, u2, p.a, p.b);
    g_friends.push_back(p);
    SaveFriends();
}
static void RemoveFriendship(const char* u1, const char* u2) {
    char a[24]={}, b[24]={};
    NormPair(u1, u2, a, b);
    g_friends.erase(std::remove_if(g_friends.begin(), g_friends.end(),
        [&](const FriendPair& p){ return strcmp(p.a,a)==0 && strcmp(p.b,b)==0; }),
        g_friends.end());
    SaveFriends();
}
static bool HasReq(const char* from, const char* to) {
    for (auto& r : g_freqs)
        if (strcmp(r.from,from)==0 && strcmp(r.to,to)==0) return true;
    return false;
}
static void AddReq(const char* from, const char* to) {
    if (HasReq(from, to)) return;
    FriendReq r{};
    strncpy(r.from, from, 23); strncpy(r.to, to, 23);
    g_freqs.push_back(r);
    SaveFriendReqs();
}
static void RemoveReq(const char* from, const char* to) {
    g_freqs.erase(std::remove_if(g_freqs.begin(), g_freqs.end(),
        [&](const FriendReq& r){ return strcmp(r.from,from)==0 && strcmp(r.to,to)==0; }),
        g_freqs.end());
    SaveFriendReqs();
}

// ── Framing helpers ───────────────────────────────────────────────────────────
static void WriteU16BE(uint8_t* dst, uint16_t v) {
    dst[0] = (v >> 8) & 0xFF;
    dst[1] =  v       & 0xFF;
}
static uint16_t ReadU16BE(const uint8_t* src) {
    return (uint16_t(src[0]) << 8) | src[1];
}

// ── Client slot ───────────────────────────────────────────────────────────────
struct Client {
    int     fd          = -1;
    bool    active      = false;
    bool    inGame      = false;
    bool    classified  = false;   // have we seen the first byte yet?
    bool    isTextConn  = false;   // true = text-line protocol connection
    uint8_t id          = 0;
    float   x=0, y=0, z=0, yaw=0;
    uint8_t skinR=249,  skinG=209, skinB=44;
    uint8_t shirtR=15,  shirtG=107, shirtB=176;
    uint8_t pantsR=28,  pantsG=135, pantsB=12;
    char    name[20]    = {};
    std::vector<uint8_t> buf;

    void SendRaw(const void* data, int len) const {
        if (fd < 0) return;
        uint8_t hdr[2];
        WriteU16BE(hdr, (uint16_t)len);
        send(fd, hdr,            2,   MSG_NOSIGNAL);
        send(fd, (const char*)data, len, MSG_NOSIGNAL);
    }
};

static std::array<Client, NET_MAX_PLAYERS> g_clients;

// ── Broadcast snapshot ────────────────────────────────────────────────────────
static void BroadcastSnapshot() {
    PktSnapshot snap{};
    snap.count = 0;
    for (const auto& c : g_clients) {
        if (!c.active || !c.inGame) continue;
        auto& s  = snap.players[snap.count++];
        s.id     = c.id;
        s.x = c.x; s.y = c.y; s.z = c.z; s.yaw = c.yaw;
        s.skinR  = c.skinR;  s.skinG  = c.skinG;  s.skinB  = c.skinB;
        s.shirtR = c.shirtR; s.shirtG = c.shirtG; s.shirtB = c.shirtB;
        s.pantsR = c.pantsR; s.pantsG = c.pantsG; s.pantsB = c.pantsB;
        memcpy(s.name, c.name, sizeof(s.name));
    }
    int plen = 2 + snap.count * (int)sizeof(RemoteState);
    for (auto& c : g_clients)
        if (c.active && c.inGame) c.SendRaw(&snap, plen);
}

// ── Disconnect ────────────────────────────────────────────────────────────────
static void DisconnectClient(int i) {
    auto& c = g_clients[i];
    if (!c.active) return;
    if (!c.isTextConn)
        printf("[Server] Slot %d (%s) disconnected\n", c.id, c.name[0] ? c.name : "?");
    close(c.fd);
    c.fd = -1; c.active = false; c.inGame = false;
    c.classified = false; c.isTextConn = false;
    c.buf.clear();
    memset(c.name, 0, sizeof(c.name));

    PktLeave leave{};
    leave.id = c.id;
    for (auto& o : g_clients)
        if (o.active && !o.isTextConn) o.SendRaw(&leave, sizeof(leave));
}

// ── Validation ────────────────────────────────────────────────────────────────
static bool ValidUsername(const char* u) {
    int len = 0;
    for (const char* p = u; *p; ++p, ++len) {
        unsigned char ch = (unsigned char)*p;
        if (!isalnum(ch) && ch != '_') return false;
    }
    return len >= 3 && len <= 20;
}

// ── Text-line send helper ─────────────────────────────────────────────────────
static void TextSend(int fd, const char* line) {
    std::string s = std::string(line) + "\n";
    send(fd, s.c_str(), (int)s.size(), MSG_NOSIGNAL);
}

// ── Handle one complete text-line command ─────────────────────────────────────
static void HandleTextCommand(int slotIdx, const std::string& line) {
    auto& c = g_clients[slotIdx];
    int fd  = c.fd;

    char cmd[32]={}, a1[64]={}, a2[64]={};
    sscanf(line.c_str(), "%31s %63s %63s", cmd, a1, a2);

    // ── FRIEND_REQUEST from to ──────────────────────────────────────────────
    if (strcmp(cmd, "FRIEND_REQUEST") == 0) {
        if (!a1[0] || !a2[0]) { TextSend(fd, "FAIL Invalid args"); }
        else if (strcmp(a1, a2) == 0) { TextSend(fd, "FAIL Cannot add yourself"); }
        else if (AreFriends(a1, a2)) { TextSend(fd, "FAIL Already friends"); }
        else {
            if (HasReq(a1, a2)) {
                TextSend(fd, "OK");   // idempotent
            } else if (HasReq(a2, a1)) {
                // Mutual request → auto-accept
                RemoveReq(a2, a1);
                AddFriendship(a1, a2);
                TextSend(fd, "OK");
                printf("[Server] Friends (auto-accept): %s <-> %s\n", a1, a2);
            } else {
                AddReq(a1, a2);
                TextSend(fd, "OK");
                printf("[Server] Friend request: %s -> %s\n", a1, a2);
            }
        }
    }
    // ── FRIEND_ACCEPT user fromUser ─────────────────────────────────────────
    else if (strcmp(cmd, "FRIEND_ACCEPT") == 0) {
        if (!a1[0] || !a2[0]) { TextSend(fd, "FAIL Invalid args"); }
        else if (!HasReq(a2, a1)) { TextSend(fd, "FAIL No pending request"); }
        else {
            RemoveReq(a2, a1);
            AddFriendship(a1, a2);
            TextSend(fd, "OK");
            printf("[Server] Friends accepted: %s <-> %s\n", a1, a2);
        }
    }
    // ── FRIEND_DECLINE user fromUser ────────────────────────────────────────
    else if (strcmp(cmd, "FRIEND_DECLINE") == 0) {
        if (!a1[0] || !a2[0]) { TextSend(fd, "FAIL Invalid args"); }
        else {
            RemoveReq(a2, a1);
            TextSend(fd, "OK");
        }
    }
    // ── FRIEND_REMOVE user target ───────────────────────────────────────────
    else if (strcmp(cmd, "FRIEND_REMOVE") == 0) {
        if (!a1[0] || !a2[0]) { TextSend(fd, "FAIL Invalid args"); }
        else {
            RemoveFriendship(a1, a2);
            TextSend(fd, "OK");
            printf("[Server] Unfriended: %s <-> %s\n", a1, a2);
        }
    }
    // ── FRIEND_LIST user ────────────────────────────────────────────────────
    else if (strcmp(cmd, "FRIEND_LIST") == 0) {
        if (!a1[0]) { TextSend(fd, "FAIL Invalid args"); }
        else {
            TextSend(fd, "OK");
            for (auto& p : g_friends) {
                const char* other = nullptr;
                if      (strcmp(p.a, a1) == 0) other = p.b;
                else if (strcmp(p.b, a1) == 0) other = p.a;
                if (!other) continue;

                // Check online / in-game status against active game clients
                bool online = false, inGame = false;
                for (auto& cl : g_clients) {
                    if (!cl.active || cl.isTextConn) continue;
                    if (strcmp(cl.name, other) == 0) {
                        online = true;
                        inGame = cl.inGame;
                        break;
                    }
                }

                char row[128];
                snprintf(row, sizeof(row), "%s %s %s",
                         other,
                         online ? "online" : "offline",
                         (inGame && !g_gameAddr.empty()) ? g_gameAddr.c_str() : "-");
                TextSend(fd, row);
            }
            TextSend(fd, "END");
        }
    }
    // ── FRIEND_REQS user ────────────────────────────────────────────────────
    else if (strcmp(cmd, "FRIEND_REQS") == 0) {
        if (!a1[0]) { TextSend(fd, "FAIL Invalid args"); }
        else {
            TextSend(fd, "OK");
            for (auto& r : g_freqs)
                if (strcmp(r.to, a1) == 0) TextSend(fd, r.from);
            TextSend(fd, "END");
        }
    }
    // ── FRIEND_JOIN user target ─────────────────────────────────────────────
    else if (strcmp(cmd, "FRIEND_JOIN") == 0) {
        if (!a1[0] || !a2[0]) { TextSend(fd, "FAIL Invalid args"); }
        else {
            bool found = false;
            for (auto& cl : g_clients) {
                if (cl.active && cl.inGame && !cl.isTextConn &&
                    strcmp(cl.name, a2) == 0)
                {
                    if (g_gameAddr.empty()) {
                        TextSend(fd, "FAIL GAME_ADDR not set on server");
                    } else {
                        std::string resp = "OK " + g_gameAddr;
                        TextSend(fd, resp.c_str());
                        printf("[Server] JoinFriend: %s -> %s @ %s\n",
                               a1, a2, g_gameAddr.c_str());
                    }
                    found = true;
                    break;
                }
            }
            if (!found) TextSend(fd, "FAIL Friend is not in a game");
        }
    }
    else {
        TextSend(fd, "FAIL Unknown command");
    }

    // Text connections are always short-lived — close after one command
    DisconnectClient(slotIdx);
}

// ── Process one binary framed packet ─────────────────────────────────────────
static void ProcessPayload(int i, const uint8_t* buf, int len) {
    auto& c = g_clients[i];
    if (len < 1) return;

    // ── Auth: register ────────────────────────────────────────────────────────
    if (buf[0] == PKT_REGISTER && len >= (int)sizeof(PktAuthRequest)) {
        const auto* req = reinterpret_cast<const PktAuthRequest*>(buf);
        char username[25] = {};
        strncpy(username, req->username, 24);

        PktAuthFail fail; memset(&fail, 0, sizeof(fail));
        fail.type = PKT_AUTH_FAIL;
        if (!ValidUsername(username)) {
            strncpy(fail.reason, "Username: 3-20 chars (letters/numbers/_)", sizeof(fail.reason)-1);
            c.SendRaw(&fail, sizeof(fail));
        } else {
            bool exists = false;
            for (const auto& a : g_accounts)
                if (strcmp(a.name, username) == 0) { exists = true; break; }
            if (exists) {
                strncpy(fail.reason, "Username already taken", sizeof(fail.reason)-1);
                c.SendRaw(&fail, sizeof(fail));
            } else {
                AppendAccount(username, req->passHash);
                PktAuthOk ok; ok.type = PKT_AUTH_OK;
                c.SendRaw(&ok, sizeof(ok));
                printf("[Server] Registered: %s\n", username);
            }
        }
        DisconnectClient(i);
        return;
    }

    // ── Auth: login ───────────────────────────────────────────────────────────
    if (buf[0] == PKT_LOGIN && len >= (int)sizeof(PktAuthRequest)) {
        const auto* req = reinterpret_cast<const PktAuthRequest*>(buf);
        char username[25] = {};
        strncpy(username, req->username, 24);

        PktAuthFail fail; memset(&fail, 0, sizeof(fail));
        fail.type = PKT_AUTH_FAIL;
        bool found = false;
        for (const auto& a : g_accounts) {
            if (strcmp(a.name, username) == 0) {
                found = true;
                if (a.hash == req->passHash) {
                    PktAuthOk ok; ok.type = PKT_AUTH_OK;
                    c.SendRaw(&ok, sizeof(ok));
                    printf("[Server] Login OK: %s\n", username);
                } else {
                    strncpy(fail.reason, "Incorrect password", sizeof(fail.reason)-1);
                    c.SendRaw(&fail, sizeof(fail));
                }
                break;
            }
        }
        if (!found) {
            strncpy(fail.reason, "No account with that username", sizeof(fail.reason)-1);
            c.SendRaw(&fail, sizeof(fail));
        }
        DisconnectClient(i);
        return;
    }

    // ── Game: join ────────────────────────────────────────────────────────────
    if (buf[0] == PKT_JOIN && len >= (int)sizeof(PktJoin)) {
        const auto* j = reinterpret_cast<const PktJoin*>(buf);
        c.skinR  = j->skinR;  c.skinG  = j->skinG;  c.skinB  = j->skinB;
        c.shirtR = j->shirtR; c.shirtG = j->shirtG; c.shirtB = j->shirtB;
        c.pantsR = j->pantsR; c.pantsG = j->pantsG; c.pantsB = j->pantsB;
        strncpy(c.name, j->name, sizeof(c.name) - 1);
        c.inGame = true;
        PktWelcome w{}; w.myId = c.id;
        c.SendRaw(&w, sizeof(w));
        printf("[Server] Player %d (%s) joined game\n", c.id, j->name);
        return;
    }

    // ── Game: position ────────────────────────────────────────────────────────
    if (buf[0] == PKT_POSITION && len >= (int)sizeof(PktPosition)) {
        const auto* p = reinterpret_cast<const PktPosition*>(buf);
        c.x = p->x; c.y = p->y; c.z = p->z; c.yaw = p->yaw;
    }
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    const char* dataDir = getenv("DATA_DIR");
    std::string dir     = dataDir ? dataDir : ".";

    g_accountsPath = dir + "/accounts.dat";
    g_friendsPath  = dir + "/friends.dat";
    g_freqsPath    = dir + "/friendreqs.dat";

    LoadAccounts();
    LoadFriends();
    LoadFriendReqs();

    printf("[Server] %d account(s), %d friendship(s), %d pending request(s)\n",
           (int)g_accounts.size(), (int)g_friends.size(), (int)g_freqs.size());

    const char* portEnv = getenv("PORT");
    uint16_t port = portEnv ? (uint16_t)atoi(portEnv) : NET_DEFAULT_PORT;

    const char* gameAddrEnv = getenv("GAME_ADDR");
    if (gameAddrEnv) {
        g_gameAddr = gameAddrEnv;
        printf("[Server] GAME_ADDR = %s  (used by FRIEND_JOIN)\n", g_gameAddr.c_str());
    } else {
        printf("[Server] GAME_ADDR not set — FRIEND_JOIN will fail. "
               "Set GAME_ADDR=host:port to enable join-friend.\n");
    }

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    bind(listener, (sockaddr*)&addr, sizeof(addr));
    listen(listener, 16);
    fcntl(listener, F_SETFL, O_NONBLOCK);

    printf("[Server] Listening on port %d (max %d players)\n",
           port, NET_MAX_PLAYERS);

    timespec last{};
    clock_gettime(CLOCK_MONOTONIC, &last);

    while (true) {
        // ── Accept ────────────────────────────────────────────────────────────
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        int newfd = accept(listener, (sockaddr*)&caddr, &clen);
        if (newfd >= 0) {
            int slot = -1;
            for (int i = 0; i < NET_MAX_PLAYERS; ++i)
                if (!g_clients[i].active) { slot = i; break; }

            if (slot == -1) {
                printf("[Server] Full — rejecting\n");
                close(newfd);
            } else {
                fcntl(newfd, F_SETFL, O_NONBLOCK);
                auto& c    = g_clients[slot];
                c.fd       = newfd;
                c.active   = true;
                c.inGame   = false;
                c.classified = false;
                c.isTextConn = false;
                c.id       = (uint8_t)slot;
                c.x = c.y = c.z = c.yaw = 0.f;
                memset(c.name, 0, sizeof(c.name));
                c.buf.clear();
            }
        }

        // ── Receive ───────────────────────────────────────────────────────────
        fd_set rfds; FD_ZERO(&rfds);
        int maxfd = -1;
        for (auto& c : g_clients) {
            if (!c.active) continue;
            FD_SET(c.fd, &rfds);
            if (c.fd > maxfd) maxfd = c.fd;
        }

        if (maxfd >= 0) {
            timeval tv{0, 0};
            if (select(maxfd + 1, &rfds, nullptr, nullptr, &tv) > 0) {
                for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
                    auto& c = g_clients[i];
                    if (!c.active || !FD_ISSET(c.fd, &rfds)) continue;

                    uint8_t tmp[512];
                    int n = (int)recv(c.fd, tmp, sizeof(tmp), 0);
                    if (n <= 0) { DisconnectClient(i); continue; }

                    c.buf.insert(c.buf.end(), tmp, tmp + n);

                    // Classify connection on first byte received
                    if (!c.classified && !c.buf.empty()) {
                        c.classified  = true;
                        c.isTextConn  = (c.buf[0] >= 0x20);
                    }

                    if (c.isTextConn) {
                        // Accumulate until '\n', then handle command
                        auto it = std::find(c.buf.begin(), c.buf.end(), (uint8_t)'\n');
                        if (it != c.buf.end()) {
                            std::string line(c.buf.begin(), it);
                            while (!line.empty() && line.back() == '\r') line.pop_back();
                            HandleTextCommand(i, line);
                            // HandleTextCommand calls DisconnectClient — buf cleared
                        }
                        // If no '\n' yet, wait for more data next iteration
                    } else {
                        // Binary framing: 2-byte BE length + payload
                        while (c.buf.size() >= 2) {
                            uint16_t plen = ReadU16BE(c.buf.data());
                            if ((int)c.buf.size() < 2 + plen) break;
                            ProcessPayload(i, c.buf.data() + 2, plen);
                            if (!c.active) break;
                            c.buf.erase(c.buf.begin(), c.buf.begin() + 2 + plen);
                        }
                    }
                }
            }
        }

        // ── Broadcast snapshot ~20 Hz ─────────────────────────────────────────
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - last.tv_sec)
                       + (now.tv_nsec - last.tv_nsec) * 1e-9;
        if (elapsed >= 1.0 / 20.0) {
            last = now;
            BroadcastSnapshot();
        }

        usleep(1000);
    }
}
