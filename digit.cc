#include "digit.h"

#define DW 5
#define DH 7

#define __ false
#define XX true

static bool zero[DH][DW] = {
    XX,XX,XX,XX,XX,
    XX,__,__,__,XX,
    XX,__,__,__,XX,
    XX,__,__,__,XX,
    XX,__,__,__,XX,
    XX,__,__,__,XX,
    XX,XX,XX,XX,XX
};

static bool one[DH][DW] = {
    __,__,XX,__,__,
    __,XX,XX,__,__,
    __,__,XX,__,__,
    __,__,XX,__,__,
    __,__,XX,__,__,
    __,__,XX,__,__,
    __,XX,XX,XX,__
};

static bool two[DH][DW] = {
    XX,XX,XX,XX,XX,
    __,__,__,__,XX,
    __,__,__,__,XX,
    XX,XX,XX,XX,XX,
    XX,__,__,__,__,
    XX,__,__,__,__,
    XX,XX,XX,XX,XX
};

static bool three[DH][DW] = {
    XX,XX,XX,XX,XX,
    __,__,__,__,XX,
    __,__,__,__,XX,
    XX,XX,XX,XX,XX,
    __,__,__,__,XX,
    __,__,__,__,XX,
    XX,XX,XX,XX,XX
};

static bool four[DH][DW] = {
    XX,__,__,__,XX,
    XX,__,__,__,XX,
    XX,__,__,__,XX,
    XX,XX,XX,XX,XX,
    __,__,__,__,XX,
    __,__,__,__,XX,
    __,__,__,__,XX
};

static bool five[DH][DW] = {
    XX,XX,XX,XX,XX,
    XX,__,__,__,__,
    XX,__,__,__,__,
    XX,XX,XX,XX,XX,
    __,__,__,__,XX,
    __,__,__,__,XX,
    XX,XX,XX,XX,XX
};

static bool six[DH][DW] = {
    XX,XX,XX,XX,XX,
    XX,__,__,__,__,
    XX,__,__,__,__,
    XX,XX,XX,XX,XX,
    XX,__,__,__,XX,
    XX,__,__,__,XX,
    XX,XX,XX,XX,XX
};

static bool seven[DH][DW] = {
    XX,XX,XX,XX,XX,
    __,__,__,__,XX,
    __,__,__,__,XX,
    __,__,__,XX,__,
    __,__,XX,__,__,
    __,XX,__,__,__,
    __,XX,__,__,__
};

static bool eight[DH][DW] = {
    XX,XX,XX,XX,XX,
    XX,__,__,__,XX,
    XX,__,__,__,XX,
    XX,XX,XX,XX,XX,
    XX,__,__,__,XX,
    XX,__,__,__,XX,
    XX,XX,XX,XX,XX
};

static bool nine[DH][DW] = {
    XX,XX,XX,XX,XX,
    XX,__,__,__,XX,
    XX,__,__,__,XX,
    XX,XX,XX,XX,XX,
    __,__,__,__,XX,
    __,__,__,__,XX,
    XX,XX,XX,XX,XX
};

Digit::Digit(int x, int y)
{
    this->num = 0;
    this->x = x;
    this->y = y;
}

void Digit::update(int n)
{
    this->num = n % 10;
}

bool Digit::hittest(int gx, int gy)
{
    int lx = gx - x;
    int ly = gy - y;

    if (lx < 0 || ly < 0 || lx >= DW || ly >= DH)
        return false;

    switch (num)
    {
        case 0: return zero[ly][lx];
        case 1: return one[ly][lx];
        case 2: return two[ly][lx];
        case 3: return three[ly][lx];
        case 4: return four[ly][lx];
        case 5: return five[ly][lx];
        case 6: return six[ly][lx];
        case 7: return seven[ly][lx];
        case 8: return eight[ly][lx];
        case 9: return nine[ly][lx];
    }
    return false;
}
