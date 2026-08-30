#!/usr/bin/env bash
set -uo pipefail

ONE=/home/ubuntu/sionna-rk/scripts/k3-ul-run-one.sh
RESULT_ROOT=/home/ubuntu/k3-ul-test/results
STATUS="$RESULT_ROOT/matrix-status.env"
LOG="$RESULT_ROOT/matrix.log"
FAILED="$RESULT_ROOT/matrix-failed.txt"
COMPLETED=0
FAILURES=0
TOTAL=110

mkdir -p "$RESULT_ROOT"
: > "$FAILED"

write_status() {
  {
    printf 'state=%s\n' "$1"
    printf 'completed=%s\n' "$COMPLETED"
    printf 'failures=%s\n' "$FAILURES"
    printf 'total=%s\n' "$TOTAL"
    printf 'updated_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'current=%s\n' "${2:-none}"
  } > "$STATUS"
}

log() {
  printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" | tee -a "$LOG"
}

run_case() {
  local label="$1" channel="$2" duration="$3" capture="$4"
  if [ -f "$RESULT_ROOT/$label/meta.env" ] && grep -q '^end_utc=' "$RESULT_ROOT/$label/meta.env"; then
    COMPLETED=$((COMPLETED + 1))
    log "SKIP completed $label"
    write_status running "$label"
    return
  fi
  write_status running "$label"
  log "BEGIN $label channel=$channel duration=$duration capture=$capture"
  if "$ONE" "$label" "$channel" "$duration" "$capture"; then
    COMPLETED=$((COMPLETED + 1))
    log "PASS $label"
  else
    FAILURES=$((FAILURES + 1))
    printf '%s\n' "$label" >> "$FAILED"
    log "FAIL $label"
  fi
  write_status running "$label"
}

log 'K3 RV64 CPU-only uplink matrix started'
write_status running initialization

# Match the source experiment's approximately 2,000 TB per native run.
for run in $(seq -w 1 20); do
  run_case "ideal_native_r$run" ideal 95 native
done

for run in $(seq -w 1 20); do
  run_case "awgn_m3p5_native_r$run" -3.5 95 native
done

# Per-TB scans retain roughly 450 stable samples/run after deleting the first
# 650 and last 50 rows, as verified by the calibration run.
NOISE_LABELS=(m6p0 m5p0 m4p5 m4p0 m3p75 m3p5 m3p25 m3p0 m2p75 m2p5)
NOISE_VALUES=(-6 -5 -4.5 -4 -3.75 -3.5 -3.25 -3 -2.75 -2.5)
for index in "${!NOISE_VALUES[@]}"; do
  for run in $(seq -w 1 7); do
    run_case "scan_${NOISE_LABELS[$index]}_r$run" "${NOISE_VALUES[$index]}" 55 csv
  done
done

if [ "$FAILURES" -eq 0 ]; then
  write_status complete none
  log "MATRIX_COMPLETE completed=$COMPLETED failures=0"
else
  write_status complete_with_failures none
  log "MATRIX_COMPLETE completed=$COMPLETED failures=$FAILURES"
fi
