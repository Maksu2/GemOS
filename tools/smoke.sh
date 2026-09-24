#!/usr/bin/env bash
# GemOS smoke test: build the image and boot it in headless QEMU.
#
# Usage:  tools/smoke.sh [--no-build]
#
# Environment:
#   QEMU           emulator binary (default: qemu-system-i386)
#   SMOKE_TIMEOUT  hard limit for the QEMU part in seconds (default: 600)
#   SMOKE_OUT      artifacts directory (default: build/smoke)
#
# The QEMU part lives in tools/smoke.py (Python 3, standard library only).
# Prints one PASS/FAIL line per check and ends with "SMOKE: PASS" or
# "SMOKE: FAIL"; the exit status is 0 only on PASS.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${SMOKE_OUT:-$root/build/smoke}"
limit="${SMOKE_TIMEOUT:-600}"
build=1

for arg in "$@"; do
  case "$arg" in
    --no-build) build=0 ;;
    -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

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
     --out "$out" --timeout "$budget")
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
