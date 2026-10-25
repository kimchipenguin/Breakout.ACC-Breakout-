#!/bin/zsh
# usage: [IMG=disk.img ACCDIR=AUTO/ACCS/] tools/rate.sh ACC tos.img [hatari options]
# Boots Hatari with the measuring build ACC (build/MEASURE.ACC or
# build/MEASORIG.ACC) as C:\BREAKOUT.ACC, waits until it has opened its
# window and plays, then prints game steps per second over 10 s.
P=${0:A:h:h}; acc=${1:A}; tos=$2; shift 2
hd=$(mktemp -d); cp $acc $hd/BREAKOUT.ACC
f=$hd.fifo; log=$hd.log
if [[ -n $IMG ]]; then
  # IMG=<copy of an AHDI hard disk image>, ACCDIR=folder on partition 1
  MTOOLS_SKIP_CHECK=1 mcopy -o -i $IMG@@1024 $acc ::/${ACCDIR:-}BREAKOUT.ACC
  drive=(--acsi 0=$IMG)
else
  drive=(-d $hd)
fi
HOME=$P/test/home hatari --tos $tos $drive --cmd-fifo $f --sound off \
  --fast-boot on --confirm-quit off --log-level error "$@" > $log 2>&1 &
pid=$!
# RETURN_AT=<s>: press Return then (boot-time dialogs)
if [[ -n $RETURN_AT ]]; then
  sleep $RETURN_AT; echo "hatari-event keypress 0x1c" > $f
  sleep $(( ${BOOT:-30} - RETURN_AT ))
else
  sleep ${BOOT:-30}
fi
# find 'BKCT' in the first 16 MB: the address of the counters
rd() { echo "hatari-debug m \$4ba 4" > $f; sleep 0.5
       echo "hatari-debug m $1 12" > $f; sleep 0.5; }
echo "hatari-debug savebin $hd.ram 0 0x400000" > $f; sleep 3
# (skip the move.l #'BKCT',marker instruction: opcode $23fc in front)
a=$(python3 -c "import re;d=open('$hd.ram','rb').read();print(hex(next((m.start() for m in re.finditer(b'BKCT',d) if d[m.start()-2:m.start()]!=b'\x23\xfc'),-1)))")
rd \$${a#0x}; sleep 10; rd \$${a#0x}
echo "hatari-debug screenshot $hd.png" > $f; sleep 1
echo "hatari-debug quit" > $f; sleep 2; kill $pid 2>/dev/null
cp $hd.png ${SHOT:-/dev/null} 2>/dev/null
python3 - $log <<'PY'
import re,sys
t=open(sys.argv[1]).read()
h=[int(x.replace(' ',''),16) for x in re.findall(r'000004BA: ([0-9a-f ]{11})',t)]
c=[(int(a.replace(' ',''),16),int(b.replace(' ',''),16)) for a,b in re.findall(r'BKCT ?|[0-9A-F]+: 42 4b 43 54 ([0-9a-f ]{11}) ([0-9a-f ]{11})',t) if a]
if len(h)<2 or len(c)<2: print('no data'); print(t[-600:]); sys.exit()
dt=(h[1]-h[0])/200; n=(c[1][0]-c[0][0])+(c[1][1]-c[0][1])
print('%d steps in %.2f s = %.1f steps/s (play %d, serve %d)'%(n,dt,n/dt,c[1][0]-c[0][0],c[1][1]-c[0][1]))
PY
[[ -n $KEEP ]] && cp $hd.ram $KEEP.ram && cp $log $KEEP.log
rm -rf $hd $hd.ram $hd.log
