//
// CreateToPlay — TCP relay game server (Linux/POSIX)
// Receives PKT_POSITION from each client, broadcasts PKT_SNAPSHOT to all.
// Deploy on Railway: see Dockerfile in this directory.
//
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <array>
#include <vector>

#include "../src/net/NetProtocol.h"

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
    int     fd     = -1;
    bool    active = false;
    uint8_t id     = 0;
    float   x=0, y=0, z=0, yaw=0;
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
        if (!c.active) continue;
        auto& s = snap.players[snap.count++];
        s.id = c.id; s.x = c.x; s.y = c.y; s.z = c.z; s.yaw = c.yaw;
    }
    int plen = 2 + snap.count * (int)sizeof(RemoteState);
    for (auto& c : g_clients)
        if (c.active) c.SendRaw(&snap, plen);
}

// ── Disconnect ────────────────────────────────────────────────────────────────
static void DisconnectClient(int i) {
    auto& c = g_clients[i];
    if (!c.active) return;
    printf("[Server] Player %d disconnected\n", c.id);
    close(c.fd);
    c.fd = -1; c.active = false; c.buf.clear();

    PktLeave leave{};
    leave.id = c.id;
    for (auto& o : g_clients)
        if (o.active) o.SendRaw(&leave, sizeof(leave));
}

// ── Process one framed packet ─────────────────────────────────────────────────
static void ProcessPayload(int i, const uint8_t* buf, int len) {
    auto& c = g_clients[i];
    if (len < 1) return;

    if (buf[0] == PKT_JOIN) {
        PktWelcome w{}; w.myId = c.id;
        c.SendRaw(&w, sizeof(w));
        printf("[Server] Player %d joined\n", c.id);

    } else if (buf[0] == PKT_POSITION && len >= (int)sizeof(PktPosition)) {
        const auto* p = reinterpret_cast<const PktPosition*>(buf);
        c.x = p->x; c.y = p->y; c.z = p->z; c.yaw = p->yaw;
    }
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
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
                c.id      = (uint8_t)slot;
                c.x = c.y = c.z = c.yaw = 0.f;
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
