#!/usr/bin/env bash
#
# tests/uat/phase-20-broadcast-uat.sh — Phase 20 broadcast UAT harness.
#
# 5-minute 2-peer populated-server UAT against video.ninjamzap.com:2049 (or a
# local ninjamzap-server-docker on localhost:2049 if the public server is down).
# Verifies the queue-observability acceptance thresholds (R3 MF4):
#   - m_rawdata_sendq_high_water_mark        < 32 items
#   - m_rawdata_cs_contention_count
#       / m_rawdata_sendq_total_enqueues     < 1%
#   - m_encoder_input_drops                  == 0
#   - on_new_interval video block worst-case <= 200,000 ns
# AT EACH OF THREE PRESETS (Low / Medium / High).
#
# This script is the orchestrating harness — it builds the standalone with
# JAMWIDE_BUILD_TESTS=ON, syntax-checks the build artifact + opens the human-
# runnable procedure document, and per Plan 20-03 Task 4 (checkpoint:human-
# verify) the actual 5-minute broadcast is run by the human operator with
# two JamWide instances (or one JamWide + a NinjamZap mobile peer). The
# operator records the per-preset counter readouts + R4 M11 teardown
# observations in tests/uat/phase-20-broadcast-uat-report.md.
#
# Per feedback_uat_scope_redflags: the user-visible happy-path (2 peers, 5
# min broadcast, each preset) MUST occur regardless of harness automation
# level; this script is the procedure orchestrator, not a replacement for
# the actual run.
#
# Usage:
#   bash tests/uat/phase-20-broadcast-uat.sh [--build|--check|--full]
#     --build      Build the standalone target with JAMWIDE_BUILD_TESTS=ON.
#     --check      Run pre-flight checks (binary exists, server reachable).
#     --full       Open the procedure markdown in $PAGER for the operator.
#   (no flag)      Print the help banner.
#
# Acceptance thresholds (R3 MF4 — concrete; planner-locked):
#   HIGH_WATER_MAX=32                 (items; absolute count)
#   CONTENTION_RATIO_MAX_PCT=1.0      (percent; contention_count / total_enqueues)
#   ENCODER_INPUT_DROPS_MAX=0         (per-preset, end-of-run)
#   AUDIO_BUDGET_NS_MAX=200000        (200 µs worst-case on_new_interval video block)
#
# Exit codes:
#   0 = pre-flight checks pass; operator may begin the 5-min UAT.
#   1 = build failure.
#   2 = binary or test build artifacts missing.
#   3 = public server unreachable AND no local fallback configured.
#   4 = unknown flag.

set -euo pipefail

# ─── Configuration ─────────────────────────────────────────────────────────
readonly HIGH_WATER_MAX=32
readonly CONTENTION_RATIO_MAX_PCT="1.0"
readonly ENCODER_INPUT_DROPS_MAX=0
readonly AUDIO_BUDGET_NS_MAX=200000   # 200,000 ns = 200 µs

readonly PUBLIC_SERVER="video.ninjamzap.com"
readonly PUBLIC_PORT=2049
readonly LOCAL_FALLBACK_SERVER="localhost"
readonly LOCAL_FALLBACK_PORT=2049

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build-juce"
readonly PROCEDURE_MD="${SCRIPT_DIR}/phase-20-broadcast-uat-procedure.md"
readonly REPORT_MD="${SCRIPT_DIR}/phase-20-broadcast-uat-report.md"

# Banner colors (only when stdout is a tty).
if [[ -t 1 ]]; then
    C_RED=$'\033[31m'
    C_GREEN=$'\033[32m'
    C_YELLOW=$'\033[33m'
    C_CYAN=$'\033[36m'
    C_BOLD=$'\033[1m'
    C_RESET=$'\033[0m'
else
    C_RED='' ; C_GREEN='' ; C_YELLOW='' ; C_CYAN='' ; C_BOLD='' ; C_RESET=''
fi

print_banner() {
    cat <<EOF
${C_BOLD}=========================================================================${C_RESET}
${C_BOLD}Phase 20 — H.264 Broadcast UAT Harness${C_RESET}
${C_BOLD}=========================================================================${C_RESET}

This script orchestrates the 5-minute 2-peer broadcast UAT at each of
three presets (Low / Medium / High) against:
  ${C_CYAN}${PUBLIC_SERVER}:${PUBLIC_PORT}${C_RESET}
  (or ${LOCAL_FALLBACK_SERVER}:${LOCAL_FALLBACK_PORT} for a local Docker fallback)

Acceptance thresholds per R3 MF4:
  - m_rawdata_sendq_high_water_mark        < ${HIGH_WATER_MAX} items
  - m_rawdata_cs_contention_count
      / m_rawdata_sendq_total_enqueues     < ${CONTENTION_RATIO_MAX_PCT}%
  - m_encoder_input_drops                  == ${ENCODER_INPUT_DROPS_MAX} at end-of-run
  - on_new_interval video block worst-case <= ${AUDIO_BUDGET_NS_MAX} ns (200 µs)

Plus subjective: zero audible audio glitches during the 5-min Low-preset run.
Plus TSan dual-scope: zero races on the --tsan Medium-preset run.
Plus R4 M11 teardown verification (three paths):
  (1) Normal broadcast-off: END within one NINJAM interval (~3-8s)
  (2) Disconnect: END before connection terminates
  (3) Plugin destruction: best-effort (NOT a hard fail)

USAGE:
  ${C_BOLD}bash tests/uat/phase-20-broadcast-uat.sh --build${C_RESET}
      Build JamWideJuce_Standalone with JAMWIDE_BUILD_TESTS=ON.
  ${C_BOLD}bash tests/uat/phase-20-broadcast-uat.sh --check${C_RESET}
      Pre-flight: confirm binary exists + public server reachable.
  ${C_BOLD}bash tests/uat/phase-20-broadcast-uat.sh --full${C_RESET}
      Run --build, --check, then open the procedure document.

The actual 5-minute broadcast is human-driven; see:
  ${C_CYAN}${PROCEDURE_MD}${C_RESET}

Record the results at:
  ${C_CYAN}${REPORT_MD}${C_RESET}
EOF
}

# ─── Sub-commands ──────────────────────────────────────────────────────────

cmd_build() {
    echo "${C_BOLD}Building JamWideJuce_Standalone with JAMWIDE_BUILD_TESTS=ON...${C_RESET}"
    if [[ ! -d "${BUILD_DIR}" ]]; then
        echo "Configuring CMake (Ninja, Release, JAMWIDE_BUILD_TESTS=ON)..."
        cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
              -G Ninja \
              -DCMAKE_BUILD_TYPE=Release \
              -DJAMWIDE_BUILD_TESTS=ON \
              -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        || { echo "${C_RED}CMake configure failed${C_RESET}"; return 1; }
    fi
    cmake --build "${BUILD_DIR}" --target JamWideJuce_Standalone -- -j8 \
        || { echo "${C_RED}Build failed${C_RESET}"; return 1; }
    echo "${C_GREEN}Build OK.${C_RESET}"
    return 0
}

cmd_check() {
    local errors=0

    echo "${C_BOLD}Pre-flight checks...${C_RESET}"

    # 1. Standalone binary
    local STANDALONE_APP
    STANDALONE_APP="${BUILD_DIR}/JamWideJuce_artefacts/Release/Standalone/JamWide.app"
    if [[ -d "${STANDALONE_APP}" ]]; then
        echo "  ${C_GREEN}[OK]${C_RESET} Standalone app: ${STANDALONE_APP}"
    else
        echo "  ${C_RED}[MISSING]${C_RESET} Standalone app: ${STANDALONE_APP}"
        echo "    Run: bash $0 --build"
        errors=$((errors + 1))
    fi

    # 2. Procedure markdown
    if [[ -f "${PROCEDURE_MD}" ]]; then
        echo "  ${C_GREEN}[OK]${C_RESET} Procedure markdown: ${PROCEDURE_MD}"
    else
        echo "  ${C_RED}[MISSING]${C_RESET} Procedure markdown: ${PROCEDURE_MD}"
        errors=$((errors + 1))
    fi

    # 3. Public-server reachability (best-effort; not a hard fail).
    echo -n "  Checking ${PUBLIC_SERVER}:${PUBLIC_PORT} reachability... "
    if command -v nc >/dev/null 2>&1 \
       && nc -z -G 5 "${PUBLIC_SERVER}" "${PUBLIC_PORT}" 2>/dev/null; then
        echo "${C_GREEN}reachable${C_RESET}"
    else
        echo "${C_YELLOW}unreachable${C_RESET} (UAT can use ${LOCAL_FALLBACK_SERVER}:${LOCAL_FALLBACK_PORT} via ninjamzap-server-docker)"
    fi

    # 4. tcpdump availability (for the wire-format snoop check in the procedure).
    if command -v tcpdump >/dev/null 2>&1; then
        echo "  ${C_GREEN}[OK]${C_RESET} tcpdump available for wire-format check"
    else
        echo "  ${C_YELLOW}[NOTE]${C_RESET} tcpdump not on PATH (wire-format snoop step optional)"
    fi

    if (( errors > 0 )); then
        echo "${C_RED}Pre-flight FAILED (${errors} blocker(s)).${C_RESET}"
        return 2
    fi
    echo "${C_GREEN}Pre-flight PASS.${C_RESET}"
    return 0
}

cmd_full() {
    cmd_build || return $?
    cmd_check || return $?
    echo ""
    echo "${C_BOLD}Pre-flight complete.${C_RESET} Open the procedure document:"
    echo "  ${C_CYAN}${PROCEDURE_MD}${C_RESET}"
    if [[ -n "${PAGER:-}" ]]; then
        ${PAGER} "${PROCEDURE_MD}" || true
    elif command -v less >/dev/null 2>&1; then
        less "${PROCEDURE_MD}" || true
    else
        cat "${PROCEDURE_MD}"
    fi
    return 0
}

# ─── Acceptance-threshold helper ───────────────────────────────────────────
#
# Reads four space-separated values from a single line of stdin
# (high_water contention_count total_enqueues drops audio_budget_ns) and
# echoes PASS or FAIL with per-gate messages. Operator pipes the values
# from the lldb readout. This is the recommended scripted gate to embed
# in the UAT report's per-preset line.
#
# Usage:
#   echo "12 5 1500 0 145000" | bash tests/uat/phase-20-broadcast-uat.sh --assert
#
cmd_assert() {
    local high_water contention_count total_enqueues drops audio_budget_ns
    if ! read -r high_water contention_count total_enqueues drops audio_budget_ns; then
        echo "${C_RED}assert: expected 5 values on stdin: high_water contention_count total_enqueues drops audio_budget_ns${C_RESET}"
        return 4
    fi

    local result="PASS"
    local why=""

    # 1. High water
    if (( high_water >= HIGH_WATER_MAX )); then
        result="FAIL"
        why="${why}  - high_water=${high_water} (threshold < ${HIGH_WATER_MAX})\n"
    fi

    # 2. Contention ratio
    if (( total_enqueues > 0 )); then
        # ratio_pct = contention_count * 100 / total_enqueues  (with 4-digit precision)
        local ratio_pct
        ratio_pct=$(awk -v c="${contention_count}" -v t="${total_enqueues}" 'BEGIN { printf "%.4f", (c * 100.0 / t) }')
        # Compare against 1.0
        if awk "BEGIN { exit !(${ratio_pct} >= ${CONTENTION_RATIO_MAX_PCT}) }"; then
            result="FAIL"
            why="${why}  - contention_ratio=${ratio_pct}% (threshold < ${CONTENTION_RATIO_MAX_PCT}%)\n"
        fi
    fi

    # 3. Encoder drops
    if (( drops != ENCODER_INPUT_DROPS_MAX )); then
        result="FAIL"
        why="${why}  - encoder_input_drops=${drops} (threshold == ${ENCODER_INPUT_DROPS_MAX})\n"
    fi

    # 4. Audio-thread budget
    if (( audio_budget_ns > AUDIO_BUDGET_NS_MAX )); then
        result="FAIL"
        why="${why}  - audio_budget_ns=${audio_budget_ns} (threshold <= ${AUDIO_BUDGET_NS_MAX})\n"
    fi

    if [[ "${result}" == "PASS" ]]; then
        echo "${C_GREEN}PASS${C_RESET}  high_water=${high_water} contention=${contention_count}/${total_enqueues} drops=${drops} audio_ns=${audio_budget_ns}"
        return 0
    else
        echo "${C_RED}FAIL${C_RESET}  high_water=${high_water} contention=${contention_count}/${total_enqueues} drops=${drops} audio_ns=${audio_budget_ns}"
        printf "%b" "${why}"
        return 1
    fi
}

# ─── Dispatch ──────────────────────────────────────────────────────────────

case "${1:-}" in
    --build)  cmd_build ;;
    --check)  cmd_check ;;
    --full)   cmd_full  ;;
    --assert) cmd_assert ;;
    "" | -h | --help)
        print_banner
        ;;
    *)
        echo "Unknown flag: $1" >&2
        echo "" >&2
        print_banner >&2
        exit 4
        ;;
esac
