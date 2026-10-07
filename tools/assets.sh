#!/bin/sh
# Rebuilds main/assets from Hanka8/Nu-pogodi: its SVGs at the board's scale (its CSS px x 480/255, the 480 px
# wide screen), its MP3s as 44.1 kHz mono PCM (louder), and its score font (Digital Numbers, 15 CSS px) as an LVGL font.
# Needs rsvg-convert, ImageMagick, ffmpeg and npx.
set -e
cd "$(dirname "$0")/.."
src=$(mktemp -d)
git clone -q --depth 1 https://github.com/Hanka8/Nu-pogodi.git "$src"
out=main/assets
mkdir -p $out
svg=$src/assets

# Sizes are the CSS boxes scaled (background-size: contain picks the side that limits).
rsvg-convert -w 480 -b '#b6bfb2' $svg/background.svg -o $out/background.png # #gameBoard's colour under it
for w in LeftUp LeftDown RightUp RightDown; do rsvg-convert -h 132 $svg/wolf_$w.svg -o $out/wolf_$w.png; done
rsvg-convert -w 13 $svg/egg.svg -o $out/egg.png
rsvg-convert -w 45 $svg/crashedEgg.svg -o $out/crashed_egg.png
rsvg-convert -w 30 $svg/life.svg -o $out/life.png
# The running chicken in its 12 x 11 box (23 x 21 here): as drawn on the left, turned round on the right.
for i in 1 2 3 4; do
    if [ $i = 1 ]; then fit="-w 23"; else fit="-h 21"; fi
    rsvg-convert $fit $svg/chickenRunning$i.svg -o $src/c.png
    magick $src/c.png -background none -gravity NorthWest -extent 23x21 $out/chicken_left$i.png
    magick $src/c.png -flop -background none -gravity NorthEast -extent 23x21 $out/chicken_right$i.png
done

# They're quiet (the loudest, gameOver, peaks at -28 dBFS): one gain for all lifts it to -1 and keeps their balance.
for s in collect lifeDown gameOver; do
    ffmpeg -v error -y -i $src/sounds/$s.mp3 -af volume=27dB -ac 1 -ar 44100 -f s16le $out/$s.pcm
done

curl -sL -o $src/digits.woff https://fonts.cdnfonts.com/s/15953/DigitalNumbers-Regular.woff
npx -y lv_font_conv --font $src/digits.woff --size 28 --bpp 4 --range 0x30-0x39 --no-compress \
    --format lvgl --lv-include lvgl.h --lv-font-name digits -o $src/digits.c
grep -v '^ \* Opts:' $src/digits.c > main/digits.c # its options name this temporary directory

rm -rf "$src"
