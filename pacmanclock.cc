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

// Clock — small 3x5 digits top-left. HH:MM with 1px gaps; total 17px wide.
static Digit g_h10(1,  1);
static Digit g_h1 (5,  1);
static Digit g_m10(11, 1);
static Digit g_m1 (15, 1);
static const int COLON_X     = 9;
static const int COLON_Y_TOP = 2;
static const int COLON_Y_BOT = 4;

// ---------------------------------------------------------------------------
// Maze — 5x5 grid of cells, each cell is 5px-wide corridor + 1px wall.
// Cell (i,j) corridor centre at (3 + 6i, 3 + 6j). Cell corridor area is the
// 5x5 block (cx-2..cx+2, cy-2..cy+2). Walls between cells are opened where
// hConn[i][j] (i->i+1) or vConn[i][j] (j->j+1) is true.
// ---------------------------------------------------------------------------

static const int GRID = 5;
static const int CELL = 6;   // 5 corridor + 1 wall
static const int OFF  = 3;   // first cell centre

static bool hConn[GRID - 1][GRID];   // hConn[i][j] connects (i,j) <-> (i+1,j)
static bool vConn[GRID][GRID - 1];   // vConn[i][j] connects (i,j) <-> (i,j+1)

static bool g_corridor[PANEL][PANEL];

static int cell_cx(int i) { return OFF + CELL * i; }
static int cell_cy(int j) { return OFF + CELL * j; }

// Maze layout — pacman-style with loops, branches and dead ends.
//
//    0   1   2   3   4
//   (0,0)─(1,0)─(2,0)─(3,0)─(4,0)
//    │     │           │     │
//   (0,1) (1,1)─(2,1)─(3,1) (4,1)
//    │                 │     │           (note: (2,1)-(2,2) vertical)
//   (0,2)─(1,2)       (2,2) (3,2)─(4,2)
//    │                 │     │           dead ends: (1,2) (3,2)
//   (0,3) (1,3)─(2,3)─(3,3) (4,3)
//    │     │                 │           dead ends: (1,3) (3,3)
//   (0,4)─(1,4)─(2,4)─(3,4)─(4,4)
//
static void BuildMazeGraph()
{
    memset(hConn, 0, sizeof(hConn));
    memset(vConn, 0, sizeof(vConn));

    // top row: fully connected horizontally
    hConn[0][0] = hConn[1][0] = hConn[2][0] = hConn[3][0] = true;
    // middle row stub
    hConn[1][1] = hConn[2][1] = true;
    // row 2 side stubs (left + right)
    hConn[0][2] = hConn[3][2] = true;
    // row 3 middle stub
    hConn[1][3] = hConn[2][3] = true;
    // bottom row fully connected
    hConn[0][4] = hConn[1][4] = hConn[2][4] = hConn[3][4] = true;

    // left + right columns fully connected
    vConn[0][0] = vConn[0][1] = vConn[0][2] = vConn[0][3] = true;
    vConn[4][0] = vConn[4][1] = vConn[4][2] = vConn[4][3] = true;
    // column 1: top and bottom stubs only
    vConn[1][0] = vConn[1][3] = true;
    // column 2: centre vertical (creates spurs (2,1)<->(2,2)<->(2,3))
    vConn[2][1] = vConn[2][2] = true;
    // column 3: top and bottom stubs only
    vConn[3][0] = vConn[3][3] = true;
}

static void BuildCorridorMask()
{
    memset(g_corridor, 0, sizeof(g_corridor));

    // cell corridor blocks
    for (int i = 0; i < GRID; ++i)
    {
        for (int j = 0; j < GRID; ++j)
        {
            int cx = cell_cx(i);
            int cy = cell_cy(j);
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    g_corridor[cy + dy][cx + dx] = true;
        }
    }
    // open horizontal connections (1-px-wide column, 5 tall)
    for (int i = 0; i < GRID - 1; ++i)
    {
        for (int j = 0; j < GRID; ++j)
        {
            if (!hConn[i][j]) continue;
            int wx = cell_cx(i) + 3;   // wall column between cell i and i+1
            int cy = cell_cy(j);
            for (int dy = -2; dy <= 2; ++dy)
                g_corridor[cy + dy][wx] = true;
        }
    }
    // open vertical connections (1-px-tall row, 5 wide)
    for (int i = 0; i < GRID; ++i)
    {
        for (int j = 0; j < GRID - 1; ++j)
        {
            if (!vConn[i][j]) continue;
            int cx = cell_cx(i);
            int wy = cell_cy(j) + 3;
            for (int dx = -2; dx <= 2; ++dx)
                g_corridor[wy][cx + dx] = true;
        }
    }
}

// Draw walls as blue outlines around the corridor (every non-corridor pixel
// adjacent to corridor). Gives the classic thin-line maze look.
static void DrawWalls(Canvas* c)
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

// ---------------------------------------------------------------------------
// Pellets — one at each cell centre + one at each connection midpoint.
// ---------------------------------------------------------------------------
struct Pellet { int x; int y; bool eaten; };
static vector<Pellet> g_pellets;

static void BuildPellets()
{
    g_pellets.clear();
    for (int i = 0; i < GRID; ++i)
        for (int j = 0; j < GRID; ++j)
            g_pellets.push_back({cell_cx(i), cell_cy(j), false});

    for (int i = 0; i < GRID - 1; ++i)
        for (int j = 0; j < GRID; ++j)
            if (hConn[i][j])
                g_pellets.push_back({cell_cx(i) + 3, cell_cy(j), false});

    for (int i = 0; i < GRID; ++i)
        for (int j = 0; j < GRID - 1; ++j)
            if (vConn[i][j])
                g_pellets.push_back({cell_cx(i), cell_cy(j) + 3, false});
}

static void DrawPellets(Canvas* c)
{
    for (const auto& p : g_pellets)
        if (!p.eaten)
            c->SetPixel(p.x, p.y, PELL_R, PELL_G, PELL_B);
}

static bool AnyPelletLeft()
{
    for (const auto& p : g_pellets)
        if (!p.eaten) return true;
    return false;
}

static void RegeneratePellets()
{
    for (auto& p : g_pellets) p.eaten = false;
}

// ---------------------------------------------------------------------------
// Pacman — 5x5 sprite with chomping mouth, wanders maze with no-backtrack.
// ---------------------------------------------------------------------------
class Pacman
{
public:
    Pacman()
    {
        ci = 0; cj = 0;
        dx = 1; dy = 0;
        progress = 0;
        frame = 0;
    }

    void step()
    {
        ++progress;
        frame ^= 1;

        if (progress >= CELL)
        {
            // arrived at next cell
            ci += dx;
            cj += dy;
            progress = 0;
            pick_direction();
        }

        // eat any pellets we now overlap
        int cx_px = cell_cx(ci) + dx * progress;
        int cy_px = cell_cy(cj) + dy * progress;
        for (auto& p : g_pellets)
        {
            if (p.eaten) continue;
            if (abs(p.x - cx_px) <= 1 && abs(p.y - cy_px) <= 1)
                p.eaten = true;
        }

        if (!AnyPelletLeft())
            RegeneratePellets();
    }

    void render(Canvas* c) const
    {
        int cx_px = cell_cx(ci) + dx * progress;
        int cy_px = cell_cy(cj) + dy * progress;

        const bool (*sprite)[5];
        if (frame == 0)
            sprite = mouth_closed;
        else if (dx ==  1) sprite = mouth_right;
        else if (dx == -1) sprite = mouth_left;
        else if (dy ==  1) sprite = mouth_down;
        else               sprite = mouth_up;

        for (int sy = 0; sy < 5; ++sy)
        {
            for (int sx = 0; sx < 5; ++sx)
            {
                if (!sprite[sy][sx]) continue;
                int px = cx_px - 2 + sx;
                int py = cy_px - 2 + sy;
                if (px < 0 || px >= PANEL || py < 0 || py >= PANEL) continue;
                c->SetPixel(px, py, PAC_R, PAC_G, PAC_B);
            }
        }
    }

private:
    int ci, cj;        // current cell (the cell we just left or just entered)
    int dx, dy;        // direction we're heading
    int progress;      // 0..CELL pixels along (dx,dy) from current cell centre
    int frame;

    bool exit_exists(int ei, int ej, int edx, int edy) const
    {
        int ni = ei + edx;
        int nj = ej + edy;
        if (ni < 0 || ni >= GRID || nj < 0 || nj >= GRID) return false;
        if (edx ==  1) return hConn[ei][ej];
        if (edx == -1) return hConn[ni][ej];
        if (edy ==  1) return vConn[ei][ej];
        if (edy == -1) return vConn[ei][nj];
        return false;
    }

    void pick_direction()
    {
        // build list of available exits (excluding reverse if possible)
        struct D { int dx, dy; };
        static const D dirs[4] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
        D options[4];
        int n = 0;
        int rev_dx = -dx;
        int rev_dy = -dy;
        for (int k = 0; k < 4; ++k)
        {
            if (!exit_exists(ci, cj, dirs[k].dx, dirs[k].dy)) continue;
            if (dirs[k].dx == rev_dx && dirs[k].dy == rev_dy) continue;
            options[n++] = dirs[k];
        }
        if (n == 0)
        {
            // dead end — backtrack
            dx = rev_dx;
            dy = rev_dy;
            return;
        }
        int pick = rand() % n;
        dx = options[pick].dx;
        dy = options[pick].dy;
    }

    static const bool mouth_closed[5][5];
    static const bool mouth_right [5][5];
    static const bool mouth_left  [5][5];
    static const bool mouth_up    [5][5];
    static const bool mouth_down  [5][5];
};

const bool Pacman::mouth_closed[5][5] = {
    {0,1,1,1,0},
    {1,1,1,1,1},
    {1,1,1,1,1},
    {1,1,1,1,1},
    {0,1,1,1,0}
};

const bool Pacman::mouth_right[5][5] = {
    {0,1,1,1,0},
    {1,1,1,0,0},
    {1,1,0,0,0},
    {1,1,1,0,0},
    {0,1,1,1,0}
};

const bool Pacman::mouth_left[5][5] = {
    {0,1,1,1,0},
    {0,0,1,1,1},
    {0,0,0,1,1},
    {0,0,1,1,1},
    {0,1,1,1,0}
};

const bool Pacman::mouth_up[5][5] = {
    {0,1,0,1,0},
    {1,1,0,1,1},
    {1,1,1,1,1},
    {1,1,1,1,1},
    {0,1,1,1,0}
};

const bool Pacman::mouth_down[5][5] = {
    {0,1,1,1,0},
    {1,1,1,1,1},
    {1,1,1,1,1},
    {1,1,0,1,1},
    {0,1,0,1,0}
};

// ---------------------------------------------------------------------------
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
    opts.hardware_mapping = "adafruit-hat";
    opts.rows = 32;
    opts.chain_length = 1;
    opts.parallel = 1;
    opts.disable_hardware_pulsing = true;
    Canvas* canvas = new RGBMatrix(&io, opts);

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT,  InterruptHandler);

    BuildMazeGraph();
    BuildCorridorMask();
    BuildPellets();

    Pacman pac;

    time_t start_t = time(0);
    int    pac_tick = 0;
    const int PAC_STEP_FRAMES = 4;   // ~60Hz / 4 = ~15 pixels/sec

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
        DrawWalls(canvas);
        DrawPellets(canvas);

        if (++pac_tick >= PAC_STEP_FRAMES)
        {
            pac.step();
            pac_tick = 0;
        }
        pac.render(canvas);

        DrawDigits(canvas);     // time on top of everything

        usleep(16000);
        if (interrupt_received)
            cont = false;
    }

    syslog(LOG_NOTICE, "end of pacmanclock");

    canvas->Clear();
    delete canvas;
    return 0;
}
