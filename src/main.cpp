#include "net/input.h"
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "audio/sfx.h"
#include "game/game.h"
#include "render/sprites.h"
#include "render/text.h"
#include "render/theme.h"
#include "terrain/generate.h"
#include "terrain/grid.h"

// ---- Front end: team setup + landscape preview ----

namespace {

Game* game = nullptr;
bool onlineGuest = false;
bool onlineStart = false;
bool onlineLeave = false;

struct Menu {
    int teams = 2;
    bool vsCpu = true;      // team 1 human vs computer teams, or all hot-seat
    int worms = 4;
    int energy = 100;       // starting health per worm
    int theme = -1;         // -1 = random
    std::string seedText;   // blank = random
    int row = 0;            // selected option row
    int previewSeed = -1;
    int previewTheme = -1;
    int rolledSeed = 0;     // the seed a blank field will actually use
    int rolledTheme = 0;
    Texture2D preview{};
    bool hasPreview = false;
} menu;

constexpr int MENU_ROWS = 7;   // teams, opponent, worms, energy, theme, seed, start

// Menu geometry, scaled so everything fits any window size.
struct MenuLayout {
    float titleSize, titleY;
    Rectangle preview;
    float rowsTop, rowH;
    bool icons;
};

MenuLayout menuLayout() {
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    MenuLayout L;
    L.titleSize = std::min({120.0f, sw / 7.0f, sh * 0.13f});
    L.titleY = sh * 0.04f;
    float subtitleBottom = L.titleY + L.titleSize + 34;
    L.rowH = std::clamp(sh * 0.058f, 34.0f, 46.0f);
    L.icons = sh > 560;
    float bottomReserve = L.icons ? 70 : 16;
    float rowsHeight = MENU_ROWS * L.rowH;
    float previewH = sh - subtitleBottom - rowsHeight - bottomReserve - 24;
    previewH = std::clamp(previewH, 60.0f, 260.0f);
    float previewW = std::min(sw * 0.7f, previewH * Terrain::COLS / Terrain::ROWS);
    previewH = previewW * Terrain::ROWS / Terrain::COLS;
    L.preview = {sw / 2 - previewW / 2, subtitleBottom + 6, previewW, previewH};
    L.rowsTop = L.preview.y + previewH + 18;
    return L;
}

int menuSeed() { return menu.seedText.empty() ? menu.rolledSeed : atoi(menu.seedText.c_str()); }
int menuTheme() { return menu.theme < 0 ? menu.rolledTheme : menu.theme; }

void rerollMenu() {
    menu.rolledSeed = GetRandomValue(1, 999999);
    menu.rolledTheme = GetRandomValue(0, THEME_COUNT - 1);
}

// A small picture of the landscape a seed produces, painted in the theme's
// colours (one pixel per terrain cell).
void refreshPreview() {
    int seed = menuSeed(), th = menuTheme();
    if (menu.hasPreview && seed == menu.previewSeed && th == menu.previewTheme) return;
    const Theme& theme = getTheme(th);
    auto gen = Terrain::generateTerrain(seed);
    Image img = GenImageColor(Terrain::COLS, Terrain::ROWS, BLANK);
    for (int y = 0; y < Terrain::ROWS; y++) {
        float t = (float)y / Terrain::ROWS;
        Color sky{(unsigned char)(theme.skyTop.r + (theme.skyBottom.r - theme.skyTop.r) * t),
                  (unsigned char)(theme.skyTop.g + (theme.skyBottom.g - theme.skyTop.g) * t),
                  (unsigned char)(theme.skyTop.b + (theme.skyBottom.b - theme.skyTop.b) * t), 255};
        for (int x = 0; x < Terrain::COLS; x++) {
            Color c = sky;
            if (gen.solid[y][x]) {
                bool top = y == 0 || !gen.solid[y - 1][x];
                c = top ? theme.crustB : (gen.material[y][x] == Terrain::Material::Rock ? theme.rockA : theme.soilA);
            }
            if (y * Terrain::CELL > Terrain::WATER_Y) c = gen.solid[y][x] ? theme.waterDeep : theme.waterTop;
            ImageDrawPixel(&img, x, y, c);
        }
    }
    if (menu.hasPreview) UnloadTexture(menu.preview);
    menu.preview = LoadTextureFromImage(img);
    UnloadImage(img);
    menu.hasPreview = true;
    menu.previewSeed = seed;
    menu.previewTheme = th;
}

void startGame() {
    GameConfig cfg;
    cfg.teams = Input::online ? 2 : menu.teams;
    cfg.wormsPerTeam = menu.worms;
    cfg.startHp = menu.energy;
    cfg.vsCpu = Input::online ? false : menu.vsCpu;
    cfg.seed = menuSeed();
    cfg.theme = menuTheme();
    game = new Game(cfg);
    Audio::playSound("turn");
}

void updateMenu() {
    auto change = [](int dir) {
        switch (menu.row) {
            case 0: menu.teams = std::clamp(menu.teams + dir, 2, 4); break;
            case 1: menu.vsCpu = !menu.vsCpu; break;
            case 2: menu.worms = std::clamp(menu.worms + dir, 1, 8); break;
            case 3: menu.energy = std::clamp(menu.energy + dir * 50, 50, 200); break;
            case 4: menu.theme = ((menu.theme + 1 + dir) % (THEME_COUNT + 1) + THEME_COUNT + 1) % (THEME_COUNT + 1) - 1; break;
            case 5: rerollMenu(); menu.seedText.clear(); break;
            default: return;
        }
        Audio::playSound("select");
    };
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) { menu.row = (menu.row + MENU_ROWS - 1) % MENU_ROWS; Audio::playSound("select", 0.5f); }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) { menu.row = (menu.row + 1) % MENU_ROWS; Audio::playSound("select", 0.5f); }
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) change(-1);
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) change(1);

    for (int ch = GetCharPressed(); ch != 0; ch = GetCharPressed()) {
        if (ch >= '0' && ch <= '9' && menu.seedText.size() < 6) {
            menu.seedText.push_back((char)ch);
            menu.row = 5;
        }
        if (ch == 'r' || ch == 'R') { rerollMenu(); menu.seedText.clear(); Audio::playSound("select"); }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !menu.seedText.empty()) menu.seedText.pop_back();

    // Mouse: click a row's arrows, or the start button.
    Vector2 m = GetMousePosition();
    float cx = GetScreenWidth() / 2.0f;
    MenuLayout L = menuLayout();
    for (int r = 0; r < MENU_ROWS; r++) {
        Rectangle rr{cx - 220, L.rowsTop + r * L.rowH, 440, L.rowH - 6};
        if (CheckCollisionPointRec(m, rr)) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                menu.row = r;
                if (r == MENU_ROWS - 1) { startGame(); return; }
                change(m.x < cx ? -1 : 1);
            }
        }
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) startGame();
}

void drawMenu() {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    const Theme& th = getTheme(menuTheme());
    float t = (float)GetTime();
    DrawRectangleGradientV(0, 0, sw, sh, th.skyTop, th.skyBottom);

    MenuLayout L = menuLayout();

    // Title: chunky pink letters bouncing in a wave.
    const char* title = "SQUIRMS";
    float size = L.titleSize;
    float total = 0;
    for (const char* c = title; *c; c++) {
        char s[2] = {*c, 0};
        total += Text::measure(s, size).x + size * 0.04f;
    }
    float x = sw / 2.0f - total / 2;
    float ty = L.titleY;
    for (int i = 0; title[i]; i++) {
        char s[2] = {title[i], 0};
        float b = sinf(t * 3.2f - i * 0.55f) * size * 0.07f;
        Text::drawOutlined(s, {x + 4, ty + b + 6}, size, Color{0, 0, 0, 70}, Color{0, 0, 0, 70}, 4);
        Text::drawOutlined(s, {x, ty + b}, size, Color{252, 164, 178, 255}, Color{110, 30, 60, 255}, 5);
        x += Text::measure(s, size).x + size * 0.04f;
    }
    Text::drawCentered("Destructible-terrain artillery mayhem", {sw / 2.0f, ty + size + 14}, 18, WHITE,
                       Color{20, 10, 20, 255}, 2);

    // Landscape preview.
    refreshPreview();
    Rectangle pr = L.preview;
    DrawRectangleRounded({pr.x - 6, pr.y - 6, pr.width + 12, pr.height + 12}, 0.06f, 6, Color{16, 14, 24, 220});
    DrawTexturePro(menu.preview, {0, 0, (float)Terrain::COLS, (float)Terrain::ROWS}, pr, {0, 0}, 0, WHITE);

    // Options.
    const char* themeName = menu.theme < 0 ? TextFormat("Random (%s)", getTheme(menuTheme()).name) : getTheme(menu.theme).name;
    std::string seed = menu.seedText.empty() ? TextFormat("%d  (R: reroll)", menu.rolledSeed) : menu.seedText + "_";
    const char* labels[MENU_ROWS] = {"Teams", "Opponents", "Worms per team", "Worm energy", "Landscape", "Map seed", ""};
    std::string values[MENU_ROWS] = {std::to_string(menu.teams), menu.vsCpu ? "Computer" : "Hot-seat",
                                      std::to_string(menu.worms), std::to_string(menu.energy), themeName, seed, ""};
    float cx = sw / 2.0f;
    float fs = std::min(20.0f, L.rowH * 0.5f);
    for (int r = 0; r < MENU_ROWS; r++) {
        bool sel = menu.row == r;
        Rectangle rr{cx - 220, L.rowsTop + r * L.rowH, 440, L.rowH - 6};
        float y = rr.y + rr.height / 2 - fs * 0.62f;
        if (r == MENU_ROWS - 1) {
            float pulse = sel ? 0.5f + 0.5f * sinf(t * 6) : 0;
            DrawRectangleRounded(rr, 0.5f, 8, Color{(unsigned char)(200 + 55 * pulse), 60, 70, 255});
            DrawRectangleRoundedLinesEx(rr, 0.5f, 8, 3, Color{60, 10, 20, 255});
            Text::drawCentered("START!", {cx, rr.y + rr.height / 2}, fs + 4, WHITE, Color{60, 10, 20, 255}, 2);
            continue;
        }
        DrawRectangleRounded(rr, 0.4f, 8, sel ? Color{255, 255, 255, 60} : Color{16, 14, 24, 150});
        Text::drawOutlined(labels[r], {rr.x + 16, y}, fs, WHITE, Color{20, 10, 20, 255}, 1.5f);
        Vector2 vm = Text::measure(values[r].c_str(), fs);
        Text::drawOutlined(values[r].c_str(), {rr.x + rr.width - 40 - vm.x, y}, fs, Color{255, 226, 90, 255},
                           Color{20, 10, 20, 255}, 1.5f);
        if (sel) {
            Text::drawOutlined("<", {rr.x + rr.width - 40 - vm.x - 22, y}, fs, WHITE, Color{20, 10, 20, 255}, 1.5f);
            Text::drawOutlined(">", {rr.x + rr.width - 28, y}, fs, WHITE, Color{20, 10, 20, 255}, 1.5f);
        }
    }

    // A parade of weapon icons along the bottom.
    for (int i = 0; i < WEAPON_COUNT && L.icons; i++) {
        float ix = sw / 2.0f + (i - (WEAPON_COUNT - 1) / 2.0f) * 52;
        float iy = sh - 40 + sinf(t * 2.5f + i * 0.7f) * 5;
        Sprites::drawWeaponIcon((WeaponId)i, {ix, iy}, 30, t);
    }
    Text::drawOutlined("Arrows: choose   Enter: start   Type digits: seed", {16, 12}, 14,
                       Color{240, 240, 250, 230}, Color{0, 0, 0, 160}, 1, false);
}

// ---- Main loop ----

// Fixed-timestep accumulator: requestAnimationFrame's rate is neither
// guaranteed to be 60Hz (high-refresh-rate displays, unthrottled headless
// browsers) nor perfectly steady, but world->step() and the gameplay code
// assume a fixed 1/60s tick. Decoupling simulation from render rate here
// keeps physics/camera/timers correct regardless of how often frame() runs.
constexpr double TICK = 1.0 / 60.0;
constexpr int MAX_STEPS_PER_FRAME = 5;
double accumulator = 0.0;

// Keep the canvas matched to the browser window so the game fills the page
// at native resolution instead of being stretched.
void fitWindowToPage() {
#ifdef __EMSCRIPTEN__
    int w = EM_ASM_INT({ return window.innerWidth; });
    int h = EM_ASM_INT({ return window.innerHeight; });
    w = std::max(640, w);
    h = std::max(400, h);
    if (w != GetScreenWidth() || h != GetScreenHeight()) SetWindowSize(w, h);
#endif
}

void frame() {
    fitWindowToPage();
    if (onlineLeave) {
        delete game;
        game = nullptr;
        Input::online = Input::connected = onlineGuest = onlineStart = onlineLeave = false;
        Input::reset();
        accumulator = 0;
    }
    if (onlineStart) {
        onlineStart = false;
        delete game;
        game = nullptr;
        startGame();
        accumulator = 0;
    }
    if (onlineGuest) return; // display the host's video; sound events play locally
    Audio::update(std::min(GetFrameTime(), 0.1f), game != nullptr, game ? game->wind : 0,
                  game ? game->cfg.theme : menuTheme());

    if (!game) {
        if (!Input::blocked) updateMenu();
        BeginDrawing();
        drawMenu();
        EndDrawing();
        return;
    }

    double dt = (double)GetFrameTime();
    if (dt > 0.25) dt = 0.25;  // clamp to avoid a spiral of death after a stall
    accumulator += dt;
    int steps = 0;
    double t0 = GetTime();
    Input::beginFrame(game->currentTeam, game->turnNumber);
    bool paused = Input::online && !Input::connected;
    if (!paused) game->frameInput();
    else accumulator = 0;
    while (!paused && accumulator >= TICK && steps < MAX_STEPS_PER_FRAME) {
        Input::setTurn(game->currentTeam, game->turnNumber);
        game->update((float)TICK);
        accumulator -= TICK;
        steps++;
    }
    if (steps == MAX_STEPS_PER_FRAME) accumulator = 0;
    double t1 = GetTime();

    BeginDrawing();
    ClearBackground(BLACK);
    game->draw();
    if (paused) {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{10, 16, 30, 170});
        Text::drawCentered("Connection interrupted - match paused", {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f}, 26, WHITE, BLACK);
    }
    EndDrawing();
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (window.SquirmsOnline) window.SquirmsOnline.state($0, $1, $2, $3); },
           game->currentTeam, game->turnNumber, (int)game->phase, game->matchTime);
#endif
    double t2 = GetTime();
    // Smoothed timings for the F1 overlay.
    game->perfUpdateMs += ((float)(t1 - t0) * 1000.0f - game->perfUpdateMs) * 0.1f;
    game->perfDrawMs += ((float)(t2 - t1) * 1000.0f - game->perfDrawMs) * 0.1f;

    if (game->wantsExit()) {
        delete game;
        game = nullptr;
        accumulator = 0;
#ifdef __EMSCRIPTEN__
        EM_ASM({ if (window.SquirmsOnline) window.SquirmsOnline.matchEnded(); });
#endif
        rerollMenu();
    }
}

} // namespace

#ifdef __EMSCRIPTEN__
extern "C" {
EMSCRIPTEN_KEEPALIVE void squirms_ui(int blocked) { Input::blocked = blocked != 0; }
EMSCRIPTEN_KEEPALIVE int squirms_in_match() { return game != nullptr; }
EMSCRIPTEN_KEEPALIVE void squirms_online(int role, int connected) {
    if (role == 0) { onlineLeave = true; return; }
    onlineGuest = role == 2;
    if (role == 1 && !Input::online) onlineStart = true;
    Input::online = true;
    Input::connected = connected != 0;
}
EMSCRIPTEN_KEEPALIVE void squirms_input(int turn, unsigned keys, unsigned pressed,
        unsigned buttons, unsigned clicks, float x, float y, float wheel) {
    Input::receive(turn, keys, pressed, buttons, clicks, x, y, wheel);
}
EMSCRIPTEN_KEEPALIVE void squirms_audio(float effects, float music, int muted) {
    Audio::setMix(effects, music);
    Audio::setMasterVolume(muted ? 0.0f : 0.8f);
}
EMSCRIPTEN_KEEPALIVE void squirms_sound(int id, float volume, float pitch) {
    Audio::playRemote(id, volume, pitch);
}
}
#endif

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    int w = 1280, h = 720;
#ifdef __EMSCRIPTEN__
    w = std::max(640, EM_ASM_INT({ return window.innerWidth; }));
    h = std::max(400, EM_ASM_INT({ return window.innerHeight; }));
#endif
    InitWindow(w, h, "Squirms");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    Audio::init();
    Text::init();
    rerollMenu();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!WindowShouldClose()) frame();
    delete game;
    Text::unload();
    CloseWindow();
#endif
    return 0;
}
