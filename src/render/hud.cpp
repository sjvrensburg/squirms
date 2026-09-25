#include "hud.h"
#include "../net/input.h"
#include "sprites.h"
#include "text.h"
#include "../game/game.h"
#include "../terrain/terrain.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

const Color PANEL{16, 14, 24, 215};
const Color INK{12, 8, 16, 255};

Color withAlpha(Color c, float a) {
    c.a = (unsigned char)std::clamp(a * 255.0f, 0.0f, 255.0f);
    return c;
}

void panel(Rectangle r, Color border, float roundness = 0.25f) {
    DrawRectangleRounded({r.x + 3, r.y + 4, r.width, r.height}, roundness, 6, Color{0, 0, 0, 90});
    DrawRectangleRounded(r, roundness, 6, PANEL);
    DrawRectangleRoundedLinesEx(r, roundness, 6, 2.0f, border);
}

// Weapon panel layout.
constexpr int PANEL_COLS = 4;
constexpr float CELL = 60;
Rectangle panelRect() {
    int rows = (WEAPON_COUNT + PANEL_COLS - 1) / PANEL_COLS;
    float w = PANEL_COLS * CELL + 28, h = rows * CELL + 120;
    return {GetScreenWidth() - w - 18, GetScreenHeight() / 2.0f - h / 2 - 30, w, h};
}
Rectangle cellRect(int i) {
    Rectangle p = panelRect();
    return {p.x + 14 + (i % PANEL_COLS) * CELL, p.y + 44 + (i / PANEL_COLS) * CELL, CELL - 6, CELL - 6};
}

// Draw `text` word-wrapped to `width`; returns the height used.
float drawWrapped(const char* text, Vector2 pos, float width, float size, Color color) {
    std::string line, word;
    float y = pos.y;
    auto flush = [&]() {
        if (!line.empty()) Text::draw(line.c_str(), {pos.x, y}, size, color, false);
        y += size + 3;
        line.clear();
    };
    for (const char* c = text;; c++) {
        if (*c == ' ' || *c == 0) {
            std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && Text::measure(trial.c_str(), size, false).x > width) {
                flush();
                line = word;
            } else {
                line = trial;
            }
            word.clear();
            if (*c == 0) break;
        } else {
            word.push_back(*c);
        }
    }
    flush();
    return y - pos.y;
}

void drawTimer(const Game& g) {
    int sh = GetScreenHeight();
    Color team = g.teams[g.currentTeam].color;
    Rectangle r{18, sh - 78.0f, 76, 60};
    panel(r, team);
    char buf[16];
    Color c = WHITE;
    if (g.phase == Phase::Retreat) {
        snprintf(buf, sizeof buf, "%d", (int)std::ceil(std::max(0.0f, g.retreatTime)));
        c = Color{255, 220, 80, 255};
    } else if (g.phase == Phase::Aiming || g.phase == Phase::Intro) {
        snprintf(buf, sizeof buf, "%d", (int)std::ceil(std::max(0.0f, g.turnTime)));
        if (g.turnTime <= 5.0f && fmodf(g.turnTime, 1.0f) > 0.5f) c = Color{255, 80, 70, 255};
    } else {
        snprintf(buf, sizeof buf, "--");
        c = Color{180, 180, 190, 255};
    }
    Text::drawCentered(buf, {r.x + r.width / 2, r.y + r.height / 2 - 2}, 36, c, INK, 2);
    int mins = (int)(g.matchTime / 60), secs = (int)fmodf(g.matchTime, 60);
    char clock[16];
    snprintf(clock, sizeof clock, "%02d:%02d", mins, secs);
    Text::drawCentered(clock, {r.x + r.width / 2, r.y - 12}, 13, Color{220, 220, 230, 255}, INK, 1, false);
}

void drawWind(const Game& g) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    Rectangle r{sw - 196.0f, sh - 50.0f, 178, 32};
    panel(r, Color{255, 255, 255, 60}, 0.4f);
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    float half = r.width / 2 - 10;
    DrawLineEx({cx, r.y + 6}, {cx, r.y + r.height - 6}, 2, Color{255, 255, 255, 120});
    float w = g.wind;
    if (fabsf(w) > 0.01f) {
        float len = half * fabsf(w);
        float x0 = w > 0 ? cx : cx - len;
        Color col = fabsf(w) < 0.4f ? Color{90, 200, 255, 255} : (fabsf(w) < 0.75f ? Color{255, 210, 60, 255} : Color{255, 80, 60, 255});
        BeginScissorMode((int)x0, (int)r.y + 7, (int)len + 1, (int)r.height - 14);
        DrawRectangle((int)x0, (int)r.y + 8, (int)len, (int)r.height - 16, withAlpha(col, 0.35f));
        // Chevrons marching in the wind direction.
        float spacing = 12, off = fmodf(g.matchTime * 40 * fabsf(w) + 100, spacing);
        int d = w > 0 ? 1 : -1;
        for (float x = (d > 0 ? x0 - spacing + off : x0 + len + spacing - off);
             d > 0 ? x < x0 + len + spacing : x > x0 - spacing; x += d * spacing) {
            DrawLineEx({x - d * 4, cy - 6}, {x + d * 2, cy}, 3, col);
            DrawLineEx({x + d * 2, cy}, {x - d * 4, cy + 6}, 3, col);
        }
        EndScissorMode();
    }
    Text::drawCentered("WIND", {cx, r.y - 11}, 13, Color{220, 220, 230, 255}, INK, 1, false);
}

void drawTeamBars(const Game& g) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    float maxHp = 1;
    for (auto& t : g.teams) maxHp = std::max(maxHp, (float)t.worms.size() * g.cfg.startHp);
    float rowH = 17, barW = std::min(260.0f, sw * 0.22f);
    float nameW = 0;
    for (auto& t : g.teams) nameW = std::max(nameW, Text::measure(t.name.c_str(), 14).x);
    float totalW = nameW + 10 + barW;
    float x0 = sw / 2.0f - totalW / 2, y0 = sh - 18 - rowH * g.teams.size();
    for (size_t i = 0; i < g.teams.size(); i++) {
        const Team& t = g.teams[i];
        float y = y0 + i * rowH;
        bool cur = (int)i == g.currentTeam && g.phase != Phase::GameOver;
        Color name = t.hasActiveWorms() ? t.color : Color{120, 120, 120, 255};
        Vector2 nm = Text::measure(t.name.c_str(), 14);
        Text::drawOutlined(t.name.c_str(), {x0 + nameW - nm.x, y}, 14, name, INK, 1.5f);
        float frac = std::clamp(t.shownHp() / maxHp, 0.0f, 1.0f);
        Rectangle bar{x0 + nameW + 10, y + 2, std::max(2.0f, barW * frac), rowH - 4};
        DrawRectangleRec({bar.x - 1, bar.y - 1, bar.width + 2, bar.height + 2}, INK);
        DrawRectangleRec(bar, t.color);
        DrawRectangleRec({bar.x, bar.y, bar.width, 3}, withAlpha(WHITE, 0.35f));
        if (cur) {
            float b = 0.5f + 0.5f * sinf(g.matchTime * 6);
            DrawTriangle({x0 - 14, y + 3}, {x0 - 14, y + rowH - 3}, {x0 - 5, y + rowH / 2}, withAlpha(t.color, 0.5f + 0.5f * b));
        }
    }
}

void drawWeaponCard(const Game& g) {
    const WeaponInfo& info = weaponInfo(g.weapon);
    const Team& team = g.teams[g.currentTeam];
    Rectangle r{18, 16, 250, 56};
    panel(r, withAlpha(team.color, 0.8f));
    DrawRectangleRounded({r.x + 8, r.y + 8, 40, 40}, 0.25f, 4, Color{255, 255, 255, 30});
    Sprites::drawWeaponIcon(g.weapon, {r.x + 28, r.y + 28}, 30, g.matchTime);
    Text::drawOutlined(info.name, {r.x + 58, r.y + 8}, 17, WHITE, INK, 1.5f);
    int ammo = team.ammo[(int)g.weapon];
    char line[64];
    if (info.fused) snprintf(line, sizeof line, "%s   Fuse %ds (1-5)", ammo < 0 ? "Ammo: inf" : TextFormat("Ammo: %d", ammo), g.fuse);
    else snprintf(line, sizeof line, "%s", ammo < 0 ? "Ammo: inf" : TextFormat("Ammo: %d", ammo));
    Text::draw(line, {r.x + 58, r.y + 32}, 13, Color{210, 210, 220, 255}, false);
    Text::drawOutlined("Tab / right-click: weapons    H: help", {r.x + 2, r.y + r.height + 6}, 12,
                       Color{230, 230, 240, 220}, Color{0, 0, 0, 160}, 1, false);
}

void drawMessage(const Game& g) {
    if (g.messageTime <= 0 || g.message.empty()) return;
    float a = std::min(1.0f, g.messageTime / 0.4f);
    int sw = GetScreenWidth();
    float size = g.phase == Phase::GameOver ? 44 : 22;
    Vector2 m = Text::measure(g.message.c_str(), size);
    float y = g.phase == Phase::GameOver ? GetScreenHeight() * 0.28f : 22;
    Rectangle r{sw / 2.0f - m.x / 2 - 18, y, m.x + 36, m.y + 12};
    DrawRectangleRounded(r, 0.4f, 6, withAlpha(PANEL, 0.85f * a));
    DrawRectangleRoundedLinesEx(r, 0.4f, 6, 2, withAlpha(g.messageColor, 0.8f * a));
    Text::drawOutlined(g.message.c_str(), {r.x + 18, r.y + 6}, size, withAlpha(g.messageColor, a),
                       withAlpha(INK, a), 2);
}

void drawWeaponPanel(const Game& g) {
    Rectangle p = panelRect();
    const Team& team = g.teams[g.currentTeam];
    panel(p, withAlpha(team.color, 0.9f), 0.08f);
    Text::drawOutlined("WEAPONS", {p.x + 14, p.y + 12}, 20, WHITE, INK, 1.5f);
    bool canPick = g.phase == Phase::Aiming && !g.fired;
    int hover = weaponPanelHit(Input::mousePosition());
    for (int i = 0; i < WEAPON_COUNT; i++) {
        Rectangle c = cellRect(i);
        int ammo = team.ammo[i];
        bool sel = (int)g.weapon == i;
        bool hov = hover == i;
        Color bg = sel ? withAlpha(team.color, 0.45f) : (hov ? Color{255, 255, 255, 50} : Color{255, 255, 255, 18});
        DrawRectangleRounded(c, 0.2f, 4, bg);
        if (sel) DrawRectangleRoundedLinesEx(c, 0.2f, 4, 2, team.color);
        Sprites::drawWeaponIcon((WeaponId)i, {c.x + c.width / 2, c.y + c.height / 2 - 2}, hov ? 34 : 30, g.matchTime);
        if (ammo == 0) {
            DrawRectangleRounded(c, 0.2f, 4, Color{10, 10, 14, 170});
        } else if (ammo > 0) {
            char a[8];
            snprintf(a, sizeof a, "%d", ammo);
            Text::drawOutlined(a, {c.x + c.width - 11, c.y + c.height - 17}, 13, WHITE, INK, 1.2f);
        }
    }
    int show = hover >= 0 ? hover : (int)g.weapon;
    const WeaponInfo& info = weaponInfo((WeaponId)show);
    float by = p.y + p.height - 70;
    Text::drawOutlined(info.name, {p.x + 14, by}, 17, WHITE, INK, 1.5f);
    float used = drawWrapped(info.blurb, {p.x + 14, by + 23}, p.width - 28, 12, Color{210, 210, 220, 255});
    if (!canPick) Text::draw("You can pick again next turn", {p.x + 14, by + 23 + used}, 12, Color{255, 180, 120, 255}, false);
}

void drawHelp() {
    const char* lines[] = {
        "Left / Right (A / D)", "Walk",
        "Up / Down (W / S)", "Aim",
        "Space (hold)", "Charge and fire",
        "Enter", "Jump",
        "Backspace", "Backflip",
        "1 - 5", "Set grenade fuse",
        "Ninja rope", "Space shoots / lets go, arrows swing + climb",
        "Tab / right-click", "Weapon panel",
        "Click", "Pick a target (air strike, teleport)",
        "Drag / wheel", "Look around / zoom",
        "Esc", "Quit to menu",
    };
    int n = (int)(sizeof(lines) / sizeof(lines[0])) / 2;
    float w = 600, h = 64 + n * 24.0f;
    Rectangle r{GetScreenWidth() / 2.0f - w / 2, GetScreenHeight() / 2.0f - h / 2, w, h};
    panel(r, Color{255, 255, 255, 90}, 0.08f);
    Text::drawOutlined("CONTROLS", {r.x + 20, r.y + 14}, 22, WHITE, INK, 1.5f);
    for (int i = 0; i < n; i++) {
        float y = r.y + 50 + i * 24;
        Text::draw(lines[i * 2], {r.x + 20, y}, 15, Color{255, 220, 110, 255});
        Text::draw(lines[i * 2 + 1], {r.x + 220, y}, 15, Color{230, 230, 240, 255}, false);
    }
}

} // namespace

int weaponPanelHit(Vector2 m) {
    if (!CheckCollisionPointRec(m, panelRect())) return -1;
    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (CheckCollisionPointRec(m, cellRect(i))) return i;
    }
    return -1;
}

void renderHUD(Game& g) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    if (g.phase != Phase::GameOver) {
        drawTimer(g);
        drawWeaponCard(g);
    }
    drawWind(g);
    drawTeamBars(g);
    drawMessage(g);

    // Context hints.
    const char* hint = nullptr;
    bool sheepOut = false;
    for (auto& p : g.projectiles) if (p.type == Entities::ProjectileType::Sheep) sheepOut = true;
    if (sheepOut && g.phase == Phase::Aiming) hint = "SPACE: detonate the sheep!";
    else if (g.rope.state == NinjaRope::State::Attached)
        hint = "Left/Right: swing   Up/Down: climb   Enter: let go";
    else if (g.weapon == WeaponId::NinjaRope && g.phase == Phase::Aiming && !g.fired)
        hint = "Aim and press SPACE to shoot the rope";
    else if (g.isTargeting()) hint = g.weapon == WeaponId::AirStrike ? "Click a target for the air strike" : "Click where to teleport";
    else if (g.phase == Phase::Retreat) hint = "Run away!";
    if (hint) Text::drawCentered(hint, {sw / 2.0f, 86}, 18, Color{255, 240, 180, 255}, INK, 2);

    if (g.panelOpen) drawWeaponPanel(g);
    if (g.helpOpen) drawHelp();

    if (g.phase == Phase::GameOver && g.phaseTime > 1.5f) {
        float a = 0.5f + 0.5f * sinf(g.matchTime * 4);
        Text::drawCentered("Press Enter to return to the menu", {sw / 2.0f, sh * 0.28f + 90}, 20,
                           withAlpha(WHITE, 0.6f + 0.4f * a), INK, 2);
    }

    if (g.confirmQuit) {
        DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 120});
        Rectangle r{sw / 2.0f - 200, sh / 2.0f - 60, 400, 120};
        panel(r, Color{255, 255, 255, 90});
        Text::drawCentered("Quit to menu?", {sw / 2.0f, r.y + 38}, 26, WHITE, INK, 2);
        Text::drawCentered("Y / Enter: quit     N / Esc: keep playing", {sw / 2.0f, r.y + 82}, 14,
                           Color{220, 220, 230, 255}, INK, 1, false);
    }

    if (g.debug) {
        char dbg[160];
        snprintf(dbg, sizeof dbg, "FPS: %d | upd %.1f ms, draw %.1f ms | Static: %d | Dynamic: %d | Shells: %d | Props: %d",
                 GetFPS(), g.perfUpdateMs, g.perfDrawMs, g.terrain->getStaticCount(), g.terrain->getDynamicCount(),
                 (int)g.projectiles.size(), (int)g.props.size());
        Text::drawOutlined(dbg, {18, sh - 110.0f}, 14, WHITE, INK, 1, false);
    }
}
