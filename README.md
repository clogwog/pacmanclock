pacman clock made using the adafruit RGB matrix HAT and a 32x32 RGB LED matrix.

A pacman-style maze wraps a centred HH:MM clock; a pacman sprite runs the perimeter
eating pellets, which regenerate after each lap.

Build:

    make -C lib        # builds librgbmatrix.a (rpi-rgb-led-matrix, bundled)
    make               # builds the pacmanclock binary

Run:

    sudo ./pacmanclock          # runs until Ctrl-C
    sudo ./pacmanclock 30       # runs for 30s then exits
