#include "digit.h"

#define __ false
#define XX true

static bool zero[DIGIT_HT][DIGIT_W] = {
    XX,XX,XX,
    XX,__,XX,
    XX,__,XX,
    XX,__,XX,
    XX,XX,XX
};

static bool one[DIGIT_HT][DIGIT_W] = {
    XX,XX,__,
    __,XX,__,
    __,XX,__,
    __,XX,__,
    XX,XX,XX
};

static bool two[DIGIT_HT][DIGIT_W] = {
    XX,XX,XX,
    __,__,XX,
    XX,XX,XX,
    XX,__,__,
    XX,XX,XX
};

static bool three[DIGIT_HT][DIGIT_W] = {
    XX,XX,XX,
    __,__,XX,
    XX,XX,XX,
    __,__,XX,
    XX,XX,XX
};

static bool four[DIGIT_HT][DIGIT_W] = {
    XX,__,XX,
    XX,__,XX,
    XX,XX,XX,
    __,__,XX,
    __,__,XX
};

static bool five[DIGIT_HT][DIGIT_W] = {
    XX,XX,XX,
    XX,__,__,
    XX,XX,XX,
    __,__,XX,
    XX,XX,XX
};

static bool six[DIGIT_HT][DIGIT_W] = {
    XX,XX,XX,
    XX,__,__,
    XX,XX,XX,
    XX,__,XX,
    XX,XX,XX
};

static bool seven[DIGIT_HT][DIGIT_W] = {
    XX,XX,XX,
    __,__,XX,
    __,XX,__,
    __,XX,__,
    __,XX,__
};

static bool eight[DIGIT_HT][DIGIT_W] = {
    XX,XX,XX,
    XX,__,XX,
    XX,XX,XX,
    XX,__,XX,
    XX,XX,XX
};

static bool nine[DIGIT_HT][DIGIT_W] = {
    XX,XX,XX,
    XX,__,XX,
    XX,XX,XX,
    __,__,XX,
    XX,XX,XX
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

    if (lx < 0 || ly < 0 || lx >= DIGIT_W || ly >= DIGIT_HT)
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
