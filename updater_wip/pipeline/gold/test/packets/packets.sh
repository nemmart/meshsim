#!/bin/sh
# The link's packets (WORKER2): the packetizer (packets), the reader (packets-read) and the test vectors.
#   1. the committed vectors (VECTORS.txt and the small packet files here) are exactly what packets-vectors writes;
#   2. check_vectors.py: every value and every "expect" of the vectors again, by a third implementation in Python;
#   3. the commands on a real guest's frames (keccak256, N 64): the round trip, --idle and --first, what packets-read
#      prints, and its refusals with exit status 1 and nothing written;
#   4. PacketsTest: the round trip on made-up files, damage, the wrap, idle zeros, length 0, the vectors by the reader.
# The round trip on every frame file gold's tests make is in test/frames/guests.sh and workloads.sh.
#   test/packets/packets.sh     from gold's directory, after build.sh has compiled
set -u
cd "$(dirname "$0")/../.."
J="java -cp build gold.Main"
W=/tmp/gold-packets; rm -rf "$W"; mkdir -p "$W/v" "$W/empty"
fail=0
bad() { echo "FAIL $*"; fail=$((fail + 1)); }

# 1
$J packets-vectors "$W/v" > /dev/null || bad "packets-vectors"
for f in $(cd "$W/v" && ls); do cmp -s "$W/v/$f" "test/packets/$f" || bad "test/packets/$f is not what packets-vectors writes"; done
for f in test/packets/*.packets test/packets/*.frames test/packets/*.txt; do [ -f "$W/v/$(basename "$f")" ] || bad "$f is not one of packets-vectors' files"; done
[ $fail = 0 ] && echo "vectors: the $(ls "$W/v" | wc -l) committed files are what packets-vectors writes"

# 2
python3 test/packets/check_vectors.py test/packets || bad "check_vectors.py"

# 3
F=$W/k.frames; P=$W/k.packets; B=$W/k.back
$J frames test/guests/keccak256 "$W/empty" "$F" --asics=64 > /dev/null || bad "the guest's frames"
says() {  # says <expected exit status> <text its output must hold> <command...>
  want=$1; text=$2; shift 2
  out=$("$@" 2>&1); rc=$?
  [ $rc = "$want" ] && echo "$out" | grep -q -- "$text" || bad "exit $rc (want $want), no \"$text\" in: $* : $out"
}
says 0 "148 packets on stream 2 (numbers 1 to 148); 0 idle bytes" $J packets "$F" "$P"
says 0 "^checks: ok" $J packets-read "$P" "$B" --compare="$F"
cmp -s "$B" "$F" || bad "the frames written back differ"
says 0 "overhead 2,368 bytes (3.258%): 148 header words, 0 idle bytes" $J packets-read "$P"
says 0 "^checks: ok" $J packets-read "$P" --no-idle
says 0 "the frames not looked at" $J packets-read "$P" --packets-only
[ "$($J packets-read "$P" --list | grep -c "^packet [0-9,]* at byte")" = 148 ] || bad "--list"
rm -f "$B"
says 0 "(numbers 1048573 to 146); 2,499 idle bytes" $J packets "$F" "$P" --idle=17 --first=0xFFFFD
says 0 "passing 0xFFFFE once" $J packets-read "$P" "$B" --first=0xFFFFD --compare="$F"
cmp -s "$B" "$F" || bad "the frames written back differ (--idle, --first)"
rm -f "$B"
says 1 "^REFUSED number: packet 1 at byte 0: number 1048573 on stream 2, 1 expected" $J packets-read "$P" "$B"
says 1 "^REFUSED first-byte: packet 2 at byte " $J packets-read "$P" "$B" --first=0xFFFFD --no-idle
$J packets "$F" "$P" > /dev/null
python3 -c "
import sys; b=bytearray(open('$P','rb').read()); b[1000]^=0x20; open('$W/bad.packets','wb').write(b)
c=bytearray(open('$F','rb').read()); c[5000]^=1; open('$W/other.frames','wb').write(c)
open('$W/short.frames','wb').write(c[:-16])"
says 1 "^REFUSED data-checksum: packet " $J packets-read "$W/bad.packets" "$B"
says 1 "^REFUSED compare: .*the first difference is at byte 5,000" $J packets-read "$P" "$B" --compare="$W/other.frames"
says 1 "^packets: the frame at byte .* runs past the end of the file" $J packets "$W/short.frames" "$W/x.packets"
[ ! -e "$B" ] && [ ! -e "$W/x.packets" ] || bad "a refused file was written out"
[ $fail = 0 ] && echo "commands: packets and packets-read on keccak256's frames (72,672 bytes, 148 packets): the round trip, --idle, --first, --no-idle, --packets-only, --list, --compare; five refusals with exit status 1 and nothing written"

# 4
java -cp build gold.PacketsTest "$F" || bad "PacketsTest"
rm -rf "$W"
[ $fail = 0 ] && echo "packets: all ok" || { echo "packets: $fail FAILED"; exit 1; }
