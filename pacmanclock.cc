#include "led-matrix.h"
#include "graphics.h"

#include <unistd.h>
#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <ctime>
#include <signal.h>
#include <syslog.h>
#include <sys/time.h>
#include <vector>
#include <string>
#include <algorithm>

#include "digit.h"

using namespace std;

using rgb_matrix::GPIO;
using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;

static const int PANEL = 32;

static const uint8_t WALL_R = 30,  WALL_G = 30,  WALL_B = 200;
static const uint8_t PELL_R = 200, PELL_G = 160, PELL_B = 60;
static const uint8_t TIME_R = 255, TIME_G = 255, TIME_B = 255;
static const uint8_t PAC_R  = 255, PAC_G  = 220, PAC_B  = 0;

volatile bool interrupt_received = false;
static void InterruptHandler(int)
{
    syslog(LOG_NOTICE, "interrupt handler ");
    interrupt_received = true;
}

// Clock — small 3x5 digits top-left. Layout: D D : D D
//   x=1..3   d   gap x=4   d x=5..7   gap x=8   colon x=9   gap x=10   d x=11..13   gap x=14   d x=15..17
// total width = 17, y=1..5
static Digit g_h10(1,  1);
static Digit g_h1 (5,  1);
static Digit g_m10(11, 1);
static Digit g_m1 (15, 1);
static const int COLON_X     = 9;
static const int COLON_Y_TOP = 2;
static const int COLON_Y_BOT = 4;

// ---------------------------------------------------------------------------
// Maze
// ---------------------------------------------------------------------------
// Snake-style corridor that traverses the whole panel:
//   horizontal strips at y_center = 2, 7, 12, 17, 22, 27 (alternating L/R)
//   joined by vertical transitions at x_center = 29 (right) and x_center = 2 (left).
// Corridor is 3 px wide everywhere (path point +/- 1 in both axes).
// Pacman is a 3x3 sprite whose centre rides the path.

enum Dir { DIR_R, DIR_L, DIR_U, DIR_D };

struct PathStep
{
    int cx;
    int cy;
    Dir dir;
};

static vector<PathStep> g_path;
static bool g_corridor[PANEL][PANEL];   // true where path widens it into a corridor

static void BuildPath()
{
    g_path.clear();

    // strip 0: y=2, x: 2 -> 29 (right)
    for (int x = 2;  x <= 29; ++x) g_path.push_back({x, 2,  DIR_R});
    // right transition down: x=29, y: 3 -> 7
    for (int y = 3;  y <= 7;  ++y) g_path.push_back({29, y, DIR_D});
    // strip 1: y=7, x: 28 -> 2 (left)
    for (int x = 28; x >= 2;  --x) g_path.push_back({x, 7,  DIR_L});
    // left transition down: x=2, y: 8 -> 12
    for (int y = 8;  y <= 12; ++y) g_path.push_back({2,  y, DIR_D});
    // strip 2: y=12, x: 3 -> 29 (right)
    for (int x = 3;  x <= 29; ++x) g_path.push_back({x, 12, DIR_R});
    // right transition down: x=29, y: 13 -> 17
    for (int y = 13; y <= 17; ++y) g_path.push_back({29, y, DIR_D});
    // strip 3: y=17, x: 28 -> 2 (left)
    for (int x = 28; x >= 2;  --x) g_path.push_back({x, 17, DIR_L});
    // left transition down: x=2, y: 18 -> 22
    for (int y = 18; y <= 22; ++y) g_path.push_back({2,  y, DIR_D});
    // strip 4: y=22, x: 3 -> 29 (right)
    for (int x = 3;  x <= 29; ++x) g_path.push_back({x, 22, DIR_R});
    // right transition down: x=29, y: 23 -> 27
    for (int y = 23; y <= 27; ++y) g_path.push_back({29, y, DIR_D});
    // strip 5: y=27, x: 28 -> 2 (left)
    for (int x = 28; x >= 2;  --x) g_path.push_back({x, 27, DIR_L});
}

static void BuildCorridor()
{
    memset(g_corridor, 0, sizeof(g_corridor));
    for (size_t i = 0; i < g_path.size(); ++i)
    {
        int cx = g_path[i].cx;
        int cy = g_path[i].cy;
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int x = cx + dx;
                int y = cy + dy;
                if (x >= 0 && x < PANEL && y >= 0 && y < PANEL)
                    g_corridor[y][x] = true;
            }
        }
    }
}

// Draw blue maze walls: a wall pixel = non-corridor pixel adjacent to corridor.
// This gives clean pacman-style outlines instead of solid blue blobs.
static void DrawMaze(Canvas* c)
{
    for (int y = 0; y < PANEL; ++y)
    {
        for (int x = 0; x < PANEL; ++x)
        {
            if (g_corridor[y][x]) continue;

            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy)
            {
                for (int dx = -1; dx <= 1 && !edge; ++dx)
                {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx < 0 || nx >= PANEL || ny < 0 || ny >= PANEL) continue;
                    if (g_corridor[ny][nx]) edge = true;
                }
            }
            if (edge)
                c->SetPixel(x, y, WALL_R, WALL_G, WALL_B);
        }
    }
}

static void DrawPellets(Canvas* c, const vector<bool>& eaten)
{
    // pellet on every other path step
    for (size_t i = 0; i < g_path.size(); i += 2)
    {
        if (eaten[i]) continue;
        c->SetPixel(g_path[i].cx, g_path[i].cy, PELL_R, PELL_G, PELL_B);
    }
}

static void DrawDigits(Canvas* c)
{
    for (int y = 0; y < PANEL; ++y)
    {
        for (int x = 0; x < PANEL; ++x)
        {
            if (g_h10.hittest(x, y) ||
                g_h1 .hittest(x, y) ||
                g_m10.hittest(x, y) ||
                g_m1 .hittest(x, y))
            {
                c->SetPixel(x, y, TIME_R, TIME_G, TIME_B);
            }
        }
    }
    c->SetPixel(COLON_X, COLON_Y_TOP, TIME_R, TIME_G, TIME_B);
    c->SetPixel(COLON_X, COLON_Y_BOT, TIME_R, TIME_G, TIME_B);
}

// ---------------------------------------------------------------------------
// Pacman — 3x3 sprite with chomping mouth, orientation follows path direction.
// ---------------------------------------------------------------------------
class Pacman
{
public:
    Pacman() : pos(0), step_dir(1), frame(0) {}

    void reset(size_t corridor_len)
    {
        pos = 0;
        step_dir = 1;
        frame = 0;
        eaten.assign(corridor_len, false);
    }

    void step()
    {
        eaten[pos] = true;
        frame ^= 1;

        int next = (int)pos + step_dir;
        if (next < 0 || next >= (int)g_path.size())
        {
            // bounce — reverse direction at end of snake
            step_dir = -step_dir;
            // regenerate pellets on bounce so the panel re-fills
            std::fill(eaten.begin(), eaten.end(), false);
            return;
        }
        pos = next;
    }

    void render(Canvas* c) const
    {
        int cx = g_path[pos].cx;
        int cy = g_path[pos].cy;
        Dir d = g_path[pos].dir;
        if (step_dir < 0)
        {
            // travelling backwards along the path — flip horizontal/vertical dirs
            switch (d)
            {
                case DIR_R: d = DIR_L; break;
                case DIR_L: d = DIR_R; break;
                case DIR_U: d = DIR_D; break;
                case DIR_D: d = DIR_U; break;
            }
        }

        bool sprite[3][3] = {{0}};
        if (frame == 0)
        {
            // mouth closed — full pacman blob
            const bool closed[3][3] = {
                {0,1,0},
                {1,1,1},
                {0,1,0}
            };
            memcpy(sprite, closed, sizeof(sprite));
        }
        else
        {
            // mouth open in direction of travel
            switch (d)
            {
                case DIR_R: {
                    const bool s[3][3] = {{1,0,0},{1,1,0},{1,0,0}};
                    memcpy(sprite, s, sizeof(sprite));
                    break;
                }
                case DIR_L: {
                    const bool s[3][3] = {{0,0,1},{0,1,1},{0,0,1}};
                    memcpy(sprite, s, sizeof(sprite));
                    break;
                }
                case DIR_U: {
                    const bool s[3][3] = {{1,0,1},{0,1,0},{1,1,1}};
                    memcpy(sprite, s, sizeof(sprite));
                    break;
                }
                case DIR_D: {
                    const bool s[3][3] = {{1,1,1},{0,1,0},{1,0,1}};
                    memcpy(sprite, s, sizeof(sprite));
                    break;
                }
            }
        }

        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (sprite[dy + 1][dx + 1])
                    c->SetPixel(cx + dx, cy + dy, PAC_R, PAC_G, PAC_B);
            }
        }
    }

    const vector<bool>& eaten_ref() const { return eaten; }

private:
    size_t       pos;
    int          step_dir;
    int          frame;
    vector<bool> eaten;
};

int main(int argc, char* argv[])
{
    setlogmask(LOG_UPTO(LOG_NOTICE));
    openlog("pacmanclock", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    int maxtime = 0;
    if (argc > 1)
    {
        string arg(argv[1]);
        syslog(LOG_NOTICE, "running for %s seconds then quitting\n", arg.c_str());
        maxtime = std::stoi(arg);
    }

    GPIO io;
    if (!io.Init())
        return 1;

    srand((unsigned int)time(NULL));

    RGBMatrix::Options opts;
    opts.hardware_mapping = "adafruit-hat"; // Adafruit RGB Matrix HAT pinout
    opts.rows = 32;
    opts.chain_length = 1;
    opts.parallel = 1;
    opts.disable_hardware_pulsing = true;   // Pi sound module compat (more flicker)
    Canvas* canvas = new RGBMatrix(&io, opts);

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT,  InterruptHandler);

    BuildPath();
    BuildCorridor();

    Pacman pac;
    pac.reset(g_path.size());

    time_t start_t = time(0);
    int    pac_tick = 0;
    const int PAC_STEP_FRAMES = 6;   // ~60Hz / 6 ≈ 10 cells/sec

    bool cont = true;
    while (cont)
    {
        time_t     t   = time(0);
        struct tm* now = localtime(&t);

        if (maxtime > 0 && difftime(t, start_t) > maxtime)
        {
            cont = false;
            printf("stopping now\n");
        }

        g_h10.update(now->tm_hour / 10);
        g_h1 .update(now->tm_hour % 10);
        g_m10.update(now->tm_min  / 10);
        g_m1 .update(now->tm_min  % 10);

        canvas->Clear();
        DrawMaze(canvas);
        DrawPellets(canvas, pac.eaten_ref());

        if (++pac_tick >= PAC_STEP_FRAMES)
        {
            pac.step();
            pac_tick = 0;
        }
        pac.render(canvas);

        // time on top — overlays maze, pellets and pacman
        DrawDigits(canvas);

        usleep(16000);
        if (interrupt_received)
            cont = false;
    }

    syslog(LOG_NOTICE, "end of pacmanclock");

    canvas->Clear();
    delete canvas;
    return 0;
}
