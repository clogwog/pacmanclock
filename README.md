pacman clock made using the adafruit RGB matrix HAT and a 32x32 RGB LED matrix.

![pacmanclock running](./pacmanclock.GIF)

A randomly generated pacman-style maze fills the panel, with HH:MM centred at
the top and a 5x5 pacman sprite that wanders the corridors eating pellets.
The four corner cells get 2x2 power pellets. When every pellet has been eaten
a fresh maze is generated and pacman starts again from the centre cell.

Build:

    make -C lib HARDWARE_DESC=adafruit-hat    # builds librgbmatrix.a (once)
    make                                       # builds the pacmanclock binary

Run:

    sudo ./pacmanclock          # runs until Ctrl-C
    sudo ./pacmanclock 30       # runs for 30s then exits

Features:

- 5x5 cell grid, randomised DFS maze generator + extra edges → loops, branches and dead ends
- 5x5 chomping pacman sprite with directional mouth, wall-clock paced (~12 px/sec)
- 2x2 power pellets in the four corner cells, biased toward each corner
- White HH:MM clock overlaid on the maze; pacman draws on top of the time
- Double-buffered via `FrameCanvas` + `SwapOnVSync` — no flicker
- 50% brightness, `disable_hardware_pulsing` for Pi sound-module compatibility
