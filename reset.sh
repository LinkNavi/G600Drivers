#!/usr/bin/env bash
set -e
DEV=warbling-mara

for p in 0 1 2; do
  echo "=== Profile $p ==="
  for b in $(seq 0 19); do
    ratbagctl $DEV profile $p button $b action set button $((b+1)) 2>/dev/null || true
  done
done
echo "Done. All buttons reset to 'button N' (firmware default passthrough)."
