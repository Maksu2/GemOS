#!/usr/bin/env bash
# GemOS smoke test: build the image and boot it in headless QEMU.
#
# Usage:  tools/smoke.sh [--no-build]
#                        [--stress [CYCLES] | --matrix | --selftest | --editor]
#
#   --stress   load test instead of the smoke test: CYCLES (default 25)
#              rounds of opening, typing into and closing all programs
#   --matrix   the smoke test on every machine variant: 32/64/256 MB,
#              no data disk, 4 MB VRAM (smaller mode, no page flip),
#              boot from the hard disk image, a data disk without GemFS
#              or with a damaged superblock (must stay unchanged), a
#              second boot of the same disk (must write nothing), and a
#              disk that writes slowly
#   --editor   the text editor with files: type, Esc and the close button
#              ask about unsaved text, save as, reopen from the File
#              Explorer, save again, discard, and a refused save over a
#              program; the saved file is checked on the host
#   --selftest build and boot the self-test image (make selftest):
#              heap, pool, ELF loader, GemFS and the file syscalls, FPU
#              state, every exception from Ring 3, and a kernel stack
#              overflow that must end in the double fault handler; boots
#              twice so the second boot checks the files of the first
#
# Environment:
#   QEMU           emulator binary (default: qemu-system-i386)
#   SMOKE_TIMEOUT  hard limit for the QEMU part in seconds
#                  (default: 600, with --stress or --matrix 1800)
#   SMOKE_OUT      artifacts directory (default: build/smoke, build/stress,
#                  build/matrix, build/selftest-run, build/editor)
#
# The QEMU part lives in tools/smoke.py (Python 3, standard library only).
# Prints one PASS/FAIL line per check and ends with "SMOKE: PASS" or
# "SMOKE: FAIL"; the exit status is 0 only on PASS.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build=1
stress=0
matrix=0
selftest=0
editor=0

while [ $# -gt 0 ]; do
  case "$1" in
    --no-build) build=0 ;;
    --stress)
      stress=25
      if [ $# -gt 1 ] && [[ "$2" =~ ^[0-9]+$ ]]; then stress="$2"; shift; fi
      ;;
    --matrix) matrix=1 ;;
    --selftest) selftest=1 ;;
    --editor) editor=1 ;;
    -h|--help) awk 'NR == 1 { next } /^#/ { print; next } { exit }' "$0"
               exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

image="$root/build/gemos.img"
target=all
if [ "$selftest" -eq 1 ]; then
  out="${SMOKE_OUT:-$root/build/selftest-run}"
  limit="${SMOKE_TIMEOUT:-600}"
  image="$root/build/selftest/gemos.img"
  target=selftest
elif [ "$matrix" -eq 1 ]; then
  out="${SMOKE_OUT:-$root/build/matrix}"
  limit="${SMOKE_TIMEOUT:-1800}"
elif [ "$editor" -eq 1 ]; then
  out="${SMOKE_OUT:-$root/build/editor}"
  limit="${SMOKE_TIMEOUT:-600}"
elif [ "$stress" -gt 0 ]; then
  out="${SMOKE_OUT:-$root/build/stress}"
  limit="${SMOKE_TIMEOUT:-1800}"
else
  out="${SMOKE_OUT:-$root/build/smoke}"
  limit="${SMOKE_TIMEOUT:-600}"
fi

if [ "$build" -eq 1 ]; then
  echo "== make $target"
  if ! make -C "$root" "$target"; then
    echo "SMOKE: FAIL (build)"
    exit 1
  fi
fi

echo "== QEMU smoke test (limit ${limit}s, artifacts in ${out})"
# leave the Python driver some slack to stop QEMU and print its report
budget=$(( limit > 60 ? limit - 30 : limit ))
cmd=(python3 "$root/tools/smoke.py" --image "$image"
     --out "$out" --timeout "$budget" --stress "$stress")
if [ "$matrix" -eq 1 ]; then
  cmd+=(--matrix)
fi
if [ "$selftest" -eq 1 ]; then
  cmd+=(--selftest)
fi
if [ "$editor" -eq 1 ]; then
  cmd+=(--editor)
fi
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
