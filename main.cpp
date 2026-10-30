// main.cpp - "horror don virus 1999": a joke rhythm game (raylib 5.x, C++17)
// Standalone: no song files, no skins, no permissions needed.
// Play until you tap Don-chan... then the "virus" appears (it's only a joke!).
// Keys: D F J K  |  Touch: tap the drum (center = red Don, rim = blue Ka)
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// ------------------------------------------------------------ config
static const int   W = 1000, H = 540;
static const float LANE_Y = 150, LANE_H = 120;
static const float JUDGE_X = 200, JUDGE_R = 38, NOTE_R = 26;
static const float SCROLL = 520, PERFECT_PX = 20;
static const Vector2 DRUM_C = {500, 400};
static const float DRUM_OUTER = 125, DRUM_INNER = 70;
static const Vector2 DONCHAN_C = {70, 455};
static const float DONCHAN_R = 40;
static const Rectangle BACK_BTN = {W / 2.0f - 120, H - 90.0f, 240, 56};
static const double BPM = 140.0;
// d = Don, k = Ka, . = rest (one char = one 8th note). The pattern repeats forever.
static const char* PATTERN = "d.d.k.d.d.d.k.k.d.d.k.d.dkdkd.k.dd.dk.dkdd.dk.k.d.k.d.k.dddddd..";

enum { K_DON = 0, K_KA = 1 };
enum { R_NONE = -1, R_PERFECT = 0, R_GOOD = 1, R_MISS = 2 };

static const Color BG = {23, 23, 31, 255};
static const Color C_KIND[2] = {{237, 69, 56, 255}, {64, 153, 242, 255}};
static const Color C_RES[3]  = {{255, 217, 51, 255}, {115, 230, 115, 255}, {166, 166, 179, 255}};
static const char* RES_TXT[3] = {"Perfect", "Good", "Miss"};

// ------------------------------------------------------------ state
struct Note   { double time; int kind; int result = R_NONE; };
struct Popup  { int res; float age; };
struct Effect { Color c; float age; };

static std::vector<Note>   notes;
static std::vector<Popup>  popups;
static std::vector<Effect> effects;
static double gTime = -2.0;
static int    gNext = 0, score = 0, combo = 0;
static float  pad[4] = {0, 0, 0, 0};               // kind*2 + side
static Sound  snd[2];
static bool   gVirus = false;
static float  gVirusT = 0;
static float  gScale = 1, gOx = 0, gOy = 0;
static std::vector<int> prevTouchIds;

// ------------------------------------------------------------ helpers
static void updateScale() {
    float w = (float)GetScreenWidth(), h = (float)GetScreenHeight();
    gScale = fminf(w / W, h / H);
    gOx = (w - W * gScale) / 2; gOy = (h - H * gScale) / 2;
}
static Vector2 toVirtual(Vector2 p) { return {(p.x - gOx) / gScale, (p.y - gOy) / gScale}; }

static void drawCentered(const char* t, float cx, float y, int size, Color c) {
    DrawText(t, (int)(cx - MeasureText(t, size) / 2.0f), (int)y, size, c);
}
static void drawButton(Rectangle r, const char* t, Color c, int size) {
    DrawRectangleRounded(r, 0.3f, 8, c);
    drawCentered(t, r.x + r.width / 2, r.y + r.height / 2 - size / 2.0f, size, WHITE);
}

static Sound makeBlip(float freq, float len) {
    const int rate = 44100, n = (int)(rate * len);
    short* d = (short*)MemAlloc(n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float env = powf(1.0f - (float)i / n, 2.0f);
        d[i] = (short)(sinf(2 * PI * freq * i / rate) * env * 0.5f * 32767);
    }
    Wave w = {(unsigned int)n, (unsigned int)rate, 16, 1, d};
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

static void resetGame() {
    notes.clear(); popups.clear(); effects.clear();
    gTime = -2.0; gNext = 0; score = 0; combo = 0;
}

// new finger touches (Android) or mouse clicks (desktop), in virtual coordinates
static std::vector<Vector2> newPresses() {
    std::vector<Vector2> out;
#if defined(PLATFORM_ANDROID)
    std::vector<int> ids;
    for (int i = 0; i < GetTouchPointCount(); i++) {
        int id = GetTouchPointId(i);
        ids.push_back(id);
        if (std::find(prevTouchIds.begin(), prevTouchIds.end(), id) == prevTouchIds.end())
            out.push_back(toVirtual(GetTouchPosition(i)));
    }
    prevTouchIds = ids;
#else
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) out.push_back(toVirtual(GetMousePosition()));
#endif
    return out;
}

// ------------------------------------------------------------ game logic
static float noteX(const Note& n) { return JUDGE_X + (float)((n.time - gTime) * SCROLL); }

static void spawnNotes() {
    const int len = (int)strlen(PATTERN);
    const double step = 60.0 / BPM / 2.0;
    while (1.0 + gNext * step < gTime + 3.5) {
        char ch = PATTERN[gNext % len];
        if (ch == 'd' || ch == 'k') notes.push_back(Note{1.0 + gNext * step, ch == 'd' ? K_DON : K_KA});
        gNext++;
    }
}

// only the earliest unhit note can be hit, and only if it collides with the judgment circle
static Note* targetNote() {
    for (Note& n : notes) {
        if (n.result != R_NONE) continue;
        if (CheckCollisionCircles({noteX(n), LANE_Y}, NOTE_R, {JUDGE_X, LANE_Y}, JUDGE_R)) return &n;
        return nullptr;
    }
    return nullptr;
}

static void resolve(Note& n, int res) {
    n.result = res;
    score += (res == R_PERFECT) ? 300 : (res == R_GOOD ? 100 : 0);
    if (res == R_MISS) combo = 0;
    else { combo++; effects.push_back({C_KIND[n.kind], 0}); }
    popups.push_back({res, 0});
}

static void press(int kind, int side) {
    pad[kind * 2 + side] = 0.12f;
    StopSound(snd[kind]); PlaySound(snd[kind]);
    Note* n = targetNote();
    if (!n) return;
    if (n->kind != kind) { resolve(*n, R_MISS); return; }
    resolve(*n, fabsf(noteX(*n) - JUDGE_X) <= PERFECT_PX ? R_PERFECT : R_GOOD);
}

static void drumPress(Vector2 v) {
    float dx = v.x - DRUM_C.x, dy = v.y - DRUM_C.y, d = sqrtf(dx * dx + dy * dy);
    if (d > DRUM_OUTER) return;
    press(d <= DRUM_INNER ? K_DON : K_KA, dx < 0 ? 0 : 1);
}

static void updateGame(float dt) {
    for (float& p : pad) p = fmaxf(0, p - dt);
    for (auto& e : effects) e.age += dt;
    for (auto& p : popups) p.age += dt;
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Effect& e) { return e.age > 0.3f; }), effects.end());
    popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup& p) { return p.age > 0.6f; }), popups.end());

    gTime += dt;
    spawnNotes();
    for (Note& n : notes)
        if (n.result == R_NONE && noteX(n) < JUDGE_X - JUDGE_R - NOTE_R) resolve(n, R_MISS);
    notes.erase(std::remove_if(notes.begin(), notes.end(), [](const Note& n) { return noteX(n) < -60; }), notes.end());
}

// ------------------------------------------------------------ drawing
static void drawDonChan(Vector2 c, float r, bool evil) {
    DrawCircleV(c, r + 4, WHITE);
    DrawCircleV(c, r, evil ? Color{140, 10, 10, 255} : Color{237, 69, 56, 255});
    float ex = r * 0.38f, ey = r * 0.15f, er = r * 0.2f;
    DrawCircleV({c.x - ex, c.y - ey}, er, WHITE);
    DrawCircleV({c.x + ex, c.y - ey}, er, WHITE);
    DrawCircleV({c.x - ex, c.y - ey}, er * 0.5f, BLACK);
    DrawCircleV({c.x + ex, c.y - ey}, er * 0.5f, BLACK);
    if (evil) {                                   // angry eyebrows + zigzag mouth
        DrawLineEx({c.x - ex - er, c.y - ey - er * 1.3f}, {c.x - ex + er, c.y - ey - er * 0.5f}, 3, BLACK);
        DrawLineEx({c.x + ex + er, c.y - ey - er * 1.3f}, {c.x + ex - er, c.y - ey - er * 0.5f}, 3, BLACK);
        Vector2 prev = {c.x - r * 0.5f, c.y + r * 0.4f};
        for (int i = 1; i <= 6; i++) {
            Vector2 nx = {c.x - r * 0.5f + i * (r / 6.0f), c.y + r * (i % 2 ? 0.25f : 0.4f)};
            DrawLineEx(prev, nx, 3, BLACK);
            prev = nx;
        }
    } else {                                      // happy smile + cheeks
        DrawRing({c.x, c.y + r * 0.1f}, r * 0.35f, r * 0.35f + 3, 20, 160, 20, BLACK);
        DrawCircleV({c.x - r * 0.62f, c.y + r * 0.2f}, r * 0.14f, Color{255, 150, 150, 200});
        DrawCircleV({c.x + r * 0.62f, c.y + r * 0.2f}, r * 0.14f, Color{255, 150, 150, 200});
    }
}

static Color padColor(int kind, int side) {
    return ColorBrightness(C_KIND[kind], pad[kind * 2 + side] > 0 ? 0.3f : -0.4f);
}

static void drawGame() {
    DrawRectangle(0, (int)(LANE_Y - LANE_H / 2), W, (int)LANE_H, Color{41, 41, 51, 255});
    DrawLine((int)JUDGE_X, (int)(LANE_Y - LANE_H / 2), (int)JUDGE_X, (int)(LANE_Y + LANE_H / 2), Fade(WHITE, 0.12f));
    DrawRing({JUDGE_X, LANE_Y}, JUDGE_R - 2, JUDGE_R + 2, 0, 360, 64, Fade(WHITE, 0.85f));
    for (const Effect& e : effects) {
        float t = e.age / 0.3f, r = JUDGE_R + t * 30;
        DrawRing({JUDGE_X, LANE_Y}, r - 2, r + 2, 0, 360, 64, Fade(e.c, 1 - t));
    }
    for (int i = (int)notes.size() - 1; i >= 0; i--) {   // back-to-front: earlier notes on top
        const Note& n = notes[i];
        if (n.result != R_NONE && n.result != R_MISS) continue;
        float x = noteX(n);
        if (x < -50 || x > W + 50) continue;
        DrawCircleV({x, LANE_Y}, NOTE_R + 4, WHITE);
        DrawCircleV({x, LANE_Y}, NOTE_R, n.result == R_MISS ? Color{89, 89, 102, 255} : C_KIND[n.kind]);
    }
    for (const Popup& p : popups) {
        float t = p.age / 0.6f;
        drawCentered(RES_TXT[p.res], JUDGE_X, LANE_Y - LANE_H / 2 - 40 - t * 20, 26, Fade(C_RES[p.res], 1 - t));
    }

    DrawText(TextFormat("Score %d", score), 20, 20, 26, WHITE);
    if (combo > 1) { const char* c = TextFormat("%d combo", combo); DrawText(c, W - 20 - MeasureText(c, 26), 20, 26, WHITE); }
    drawCentered("horror don virus 1999", W / 2.0f, 12, 20, Color{255, 80, 80, 255});

    DrawCircleSector(DRUM_C, DRUM_OUTER, 90, 270, 40, padColor(K_KA, 0));
    DrawCircleSector(DRUM_C, DRUM_OUTER, -90, 90, 40, padColor(K_KA, 1));
    DrawCircleSector(DRUM_C, DRUM_INNER, 90, 270, 40, padColor(K_DON, 0));
    DrawCircleSector(DRUM_C, DRUM_INNER, -90, 90, 40, padColor(K_DON, 1));
    DrawLineEx({DRUM_C.x, DRUM_C.y - DRUM_OUTER}, {DRUM_C.x, DRUM_C.y + DRUM_OUTER}, 4, BG);
    DrawRing(DRUM_C, DRUM_INNER - 2, DRUM_INNER + 2, 0, 360, 64, BG);
    DrawRing(DRUM_C, DRUM_OUTER - 2, DRUM_OUTER + 2, 0, 360, 64, BG);
    drawCentered("D", DRUM_C.x - 98, DRUM_C.y - 13, 26, WHITE);
    drawCentered("F", DRUM_C.x - 35, DRUM_C.y - 13, 26, WHITE);
    drawCentered("J", DRUM_C.x + 35, DRUM_C.y - 13, 26, WHITE);
    drawCentered("K", DRUM_C.x + 98, DRUM_C.y - 13, 26, WHITE);

    Vector2 dc = {DONCHAN_C.x, DONCHAN_C.y + sinf((float)GetTime() * 2.0f) * 4.0f};   // Don-chan bobs gently
    drawDonChan(dc, DONCHAN_R, false);
    DrawText("Don-chan", (int)DONCHAN_C.x - 40, (int)DONCHAN_C.y + 48, 18, LIGHTGRAY);

    if (gTime < 0) drawCentered(TextFormat("%d", (int)ceil(-gTime)), W / 2.0f, 250, 56, Fade(WHITE, 0.9f));
}

static void drawVirus() {
    float t = gVirusT;
    bool joking = t >= 7.0f;                           // after 7 seconds: "just kidding!"
    float pulse = 0.5f + 0.5f * sinf(t * 3.0f);        // slow pulse, nothing flashes fast
    DrawRectangle(0, 0, W, H, joking ? Color{20, 60, 30, 255} : Color{(unsigned char)(25 + 45 * pulse), 0, 0, 255});
    float sx = joking ? 0 : (float)GetRandomValue(-2, 2), sy = joking ? 0 : (float)GetRandomValue(-2, 2);
    Rectangle win = {170 + sx, 60 + sy, 660, 340};
    DrawRectangleRec(win, Color{192, 192, 192, 255});
    DrawRectangleRec({win.x, win.y, win.width, 34}, Color{0, 0, 128, 255});
    DrawText("horror don virus 1999", (int)win.x + 10, (int)win.y + 8, 20, WHITE);
    DrawRectangleLinesEx(win, 3, BLACK);
    drawDonChan({win.x + 120, win.y + 190}, 80, !joking);
    float tx = win.x + 240;
    if (!joking) {
        DrawText("Don-chan.exe has stopped!", (int)tx, (int)win.y + 70, 28, Color{160, 0, 0, 255});
        DrawText("Don-chan is eating your drum notes...", (int)tx, (int)win.y + 115, 20, BLACK);
        float p = t / 7.0f;
        Rectangle bar = {tx, win.y + 170, 380, 34};
        DrawRectangleRec(bar, WHITE);
        DrawRectangleRec({bar.x, bar.y, bar.width * p, bar.height}, Color{200, 30, 30, 255});
        DrawRectangleLinesEx(bar, 2, BLACK);
        DrawText(TextFormat("%d%%", (int)(p * 100)), (int)tx + 170, (int)win.y + 215, 22, BLACK);
    } else {
        DrawText("JUST KIDDING!", (int)tx, (int)win.y + 80, 40, Color{0, 110, 30, 255});
        DrawText("It's only a joke game.", (int)tx, (int)win.y + 140, 22, BLACK);
        DrawText("Nothing on your tablet was changed. :)", (int)tx, (int)win.y + 175, 20, BLACK);
    }
    drawButton(BACK_BTN, "Back to game", Color{60, 120, 60, 255}, 26);
}

// ------------------------------------------------------------ main
static void leaveVirus() { gVirus = false; resetGame(); }

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(W, H, "horror don virus 1999");
    SetExitKey(KEY_NULL);
    InitAudioDevice();
    snd[K_DON] = makeBlip(160, 0.09f);
    snd[K_KA]  = makeBlip(520, 0.07f);
    SetTargetFPS(60);
    resetGame();

    while (!WindowShouldClose()) {
        updateScale();
        float dt = fminf(GetFrameTime(), 0.05f);

        if (gVirus) {
            if (IsKeyPressed(KEY_ESCAPE)) leaveVirus();
        } else {
            if (IsKeyPressed(KEY_D)) press(K_KA, 0);
            if (IsKeyPressed(KEY_F)) press(K_DON, 0);
            if (IsKeyPressed(KEY_J)) press(K_DON, 1);
            if (IsKeyPressed(KEY_K)) press(K_KA, 1);
            if (IsKeyPressed(KEY_R)) resetGame();
        }
        for (Vector2 v : newPresses()) {
            if (gVirus) {
                if (CheckCollisionPointRec(v, BACK_BTN)) { leaveVirus(); break; }
            } else if (CheckCollisionPointCircle(v, DONCHAN_C, DONCHAN_R + 10)) {
                gVirus = true; gVirusT = 0;            // you tapped Don-chan!
                break;
            } else {
                drumPress(v);
            }
        }
        if (gVirus) gVirusT += dt; else updateGame(dt);

        BeginDrawing();
        ClearBackground(BG);
        Camera2D cam = {};
        cam.offset = {gOx, gOy}; cam.zoom = gScale;
        BeginMode2D(cam);
        if (gVirus) drawVirus(); else drawGame();
        EndMode2D();
        EndDrawing();
    }
    UnloadSound(snd[0]); UnloadSound(snd[1]);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
// END OF FILE