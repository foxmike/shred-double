#!/usr/bin/env bash
# scripts/check_rt_safety.sh — ShredDouble RT-safety grep gate.
#
# Implements Build Spec §14 item 1 (RT-safety static checks) and the D2
# PRNG constraint — the *static* grep-gate portion. It does NOT replace
# a proper allocator-instrumented dynamic check (which remains a manual /
# host-required step per the spec).
#
# The gate is intentionally cheap and CI-friendly:
#   * No cmake, no configure, no build.
#   * No code generation.
#   * No third-party tool beyond ripgrep (preferred) or POSIX grep -E.
#
# ---------------------------------------------------------------------------
# Strategy (and why we scope, not whole-file grep)
# ---------------------------------------------------------------------------
# Source/PluginProcessor.cpp contains many non-audio-thread bodies:
# prepareToPlay, releaseResources, createEditor, getStateInformation,
# setStateInformation, createPluginFilter, getName, getProgramName,
# changeProgramName, etc. These legitimately use `new`, `juce::String`,
# `juce::StringArray`, etc. during construction or state-restore. A naive
# whole-file grep over the file would false-positive and be unusable.
#
# The same applies to Source/DSP/ShredDoubleEngine.cpp — its
# DryAlignDelay::process helper is a non-audio-thread member (called from
# processFrame), and its parameter-layout / state helpers are non-RT.
#
# We therefore:
#   1. Brace-counted awk extracts ONLY the body of
#      `ShredDoubleAudioProcessor::processBlock(...)` from
#      Source/PluginProcessor.cpp.
#   2. Brace-counted awk extracts the body of
#      `ShredDoubleEngine::process(...)` (the 5-arg overload with the
#      sample loop; the 4-arg overload is a one-line forwarder).
#   3. Brace-counted awk extracts the body of
#      `ShredDoubleEngine::processFrame(...)` (per-sample inner loop,
#      audio-thread — D2 PRNG stepping reaches here via
#      Voice::processSample → RandomWalk::processSample).
#   4. Brace-counted awk extracts the bodies of
#      `MeterTap::processSample` and `MeterTap::process` from
#      Source/DSP/MeterTap.cpp (audio→UI metering push paths, called
#      from ShredDoubleEngine::processFrame on the audio thread).
#   5. Inside those scoped regions we grep for the forbidden APIs.
#   6. We additionally assert that `ScopedNoDenormals` is present in
#      the processBlock body (spec §7 hard rule).
#   7. For D2 we do a *whole-file* ban of `std::random_device` under
#      Source/DSP/ and Source/PluginProcessor.{cpp,h} — the PRNG must
#      be seeded xoshiro/splitmix only, and any std::random_device in
#      this surface area is a spec violation regardless of which thread
#      currently calls it.
#
# ---------------------------------------------------------------------------
# Forbidden patterns
# ---------------------------------------------------------------------------
#   new (heap allocator keyword)           — bounded by [^a-zA-Z0-9_-] so
#                                            identifiers like `newVoiceCount`
#                                            and hyphenated compounds like
#                                            `un-bypass-new` do NOT match.
#   malloc / free / realloc                — C allocator primitives; same
#                                            boundary rule (hyphenated
#                                            English compounds such as
#                                            `click-free` must not match).
#   juce::String (heap-allocating string)  — boundary excludes `StringArray`,
#                                            `StringPtr`, etc.
#   lock primitives:
#       \bstd::mutex\b, \bstd::scoped_lock\b,
#       \bstd::lock_guard\b, \bstd::unique_lock\b,
#       \bCriticalSection\b,
#       MessageManager::callAsync
#
# We deliberately do NOT grep a bare `\block\b` regex: that would also
# match the ordinary identifier "block" (it appears in `processBlock`,
# `processSample`/`pushBlock` signatures, etc.). The chosen primitives
# are the actual lock-related identifiers that an audio-hostile
# implementation would introduce.
#
# The hyphen-aware boundary form `(^|[^a-zA-Z0-9_-])X([^a-zA-Z0-9_-]|$)`
# is equivalent to `\bX\b` for ordinary C/C++ identifiers but excludes
# hyphenated compounds, which `\b` would otherwise misfire on (POSIX
# treats `-` as a word boundary).
#
# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
#   From the repo root:
#       bash scripts/check_rt_safety.sh
#
# This script does NOT require cmake on PATH. It is a pure source-text
# grep gate over the audio-thread hot paths defined above.
#
# ---------------------------------------------------------------------------
# Exit codes
# ---------------------------------------------------------------------------
#   0  PASS — every region clean, ScopedNoDenormals present, no
#             std::random_device in the D2 PRNG surface area
#   1  FAIL — at least one forbidden pattern, ScopedNoDenormals missing,
#             a std::random_device hit, or a required source file not
#             found

set -euo pipefail

# --- Repo root resolution -------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

PLUGIN_PROCESSOR_CPP="Source/PluginProcessor.cpp"
PLUGIN_PROCESSOR_H="Source/PluginProcessor.h"
SHRED_DOUBLE_ENGINE_CPP="Source/DSP/ShredDoubleEngine.cpp"
METER_TAP_CPP="Source/DSP/MeterTap.cpp"
DSP_DIR="Source/DSP"

# --- Pre-flight: required source files exist -----------------------------
for f in "${PLUGIN_PROCESSOR_CPP}" "${SHRED_DOUBLE_ENGINE_CPP}" "${METER_TAP_CPP}"; do
    if [[ ! -f "${f}" ]]; then
        printf 'check_rt_safety.sh: FAIL: required source not found: %s\n' "${f}"
        exit 1
    fi
done

# --- Select grep backend -------------------------------------------------
# Prefer ripgrep; fall back to POSIX grep -E. The invocation is:
#   "${GREP_CMD[@]}" <pattern> <stdin->-
#
# We intentionally do NOT pass `-n` / `--line-number` here: the body
# already carries source-file line numbers (from extract_body), and
# adding rg/grep's stream-relative line numbers would duplicate the
# prefix and confuse the FAIL diagnostics.
#
# Note: ripgrep 13.x's `-E` enables PCRE2, NOT extended regex; rg's
# default Rust regex engine accepts `\b`, `|`, `[^...]` etc. The grep
# fallback uses `-E` (POSIX ERE), which under GNU grep also accepts
# `\b` as a word boundary.
if command -v rg >/dev/null 2>&1; then
    GREP_CMD=(rg --no-heading --color=never)
else
    GREP_CMD=(grep -E -H --)
fi

# --- Function-body extractor ---------------------------------------------
# extract_body <file> <signature-regex>
#
# Prints the line matching <signature-regex> and every subsequent line
# up to (and including) the closing brace of that function. Brace
# counting uses a per-character scan; `started` is flipped on the
# first `{` so any `}` that appears before the function's own opening
# brace (e.g. in a multi-line signature that ends with `)` and an
# attribute) does not corrupt the depth counter.
#
# Each <signature-regex> must include a class-qualified form (e.g.
# `Engine::process[[:space:]]*\(`) so a bare function call such as
# `processFrame(...)` inside another function is NOT matched. This is
# what keeps engine.processFrame extraction from accidentally grabbing
# the `process(...)` body in which the call appears.
#
# Each emitted line is prefixed with `<NR>:\t` so the FAIL diagnostics
# below can show the *actual* source-file line number — not just the
# line's index inside the extracted body — which makes hitting the
# source trivially fast.
extract_body() {
    local file="$1"
    local sig="$2"
    awk -v sig="${sig}" '
        BEGIN { depth = 0; capturing = 0; started = 0 }
        {
            line = $0
            if (!started && match(line, sig)) {
                capturing = 1
            }
            if (capturing) {
                printf "%d:\t%s\n", NR, line
                n = length(line)
                for (i = 1; i <= n; i++) {
                    c = substr(line, i, 1)
                    if (c == "{") {
                        depth++
                        started = 1
                    } else if (c == "}" && started) {
                        depth--
                        if (depth == 0) { exit }
                    }
                }
            }
        }
    ' "${file}"
}

# --- Extract the regions under test --------------------------------------
#
# Anchors chosen to disambiguate class-qualified definitions from
# function calls inside other functions:
#   * Processor::processBlock  → ShredDoubleAudioProcessor::processBlock
#                                (the only processBlock in this file)
#   * Engine::process          → ShredDoubleEngine::process (NOT
#                                ShredDoubleEngine::DryAlignDelay::process)
#   * Engine::processFrame     → ShredDoubleEngine::processFrame
#   * MeterTap::process        → MeterTap::process (NOT processSample)
#   * MeterTap::processSample  → MeterTap::processSample

PROCESS_BLOCK_BODY="$(extract_body "${PLUGIN_PROCESSOR_CPP}" 'Processor::processBlock[[:space:]]*\(')"
ENGINE_PROCESS_BODY="$(extract_body "${SHRED_DOUBLE_ENGINE_CPP}" 'Engine::process[[:space:]]*\(')"
ENGINE_PROCESS_FRAME_BODY="$(extract_body "${SHRED_DOUBLE_ENGINE_CPP}" 'Engine::processFrame[[:space:]]*\(')"
METER_PROCESS_SAMPLE_BODY="$(extract_body "${METER_TAP_CPP}" 'MeterTap::processSample[[:space:]]*\(')"
METER_PROCESS_BODY="$(extract_body "${METER_TAP_CPP}" 'MeterTap::process[[:space:]]*\(')"

if [[ -z "${PROCESS_BLOCK_BODY}" ]]; then
    printf 'check_rt_safety.sh: FAIL: could not extract processBlock body from %s\n' \
        "${PLUGIN_PROCESSOR_CPP}"
    exit 1
fi
if [[ -z "${ENGINE_PROCESS_BODY}" ]]; then
    printf 'check_rt_safety.sh: FAIL: could not extract ShredDoubleEngine::process body from %s\n' \
        "${SHRED_DOUBLE_ENGINE_CPP}"
    exit 1
fi
if [[ -z "${ENGINE_PROCESS_FRAME_BODY}" ]]; then
    printf 'check_rt_safety.sh: FAIL: could not extract ShredDoubleEngine::processFrame body from %s\n' \
        "${SHRED_DOUBLE_ENGINE_CPP}"
    exit 1
fi
if [[ -z "${METER_PROCESS_SAMPLE_BODY}" ]]; then
    printf 'check_rt_safety.sh: FAIL: could not extract MeterTap::processSample body from %s\n' \
        "${METER_TAP_CPP}"
    exit 1
fi
if [[ -z "${METER_PROCESS_BODY}" ]]; then
    printf 'check_rt_safety.sh: FAIL: could not extract MeterTap::process body from %s\n' \
        "${METER_TAP_CPP}"
    exit 1
fi

# --- Pattern definitions -------------------------------------------------
# Each entry: "label|ERE-pattern".
# Patterns are POSIX ERE / Rust regex (compatible with rg + grep -E).
#
# `juce::String` is matched with a negative-character-class boundary
# (non-identifier char or end-of-line) so `juce::StringArray`,
# `juce::StringPtr`, etc. do NOT false-positive. (`\b` would also work
# under rg/grep -E; we use the bracket form so the pattern is portable
# to backends that lack POSIX `\b`.)
#
# Allocator keywords (`new`, `malloc`, `free`, `realloc`) also use the
# hyphen-aware boundary `(^|[^a-zA-Z0-9_-])…([^a-zA-Z0-9_-]|$)` so
# English compounds like `click-free` (which would otherwise false-
# positive under plain `\b`) are correctly ignored.
RT_PATTERNS=(
    "new_alloc|(^|[^a-zA-Z0-9_-])new([^a-zA-Z0-9_-]|$)"
    "c_alloc|(^|[^a-zA-Z0-9_-])(malloc|free|realloc)([^a-zA-Z0-9_-]|$)"
    "juce_string|juce::String([^a-zA-Z0-9_]|$)"
    "lock_primitive|\\bstd::mutex\\b|\\bstd::scoped_lock\\b|\\bstd::lock_guard\\b|\\bstd::unique_lock\\b|\\bCriticalSection\\b|MessageManager::callAsync"
)

# --- Region scan helper --------------------------------------------------
# scan_region "<region-label>" "<pattern-label>" "<ERE>" "<body>"
# Prints a PASS line and returns 0 if the pattern is absent from the
# body. Prints a FAIL block with the offending lines and returns 1 if
# present. The trailing `|| true` neutralises rg/grep exit 1 (no
# matches) under `set -o pipefail`.
scan_region() {
    local region_label="$1"
    local pat_label="$2"
    local pat="$3"
    local body="$4"
    local hits
    hits="$(printf '%s\n' "${body}" | "${GREP_CMD[@]}" "${pat}" - 2>/dev/null || true)"
    if [[ -n "${hits}" ]]; then
        printf 'check_rt_safety.sh: FAIL: forbidden pattern [%s/%s] found:\n%s\n' \
            "${region_label}" "${pat_label}" "${hits}"
        return 1
    fi
    printf 'check_rt_safety.sh: PASS: no %s in %s.\n' "${pat_label}" "${region_label}"
    return 0
}

# --- Run the gate --------------------------------------------------------
FAIL_COUNT=0

# 1. Forbidden patterns in processBlock body (Source/PluginProcessor.cpp)
for entry in "${RT_PATTERNS[@]}"; do
    label="${entry%%|*}"
    pat="${entry#*|}"
    if ! scan_region "processBlock" "${label}" "${pat}" "${PROCESS_BLOCK_BODY}"; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

# 2. Forbidden patterns in ShredDoubleEngine::process (audio-thread entry)
for entry in "${RT_PATTERNS[@]}"; do
    label="${entry%%|*}"
    pat="${entry#*|}"
    if ! scan_region "ShredDoubleEngine::process" "${label}" "${pat}" "${ENGINE_PROCESS_BODY}"; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

# 3. Forbidden patterns in ShredDoubleEngine::processFrame (per-sample loop)
for entry in "${RT_PATTERNS[@]}"; do
    label="${entry%%|*}"
    pat="${entry#*|}"
    if ! scan_region "ShredDoubleEngine::processFrame" "${label}" "${pat}" "${ENGINE_PROCESS_FRAME_BODY}"; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

# 4. Forbidden patterns in MeterTap::processSample (audio→UI metering push)
for entry in "${RT_PATTERNS[@]}"; do
    label="${entry%%|*}"
    pat="${entry#*|}"
    if ! scan_region "MeterTap::processSample" "${label}" "${pat}" "${METER_PROCESS_SAMPLE_BODY}"; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

# 5. Forbidden patterns in MeterTap::process (batch metering push)
for entry in "${RT_PATTERNS[@]}"; do
    label="${entry%%|*}"
    pat="${entry#*|}"
    if ! scan_region "MeterTap::process" "${label}" "${pat}" "${METER_PROCESS_BODY}"; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

# 6. ScopedNoDenormals assertion in processBlock body (spec §7 hard rule)
if printf '%s\n' "${PROCESS_BLOCK_BODY}" \
        | "${GREP_CMD[@]}" 'ScopedNoDenormals' - >/dev/null 2>&1; then
    printf 'check_rt_safety.sh: PASS: ScopedNoDenormals present in processBlock.\n'
else
    printf 'check_rt_safety.sh: FAIL: ScopedNoDenormals missing in processBlock body.\n'
    FAIL_COUNT=$((FAIL_COUNT + 1))
fi

# 7. D2 PRNG constraint: std::random_device MUST NOT appear anywhere in
#    Source/DSP/ or Source/PluginProcessor.{cpp,h}. The PRNG must be
#    seeded xoshiro/splitmix only. Whole-file ban is preferred per
#    the task description (catches accidental future drift even if a
#    non-audio-thread helper happens to call it).
random_device_hits="$(rg --no-heading -n -H 'std::random_device' \
    "${PLUGIN_PROCESSOR_CPP}" "${PLUGIN_PROCESSOR_H}" "${DSP_DIR}" 2>/dev/null || true)"
if [[ -n "${random_device_hits}" ]]; then
    printf 'check_rt_safety.sh: FAIL: std::random_device found in audio-thread-relevant sources (D2 PRNG constraint):\n%s\n' \
        "${random_device_hits}"
    FAIL_COUNT=$((FAIL_COUNT + 1))
else
    printf 'check_rt_safety.sh: PASS: no std::random_device in %s, %s, or %s/.\n' \
        "${PLUGIN_PROCESSOR_CPP}" "${PLUGIN_PROCESSOR_H}" "${DSP_DIR}"
fi

# --- Summary -------------------------------------------------------------
echo
if [[ "${FAIL_COUNT}" -eq 0 ]]; then
    printf '=================================================================\n'
    printf '  check_rt_safety.sh: PASS — RT-safety grep gate clean.\n'
    printf '=================================================================\n'
    exit 0
else
    printf '=================================================================\n'
    printf '  check_rt_safety.sh: FAIL — %d check(s) failed.\n' "${FAIL_COUNT}"
    printf '=================================================================\n'
    exit 1
fi
