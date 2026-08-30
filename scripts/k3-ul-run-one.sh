#!/usr/bin/env bash
set -euo pipefail

# Run one independent K3 CPU-only OAI RFsim uplink experiment.
# Usage: k3-ul-run-one.sh LABEL ideal|NOISE_DB DURATION_SECONDS csv|native

if [ "$#" -ne 4 ]; then
  printf 'Usage: %s LABEL ideal|NOISE_DB DURATION_SECONDS csv|native\n' "$0" >&2
  exit 2
fi

LABEL="$1"
CHANNEL="$2"
DURATION="$3"
CAPTURE="$4"
OAI_ROOT=/home/ubuntu/sionna-rk/ext/openairinterface5g
BUILD="$OAI_ROOT/cmake_targets/ran_build/build"
BASE_CONF="$OAI_ROOT/ci-scripts/conf_files/gnb.sa.band78.106prb.rfsim.conf"
UE_CONF="$OAI_ROOT/targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.conf"
RESULT_ROOT=/home/ubuntu/k3-ul-test/results
RUN_DIR="$RESULT_ROOT/$LABEL"
GNB_UNIT=k3-ul-gnb.service
UE_UNIT=k3-ul-nrue.service

case "$LABEL" in
  *[!A-Za-z0-9._-]*|'') printf 'Invalid label: %s\n' "$LABEL" >&2; exit 2 ;;
esac
if [ "$CHANNEL" != ideal ] && [[ ! "$CHANNEL" =~ ^-?[0-9]+([.][0-9]+)?$ ]]; then
  printf 'Invalid channel: %s\n' "$CHANNEL" >&2
  exit 2
fi
case "$DURATION" in
  *[!0-9]*|'') printf 'Invalid duration: %s\n' "$DURATION" >&2; exit 2 ;;
esac
case "$CAPTURE" in csv|native) ;; *) printf 'Capture must be csv or native\n' >&2; exit 2 ;; esac

[ "$(uname -m)" = riscv64 ] || { printf 'This experiment must run on RV64 K3.\n' >&2; exit 1; }
[ -x "$BUILD/nr-softmodem" ] || { printf 'Missing nr-softmodem.\n' >&2; exit 1; }
[ -x "$BUILD/nr-uesoftmodem" ] || { printf 'Missing nr-uesoftmodem.\n' >&2; exit 1; }
[ ! -e "$RUN_DIR" ] || { printf 'Refusing to overwrite %s\n' "$RUN_DIR" >&2; exit 1; }

mkdir -p "$RUN_DIR"
cp "$BASE_CONF" "$RUN_DIR/gnb.conf"

GNB_EXTRA=()
if [ "$CHANNEL" != ideal ]; then
  # RFsim server names the uplink path rfsimu_channel_ue0.  Keep the
  # downlink effectively ideal so only the requested uplink AWGN changes.
  printf '\nchannelmod = {\n  max_chan = 10;\n  modellist = "modellist_rfsimu_1";\n  modellist_rfsimu_1 = (\n    { model_name = "rfsimu_channel_enB0"; type = "AWGN"; ploss_dB = 0; noise_power_dB = -100; forgetfact = 0; offset = 0; ds_tdl = 0; },\n    { model_name = "rfsimu_channel_ue0"; type = "AWGN"; ploss_dB = 0; noise_power_dB = %s; forgetfact = 0; offset = 0; ds_tdl = 0; }\n  );\n};\n' "$CHANNEL" >> "$RUN_DIR/gnb.conf"
  GNB_EXTRA=(--rfsimulator.options chanmod)
fi

cleanup() {
  sudo systemctl stop "$UE_UNIT" 2>/dev/null || true
  sudo systemctl stop "$GNB_UNIT" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
cleanup
sudo systemctl reset-failed "$GNB_UNIT" "$UE_UNIT" 2>/dev/null || true
rm -f "$BUILD/nrL1_stats.log"

START_UTC="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
{
  printf 'label=%s\n' "$LABEL"
  printf 'channel=%s\n' "$CHANNEL"
  printf 'duration_seconds=%s\n' "$DURATION"
  printf 'capture=%s\n' "$CAPTURE"
  printf 'start_utc=%s\n' "$START_UTC"
  printf 'architecture=%s\n' "$(uname -m)"
  printf 'kernel=%s\n' "$(uname -r)"
  printf 'oai_head=%s\n' "$(git -C "$OAI_ROOT" rev-parse HEAD)"
  printf 'oai_describe=%s\n' "$(git -C "$OAI_ROOT" describe --tags --always --dirty)"
  printf 'cpu_online=%s\n' "$(cat /sys/devices/system/cpu/online)"
  printf 'cpu_governor=%s\n' "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || printf unknown)"
  printf 'cpu_max_khz=%s\n' "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null || printf unknown)"
} > "$RUN_DIR/meta.env"

GNB_RUN=(sudo systemd-run --unit="$GNB_UNIT" --service-type=exec --property=Restart=no --property=KillMode=mixed --working-directory="$BUILD")
if [ "$CAPTURE" = csv ]; then
  GNB_RUN+=(--setenv="OAI_UL_TB_CSV=$RUN_DIR/tb.csv")
fi
GNB_RUN+=(taskset -c 0-7 "$BUILD/nr-softmodem" -O "$RUN_DIR/gnb.conf" --rfsim --rfsimulator.serveraddr server --phy-test --noS1 --gNBs.[0].min_rxtxtime 6 --T_stdout 1 -q)
GNB_RUN+=("${GNB_EXTRA[@]}")

printf 'RUN_START label=%s channel=%s duration=%ss capture=%s\n' "$LABEL" "$CHANNEL" "$DURATION" "$CAPTURE"
"${GNB_RUN[@]}"
GNB_INVOCATION="$(sudo systemctl show "$GNB_UNIT" -p InvocationID --value)"

PORT_READY=0
for _ in $(seq 1 40); do
  if ss -ltn 'sport = :4043' 2>/dev/null | grep -q LISTEN; then PORT_READY=1; break; fi
  if ! sudo systemctl is-active --quiet "$GNB_UNIT"; then break; fi
  sleep 1
done
if [ "$PORT_READY" -ne 1 ]; then
  sudo journalctl _SYSTEMD_INVOCATION_ID="$GNB_INVOCATION" --no-pager > "$RUN_DIR/gnb.log"
  printf 'gNB did not open RFsim port.\n' >&2
  exit 1
fi

sudo systemd-run --unit="$UE_UNIT" --service-type=exec --property=Restart=no --property=KillMode=mixed --working-directory="$BUILD" \
  taskset -c 0-7 "$BUILD/nr-uesoftmodem" -O "$UE_CONF" --rfsim --rfsimulator.serveraddr 127.0.0.1 \
  --phy-test --noS1 -r 106 --numerology 1 --band 78 -C 3319680000 --ue-rxgain 140 --ue-txgain 0 --T_stdout 1
UE_INVOCATION="$(sudo systemctl show "$UE_UNIT" -p InvocationID --value)"

sleep "$DURATION"
sudo systemctl stop "$UE_UNIT" 2>/dev/null || true
sleep 2
if [ -f "$BUILD/nrL1_stats.log" ]; then cp "$BUILD/nrL1_stats.log" "$RUN_DIR/nrL1_stats.log"; fi
sudo systemctl stop "$GNB_UNIT" 2>/dev/null || true
trap - EXIT INT TERM

sudo journalctl _SYSTEMD_INVOCATION_ID="$GNB_INVOCATION" --no-pager > "$RUN_DIR/gnb.log"
sudo journalctl _SYSTEMD_INVOCATION_ID="$UE_INVOCATION" --no-pager > "$RUN_DIR/nrue.log"
sudo chown -R ubuntu:ubuntu "$RUN_DIR"

SYNC_COUNT="$(grep -Ec 'In synch|Got synch|Synchronized|PBCH decoded' "$RUN_DIR/nrue.log" || true)"
CRC_LINES="$(grep -Ec 'ULSCH.*(CRC|round|DTX)|ULSCH total' "$RUN_DIR/gnb.log" || true)"
CSV_ROWS=0
if [ -f "$RUN_DIR/tb.csv" ]; then CSV_ROWS="$(($(wc -l < "$RUN_DIR/tb.csv") - 1))"; fi
{
  printf 'end_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'ue_sync_markers=%s\n' "$SYNC_COUNT"
  printf 'gnb_ulsch_log_lines=%s\n' "$CRC_LINES"
  printf 'csv_data_rows=%s\n' "$CSV_ROWS"
} >> "$RUN_DIR/meta.env"

if [ "$SYNC_COUNT" -eq 0 ]; then
  printf 'RUN_FAILED label=%s reason=no_ue_sync csv_rows=%s\n' "$LABEL" "$CSV_ROWS" >&2
  exit 1
fi
printf 'RUN_DONE label=%s sync_markers=%s csv_rows=%s\n' "$LABEL" "$SYNC_COUNT" "$CSV_ROWS"
