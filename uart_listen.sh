#!/bin/sh
# mav_hb_arm.sh — MAVLink2 HEARTBEAT-only sniffer (no CRC)
# Prints ARMED/DISARMED based on HEARTBEAT base_mode bit 0x80.

DEV="${1:-/dev/ttyS2}"
BAUD="${2:-57600}"

stty -F "$DEV" "$BAUD" raw -echo -ixon -ixoff -crtscts

od -An -tx1 -v -w1 "$DEV" 2>/dev/null |
awk '
function h2d(h,    n,i,c) {
  n=0
  for (i=1;i<=length(h);i++) {
    c=tolower(substr(h,i,1))
    n*=16
    if (c>="0" && c<="9") n+=c-"0"
    else n+=10+index("abcdef",c)-1
  }
  return n
}

BEGIN {
  state=0
  skip=0

  hb_need=0
  hb_i=0
  hb_base_mode=0
}

{
  b = h2d($1)

  # Collect HEARTBEAT payload bytes (plen==9) to extract base_mode at payload offset 6
  if (hb_need > 0) {
    if (hb_i == 6) hb_base_mode = b
    hb_i++
    hb_need--
    if (hb_need == 0) {
      armed = and(hb_base_mode, 128) ? "ARMED" : "DISARMED"
      printf("HEARTBEAT sys=%d comp=%d base_mode=0x%02X => %s\n",
             hb_sysid, hb_compid, hb_base_mode, armed)
      fflush()

      # Skip checksum + optional signature
      sig = (and(hb_incompat, 1) ? 13 : 0)
      skip = 2 + sig
    }
    next
  }

  # Skip payload/checksum/signature for non-heartbeat frames
  if (skip > 0) { skip--; next }

  # MAVLink2 sync and header parse
  if (state == 0) { if (b == 253) state=1; next }     # 0xFD
  if (state == 1) { plen=b; state=2; next }
  if (state == 2) { incompat=b; state=3; next }
  if (state == 3) { compat=b; state=4; next }
  if (state == 4) { seq=b; state=5; next }
  if (state == 5) { sysid=b; state=6; next }
  if (state == 6) { compid=b; state=7; next }
  if (state == 7) { mid0=b; state=8; next }
  if (state == 8) { mid1=b; state=9; next }
  if (state == 9) {
    mid2=b
    msgid = mid0 + 256*mid1 + 65536*mid2

    if (msgid == 0 && plen == 9) {
      # Read exactly the HEARTBEAT payload next
      hb_need = 9
      hb_i = 0
      hb_base_mode = 0
      hb_sysid = sysid
      hb_compid = compid
      hb_incompat = incompat
      state = 0
      next
    }

    # Ignore all other messages (but still skip their bytes to stay in sync)
    sig = (and(incompat, 1) ? 13 : 0)
    skip = plen + 2 + sig
    state = 0
    next
  }

  # Failsafe: resync
  state=0
}
'
