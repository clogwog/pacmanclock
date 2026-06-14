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
#include <cmath>

#include "digit.h"

using namespace std;

using rgb_matrix::GPIO;
using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;
using rgb_matrix::FrameCanvas;

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

// Clock — small 3x5 digits, centred at the top of the panel.
// HH:MM total width = 17 (3+1+3+1+1+1+3+1+3). Start at x=7 to centre on 32.
static Digit g_h10(7,  1);
static Digit g_h1 (11, 1);
static Digit g_m10(17, 1);
static Digit g_m1 (21, 1);
static const int COLON_X     = 15;
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

// Randomised DFS spanning tree + extra random edges for loops/cross-connections.
// Every call produces a new connected maze with dead ends.
static void GenerateRandomMaze()
{
    memset(hConn, 0, sizeof(hConn));
    memset(vConn, 0, sizeof(vConn));

    bool visited[GRID][GRID];
    memset(visited, 0, sizeof(visited));

    struct Cell { int i, j; };
    vector<Cell> stack;
    int si = rand() % GRID;
    int sj = rand() % GRID;
    visited[si][sj] = true;
    stack.push_back({si, sj});

    while (!stack.empty())
    {
        int i = stack.back().i;
        int j = stack.back().j;

        struct Move { int di, dj; };
        Move opts[4];
        int n = 0;
        if (i > 0        && !visited[i-1][j]) opts[n++] = {-1,  0};
        if (i < GRID - 1 && !visited[i+1][j]) opts[n++] = { 1,  0};
        if (j > 0        && !visited[i][j-1]) opts[n++] = { 0, -1};
        if (j < GRID - 1 && !visited[i][j+1]) opts[n++] = { 0,  1};

        if (n == 0) { stack.pop_back(); continue; }

        Move m = opts[rand() % n];
        int ni = i + m.di;
        int nj = j + m.dj;

        if      (m.di ==  1) hConn[i ][j ] = true;
        else if (m.di == -1) hConn[ni][nj] = true;
        else if (m.dj ==  1) vConn[i ][j ] = true;
        else                 vConn[ni][nj] = true;

        visited[ni][nj] = true;
        stack.push_back({ni, nj});
    }

    // Extra edges (~25% chance) — adds loops + cross connections on top of the
    // spanning tree, so the maze still has dead ends but isn't a tree.
    for (int i = 0; i < GRID - 1; ++i)
        for (int j = 0; j < GRID; ++j)
            if (!hConn[i][j] && (rand() % 100) < 25) hConn[i][j] = true;

    for (int i = 0; i < GRID; ++i)
        for (int j = 0; j < GRID - 1; ++j)
            if (!vConn[i][j] && (rand() % 100) < 25) vConn[i][j] = true;
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
struct Pellet { int x; int y; bool eaten; bool power; };
static vector<Pellet> g_pellets;

// Rainbow flash — triggered when pacman eats a power pellet. For the duration
// the remaining pellets cycle through all hues, then settle back to normal.
static bool          g_flash_active = false;
static struct timeval g_flash_start;
static const long    FLASH_DURATION_US = 700 * 1000;   // 0.7s burst

static void TriggerFlash()
{
    g_flash_active = true;
    gettimeofday(&g_flash_start, NULL);
}

// h in [0,360), s,v in [0,1] -> 0..255 rgb.
static void HsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b)
{
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf = 0, gf = 0, bf = 0;
    if      (h <  60) { rf = c; gf = x; }
    else if (h < 120) { rf = x; gf = c; }
    else if (h < 180) { gf = c; bf = x; }
    else if (h < 240) { gf = x; bf = c; }
    else if (h < 300) { rf = x; bf = c; }
    else              { rf = c; bf = x; }
    r = (uint8_t)((rf + m) * 255.0f);
    g = (uint8_t)((gf + m) * 255.0f);
    b = (uint8_t)((bf + m) * 255.0f);
}

static void BuildPellets()
{
    g_pellets.clear();
    for (int i = 0; i < GRID; ++i)
    {
        for (int j = 0; j < GRID; ++j)
        {
            bool corner = (i == 0 || i == GRID - 1) && (j == 0 || j == GRID - 1);
            g_pellets.push_back({cell_cx(i), cell_cy(j), false, corner});
        }
    }

    for (int i = 0; i < GRID - 1; ++i)
        for (int j = 0; j < GRID; ++j)
            if (hConn[i][j])
                g_pellets.push_back({cell_cx(i) + 3, cell_cy(j), false, false});

    for (int i = 0; i < GRID; ++i)
        for (int j = 0; j < GRID - 1; ++j)
            if (vConn[i][j])
                g_pellets.push_back({cell_cx(i), cell_cy(j) + 3, false, false});
}

static void DrawPellets(Canvas* c)
{
    // During a flash, all remaining pellets share a hue that spins through the
    // full colour wheel a few times, with a per-pellet offset so the rainbow
    // ripples across the maze. Once the burst ends, pellets return to normal.
    bool  rainbow   = false;
    float flash_hue = 0.0f;
    if (g_flash_active)
    {
        struct timeval now;
        gettimeofday(&now, NULL);
        long el = (now.tv_sec  - g_flash_start.tv_sec)  * 1000000L
                + (now.tv_usec - g_flash_start.tv_usec);
        if (el >= FLASH_DURATION_US)
            g_flash_active = false;
        else
        {
            rainbow   = true;
            flash_hue = (float)el / FLASH_DURATION_US * 360.0f;   // one full spin
        }
    }

    float hue_step = g_pellets.empty() ? 0.0f : 360.0f / g_pellets.size();

    int idx = 0;
    for (const auto& p : g_pellets)
    {
        ++idx;
        if (p.eaten) continue;

        uint8_t pr = PELL_R, pg = PELL_G, pb = PELL_B;
        if (rainbow)
            HsvToRgb(flash_hue + idx * hue_step, 1.0f, 1.0f, pr, pg, pb);

        if (p.power)
        {
            // 2x2 power pellet, biased toward the maze corner it sits in
            int bx = (p.x < PANEL / 2) ? -1 : 0;
            int by = (p.y < PANEL / 2) ? -1 : 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                    c->SetPixel(p.x + bx + dx, p.y + by + dy, pr, pg, pb);
        }
        else
        {
            c->SetPixel(p.x, p.y, pr, pg, pb);
        }
    }
}

static bool AnyPelletLeft()
{
    for (const auto& p : g_pellets)
        if (!p.eaten) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Pacman — 5x5 sprite with chomping mouth, wanders maze with no-backtrack.
// ---------------------------------------------------------------------------
class Pacman
{
public:
    Pacman()
    {
        reset();
    }

    void reset()
    {
        // start as close to the centre cell as possible, but skip any cell
        // whose 5x5 corridor block isn't fully clear of walls (search outward
        // in expanding rings until we find a clear one).
        int t_i = GRID / 2;
        int t_j = GRID / 2;
        ci = t_i;
        cj = t_j;
        for (int radius = 0; radius < GRID; ++radius)
        {
            bool found = false;
            for (int di = -radius; di <= radius && !found; ++di)
            {
                for (int dj = -radius; dj <= radius && !found; ++dj)
                {
                    if (std::max(std::abs(di), std::abs(dj)) != radius) continue;
                    int ni = t_i + di;
                    int nj = t_j + dj;
                    if (ni < 0 || ni >= GRID || nj < 0 || nj >= GRID) continue;
                    if (cell_is_clear(ni, nj))
                    {
                        ci = ni;
                        cj = nj;
                        found = true;
                    }
                }
            }
            if (found) break;
        }
        dx = 0; dy = 0;
        progress = 0;
        frame = 0;
        pick_direction();
    }

    void step()
    {
        ++progress;
        frame ^= 1;

        if (progress >= CELL)
        {
            ci += dx;
            cj += dy;
            progress = 0;
            pick_direction();
        }

        int cx_px = cell_cx(ci) + dx * progress;
        int cy_px = cell_cy(cj) + dy * progress;
        for (auto& p : g_pellets)
        {
            if (p.eaten) continue;
            if (abs(p.x - cx_px) <= 1 && abs(p.y - cy_px) <= 1)
            {
                p.eaten = true;
                if (p.power)
                    TriggerFlash();
            }
        }
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

    bool cell_is_clear(int i, int j) const
    {
        int cx = cell_cx(i);
        int cy = cell_cy(j);
        for (int dy = -2; dy <= 2; ++dy)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                int px = cx + dx;
                int py = cy + dy;
                if (px < 0 || px >= PANEL || py < 0 || py >= PANEL) return false;
                if (!g_corridor[py][px]) return false;
            }
        }
        return true;
    }

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
    opts.brightness = 50;                   // half strength
    RGBMatrix*   matrix    = new RGBMatrix(&io, opts);
    FrameCanvas* offscreen = matrix->CreateFrameCanvas();

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT,  InterruptHandler);

    GenerateRandomMaze();
    BuildCorridorMask();
    BuildPellets();

    Pacman pac;

    time_t start_t = time(0);
    // Wall-clock pacing — refresh rate varies on the LED matrix, so frame
    // counts can't drive game speed. ~80ms/step ≈ 12 pixels per second.
    static const long PAC_STEP_US = 80 * 1000;
    struct timeval last_step;
    gettimeofday(&last_step, NULL);

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

        offscreen->Clear();
        DrawWalls(offscreen);
        DrawPellets(offscreen);

        struct timeval now_tv;
        gettimeofday(&now_tv, NULL);
        long elapsed_us = (now_tv.tv_sec  - last_step.tv_sec)  * 1000000L
                        + (now_tv.tv_usec - last_step.tv_usec);
        if (elapsed_us >= PAC_STEP_US)
        {
            pac.step();
            last_step = now_tv;
            if (!AnyPelletLeft())
            {
                GenerateRandomMaze();
                BuildCorridorMask();
                BuildPellets();
                pac.reset();
            }
        }

        DrawDigits(offscreen);     // time over maze + pellets
        pac.render(offscreen);     // pacman on top of everything

        // Atomic flip on the next vsync — no flicker.
        offscreen = matrix->SwapOnVSync(offscreen);
        if (interrupt_received)
            cont = false;
    }

    syslog(LOG_NOTICE, "end of pacmanclock");

    matrix->Clear();
    delete matrix;
    return 0;
}
