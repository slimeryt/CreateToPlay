#include "Engine.h"
#include <SDL.h>
#include <glad/glad.h>
#include <btBulletDynamicsCommon.h>
#include <algorithm>
#include <cstdio>
#include <string>

bool Engine::Init() {
    // 1. Window
    if (!m_window.Init("CreateToPlay", 1280, 720)) return false;

    // 2. Renderer — resolve shader path via SDL_GetBasePath
    char* base = SDL_GetBasePath();
    std::string shaderDir = std::string(base) + "assets/shaders";
    SDL_free(base);
    if (!m_renderer.Init(shaderDir)) return false;

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

            // Read server address from assets/server.txt (set this after Railway deploy)
            // Format: "host:port"  e.g. "your-service.railway.app:12345"
            std::string serverAddr;
            char* base = SDL_GetBasePath();
            std::string cfgPath = std::string(base) + "assets/server.txt";
            SDL_free(base);

            if (FILE* f = fopen(cfgPath.c_str(), "r")) {
                char line[256] = {};
                if (fgets(line, sizeof(line), f)) {
                    // Strip newline
                    for (char* p = line; *p; ++p)
                        if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
                    serverAddr = line;
                }
                fclose(f);
            }

            if (!serverAddr.empty()) {
                std::string host = serverAddr;
                uint16_t    port = NET_DEFAULT_PORT;
                auto colon = serverAddr.rfind(':');
                if (colon != std::string::npos) {
                    host = serverAddr.substr(0, colon);
                    port = (uint16_t)std::stoi(serverAddr.substr(colon + 1));
                }
                printf("[Engine] Connecting to server: %s:%u\n", host.c_str(), port);
                m_netClient.Connect(host, port);
            } else {
                printf("[Engine] No server.txt found — offline/solo mode\n");
            }
        }

        // ── Game simulation ───────────────────────────────────────────────────
        if (m_coreGui.GameStarted() && !m_coreGui.IsMenuOpen()) {
            accum += frame;
            while (accum >= kFixedDt) {
                FixedUpdate(kFixedDt);
                accum -= kFixedDt;
            }

            // Send position + drain server packets every display frame
            m_netClient.Update(m_character.GetPosition(), m_character.GetFacingYaw());
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

    bool menuOpen = m_coreGui.IsMenuOpen();
    SDL_SetRelativeMouseMode(menuOpen ? SDL_FALSE : SDL_TRUE);
    if (menuOpen) m_input.ClearMouseDelta();
}

void Engine::FixedUpdate(float dt) {
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
    m_camera.FollowTarget(m_character.GetPosition(), &m_physics, m_character.GetBody());

    // ── Shadow pass ───────────────────────────────────────────────────────────
    m_renderer.BeginShadowPass();
    m_workspace.RenderAll(m_renderer);
    // Shadow ghosts
    for (const auto& r : m_netClient.GetRemotePlayers()) {
        if (!r.active) continue;
        // Torso
        m_renderer.DrawBox(r.pos + glm::vec3(0.f, 0.75f, 0.f),
                           {1.0f, 1.5f, 0.6f}, {}, r.yaw);
        // Head
        m_renderer.DrawBox(r.pos + glm::vec3(0.f, 1.9f, 0.f),
                           {0.8f, 0.8f, 0.8f}, {}, r.yaw);
    }

    // ── Main pass ─────────────────────────────────────────────────────────────
    m_renderer.BeginMainPass(m_camera);
    m_workspace.RenderAll(m_renderer);
    // Render remote players as ghost humanoids (distinct cyan tint)
    for (const auto& r : m_netClient.GetRemotePlayers()) {
        if (!r.active) continue;
        glm::vec3 ghostColor = {0.30f, 0.70f, 0.95f};
        // Torso
        m_renderer.DrawBox(r.pos + glm::vec3(0.f, 0.75f, 0.f),
                           {1.0f, 1.5f, 0.6f}, ghostColor, r.yaw);
        // Head
        m_renderer.DrawBox(r.pos + glm::vec3(0.f, 1.9f, 0.f),
                           {0.8f, 0.8f, 0.8f}, ghostColor * 0.85f, r.yaw);
        // Left arm
        m_renderer.DrawBox(r.pos + glm::vec3(-0.75f, 0.6f, 0.f),
                           {0.5f, 1.2f, 0.5f}, ghostColor * 0.90f, r.yaw);
        // Right arm
        m_renderer.DrawBox(r.pos + glm::vec3( 0.75f, 0.6f, 0.f),
                           {0.5f, 1.2f, 0.5f}, ghostColor * 0.90f, r.yaw);
        // Left leg
        m_renderer.DrawBox(r.pos + glm::vec3(-0.28f, -0.55f, 0.f),
                           {0.46f, 1.1f, 0.5f}, ghostColor * 0.80f, r.yaw);
        // Right leg
        m_renderer.DrawBox(r.pos + glm::vec3( 0.28f, -0.55f, 0.f),
                           {0.46f, 1.1f, 0.5f}, ghostColor * 0.80f, r.yaw);
    }

    // ── Post-process ──────────────────────────────────────────────────────────
    m_renderer.EndFrame();

    m_coreGui.BeginFrame();
    m_coreGui.Render();

    m_window.SwapBuffers();
}

void Engine::Shutdown() {
    m_netClient.Disconnect();
    m_coreGui.Shutdown();
    m_physics.Shutdown();
    m_renderer.Shutdown();
    m_window.Shutdown();
}
