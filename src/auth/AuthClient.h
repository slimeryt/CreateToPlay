#pragma once
#include <string>
#include <cstdint>

struct AuthResult {
    bool        ok    = false;
    std::string error;          // empty on success
};

// Synchronous (blocking) auth client.
// Call from a background thread — blocks until server responds or times out.
class AuthClient {
public:
    AuthResult Login   (const std::string& host, uint16_t port,
                        const std::string& username, const std::string& password);
    AuthResult Register(const std::string& host, uint16_t port,
                        const std::string& username, const std::string& password);
private:
    AuthResult Send(uint8_t type,
                    const std::string& host, uint16_t port,
                    const std::string& username, const std::string& password);
};
