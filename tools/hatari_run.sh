#!/bin/zsh
# usage: tools/hatari_run.sh <name> <tos.img> <script> [hatari options...]
# Boots Hatari with test/hd/<name> as drive C: (own folder per run, so runs
# can go in parallel) with bin/neko.acc (or bin/$ACC) as C:\BREAKOUT.ACC, then
# plays <script>, one command per line:
#   wait <secs>        sleep
#   shot               screenshot -> test/shots/<name>-<n>.png
#   key <scancode>     press and release a key (hex, e.g. 0x10)
#   down <scancode>    hold a key       up <scancode>   release it
#   dclick             double click the left mouse button
#   fast <scan> <n>    like alt, but quickly (fast mouse moves)
#   debug <cmd...>     Hatari debugger command, output in test/shots/<name>.log
#   rclick             right click (Alt+Home, held 0.2 s)
#   click              click it (Alt+Insert, held for 0.2 s)
#   alt <scan> <n>     press <scan> n times with Alternate held (moves
#                      the mouse 8 px per press in TOS/EmuTOS)
#   copy <file> [dir] [name]
#                      copy test/<file> to drive C: (or C:\dir, as name)
#                      before booting
#                      (NEKO.PRG: the current bin/neko.acc)
#                      (only valid before the first other command)
P=${0:A:h:h}; name=$1 tos=$2 script=${3:A}; shift 3
hd=$P/test/hd/$name out=$P/test/shots
rm -rf $hd; mkdir -p $hd $out
[[ -z $NOACC ]] && cp $P/bin/${ACC:-BREAKOUT.ACC} $hd/BREAKOUT.ACC
while read -r cmd a b c; do
  # NEKO.PRG = the accessory renamed, always the current build
  [[ $cmd == copy && $a == NEKO.PRG ]] && cp $P/bin/${ACC:-BREAKOUT.ACC} $hd/NEKO.PRG
  [[ $cmd == copy && $a != NEKO.PRG ]] && mkdir -p $hd/$b && cp "$P/test/$a" $hd/$b/${c:-$a}
done < $script
fifo=${TMPDIR:-/tmp}/bo-hatari-$name.fifo; rm -f $fifo
mkdir -p $P/test/.hatari-home
HOME=$P/test/.hatari-home hatari --tos $tos -d $hd \
  --cmd-fifo $fifo --sound off --fast-boot on --confirm-quit off \
  --statusbar off --log-level error "$@" > $out/$name.log 2>&1 &
pid=$!
for i in {1..50}; do [[ -p $fifo ]] && break; sleep 0.2; done
n=0; tmp=$(mktemp -d)
send() { echo "$1" > $fifo; sleep ${DELAY:-0.6}; }
while read -r cmd a b; do
  case $cmd in
    wait) sleep $a ;;
    shot) n=$((n+1)); send "hatari-debug screenshot $tmp/s.png"; sleep 1
          mv $tmp/s.png $out/$name-$n.png && echo "$out/$name-$n.png" ;;
    key)  send "hatari-event keypress $a" ;;
    down) send "hatari-event keydown $a" ;;
    up)   send "hatari-event keyup $a" ;;
    dclick) send "hatari-event doubleclick" ;;
    fast) send "hatari-event keydown 0x38"       # like alt, but quick
          for i in {1..$b}; do
            echo "hatari-event keydown $a" > $fifo; sleep 0.03
            echo "hatari-event keyup $a" > $fifo; sleep 0.03
          done
          send "hatari-event keyup 0x38" ;;
    debug) send "hatari-debug $a $b $c" ;;   # output goes to <name>.log
    rclick) send "hatari-event keydown 0x38"   # right button: Alt+Home
          echo "hatari-event keydown 0x47" > $fifo; sleep 0.2
          send "hatari-event keyup 0x47"
          send "hatari-event keyup 0x38" ;;
    click) send "hatari-event keydown 0x38"
          echo "hatari-event keydown 0x52" > $fifo; sleep 0.2
          send "hatari-event keyup 0x52"
          send "hatari-event keyup 0x38" ;;
    alt)  send "hatari-event keydown 0x38"
          for i in {1..$b}; do
            echo "hatari-event keydown $a" > $fifo; sleep 0.1
            echo "hatari-event keyup $a" > $fifo; sleep 0.25
          done
          send "hatari-event keyup 0x38" ;;
  esac
done < $script
echo "hatari-debug quit" > $fifo
for i in {1..25}; do kill -0 $pid 2>/dev/null || break; sleep 0.2; done
kill $pid 2>/dev/null; rm -f $fifo
