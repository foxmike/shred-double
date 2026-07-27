#!/usr/bin/env bash
set -euo pipefail

# Run pluginval's complete strict suite only for VST3 and CLAP.  LV2 and the
# JUCE Standalone target are deliberately load-smoked: pluginval's strict host
# checks are format-specific, while this still catches missing libraries and
# basic load/initialisation failures without running an unbounded host matrix.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/linux-relwithdebinfo}"
CONFIG="${CONFIG:-RelWithDebInfo}"
STRICTNESS="${PLUGINVAL_STRICTNESS:-8}"

if (($# > 0)); then BUILD_DIR="$1"; shift; fi
if (($# > 0)); then CONFIG="$1"; shift; fi
if (($# > 0)); then echo "usage: bash scripts/run_pluginval.sh [build-dir] [config]" >&2; exit 2; fi

find_pluginval() {
    local candidate
    if [[ -n "${PLUGINVAL:-}" && -x "${PLUGINVAL}" ]]; then printf '%s\n' "$PLUGINVAL"; return; fi
    for candidate in \
        "${ROOT_DIR}/pluginval" "${ROOT_DIR}/tools/pluginval" \
        "${HOME}/bin/pluginval" "${HOME}/.local/bin/pluginval" \
        "/usr/local/bin/pluginval" "/usr/bin/pluginval"; do
        [[ -x "$candidate" ]] && { printf '%s\n' "$candidate"; return; }
    done
    if command -v pluginval >/dev/null 2>&1; then command -v pluginval; return; fi
    echo "ERROR: pluginval not found. Set PLUGINVAL=/path/to/pluginval or install it in a common path/build tree." >&2
    return 1
}

PLUGINVAL_BIN="$(find_pluginval)"
ARTIFACT_DIR="${BUILD_DIR}/ShredDouble_artefacts/${CONFIG}"

find_artifact() {
    local format="$1" pattern="$2" found
    found="$(find "$ARTIFACT_DIR" -type f -o -type d 2>/dev/null | grep -E "$pattern" | head -n1 || true)"
    [[ -n "$found" ]] && printf '%s\n' "$found"
}

run_format() {
    local format="$1" artifact="$2" mode="$3" status=0
    if [[ ! -e "$artifact" ]]; then echo "${format}: FAIL (artifact missing: ${artifact})"; return 1; fi
    echo "${format}: testing ${artifact}"
    if [[ "$mode" == strict ]]; then
        "$PLUGINVAL_BIN" --strictness-level "$STRICTNESS" "$artifact" || status=$?
    else
        # Smoke mode intentionally avoids the full strict validation suite.
        "$PLUGINVAL_BIN" --validate-in-process "$artifact" || status=$?
    fi
    if ((status == 0)); then echo "${format}: PASS"; else echo "${format}: FAIL (exit ${status})"; fi
    return "$status"
}

vst3="${ARTIFACT_DIR}/VST3/ShredDouble.vst3"
clap="${ARTIFACT_DIR}/CLAP/ShredDouble.clap"
lv2="${ARTIFACT_DIR}/LV2/ShredDouble.lv2"
standalone="${ARTIFACT_DIR}/Standalone/ShredDouble"

fail=0
run_format VST3 "$vst3" strict || fail=1
run_format CLAP "$clap" strict || fail=1
run_format LV2 "$lv2" smoke || fail=1
run_format Standalone "$standalone" smoke || fail=1
exit "$fail"
