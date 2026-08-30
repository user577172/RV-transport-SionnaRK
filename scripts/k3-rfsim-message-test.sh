#!/usr/bin/env bash
set -euo pipefail
GNB_UNIT=k3-rfsim-gnb.service
UE_UNIT=k3-rfsim-nrue.service
for unit in "$GNB_UNIT" "$UE_UNIT"; do
  systemctl is-active --quiet "$unit" || { echo "ERROR: $unit is not active." >&2; exit 1; }
done
for dev in oaitun_enb1 oaitun_ue1; do
  ip link show "$dev" >/dev/null 2>&1 || { echo "ERROR: interface $dev is missing." >&2; exit 1; }
done
old_all_rpf=$(sysctl -n net.ipv4.conf.all.rp_filter)
old_enb_rpf=$(sysctl -n net.ipv4.conf.oaitun_enb1.rp_filter)
old_ue_rpf=$(sysctl -n net.ipv4.conf.oaitun_ue1.rp_filter)
old_all_local=$(sysctl -n net.ipv4.conf.all.accept_local)
old_enb_local=$(sysctl -n net.ipv4.conf.oaitun_enb1.accept_local)
old_ue_local=$(sysctl -n net.ipv4.conf.oaitun_ue1.accept_local)
restore() {
  sudo sysctl -q -w net.ipv4.conf.all.rp_filter="$old_all_rpf"     net.ipv4.conf.oaitun_enb1.rp_filter="$old_enb_rpf"     net.ipv4.conf.oaitun_ue1.rp_filter="$old_ue_rpf"     net.ipv4.conf.all.accept_local="$old_all_local"     net.ipv4.conf.oaitun_enb1.accept_local="$old_enb_local"     net.ipv4.conf.oaitun_ue1.accept_local="$old_ue_local" >/dev/null 2>&1 || true
}
trap restore EXIT INT TERM
sudo sysctl -q -w net.ipv4.conf.all.rp_filter=0   net.ipv4.conf.oaitun_enb1.rp_filter=0   net.ipv4.conf.oaitun_ue1.rp_filter=0   net.ipv4.conf.all.accept_local=1   net.ipv4.conf.oaitun_enb1.accept_local=1   net.ipv4.conf.oaitun_ue1.accept_local=1
python3 - <<'PY'
import queue
import socket
import threading
import time

SO_BINDTODEVICE = socket.SO_BINDTODEVICE

def exchange(label, recv_ip, recv_port, send_ip, send_port, send_dev, payload):
    result = queue.Queue()
    ready = threading.Event()
    def receiver():
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.settimeout(10)
                s.bind((recv_ip, recv_port))
                ready.set()
                data, peer = s.recvfrom(2048)
                result.put((data, peer, None))
        except Exception as exc:
            result.put((None, None, exc))
    thread = threading.Thread(target=receiver, daemon=True)
    thread.start()
    if not ready.wait(2):
        raise RuntimeError(f"{label}: receiver did not start")
    time.sleep(0.3)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.setsockopt(socket.SOL_SOCKET, SO_BINDTODEVICE, send_dev.encode() + b"\0")
        s.bind((send_ip, send_port))
        sent = s.sendto(payload, (recv_ip, recv_port))
    thread.join(11)
    if thread.is_alive():
        raise RuntimeError(f"{label}: receive timeout")
    data, peer, error = result.get_nowait()
    if error:
        raise error
    if data != payload:
        raise RuntimeError(f"{label}: expected {payload!r}, received {data!r}")
    print(f"{label}: sent {sent} bytes; received {data.decode()} from {peer[0]}:{peer[1]}")

exchange("UE -> gNB", "10.0.1.1", 5001, "10.0.1.2", 5002, "oaitun_ue1", b"hello")
exchange("gNB -> UE", "10.0.1.2", 5002, "10.0.1.1", 5001, "oaitun_enb1", b"world")
print("PASS: bidirectional application messages crossed the OAI RFsim data bearer.")
PY
