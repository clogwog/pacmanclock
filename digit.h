#ifndef DIGIT_H
#define DIGIT_H

#define DIGIT_W 3
#define DIGIT_HT 5

class Digit
{
    public:
        Digit(int x, int y);
        bool hittest(int gx, int gy);
        void update(int n);

    private:
        int num;
        int x;
        int y;
};

#endif
