#!/bin/bash
#
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Integration test for Link Adaptation MAC plugin
#
# Starts the 5G system in rfsim mode, loads each link adaptation
# algorithm variant, runs traffic, and verifies the system remains
# stable. Tests all variants sequentially by restarting the gNB.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
CONFIG_NAME="${CONFIG_NAME:-testing}"

CONFIGS_DIR="$REPO_ROOT/config"
COMMON_CONFIG_DIR="$CONFIGS_DIR/common"
ENV_FILE="$CONFIGS_DIR/$CONFIG_NAME/.env"

IPERF_DURATION=10
IPERF_SERVER="192.168.72.135"

LA_VARIANTS=(
    "_olla"
    "_mcs_hist_olla"
    "_log"
    "_mcs_hist_log"
)

if [[ ! -f "$ENV_FILE" ]]; then
    echo "Error: .env file not found at $ENV_FILE"
    exit 1
fi

echo "Link Adaptation Integration Test (config: $CONFIG_NAME)"

cd "$COMMON_CONFIG_DIR"

cleanup() {
    RET=$?
    echo "Cleaning up..."
    docker compose --env-file "$ENV_FILE" down --remove-orphans 2>/dev/null || true

    if [ $RET -ne 0 ]; then
        echo "--- oai-gnb (last 80 lines) ---"
        docker logs oai-gnb --tail 80 2>&1 || true
        echo "--- oai-nr-ue (last 30 lines) ---"
        docker logs oai-nr-ue --tail 30 2>&1 || true
    fi
}
trap cleanup EXIT

docker compose --env-file "$ENV_FILE" down --remove-orphans 2>/dev/null || true

# Start 5G Core (shared across all variant tests)
echo "Starting 5G Core..."
docker compose --env-file "$ENV_FILE" up -d mysql oai-amf oai-smf oai-upf oai-ext-dn
sleep 20

test_variant() {
    local variant="$1"
    local lib_name="liblink_adaptation${variant}.so"

    echo ""
    echo "============================================"
    echo "Testing variant: link_adaptation${variant}"
    echo "============================================"

    # Stop gNB and UE from previous run
    docker compose --env-file "$ENV_FILE" stop oai-nr-ue oai-gnb 2>/dev/null || true
    docker compose --env-file "$ENV_FILE" rm -f oai-nr-ue oai-gnb 2>/dev/null || true
    sleep 5

    # Start gNB with this LA variant
    export GNB_EXTRA_OPTIONS="--loader.link_adaptation.shlibversion ${variant}"
    docker compose --env-file "$ENV_FILE" up -d oai-gnb
    sleep 15

    # Verify the LA library was loaded (check for init message or loader message)
    if docker logs oai-gnb 2>&1 | grep -qE "Initializing.*link adaptation|library ${lib_name} successfully loaded"; then
        echo "  PASS: ${lib_name} loaded"
    else
        echo "  FAIL: ${lib_name} NOT loaded"
        docker logs oai-gnb --tail 30
        return 1
    fi

    # Verify the BLER/SNR data tables loaded without errors.
    #
    # The plugins fail gracefully on a missing or malformed data file: instead
    # of crashing, they log an error and run in a degraded state. That means the
    # gNB can stay up and pass the checks below even though link adaptation is
    # effectively broken. Catch those data-load errors here so the test does NOT
    # pass when the data files are missing or corrupt.
    #
    # Matched messages (all emitted from the plugin init/table loaders):
    #   "<func>: open <path>.csv: <reason>"  -> fopen() failure on a data table
    #   "BLER FILE ERROR ..."                -> malformed data table (rows/cols)
    DATA_LOAD_ERRORS=$(docker logs oai-gnb 2>&1 \
        | grep -iE "open[[:space:]].*\.csv:|BLER FILE ERROR" || true)
    if [[ -n "$DATA_LOAD_ERRORS" ]]; then
        echo "  FAIL: ${lib_name} reported data-table load errors:"
        echo "$DATA_LOAD_ERRORS" | sed 's/^/        /'
        return 1
    fi
    echo "  PASS: data tables loaded without errors"

    # Verify gNB is still running
    if ! docker ps --format '{{.Names}}' | grep -q oai-gnb; then
        echo "  FAIL: gNB crashed after loading ${lib_name}"
        return 1
    fi
    echo "  PASS: gNB running"

    # Start UE
    docker compose --env-file "$ENV_FILE" up -d oai-nr-ue
    sleep 20

    # Check UE attachment
    UE_IP=$(docker exec oai-nr-ue ip addr show oaitun_ue1 2>/dev/null \
            | grep -oP 'inet \K[\d.]+' || echo "")
    if [[ -z "$UE_IP" ]]; then
        echo "  FAIL: UE has no IP"
        docker logs oai-nr-ue --tail 20
        return 1
    fi
    echo "  PASS: UE connected ($UE_IP)"

    # Run downlink traffic to exercise LA (MCS selection happens on DL scheduling)
    echo "  Running iperf3 downlink (${IPERF_DURATION}s)..."
    IPERF_OUTPUT=$(docker exec oai-nr-ue \
        iperf3 -t "$IPERF_DURATION" -i 1 -B "$UE_IP" -c "$IPERF_SERVER" -R 2>&1 || true)

    if echo "$IPERF_OUTPUT" | grep -q "receiver"; then
        RECV=$(echo "$IPERF_OUTPUT" | grep "receiver" | awk '{print $5, $6}')
        echo "  PASS: iperf3 received $RECV"
    else
        echo "  WARN: iperf3 reported no data (non-fatal)"
    fi

    # Verify both containers survived
    for c in oai-gnb oai-nr-ue; do
        if ! docker ps --format '{{.Names}}' | grep -q "$c"; then
            echo "  FAIL: $c crashed during traffic"
            return 1
        fi
    done
    echo "  PASS: system stable after traffic"

    return 0
}

PASSED=0
FAILED=0

for variant in "${LA_VARIANTS[@]}"; do
    if test_variant "$variant"; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
        echo "  FAIL: variant ${variant}"
    fi
done

echo ""
echo "============================================"
echo "Results: $PASSED passed, $FAILED failed (of ${#LA_VARIANTS[@]} variants)"
echo "============================================"

if [ "$FAILED" -gt 0 ]; then
    echo "FAIL: Link Adaptation Integration Test"
    exit 1
fi

echo "PASS: Link Adaptation Integration Test"
