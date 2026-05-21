#include "led-matrix.h"
#include "graphics.h"

#include <unistd.h>
#include <stdio.h>
#include <cstdlib>
#include <iostream>
#include <ctime>
#include <signal.h>
#include <syslog.h>
#include <sys/time.h>
#include <vector>
#include <utility>
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
static const uint8_t TIME_R = 240, TIME_G = 180, TIME_B = 40;
static const uint8_t PAC_R  = 255, PAC_G  = 220, PAC_B  = 0;

volatile bool interrupt_received = false;
static void InterruptHandler(int)
{
    syslog(LOG_NOTICE, "interrupt handler ");
    interrupt_received = true;
}

// Clock digits — positioned so HH:MM is centred on a 32-wide panel.
// Layout: digit(5) gap(1) digit(5) gap(1) colon(1) gap(1) digit(5) gap(1) digit(5) = 25 wide, starts at x=3.
static Digit g_h10(3,  13);
static Digit g_h1 (9,  13);
static Digit g_m10(17, 13);
static Digit g_m1 (23, 13);
static const int COLON_X = 15;
static const int COLON_Y_TOP = 14;
static const int COLON_Y_BOT = 17;

// Pacman corridor — clockwise loop just inside the outer wall.
// row 1 (left→right), col 30 (top→bottom), row 30 (right→left), col 1 (bottom→top).
static vector<pair<int,int>> g_path;

static void BuildPath()
{
    g_path.clear();
    for (int x = 1;  x <= 30; ++x) g_path.push_back({x, 1});
    for (int y = 2;  y <= 30; ++y) g_path.push_back({30, y});
    for (int x = 29; x >= 1;  --x) g_path.push_back({x, 30});
    for (int y = 29; y >= 2;  --y) g_path.push_back({1, y});
}

static void DrawOuterWall(Canvas* c)
{
    for (int i = 0; i < PANEL; ++i)
    {
        c->SetPixel(i, 0,         WALL_R, WALL_G, WALL_B);
        c->SetPixel(i, PANEL - 1, WALL_R, WALL_G, WALL_B);
        c->SetPixel(0,         i, WALL_R, WALL_G, WALL_B);
        c->SetPixel(PANEL - 1, i, WALL_R, WALL_G, WALL_B);
    }
}

// Small pacman-style inner maze accents that hint at the classic maze shape
// without crowding the clock. Just short blue stubs in each corner.
static void DrawInnerAccents(Canvas* c)
{
    // top-left bracket
    c->SetPixel(3, 3, WALL_R, WALL_G, WALL_B);
    c->SetPixel(4, 3, WALL_R, WALL_G, WALL_B);
    c->SetPixel(3, 4, WALL_R, WALL_G, WALL_B);
    // top-right bracket
    c->SetPixel(28, 3, WALL_R, WALL_G, WALL_B);
    c->SetPixel(27, 3, WALL_R, WALL_G, WALL_B);
    c->SetPixel(28, 4, WALL_R, WALL_G, WALL_B);
    // bottom-left bracket
    c->SetPixel(3, 28, WALL_R, WALL_G, WALL_B);
    c->SetPixel(4, 28, WALL_R, WALL_G, WALL_B);
    c->SetPixel(3, 27, WALL_R, WALL_G, WALL_B);
    // bottom-right bracket
    c->SetPixel(28, 28, WALL_R, WALL_G, WALL_B);
    c->SetPixel(27, 28, WALL_R, WALL_G, WALL_B);
    c->SetPixel(28, 27, WALL_R, WALL_G, WALL_B);

    // small dotted side walls flanking the clock to suggest a maze
    for (int y = 10; y <= 21; y += 3)
    {
        c->SetPixel(2,  y, WALL_R, WALL_G, WALL_B);
        c->SetPixel(29, y, WALL_R, WALL_G, WALL_B);
    }
}

static void DrawPellets(Canvas* c, const vector<bool>& eaten)
{
    for (size_t i = 0; i < g_path.size(); ++i)
    {
        if (i % 2 != 0) continue;       // pellet every other cell
        if (eaten[i])   continue;
        c->SetPixel(g_path[i].first, g_path[i].second, PELL_R, PELL_G, PELL_B);
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

class Pacman
{
public:
    Pacman() : pos(0), frame(0), eaten(0) {}

    void reset(size_t corridor_len)
    {
        pos = 0;
        frame = 0;
        eaten.assign(corridor_len, false);
    }

    void step()
    {
        eaten[pos] = true;
        pos = (pos + 1) % g_path.size();
        frame ^= 1;
        if (pos == 0)
        {
            // completed a lap — pellets regenerate
            std::fill(eaten.begin(), eaten.end(), false);
        }
    }

    void render(Canvas* c) const
    {
        int x = g_path[pos].first;
        int y = g_path[pos].second;
        c->SetPixel(x, y, PAC_R, PAC_G, PAC_B);
        if (frame == 0)
        {
            // "mouth open" — a small leading-edge pixel either side along travel
            int next = (pos + 1) % g_path.size();
            int px = g_path[next].first;
            int py = g_path[next].second;
            // soften the trailing edge by darkening the cell behind
            int prev = (pos + g_path.size() - 1) % g_path.size();
            c->SetPixel(g_path[prev].first, g_path[prev].second, 80, 60, 0);
            // forward chomp glow
            c->SetPixel(px, py, PAC_R, PAC_G, PAC_B);
        }
    }

    const vector<bool>& eaten_ref() const { return eaten; }

private:
    size_t       pos;
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

    Canvas* canvas = new RGBMatrix(&io, 32, 1);

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT,  InterruptHandler);

    BuildPath();

    Pacman pac;
    pac.reset(g_path.size());

    time_t start_t = time(0);
    int    pac_tick = 0;
    const int PAC_STEP_FRAMES = 4;   // ~60Hz / 4 ≈ 15 steps/sec

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
        DrawOuterWall(canvas);
        DrawInnerAccents(canvas);
        DrawPellets(canvas, pac.eaten_ref());
        DrawDigits(canvas);

        if (++pac_tick >= PAC_STEP_FRAMES)
        {
            pac.step();
            pac_tick = 0;
        }
        pac.render(canvas);

        usleep(16000);
        if (interrupt_received)
            cont = false;
    }

    syslog(LOG_NOTICE, "end of pacmanclock");

    canvas->Clear();
    delete canvas;
    return 0;
}
