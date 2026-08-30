#!/usr/bin/env bash
set -euo pipefail
ROOT="${SIONNA_RK_ROOT:-$HOME/sionna-rk}"
BUILD="$ROOT/ext/openairinterface5g/cmake_targets/ran_build/build"
GNB_CONF="$ROOT/ext/openairinterface5g/ci-scripts/conf_files/gnb.sa.band78.106prb.rfsim.conf"
UE_CONF="$ROOT/ext/openairinterface5g/targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.conf"
GNB_UNIT=k3-rfsim-gnb.service
UE_UNIT=k3-rfsim-nrue.service
PORT=4043
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
active() { sudo systemctl is-active --quiet "$1"; }
show_processes() { ps -eo pid,ppid,pgid,sid,stat,comm,args | awk 'NR==1 || /nr-softmodem|nr-uesoftmodem|[g]db .*softmodem|[s]trace .*softmodem/'; }
preflight_common() {
  [ "$(uname -m)" = "riscv64" ] || die "This launcher is for RV64 K3; detected $(uname -m)."
  [ -x "$BUILD/nr-softmodem" ] || die "Missing $BUILD/nr-softmodem; build OAI first."
  [ -x "$BUILD/nr-uesoftmodem" ] || die "Missing $BUILD/nr-uesoftmodem; build OAI first."
  [ -r "$GNB_CONF" ] || die "Missing gNB config: $GNB_CONF"
  [ -r "$UE_CONF" ] || die "Missing UE config: $UE_CONF"
}
start_gnb() {
  preflight_common
  active "$GNB_UNIT" && die "$GNB_UNIT is already active."
  pgrep -x nr-softmodem >/dev/null && { show_processes; die "An unmanaged gNB exists. Stop its original terminal process first."; }
  pgrep -x nr-uesoftmodem >/dev/null && { show_processes; die "An unmanaged nrUE exists. Stop it first."; }
  ss -ltn "sport = :$PORT" 2>/dev/null | grep -q LISTEN && die "TCP $PORT is occupied."
  sudo systemctl reset-failed "$GNB_UNIT" 2>/dev/null || true
  sudo systemd-run --unit="$GNB_UNIT" --service-type=exec --property=Restart=no --property=KillMode=mixed --working-directory="$BUILD" "$BUILD/nr-softmodem" -O "$GNB_CONF" --rfsim --rfsimulator.serveraddr server --phy-test --noS1 --gNBs.[0].min_rxtxtime 6 --T_stdout 1
  sleep 2
  active "$GNB_UNIT" || { sudo journalctl -u "$GNB_UNIT" -n 100 --no-pager; die "gNB unit failed."; }
  echo "gNB is managed by systemd and survives terminal closure."
  echo "View logs: $0 gnb-log"
}
start_ue() {
  preflight_common
  active "$UE_UNIT" && die "$UE_UNIT is already active."
  active "$GNB_UNIT" || die "$GNB_UNIT is not active. Run: $0 gnb-start"
  ss -ltn "sport = :$PORT" 2>/dev/null | grep -q LISTEN || die "gNB is not listening on TCP $PORT."
  pgrep -x nr-uesoftmodem >/dev/null && { show_processes; die "An unmanaged nrUE exists. Stop it first."; }
  sudo systemctl reset-failed "$UE_UNIT" 2>/dev/null || true
  sudo systemd-run --unit="$UE_UNIT" --service-type=exec --property=Restart=no --property=KillMode=mixed --working-directory="$BUILD" "$BUILD/nr-uesoftmodem" -O "$UE_CONF" --rfsim --rfsimulator.serveraddr 127.0.0.1 --phy-test --noS1 -r 106 --numerology 1 --band 78 -C 3619200000 --ue-rxgain 140 --ue-txgain 0 --T_stdout 1
  sleep 2
  active "$UE_UNIT" || { sudo journalctl -u "$UE_UNIT" -n 120 --no-pager; die "nrUE unit failed."; }
  echo "nrUE is managed by systemd and survives terminal closure."
  echo "View logs: $0 ue-log"
}
case "${1:-}" in
  check|status)
    preflight_common
    echo "Architecture: $(uname -m)"; free -h
    sudo systemctl --no-pager --full status "$GNB_UNIT" "$UE_UNIT" 2>/dev/null || true
    echo "RFsimulator TCP port $PORT:"; ss -ltnp "sport = :$PORT" 2>/dev/null || true
    echo "Relevant processes:"; show_processes
    ;;
  gnb-start|gnb) start_gnb ;;
  ue-start|ue) start_ue ;;
  gnb-log) exec sudo journalctl -fu "$GNB_UNIT" -n 80 ;;
  ue-log) exec sudo journalctl -fu "$UE_UNIT" -n 120 ;;
  message-test) exec "$ROOT/scripts/k3-rfsim-message-test.sh" ;;
  ue-stop)
    sudo systemctl stop "$UE_UNIT"
    echo "nrUE stopped."
    ;;
  gnb-stop)
    active "$UE_UNIT" && die "Stop nrUE first: $0 ue-stop"
    sudo systemctl stop "$GNB_UNIT"
    echo "gNB stopped."
    ;;
  stop)
    sudo systemctl stop "$UE_UNIT" 2>/dev/null || true
    sudo systemctl stop "$GNB_UNIT" 2>/dev/null || true
    echo "K3 RFsimulator units stopped."
    ;;
  *)
    echo "Usage: $0 check|gnb-start|gnb-log|ue-start|ue-log|message-test|ue-stop|gnb-stop|stop"
    exit 2
    ;;
esac
