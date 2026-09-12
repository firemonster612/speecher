#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
TCC_SEED="$(dirname "$0")/tcc_seed.py"
baseline_reset
seed_common_tcc || exit 1
case_begin T3-PASTE
export APP_BIN
launch_app || exit 1
"$RUNNER_TEMP/driver/bin/python" "$(dirname "$0")/t3_repro.py" "$CASE_DIR"
result=$?
case_evidence
stop_app
exit "$result"
