//
// CreateToPlay — TCP relay + auth server (Linux/POSIX)
// Handles PKT_REGISTER / PKT_LOGIN for account management,
// then relays PKT_POSITION and broadcasts PKT_SNAPSHOT.
// Deploy on Railway: see Dockerfile in this directory.
//
// Account storage: DATA_DIR env var (default ".") / accounts.dat
// On Railway set DATA_DIR to a mounted persistent volume to survive redeploys.
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
    int     fd        = -1;
    bool    active    = false;
    bool    inGame    = false;   // true after PKT_JOIN (auth-only connections stay false)
    uint8_t id        = 0;
    float   x=0, y=0, z=0, yaw=0;
    uint8_t skinR=249, skinG=209, skinB=44;
    uint8_t shirtR=15, shirtG=107, shirtB=176;
    uint8_t pantsR=28, pantsG=135, pantsB=12;
    char    name[20]  = {};      // display name set on PKT_JOIN
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
    printf("[Server] Player %d disconnected\n", c.id);
    close(c.fd);
    c.fd = -1; c.active = false; c.inGame = false; c.buf.clear();
    memset(c.name, 0, sizeof(c.name));

    PktLeave leave{};
    leave.id = c.id;
    for (auto& o : g_clients)
        if (o.active) o.SendRaw(&leave, sizeof(leave));
}

// ── Validation (same rules as client) ────────────────────────────────────────
static bool ValidUsername(const char* u) {
    int len = 0;
    for (const char* p = u; *p; ++p, ++len) {
        unsigned char c = (unsigned char)*p;
        if (!isalnum(c) && c != '_') return false;
    }
    return len >= 3 && len <= 20;
}

// ── Process one framed packet ─────────────────────────────────────────────────
static void ProcessPayload(int i, const uint8_t* buf, int len) {
    auto& c = g_clients[i];
    if (len < 1) return;

    // ── Auth: register ────────────────────────────────────────────────────────
    if (buf[0] == PKT_REGISTER && len >= (int)sizeof(PktAuthRequest)) {
        const auto* req = reinterpret_cast<const PktAuthRequest*>(buf);
        char username[25] = {};
        strncpy(username, req->username, 24);

        PktAuthFail fail;
        memset(&fail, 0, sizeof(fail));
        fail.type = PKT_AUTH_FAIL;
        if (!ValidUsername(username)) {
            strncpy(fail.reason, "Username: 3-20 chars (letters/numbers/_)", sizeof(fail.reason)-1);
            c.SendRaw(&fail, sizeof(fail));
        } else {
            // Check duplicate
            bool exists = false;
            for (const auto& a : g_accounts)
                if (strcmp(a.name, username) == 0) { exists = true; break; }
            if (exists) {
                strncpy(fail.reason, "Username already taken", sizeof(fail.reason)-1);
                c.SendRaw(&fail, sizeof(fail));
            } else {
                AppendAccount(username, req->passHash);
                PktAuthOk ok;
                ok.type = PKT_AUTH_OK;
                c.SendRaw(&ok, sizeof(ok));
                printf("[Server] Registered: %s\n", username);
            }
        }
        // Auth-only connection: close after responding
        DisconnectClient(i);
        return;
    }

    // ── Auth: login ───────────────────────────────────────────────────────────
    if (buf[0] == PKT_LOGIN && len >= (int)sizeof(PktAuthRequest)) {
        const auto* req = reinterpret_cast<const PktAuthRequest*>(buf);
        char username[25] = {};
        strncpy(username, req->username, 24);

        PktAuthFail fail;
        memset(&fail, 0, sizeof(fail));
        fail.type = PKT_AUTH_FAIL;
        bool found = false;
        for (const auto& a : g_accounts) {
            if (strcmp(a.name, username) == 0) {
                found = true;
                if (a.hash == req->passHash) {
                    PktAuthOk ok;
                    ok.type = PKT_AUTH_OK;
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
        // Auth-only: close after responding
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
    // Account file location — set DATA_DIR to a Railway persistent volume path
    // (e.g. /data) to survive redeploys; defaults to working directory.
    const char* dataDir = getenv("DATA_DIR");
    g_accountsPath = std::string(dataDir ? dataDir : ".") + "/accounts.dat";
    LoadAccounts();
    printf("[Server] Loaded %d account(s) from %s\n",
           (int)g_accounts.size(), g_accountsPath.c_str());

    const char* portEnv = getenv("PORT");
    uint16_t port = portEnv ? (uint16_t)atoi(portEnv) : NET_DEFAULT_PORT;

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    bind(listener, (sockaddr*)&addr, sizeof(addr));
    listen(listener, 8);

    // Non-blocking listener
    fcntl(listener, F_SETFL, O_NONBLOCK);

    printf("[Server] Listening on port %d (max %d players)\n",
           port, NET_MAX_PLAYERS);

    // Snapshot timer
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
                auto& c   = g_clients[slot];
                c.fd      = newfd;
                c.active  = true;
                c.inGame  = false;
                c.id      = (uint8_t)slot;
                c.x = c.y = c.z = c.yaw = 0.f;
                memset(c.name, 0, sizeof(c.name));
                c.buf.clear();
                printf("[Server] New connection in slot %d\n", slot);
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

                    while (c.buf.size() >= 2) {
                        uint16_t plen = ReadU16BE(c.buf.data());
                        if ((int)c.buf.size() < 2 + plen) break;
                        ProcessPayload(i, c.buf.data() + 2, plen);
                        // ProcessPayload may have disconnected this client
                        // (auth packets call DisconnectClient which clears buf).
                        // Guard the erase so we never operate on an empty vector.
                        if (!c.active) break;
                        c.buf.erase(c.buf.begin(), c.buf.begin() + 2 + plen);
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

        usleep(1000);  // 1 ms sleep — yields CPU
    }
}
