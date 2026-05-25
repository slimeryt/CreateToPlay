#include "Engine.h"
#include "embedded/EmbeddedAssets.h"
#include "character/Character.h"
#include <SDL.h>
#include <glad/glad.h>
#include <btBulletDynamicsCommon.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>

bool Engine::Init() {
    // 1. Window
    if (!m_window.Init("CreateToPlay", 1280, 720)) return false;

    // 2. Renderer — uses embedded shaders, no file I/O
    if (!m_renderer.Init()) return false;

    // 4. Physics
    m_physics.Init();

    // 5. Workspace — create green baseplate
    {
        auto plate = std::make_unique<BasePart>();
        plate->name        = "Baseplate";
        plate->position    = glm::vec3(0.0f, -0.2f, 0.0f);
        plate->size        = glm::vec3(100.0f, 0.4f, 100.0f);
        plate->color       = glm::vec3(0.38f, 0.56f, 0.29f);
        plate->reflectance = 0.02f;
        plate->rigidBody   = m_physics.CreateBoxBody(
            glm::vec3(50.0f, 0.2f, 50.0f), 0.0f, plate->position);
        m_workspace.AddPart(std::move(plate));
    }

    // Add a few decorative blocks
    auto addBlock = [&](glm::vec3 pos, glm::vec3 sz, glm::vec3 col) {
        auto p = std::make_unique<BasePart>();
        p->position    = pos;
        p->size        = sz;
        p->color       = col;
        p->reflectance = 0.08f;
        p->rigidBody   = m_physics.CreateBoxBody(sz * 0.5f, 0.0f, pos);
        m_workspace.AddPart(std::move(p));
    };
    addBlock({10.0f, 1.0f, 10.0f}, {4.0f, 2.0f, 4.0f}, {0.80f, 0.22f, 0.22f});
    addBlock({-8.0f, 1.0f, -6.0f}, {3.0f, 2.0f, 3.0f}, {0.22f, 0.35f, 0.80f});
    addBlock({ 5.0f, 0.5f,-12.0f}, {6.0f, 1.0f, 2.0f}, {0.85f, 0.80f, 0.20f});

    // 6. Camera
    m_camera.Init(60.0f, 1280.0f / 720.0f, 0.1f, 1000.0f);

    // 7. Character
    m_character.Init(m_workspace, m_physics, glm::vec3(0.0f, 5.0f, 0.0f));

    // 8. Controller
    m_controller.Init(&m_character, &m_camera, &m_input, &m_physics);

    // 9. CoreGUI
    m_coreGui.Init(m_window.GetSDLWindow(), m_window.GetGLContext());

    m_running = true;
    printf("Engine initialized.\n");
    return true;
}

void Engine::Run() {
    Uint64 last  = SDL_GetPerformanceCounter();
    Uint64 freq  = SDL_GetPerformanceFrequency();
    float  accum = 0.0f;
    bool   wasGameStarted = false;

    while (m_running) {
        Uint64 now   = SDL_GetPerformanceCounter();
        float  frame = (float)(now - last) / (float)freq;
        last = now;
        frame = std::min(frame, 0.25f);

        ProcessEvents();
        if (m_input.WantsQuit() || m_coreGui.WantsQuit()) m_running = false;

        // ── Handle Leave ──────────────────────────────────────────────────────
        if (m_coreGui.WantsLeave()) {
            m_netClient.Disconnect();
            btRigidBody* body = m_character.GetBody();
            btTransform t; t.setIdentity();
            t.setOrigin(btVector3(0.f, 5.f, 0.f));
            body->setWorldTransform(t);
            body->getMotionState()->setWorldTransform(t);
            body->setLinearVelocity(btVector3(0,0,0));
            body->setAngularVelocity(btVector3(0,0,0));
            m_character.SetFacingYaw(0.f);
            m_coreGui.ClearLeave();
            wasGameStarted = false;
        }

        // ── Connect to server when game first starts ──────────────────────────
        if (m_coreGui.GameStarted() && !wasGameStarted) {
            wasGameStarted = true;

            std::string serverAddr;

            // Join-friend override: CoreGui sets this when JoinFriend result arrives
            if (m_coreGui.HasOverrideJoinAddr()) {
                serverAddr = m_coreGui.GetOverrideJoinAddr();
                m_coreGui.ClearOverrideJoinAddr();
                printf("[Engine] Joining friend's server: %s\n", serverAddr.c_str());
            } else {
                // Read server address from assets/server.txt
                // Format: "host:port"  e.g. "your-service.railway.app:12345"
                char* base = SDL_GetBasePath();
                std::string cfgPath = std::string(base) + "assets/server.txt";
                SDL_free(base);

                if (FILE* f = fopen(cfgPath.c_str(), "r")) {
                    char line[256] = {};
                    if (fgets(line, sizeof(line), f)) {
                        for (char* p = line; *p; ++p)
                            if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
                        serverAddr = line;
                    }
                    fclose(f);
                }

                // Fall back to compiled-in address if no file found
                if (serverAddr.empty())
                    serverAddr = kEmbeddedServerAddr;
            }

            if (!serverAddr.empty()) {
                std::string host = serverAddr;
                uint16_t    port = NET_DEFAULT_PORT;
                auto colon = serverAddr.rfind(':');
                if (colon != std::string::npos) {
                    host = serverAddr.substr(0, colon);
                    port = (uint16_t)std::stoi(serverAddr.substr(colon + 1));
                }
                // Push name + avatar colours so they're included in PKT_JOIN
                m_netClient.SetLocalName(m_coreGui.GetUsername());
                {
                    const float* s  = m_coreGui.GetAvatarSkin();
                    const float* sh = m_coreGui.GetAvatarShirt();
                    const float* pa = m_coreGui.GetAvatarPants();
                    m_netClient.SetLocalAvatar({s[0],s[1],s[2]}, {sh[0],sh[1],sh[2]}, {pa[0],pa[1],pa[2]});
                }
                printf("[Engine] Connecting to server: %s:%u\n", host.c_str(), port);
                m_netClient.Connect(host, port);
            } else {
                printf("[Engine] No server.txt found — offline/solo mode\n");
            }
        }

        // ── Game simulation (runs even while menu is open) ───────────────────
        if (m_coreGui.GameStarted()) {
            accum += frame;
            while (accum >= kFixedDt) {
                FixedUpdate(kFixedDt);
                accum -= kFixedDt;
            }

            // Send position + drain server packets every display frame
            m_netClient.Update(m_character.GetPosition(), m_character.GetFacingYaw());

            // Sync player names to CoreGui for the pause-menu player list
            {
                std::vector<std::string> players;
                players.push_back(m_coreGui.GetUsername());  // local player first
                for (const auto& r : m_netClient.GetRemotePlayers())
                    if (r.active && !r.name.empty())
                        players.push_back(r.name);
                m_coreGui.SetSessionPlayers(std::move(players));
            }
        }

        // ── Aspect ratio / resize ─────────────────────────────────────────────
        {
            int w, h;
            SDL_GetWindowSize(m_window.GetSDLWindow(), &w, &h);
            if (h > 0) m_camera.SetAspect((float)w / (float)h);
            m_renderer.Resize(w, h);
        }

        Render();
    }
}

void Engine::ProcessEvents() {
    m_input.BeginFrame();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        m_coreGui.ProcessEvent(e);
        m_input.HandleEvent(e);
    }

    if (!m_coreGui.GameStarted()) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        m_input.ClearMouseDelta();
        return;
    }

    if (m_input.IsKeyJustPressed(SDL_SCANCODE_ESCAPE) && !m_coreGui.IsMenuOpen())
        m_coreGui.SetMenuOpen(true);

    // Shift lock toggle — must live here (once per display frame) so the fixed-
    // update accumulator can't fire it twice in one frame and cancel itself.
    if (m_input.IsKeyJustPressed(SDL_SCANCODE_LSHIFT) && !m_coreGui.IsMenuOpen())
        m_controller.ToggleShiftLock();

    bool menuOpen = m_coreGui.IsMenuOpen();
    SDL_SetRelativeMouseMode(menuOpen ? SDL_FALSE : SDL_TRUE);
    if (menuOpen) m_input.ClearMouseDelta();
}

void Engine::FixedUpdate(float dt) {
    // Apply avatar colours to local character whenever they change
    if (m_coreGui.IsAvatarDirty()) {
        const float* s  = m_coreGui.GetAvatarSkin();
        const float* sh = m_coreGui.GetAvatarShirt();
        const float* pa = m_coreGui.GetAvatarPants();
        AvatarConfig cfg;
        cfg.skin  = {s[0],  s[1],  s[2]};
        cfg.shirt = {sh[0], sh[1], sh[2]};
        cfg.pants = {pa[0], pa[1], pa[2]};
        m_character.SetAvatar(cfg);
        m_coreGui.ClearAvatarDirty();
    }

    if (m_coreGui.WantsReset()) {
        btRigidBody* body = m_character.GetBody();
        btTransform t;
        t.setIdentity();
        t.setOrigin(btVector3(0.f, 5.f, 0.f));
        body->setWorldTransform(t);
        body->getMotionState()->setWorldTransform(t);
        body->setLinearVelocity(btVector3(0, 0, 0));
        body->setAngularVelocity(btVector3(0, 0, 0));
        m_character.SetFacingYaw(0.f);
        m_coreGui.ClearReset();
    }

    m_controller.Update(dt);

    // ── Local character animation ──────────────────────────────────────────────
    {
        btVector3 vel = m_character.GetBody()->getLinearVelocity();
        bool moving   = (vel.x() * vel.x() + vel.z() * vel.z()) > 1.0f;
        m_character.AdvanceAnimation(dt, moving, vel.y());
    }

    // ── Remote player smoothing, animation, and collision bodies ──────────────
    {
        auto& remotes = m_netClient.GetMutableRemotePlayers();
        const float alpha = 1.0f - std::exp(-18.0f * dt);

        for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
            auto& r = remotes[i];

            if (r.active && !m_remoteBodies[i]) {
                // First time this slot becomes active — snap smooth state to
                // network position so the player doesn't lerp from the origin.
                r.smoothPos  = r.pos;
                r.smoothYaw  = r.yaw;
                r.smoothVy   = 0.f;
                r.walkPhase  = 0.f;
                r.jumpBlend  = 0.f;
                r.armAirPose = 0.f;
                r.legAirPose = 0.f;

                // Create kinematic capsule (same proportions as local player)
                m_remoteBodies[i] = m_physics.CreateCapsuleBody(0.7f, 2.8f, 0.f, r.pos);
                m_remoteBodies[i]->setCollisionFlags(
                    m_remoteBodies[i]->getCollisionFlags() |
                    btCollisionObject::CF_KINEMATIC_OBJECT);
                m_remoteBodies[i]->setActivationState(DISABLE_DEACTIVATION);
            } else if (!r.active && m_remoteBodies[i]) {
                // Player left — destroy their collision body
                m_physics.RemoveBody(m_remoteBodies[i]);
                m_remoteBodies[i] = nullptr;
            }

            if (!r.active) continue;

            // Smooth position
            glm::vec3 prevSmooth = r.smoothPos;
            r.smoothPos += (r.pos - r.smoothPos) * alpha;

            // Smooth yaw (take short arc)
            float yawDiff = r.yaw - r.smoothYaw;
            while (yawDiff >  3.14159f) yawDiff -= 6.28318f;
            while (yawDiff < -3.14159f) yawDiff += 6.28318f;
            r.smoothYaw += yawDiff * alpha;

            // Estimate velocities from smooth position delta
            glm::vec3 moved = r.smoothPos - prevSmooth;
            r.smoothVy      = moved.y / dt;

            bool moving = (moved.x * moved.x + moved.z * moved.z) > (0.001f * dt * dt);
            bool inAir  = std::abs(r.smoothVy) > 2.5f;

            // Walk cycle — only when grounded
            float groundFactor = 1.0f - r.jumpBlend;
            if (moving && groundFactor > 0.2f) {
                r.walkPhase += dt * 7.0f;
                if (r.walkPhase > 6.28318f) r.walkPhase -= 6.28318f;
            } else {
                r.walkPhase *= std::exp(-8.0f * dt);
            }

            // Air blend
            float blendRate = inAir ? 14.0f : 9.0f;
            float blendTgt  = inAir ?  1.0f : 0.0f;
            r.jumpBlend += (blendTgt - r.jumpBlend) * (1.f - std::exp(-blendRate * dt));

            // Air pose angles
            bool rising        = r.smoothVy > 2.5f;
            float armAirTarget = rising ? -0.85f :  0.50f;
            float legAirTarget = rising ? -0.20f :  0.18f;
            r.armAirPose += (armAirTarget - r.armAirPose) * (1.f - std::exp(-10.f * dt));
            r.legAirPose += (legAirTarget - r.legAirPose) * (1.f - std::exp(-10.f * dt));

            // Keep kinematic body in sync with smooth position
            if (m_remoteBodies[i]) {
                btTransform t;
                t.setIdentity();
                t.setOrigin(btVector3(r.smoothPos.x, r.smoothPos.y, r.smoothPos.z));
                m_remoteBodies[i]->getMotionState()->setWorldTransform(t);
            }
        }
    }

    // Pass shift-lock state to HUD so it can draw the crosshair dot
    m_coreGui.SetShiftLock(m_controller.IsShiftLocked());

    m_physics.Step(dt);
    m_workspace.UpdateAll();
    m_character.SyncVisuals();
}

void Engine::Render() {
    if (!m_coreGui.GameStarted()) {
        glClearColor(0.07f, 0.07f, 0.10f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        m_coreGui.BeginFrame();
        m_coreGui.RenderHomePage();
        m_window.SwapBuffers();
        return;
    }

    if (!m_coreGui.IsMenuOpen()) {
        m_camera.UpdateOrbit(m_input.GetMouseDX(), m_input.GetMouseDY());
        m_camera.Zoom(m_input.GetScrollDelta());
    }
    m_camera.SetShiftLock(m_controller.IsShiftLocked());
    m_camera.FollowTarget(m_character.GetPosition(), &m_physics, m_character.GetBody());

    // Helper: draw one animated R6 character (both shadow + main pass share this logic)
    // inShadow = true  → color ignored, yaw-overload used for shadow pass
    // inShadow = false → full colour + quat rotation for proper limb animation
    auto DrawRemote = [&](const RemotePlayer& r, bool inShadow) {
        glm::quat facing = glm::angleAxis(r.smoothYaw, glm::vec3(0.f, 1.f, 0.f));

        // Walk swing fades out as the player becomes airborne
        float groundFactor = 1.0f - r.jumpBlend;
        float swing        = std::sin(r.walkPhase) * 0.50f * groundFactor;

        static const glm::vec3 kArmPivot[2] = {{-1.12f,  0.84f, 0.f}, { 1.12f,  0.84f, 0.f}};
        static const glm::vec3 kLegPivot[2] = {{-0.385f,-0.63f, 0.f}, { 0.385f,-0.63f, 0.f}};
        static const glm::vec3 kArmCtr[2]   = {{-1.12f,  0.14f, 0.f}, { 1.12f,  0.14f, 0.f}};
        static const glm::vec3 kLegCtr[2]   = {{-0.385f,-1.33f, 0.f}, { 0.385f,-1.33f, 0.f}};

        // Torso + head (no limb rotation)
        auto P = [&](glm::vec3 loc) { return r.smoothPos + facing * loc; };
        m_renderer.DrawBox(P({0.f, 0.14f, 0.f}), {1.40f,1.40f,0.70f},
                           inShadow ? glm::vec3{} : r.shirt, facing);
        m_renderer.DrawBox(P({0.f, 1.51f, 0.f}), {1.20f,1.20f,1.20f},
                           inShadow ? glm::vec3{} : r.skin,  facing);

        // Arms — walk: L=+swing R=−swing; air: both → r.armAirPose
        const float walkSwing[2] = { swing, -swing };
        for (int i = 0; i < 2; ++i) {
            float angle    = walkSwing[i] + r.armAirPose * r.jumpBlend;
            glm::quat rot  = glm::angleAxis(angle, glm::vec3(1,0,0));
            glm::vec3 lpos = kArmPivot[i] + rot * (kArmCtr[i] - kArmPivot[i]);
            m_renderer.DrawBox(r.smoothPos + facing * lpos, {0.70f,1.40f,0.70f},
                               inShadow ? glm::vec3{} : r.skin, facing * rot);
        }

        // Legs — walk: L=−swing R=+swing; air: both → r.legAirPose
        const float legWalkSwing[2] = { -swing, swing };
        for (int i = 0; i < 2; ++i) {
            float angle    = legWalkSwing[i] + r.legAirPose * r.jumpBlend;
            glm::quat rot  = glm::angleAxis(angle, glm::vec3(1,0,0));
            glm::vec3 lpos = kLegPivot[i] + rot * (kLegCtr[i] - kLegPivot[i]);
            m_renderer.DrawBox(r.smoothPos + facing * lpos, {0.63f,1.40f,0.70f},
                               inShadow ? glm::vec3{} : r.pants, facing * rot);
        }
    };

    // ── Shadow pass ───────────────────────────────────────────────────────────
    m_renderer.BeginShadowPass();
    m_workspace.RenderAll(m_renderer);
    for (const auto& r : m_netClient.GetRemotePlayers())
        if (r.active) DrawRemote(r, true);

    // ── Main pass ─────────────────────────────────────────────────────────────
    m_renderer.BeginMainPass(m_camera);
    m_workspace.RenderAll(m_renderer);
    for (const auto& r : m_netClient.GetRemotePlayers())
        if (r.active) DrawRemote(r, false);

    // ── Post-process ──────────────────────────────────────────────────────────
    m_renderer.EndFrame();

    // Count active remote players for HUD
    int remotePlayers = 0;
    for (const auto& r : m_netClient.GetRemotePlayers())
        if (r.active) ++remotePlayers;
    m_coreGui.SetConnected(m_netClient.IsConnected(), 1 + remotePlayers);

    m_coreGui.BeginFrame();

    // ── Nametags — project remote player head positions to screen ────────────
    {
        int w = 1280, h = 720;
        SDL_GetWindowSize(m_window.GetSDLWindow(), &w, &h);
        glm::mat4 vp = m_camera.GetProjection() * m_camera.GetView();

        std::vector<CoreGui::NametagInfo> tags;
        for (const auto& r : m_netClient.GetRemotePlayers()) {
            if (!r.active || r.name.empty()) continue;

            // Point just above the head
            glm::vec4 clip = vp * glm::vec4(r.pos + glm::vec3(0.f, 2.55f, 0.f), 1.f);
            if (clip.w <= 0.f) continue;          // behind camera
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.f || ndc.x > 1.f ||
                ndc.y < -1.f || ndc.y > 1.f) continue;  // off screen

            float sx = (ndc.x + 1.f) * 0.5f * (float)w;
            float sy = (1.f - ndc.y) * 0.5f * (float)h;
            tags.push_back({sx, sy, r.name});
        }
        m_coreGui.SetNametags(std::move(tags));
    }

    m_coreGui.Render();

    m_window.SwapBuffers();
}

void Engine::Shutdown() {
    m_netClient.Disconnect();
    m_coreGui.Shutdown();
    m_character.Shutdown();   // remove per-part hitbox bodies before physics shuts down
    m_physics.Shutdown();
    m_renderer.Shutdown();
    m_window.Shutdown();
}
