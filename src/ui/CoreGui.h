#pragma once
#include <SDL.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <future>
#include "auth/AuthClient.h"
#include "auth/FriendClient.h"

// Forward-declare OpenGL types without pulling in glad.h here
// (GLuint == unsigned int on every platform we target)
using GuiGLuint = unsigned int;

class CoreGui {
public:
    void Init(SDL_Window* window, SDL_GLContext glContext);
    void Shutdown();

    void ProcessEvent(const SDL_Event& e);
    void BeginFrame();
    void Render();          // in-game HUD + pause menu
    void RenderHomePage();  // lobby screen (before game starts)

    bool GameStarted()   const { return m_gameStarted; }
    bool IsMenuOpen()    const { return m_menuOpen; }
    void SetMenuOpen(bool open) {
        if (open && !m_menuOpen) m_skipEscapeThisFrame = true;
        m_menuOpen = open;
    }
    bool WantsQuit()     const { return m_wantsQuit; }
    bool WantsLeave()    const { return m_wantsLeave; }
    void ClearLeave() {
        m_wantsLeave       = false;
        m_gameStarted      = false;
        m_menuOpen         = false;
        m_leaveConfirmOpen = false;
    }
    bool WantsReset()    const { return m_wantsReset; }
    void ClearReset()          { m_wantsReset = false; }

    void SetConnected(bool connected, int playerCount = 0) {
        m_connected   = connected;
        m_playerCount = playerCount;
    }
    void SetShiftLock(bool on) { m_shiftLock = on; }

    // Current session player names (set by Engine each frame from NetClient)
    void SetSessionPlayers(std::vector<std::string> players) {
        m_sessionPlayers = std::move(players);
    }

    // Join-friend override: Engine reads this after GameStarted() fires
    bool               HasOverrideJoinAddr() const { return m_hasOverrideJoinAddr; }
    const std::string& GetOverrideJoinAddr() const { return m_overrideJoinAddr; }
    void               ClearOverrideJoinAddr()      {
        m_hasOverrideJoinAddr = false;
        m_overrideJoinAddr.clear();
    }

    // Avatar colour accessors (float[3] = {r,g,b} in 0-1 range)
    const float* GetAvatarSkin()  const { return m_avatarSkin;  }
    const float* GetAvatarShirt() const { return m_avatarShirt; }
    const float* GetAvatarPants() const { return m_avatarPants; }
    bool IsAvatarDirty()          const { return m_avatarDirty; }
    void ClearAvatarDirty()             { m_avatarDirty = false; }

    // Nametags — call each frame before Render() with screen-space positions
    struct NametagInfo { float x, y; std::string name; };
    void SetNametags(std::vector<NametagInfo> tags) { m_nametags = std::move(tags); }

    // Toast notifications — push from anywhere (Engine, GUI, etc.)
    void PushToast(const std::string& text, const std::string& sub = "",
                   float life = 4.f, ImU32 iconColor = IM_COL32(100, 200, 255, 255));

    // In-game chat
    struct ChatEntry { std::string name, text; float age = 0.f; };
    void AddChatMessage(const std::string& name, const std::string& text);
    void SetChatOpen(bool open) { m_chatOpen = open; }
    bool IsChatOpen()    const  { return m_chatOpen; }
    // Returns pending chat text to send (cleared on read)
    std::string GetAndClearPendingChatSend() {
        std::string s = m_pendingChatSend;
        m_pendingChatSend.clear();
        return s;
    }

    // Auth / user info
    bool               IsLoggedIn()    const { return m_loggedIn; }
    const std::string& GetUsername()   const { return m_username; }

    // App-level settings (not game settings — launcher/UI preferences only)
    struct AppSettings {
        float accentR   = 0.11f;  // accent colour (default: blue)
        float accentG   = 0.36f;
        float accentB   = 0.94f;
        float uiScale   = 1.0f;   // 0.85 / 1.0 / 1.15 / 1.30
        bool  showNametags    = true;
        bool  showOnlineStatus = true;
    };
    const AppSettings& GetAppSettings() const { return m_appSettings; }

private:
    void DrawMenuButton();
    void DrawEscapeMenu();
    void DrawHomePage();
    void DrawAuthScreen();   // login / signup card (calls server)
    void DrawSettingsPanel();       // app-level settings overlay
    void DrawProfilePage();         // standalone own-profile full-screen page
    void DrawFriendProfilePage();   // standalone friend-profile full-screen page
    void DrawServerBrowser();       // full-screen server info panel
    void DrawToasts();              // floating toast notifications (foreground draw list)
    void DrawChat();                // in-game chat overlay

    // 3-D avatar preview (off-screen FBO rendered before ImGui flushes)
    void InitAvatarPreview();
    void ShutdownAvatarPreview();
    void RenderAvatarPreview(bool headshot = false);  // renders to m_avatarFBO; headshot=true → static crop on head+shoulders

    ImFont* m_fontTitle        = nullptr;

    // Avatar 3-D preview FBO (unsigned int == GLuint, avoids glad.h in header)
    static constexpr int kAvatarFBOW = 200;
    static constexpr int kAvatarFBOH = 280;
    GuiGLuint m_avatarFBO     = 0;
    GuiGLuint m_avatarTex     = 0;
    GuiGLuint m_avatarDepthRB = 0;
    GuiGLuint m_avatarVAO     = 0;
    GuiGLuint m_avatarVBO     = 0;
    GuiGLuint m_avatarEBO     = 0;
    GuiGLuint m_avatarProg    = 0;

    AppSettings m_appSettings;
    bool        m_settingsOpen = false;
    int         m_settingsTab  = 0;   // 0=Account 1=Appearance 2=Privacy 3=About
    bool        m_profileOpen       = false;
    bool        m_friendProfileOpen = false;  // viewing a specific friend's profile
    std::string m_viewingFriendName;          // which friend we're viewing
    void        SaveAppSettings();
    void        LoadAppSettings();

    // Session players (for pause menu list)
    std::vector<std::string> m_sessionPlayers;

    // ── Friend system ─────────────────────────────────────────────────────────
    std::vector<FriendEntry>    m_friends;
    std::vector<FriendReqEntry> m_friendRequests;

    // Async futures
    std::future<FriendListResult>  m_friendListFuture;
    std::future<FriendReqsResult>  m_friendReqsFuture;
    std::future<FriendOpResult>    m_friendOpFuture;
    std::future<JoinFriendResult>  m_joinFriendFuture;
    std::future<UserProfileResult> m_friendProfileFuture;
    bool m_friendListInFlight    = false;
    bool m_friendReqsInFlight    = false;
    bool m_friendOpInFlight      = false;
    bool m_joinInFlight          = false;
    bool m_friendProfileInFlight = false;

    // Cached friend profile (for DrawFriendProfilePage)
    std::string       m_profileViewingUser;   // username the cache is for
    UserProfileResult m_cachedFriendProfile;

    // ── Server Browser ────────────────────────────────────────────────────────
    bool               m_serverBrowserOpen    = false;
    bool               m_serverStatusInFlight = false;
    std::future<ServerStatusResult> m_serverStatusFuture;
    ServerStatusResult m_cachedServerStatus;

    // ── Toast notifications ───────────────────────────────────────────────────
    struct Toast {
        std::string text, sub;
        float       age  = 0.f;
        float       life = 4.f;
        ImU32       iconColor = IM_COL32(100, 200, 255, 255);
    };
    std::deque<Toast> m_toasts;

    // ── In-game chat ──────────────────────────────────────────────────────────
    std::deque<ChatEntry> m_chatLog;
    bool   m_chatOpen          = false;
    char   m_chatBuf[80]       = {};
    std::string m_pendingChatSend;

    float m_friendRefreshT     = 0.f;  // seconds until next auto-refresh
    int   m_prevFriendReqCount = 0;    // count from last poll, for toast detection
    bool  m_toastDismissed     = false; // user dismissed the "new request" toast

    // Add-friend input (Friends tab)
    char        m_addFriendBuf[32] = {};
    std::string m_addFriendStatus;  // "" | "Sent!" | "Error: ..."

    // Friends sub-tabs: 0=Friends, 1=Pending, 2=Blocked
    int  m_friendsSubTab = 0;

    // Blocked users (local list — persisted in profile)
    std::vector<std::string> m_blockedUsers;
    char        m_blockUserBuf[32] = {};
    std::string m_blockStatus;

    // Per-session tracking so we don't double-send
    std::set<std::string> m_sentFriendReqs;

    // Override join addr — set by CoreGui when a friend join result arrives
    bool        m_hasOverrideJoinAddr = false;
    std::string m_overrideJoinAddr;

    // Helpers
    void DrawFriendsTab(ImDrawList* cdl, float padX, float W, float sideW);
    void PollFriendFutures();
    void KickFriendRefresh();

    int  m_sidebarTab          = 0;
    bool m_wantsLeave          = false;
    bool m_gameStarted         = false;
    bool m_menuOpen            = false;
    bool m_leaveConfirmOpen    = false;
    bool m_wantsQuit           = false;
    bool m_wantsReset          = false;
    bool m_skipEscapeThisFrame = false;
    bool m_connected           = false;
    bool m_shiftLock           = false;
    int  m_playerCount         = 0;

    // Avatar colours
    float m_avatarSkin[3]  = {0.976f, 0.820f, 0.173f};
    float m_avatarShirt[3] = {0.059f, 0.420f, 0.690f};
    float m_avatarPants[3] = {0.110f, 0.529f, 0.047f};
    bool  m_avatarDirty    = true;

    // Auth — server address (read from server.txt / embedded constant in Init)
    std::string m_authHost;
    uint16_t    m_authPort = 7777;
    std::string m_sessionPath;   // path to session.dat (written on login, deleted on logout)
    std::string m_avatarPath;    // path to avatar.dat  (written whenever colours change)

    void SaveAvatar();
    void LoadAvatar();

    // User profile (locally stored, per-account)
    std::string m_displayName;
    std::string m_email;
    std::string m_phoneNumber;
    std::string m_bio;                          // short "About me" text
    bool        m_editingDisplayName = false;
    bool        m_editingEmail       = false;
    bool        m_editingPhone       = false;
    bool        m_editingBio         = false;
    char        m_inputDisplayName[64]  = {};
    char        m_inputEmail[128]       = {};
    char        m_inputPhone[32]        = {};
    char        m_inputBio[160]         = {};   // 150-char visible limit + null
    void        SaveProfile();
    void        LoadProfile();

    // Nametag data set each frame by Engine
    std::vector<NametagInfo> m_nametags;

    void SaveSession();
    void ClearSession();

    // Async auth state
    enum class AuthState { Login, Signup };
    AuthState   m_authState     = AuthState::Login;
    bool        m_loggedIn      = false;
    std::string m_username;
    bool        m_authInFlight  = false;   // background thread running
    std::string m_pendingUser;             // username submitted while in flight
    std::future<AuthResult> m_authFuture;
    char m_inputUser[24]        = {};
    char m_inputPass[64]        = {};
    char m_inputPassConfirm[64] = {};
    char m_authError[100]       = {};
};
