# Pink Tetris demo

Primitive external `.pink` Tetris app.

Controls:

- left half: move left
- right half: move right
- top band: rotate
- bottom band: move down

Build:

```sh
tools/build-pink-app.sh examples/pink/tetris/tetris.cpp build/pink/tetris.pink
```

Copy `build/pink/tetris.pink` to `/bin/games/tetris.pink` on the SD card.
Rebuild after firmware ABI changes; older `.pink` files are rejected by current
firmware.
