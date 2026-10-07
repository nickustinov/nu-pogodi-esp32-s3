# Nu, pogodi!

[Hanka8/Nu-pogodi](https://github.com/Hanka8/Nu-pogodi)'s game A on a Waveshare ESP32-S3-Touch-AMOLED-2.16: its
pictures, sounds and rules, without the console around the screen. Tap the left or right half: in the middle the
wolf holds the basket up, at the bottom down. A tap starts a game; three broken eggs end it. The keys on the side:
"+" and "–" set the volume, PWR in the middle locks it (the screen dark, the game paused) until pressed again.

```sh
. ~/esp/esp-idf-v6.1/export.sh && idf.py build                    # the firmware
idf.py -p /dev/cu.usbmodem* flash monitor                          # onto the board
c++ -std=c++20 -Imain test/test_game.cpp main/game.cpp -o build/test_game && build/test_game   # the rules
tools/assets.sh                                                    # main/assets and main/digits.c from the original
```
