#!/usr/bin/env bash
# GemOS smoke test: build the image and boot it in headless QEMU.
#
# Usage:  tools/smoke.sh [--no-build] [--stress [CYCLES]]
#
#   --stress   load test instead of the smoke test: CYCLES (default 25)
#              rounds of opening, typing into and closing all programs
#
# Environment:
#   QEMU           emulator binary (default: qemu-system-i386)
#   SMOKE_TIMEOUT  hard limit for the QEMU part in seconds
#                  (default: 600, with --stress 1800)
#   SMOKE_OUT      artifacts directory (default: build/smoke, build/stress)
#
# The QEMU part lives in tools/smoke.py (Python 3, standard library only).
# Prints one PASS/FAIL line per check and ends with "SMOKE: PASS" or
# "SMOKE: FAIL"; the exit status is 0 only on PASS.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build=1
stress=0

while [ $# -gt 0 ]; do
  case "$1" in
    --no-build) build=0 ;;
    --stress)
      stress=25
      if [ $# -gt 1 ] && [[ "$2" =~ ^[0-9]+$ ]]; then stress="$2"; shift; fi
      ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

if [ "$stress" -gt 0 ]; then
  out="${SMOKE_OUT:-$root/build/stress}"
  limit="${SMOKE_TIMEOUT:-1800}"
else
  out="${SMOKE_OUT:-$root/build/smoke}"
  limit="${SMOKE_TIMEOUT:-600}"
fi

if [ "$build" -eq 1 ]; then
  echo "== make all"
  if ! make -C "$root" all; then
    echo "SMOKE: FAIL (build)"
    exit 1
  fi
fi

echo "== QEMU smoke test (limit ${limit}s, artifacts in ${out})"
# leave the Python driver some slack to stop QEMU and print its report
budget=$(( limit > 60 ? limit - 30 : limit ))
cmd=(python3 "$root/tools/smoke.py" --image "$root/build/gemos.img"
     --out "$out" --timeout "$budget" --stress "$stress")
status=0
if command -v timeout >/dev/null 2>&1; then
  timeout --kill-after=15 "$limit" "${cmd[@]}" || status=$?
else
  "${cmd[@]}" || status=$?
fi

case "$status" in
  0) ;;
  124|137) echo "SMOKE: FAIL (hard timeout after ${limit}s)"; status=1 ;;
esac
exit "$status"
