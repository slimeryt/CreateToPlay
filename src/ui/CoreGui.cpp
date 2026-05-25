#include "CoreGui.h"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <SDL.h>
#include <string>
#include <algorithm>
#include <cstdio>

void CoreGui::Init(SDL_Window* window, SDL_GLContext glContext) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    char* base = SDL_GetBasePath();
    std::string fontPath = std::string(base) + "assets/fonts/Roboto-Medium.ttf";
    SDL_free(base);

    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 3;
    cfg.PixelSnapH  = false;

    // Body font — 17px, high oversample
    if (!io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 17.f, &cfg))
        io.Fonts->AddFontDefault();

    // Title font — 30px, same oversample; used instead of SetWindowFontScale
    m_fontTitle = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 30.f, &cfg);
    if (!m_fontTitle) m_fontTitle = io.Fonts->Fonts[0];

    ImGui::GetStyle().ScaleAllSizes(1.25f);

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330 core");
}

void CoreGui::Shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void CoreGui::ProcessEvent(const SDL_Event& e) {
    ImGui_ImplSDL2_ProcessEvent(&e);
}

void CoreGui::BeginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void CoreGui::Render() {
    if (m_menuOpen) {
        DrawEscapeMenu();

        if (m_leaveConfirmOpen) {
            // Confirm mode: Enter = leave, Esc = cancel
            if (ImGui::IsKeyPressed(ImGuiKey_Enter,  false)) m_wantsLeave       = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !m_skipEscapeThisFrame)
                m_leaveConfirmOpen = false;
        } else {
            // Normal mode keybinds
            if (ImGui::IsKeyPressed(ImGuiKey_L,      false)) m_leaveConfirmOpen = true;
            if (ImGui::IsKeyPressed(ImGuiKey_R,      false)) m_wantsReset       = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !m_skipEscapeThisFrame)
                m_menuOpen = false;
        }
        m_skipEscapeThisFrame = false;
    }

    DrawMenuButton(); // always on top

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Top-left menu button (drawn with primitives — no font dependency) ─────────

void CoreGui::DrawMenuButton() {
    const float kSize = 44.f;
    const float kPad  = 10.f;

    // Transparent host window just to anchor the hit-test area
    ImGui::SetNextWindowPos({kPad, kPad});
    ImGui::SetNextWindowSize({kSize, kSize});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.f, 0.f));
    ImGui::Begin("##menubtn", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Record top-left before the invisible button moves the cursor
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##hit", ImVec2(kSize, kSize));
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    if (clicked) m_menuOpen = !m_menuOpen;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background — black, blue tint on hover/active/open
    ImU32 bgCol = active      ? IM_COL32( 27,  92, 240, 255) :
                  hovered     ? IM_COL32( 18,  18,  26, 245) :
                  m_menuOpen  ? IM_COL32( 20,  60, 160, 230) :
                                IM_COL32(  8,   8,  12, 210);
    dl->AddRectFilled(p, ImVec2(p.x + kSize, p.y + kSize), bgCol, 10.f);

    // Subtle blue border
    ImU32 borderCol = m_menuOpen ? IM_COL32(27, 92, 240, 180) : IM_COL32(50, 50, 80, 140);
    dl->AddRect(p, ImVec2(p.x + kSize, p.y + kSize), borderCol, 10.f, 0, 1.2f);

    ImU32 lineCol = IM_COL32(240, 240, 255, 248);
    const float margin = kSize * 0.28f;

    if (m_menuOpen) {
        // X — two diagonal lines
        ImVec2 tl = { p.x + margin,         p.y + margin         };
        ImVec2 br = { p.x + kSize - margin,  p.y + kSize - margin };
        ImVec2 tr = { p.x + kSize - margin,  p.y + margin         };
        ImVec2 bl = { p.x + margin,          p.y + kSize - margin };
        dl->AddLine(tl, br, lineCol, 2.8f);
        dl->AddLine(tr, bl, lineCol, 2.8f);
    } else {
        // Three hamburger lines
        const float lineW  = kSize * 0.52f;
        const float lineH  = 2.8f;
        const float gap    = 5.5f;
        const float totalH = lineH * 3.f + gap * 2.f;
        float lx = p.x + (kSize - lineW) * 0.5f;
        float ly = p.y + (kSize - totalH) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            float y = ly + i * (lineH + gap);
            dl->AddRectFilled(ImVec2(lx, y), ImVec2(lx + lineW, y + lineH), lineCol, 1.4f);
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Escape / pause menu ───────────────────────────────────────────────────────

void CoreGui::DrawEscapeMenu() {
    ImGuiIO& io = ImGui::GetIO();

    // Full-screen dim overlay — deep black
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.60f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##overlay", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs      |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove         |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Panel — scales with window
    const float pw = std::max(620.f, std::min(1100.f, io.DisplaySize.x * 0.70f));
    const float ph = std::max(400.f, std::min( 700.f, io.DisplaySize.y * 0.62f));
    ImGui::SetNextWindowPos({
        (io.DisplaySize.x - pw) * 0.5f,
        (io.DisplaySize.y - ph) * 0.5f
    });
    ImGui::SetNextWindowSize({pw, ph});

    const float kPad  = 28.f;
    const float kBotH = 76.f;
    const float kBtnH = 42.f;

    // ── Palette ───────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.06f, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.11f, 0.11f, 0.18f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   7.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(kPad, kPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(0.f, 0.f));

    ImGui::Begin("##menu", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoSavedSettings);

    // ── Title — white, large, centered ───────────────────────────────────────
    ImGui::PushFont(m_fontTitle);
    const char* title = "CREATETOPLAY";
    ImGui::SetCursorPosX((pw - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextColored(ImVec4(1.f, 1.f, 1.f, 1.f), "%s", title);
    ImGui::PopFont();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.f);
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.f, 1.f, 1.f, 0.60f)); // blue separator
    ImGui::Separator();
    ImGui::PopStyleColor();

    // ── Bottom action bar ─────────────────────────────────────────────────────
    ImGui::SetCursorPosY(ph - kBotH);

    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.f, 1.f, 1.f, 0.55f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.f);

    float avail = pw - kPad * 2.f;
    float gap   = 10.f;

    if (m_leaveConfirmOpen) {
        // ── Confirm bar: Cancel (left) + Leave (right) ────────────────────────
        float btnW = (avail - gap) * 0.5f;

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.10f, 0.15f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.24f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.08f, 0.08f, 0.12f, 1.f));
        if (ImGui::Button("Cancel  [Esc]", ImVec2(btnW, kBtnH)))
            m_leaveConfirmOpen = false;
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.f, gap);

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.11f, 0.36f, 0.94f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.48f, 1.00f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.07f, 0.26f, 0.80f, 1.f));
        if (ImGui::Button("Leave  [Enter]", ImVec2(btnW, kBtnH)))
            m_wantsLeave = true;
        ImGui::PopStyleColor(3);
    } else {
        // ── Normal bar: Leave | Reset Character | Resume ──────────────────────
        float btnW = (avail - gap * 2.f) / 3.f;

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.07f, 0.07f, 0.10f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.11f, 0.11f, 0.18f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.05f, 0.05f, 0.08f, 1.f));
        if (ImGui::Button("Leave  [L]", ImVec2(btnW, kBtnH)))
            m_leaveConfirmOpen = true;
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.f, gap);

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.10f, 0.15f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.24f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.08f, 0.08f, 0.12f, 1.f));
        if (ImGui::Button("Reset Character  [R]", ImVec2(btnW, kBtnH)))
            m_wantsReset = true;
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.f, gap);

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.11f, 0.36f, 0.94f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.48f, 1.00f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.07f, 0.26f, 0.80f, 1.f));
        if (ImGui::Button("Resume  [Esc]", ImVec2(btnW, kBtnH)))
            m_menuOpen = false;
        ImGui::PopStyleColor(3);
    }

    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

// ── Home page ─────────────────────────────────────────────────────────────────

void CoreGui::RenderHomePage() {
    DrawHomePage();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void CoreGui::DrawHomePage() {
    ImGuiIO& io   = ImGui::GetIO();
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;

    // ── Full-screen background window ─────────────────────────────────────────
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize({W, H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.10f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
    ImGui::Begin("##home", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl       = ImGui::GetWindowDrawList();
    const float sideW    = 76.f;
    const float btnSlotH = 76.f;

    // ── Sidebar ───────────────────────────────────────────────────────────────
    dl->AddRectFilled({0.f, 0.f}, {sideW, H}, IM_COL32(8, 8, 14, 255));
    dl->AddLine({sideW, 0.f}, {sideW, H}, IM_COL32(38, 38, 60, 200), 1.f);

    // Logo — "C2P" monogram centered in sidebar at the top
    {
        const char* mono = "C2P";
        ImVec2 tsz = ImGui::CalcTextSize(mono);
        float lx = (sideW - tsz.x) * 0.5f;
        float ly = 14.f;
        // Pill background
        ImVec2 pillTL = {lx - 7.f, ly - 4.f};
        ImVec2 pillBR = {lx + tsz.x + 7.f, ly + tsz.y + 4.f};
        dl->AddRectFilled(pillTL, pillBR, IM_COL32(28, 92, 240, 200), 7.f);
        ImGui::SetCursorPos({lx, ly});
        ImGui::TextColored({1.f, 1.f, 1.f, 1.f}, "%s", mono);
    }

    struct SideItem { const char* label; };
    static const SideItem kItems[] = { {"Home"}, {"Avatar"}, {"More"} };

    // Push nav items down below the logo
    const float navOffsetY = 58.f;

    for (int i = 0; i < 3; ++i) {
        float by = navOffsetY + i * (btnSlotH + 4.f);

        ImGui::SetCursorPos({0.f, by});
        char bid[16]; snprintf(bid, sizeof(bid), "##sb%d", i);
        bool clicked = ImGui::InvisibleButton(bid, {sideW, btnSlotH});
        bool hovered = ImGui::IsItemHovered();
        if (clicked) m_sidebarTab = i;
        bool active  = (m_sidebarTab == i);

        // Hover / active tint
        if (active)
            dl->AddRectFilled({0.f, by}, {sideW, by + btnSlotH},
                IM_COL32(28, 92, 240, 28));
        else if (hovered)
            dl->AddRectFilled({0.f, by}, {sideW, by + btnSlotH},
                IM_COL32(255, 255, 255, 12));

        // Left accent bar — same height as background
        if (active)
            dl->AddRectFilled({0.f, by}, {3.f, by + btnSlotH},
                IM_COL32(28, 92, 240, 255), 2.f);

        ImU32 iconCol = active ? IM_COL32(80, 150, 255, 255)
                               : IM_COL32(170, 170, 185, 255);
        float cx = sideW * 0.5f;
        float iy = by + 12.f;

        if (i == 0) {
            // House: roof triangle + body rect + door cutout
            dl->AddTriangleFilled({cx, iy}, {cx - 12.f, iy + 11.f}, {cx + 12.f, iy + 11.f}, iconCol);
            dl->AddRectFilled({cx - 8.f, iy + 11.f}, {cx + 8.f, iy + 24.f}, iconCol, 1.f);
            dl->AddRectFilled({cx - 3.5f, iy + 16.f}, {cx + 3.5f, iy + 24.f}, IM_COL32(8, 8, 14, 255));
        } else if (i == 1) {
            // Person: circle head + rounded body
            dl->AddCircleFilled({cx, iy + 6.f}, 5.5f, iconCol);
            dl->AddRectFilled({cx - 7.f, iy + 13.f}, {cx + 7.f, iy + 24.f}, iconCol, 3.f);
        } else {
            // Three horizontal dots
            for (int d = 0; d < 3; ++d)
                dl->AddCircleFilled({cx - 8.f + d * 8.f, iy + 12.f}, 2.8f, iconCol);
        }

        // Label
        float lw = ImGui::CalcTextSize(kItems[i].label).x;
        ImGui::SetCursorPos({(sideW - lw) * 0.5f, by + 38.f});
        ImGui::PushStyleColor(ImGuiCol_Text,
            active ? ImVec4(0.50f, 0.72f, 1.f, 1.f) : ImVec4(0.62f, 0.62f, 0.72f, 1.f));
        ImGui::TextUnformatted(kItems[i].label);
        ImGui::PopStyleColor();
    }

    // ── Content area ──────────────────────────────────────────────────────────
    ImGui::SetCursorPos({sideW, 0.f});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.f, 0.f, 0.f, 0.f});
    ImGui::BeginChild("##content", {W - sideW, H}, false);
    ImDrawList* cdl = ImGui::GetWindowDrawList();

    const float padX = 40.f;

    if (m_sidebarTab == 0) {
        // ── Home tab ──────────────────────────────────────────────────────────
        ImGui::SetCursorPos({padX, 30.f});
        ImGui::PushFont(m_fontTitle);
        ImGui::TextColored({0.88f, 0.88f, 0.94f, 1.f}, "CreateToPlay");
        ImGui::PopFont();

        // Single game card
        const float cw     = 280.f;
        const float thumbH = cw * 0.58f;
        const float infoH  = 110.f;  // name + server input + play button
        const float ch     = thumbH + infoH;
        const float corner = 10.f;
        const float cardY  = 30.f + ImGui::GetTextLineHeight() + 22.f;

        ImGui::SetCursorPos({padX, cardY});
        ImVec2 tl = ImGui::GetCursorScreenPos();
        ImVec2 br = {tl.x + cw, tl.y + ch};

        bool cardHov = ImGui::IsMouseHoveringRect(tl, br);

        cdl->AddRectFilled(tl, br, IM_COL32(13, 13, 20, 255), corner);
        cdl->AddRect      (tl, br,
            cardHov ? IM_COL32(60, 60, 95, 220) : IM_COL32(40, 40, 65, 180),
            corner, 0, 1.f);

        // Thumbnail (subtle dim on hover to hint interactivity)
        ImVec2 thumbBR = {tl.x + cw, tl.y + thumbH};
        cdl->AddRectFilled(tl, thumbBR,
            IM_COL32(46, 107, 183, 255), corner, ImDrawFlags_RoundCornersTop);
        if (cardHov)
            cdl->AddRectFilled(tl, thumbBR,
                IM_COL32(0, 0, 0, 60), corner, ImDrawFlags_RoundCornersTop);

        // Game name (always visible)
        ImGui::SetCursorPos({padX + 12.f, cardY + thumbH + 12.f});
        ImGui::TextColored({1.f, 1.f, 1.f, 1.f}, "Test");

        // Server address input (always visible under name)
        {
            float inputW = cw - 24.f;
            ImGui::SetCursorPos({padX + 12.f, cardY + thumbH + 32.f});
            ImGui::SetNextItemWidth(inputW);
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.10f, 0.10f, 0.16f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.14f, 0.14f, 0.22f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.16f, 0.16f, 0.26f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.80f, 0.80f, 0.90f, 1.f));
            ImGui::InputTextWithHint("##srv", "Server IP:port  (blank = offline)",
                m_serverAddr, sizeof(m_serverAddr));
            ImGui::PopStyleColor(4);
        }

        // Play button — appears below server input on hover
        if (cardHov) {
            float pbW   = cw - 24.f;
            float pbH   = 34.f;
            float pbX   = tl.x + 12.f;
            float pbY   = tl.y + thumbH + infoH - pbH - 12.f;
            ImVec2 pbTL = {pbX, pbY};
            ImVec2 pbBR = {pbX + pbW, pbY + pbH};

            bool btnHov = ImGui::IsMouseHoveringRect(pbTL, pbBR);
            cdl->AddRectFilled(pbTL, pbBR,
                btnHov ? IM_COL32(40, 110, 255, 240) : IM_COL32(28, 92, 240, 220), 7.f);

            // Play triangle centered in button
            float cx = pbX + pbW * 0.5f;
            float cy = pbY + pbH * 0.5f;
            float ts = 8.f;
            cdl->AddTriangleFilled(
                {cx - ts * 0.55f, cy - ts * 0.85f},
                {cx - ts * 0.55f, cy + ts * 0.85f},
                {cx + ts * 1.0f,  cy},
                IM_COL32(255, 255, 255, 255));

            if (btnHov && ImGui::IsMouseClicked(0))
                m_gameStarted = true;
        }

    } else if (m_sidebarTab == 1) {
        // ── Avatar tab — coming soon ──────────────────────────────────────────
        float tw = ImGui::CalcTextSize("Avatar").x;
        ImGui::SetCursorPos({(W - sideW - tw) * 0.5f, H * 0.44f});
        ImGui::TextColored({0.35f, 0.35f, 0.45f, 1.f}, "Avatar");
        const char* sub = "Coming soon";
        float sw = ImGui::CalcTextSize(sub).x;
        ImGui::SetCursorPos({(W - sideW - sw) * 0.5f, H * 0.44f + 24.f});
        ImGui::TextColored({0.28f, 0.28f, 0.36f, 1.f}, "%s", sub);

    } else {
        // ── More / Options tab ────────────────────────────────────────────────
        ImGui::SetCursorPos({padX, 30.f});
        ImGui::PushFont(m_fontTitle);
        ImGui::TextColored({0.88f, 0.88f, 0.94f, 1.f}, "More");
        ImGui::PopFont();

        struct OptionCard { const char* label; };
        static const OptionCard kOpts[] = { {"Settings"}, {"Profile"} };

        const float csz    = 128.f;   // square card size
        const float corner = 14.f;
        const float gapX   = 18.f;
        const float cardY  = 30.f + ImGui::GetTextLineHeight() + 22.f;

        for (int i = 0; i < 2; ++i) {
            float lx = padX + i * (csz + gapX);
            ImGui::SetCursorPos({lx, cardY});
            ImVec2 tl = ImGui::GetCursorScreenPos();
            ImVec2 br = {tl.x + csz, tl.y + csz};

            bool hov = ImGui::IsMouseHoveringRect(tl, br);

            // Card background
            cdl->AddRectFilled(tl, br,
                hov ? IM_COL32(20, 20, 30, 255) : IM_COL32(13, 13, 20, 255), corner);
            cdl->AddRect(tl, br,
                hov ? IM_COL32(70, 70, 110, 220) : IM_COL32(40, 40, 65, 180),
                corner, 0, 1.2f);

            float cx  = tl.x + csz * 0.5f;
            float icy = tl.y + csz * 0.34f;  // icon centre Y
            ImU32 ic  = hov ? IM_COL32(100, 160, 255, 255) : IM_COL32(160, 160, 180, 255);

            if (i == 0) {
                // Settings icon — three slider lines
                for (int s = 0; s < 3; ++s) {
                    float ly2 = icy - 10.f + s * 10.f;
                    cdl->AddRectFilled({cx - 18.f, ly2 - 1.5f}, {cx + 18.f, ly2 + 1.5f}, ic, 2.f);
                    float kx = cx - 10.f + s * 10.f;
                    cdl->AddCircleFilled({kx, ly2}, 4.f, IM_COL32(13, 13, 20, 255));
                    cdl->AddCircle      ({kx, ly2}, 4.f, ic, 12, 1.5f);
                }
            } else {
                // Profile icon — head + body
                cdl->AddCircleFilled({cx, icy - 8.f}, 9.f, ic);
                cdl->AddRectFilled({cx - 11.f, icy + 3.f}, {cx + 11.f, icy + 18.f}, ic, 5.f);
            }

            // Label centred at bottom of card
            float lw2 = ImGui::CalcTextSize(kOpts[i].label).x;
            ImGui::SetCursorPos({lx + (csz - lw2) * 0.5f, cardY + csz * 0.72f});
            ImGui::TextColored(
                hov ? ImVec4(1.f, 1.f, 1.f, 1.f) : ImVec4(0.65f, 0.65f, 0.75f, 1.f),
                "%s", kOpts[i].label);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
