#pragma once
#include "Window.h"
#include "input/InputManager.h"
#include "renderer/Renderer.h"
#include "scene/Workspace.h"
#include "physics/PhysicsWorld.h"
#include "camera/Camera.h"
#include "character/Character.h"
#include "character/CharacterController.h"
#include "ui/CoreGui.h"
#include "net/NetClient.h"
#include <future>
#include <memory>
#include <string>

class Engine {
public:
    bool Init();
    void Run();
    void Shutdown();

private:
    void ProcessEvents();
    void FixedUpdate(float dt);
    void Render();

    Window              m_window;
    InputManager        m_input;
    Renderer            m_renderer;
    Workspace           m_workspace;
    PhysicsWorld        m_physics;
    Camera              m_camera;
    Character           m_character;
    CharacterController m_controller;
    CoreGui             m_coreGui;
    NetClient           m_netClient;

    bool m_running = false;

    // Kinematic collision bodies for remote players (null = slot unused)
    btRigidBody* m_remoteBodies[NET_MAX_PLAYERS] = {};

    // Track per-slot active state + name last frame for join/leave toast detection
    bool        m_prevRemoteActive[NET_MAX_PLAYERS] = {};
    std::string m_prevRemoteName[NET_MAX_PLAYERS];

    // Async connection during loading screen
    std::future<bool> m_connectFuture;
    bool              m_connectInFlight = false;
    float             m_loadMinTimer    = 0.f;  // minimum loading time (seconds)
    std::string       m_pendingServerAddr;       // stored while connecting

    static constexpr float kFixedDt = 1.0f / 60.0f;
};
