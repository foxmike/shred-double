# ShredDouble — CI Verification Checklist

> Maps directly to Build Specification §14 (Verification & Test Plan). All
> items 1–11 are CI-runnable; ear-pass and full-host-matrix are explicitly
> **manual** and not gated.

---

## CI-Runnable Checks (gated)

### 1. RT-Safety (Build Spec §14.1)

**Script:** `scripts/check_rt_safety.sh`

- Static grep-gate: no `new`, `malloc`, `juce::String`, or lock primitives inside
  `processBlock` or any audio-thread path.
- `juce::ScopedNoDenormals` present at top of `processBlock`.
- Asserts PRNG never touches `std::random_device` on the audio thread.
- Does **not** replace a proper allocator-instrumented dynamic check (that
  remains a manual/host step per the spec). The script is brace-counted awk,
  not a whole-file grep, to avoid false-positives from non-audio-thread
  functions (`prepareToPlay`, `getStateInformation`, etc.).

**Invoke:**
```bash
bash scripts/check_rt_safety.sh
```

### 2. Seed-Determinism (Build Spec §14.2)

**Target:** `ShredDoubleTests` — `SeedingTests`

- Same `masterSeed` + same parameters over two runs ⇒ bit-identical output
  checksum (guards D2: reproducibility from seed).
- Different seeds ⇒ different output.
- Each voice/axis PRNG stream is derived via `splitmix64(masterSeed ⊕ voiceIndex ⊕ axisTag)`.
- On `prepareToPlay` the walk states reset to their means so the double starts
  settled, not mid-excursion.

**Run:**
```bash
cd build && ctest -R ShredDoubleTests -R Seeding
```

### 3. Bypass Null Test (Build Spec §14.3)

**Target:** `ShredDoubleTests` — `CorrectnessTests`

- True-bypass output == input to within floating-point precision (minus
  reported latency). Verifies the bypass logic is sample-exact.

**Run:**
```bash
cd build && ctest -R ShredDoubleTests -R Correctness
```

### 4. Mix=0 Passthrough (Build Spec §14.4)

**Target:** `ShredDoubleTests` — `CorrectnessTests`

- Output == dry input delayed by reported PDC (exact to integer-sample tolerance
  when alignment delay is integer). Verifies the dry/wet mix path and dry-align
  delay are correct.

**Run:**
```bash
cd build && ctest -R ShredDoubleTests -R Correctness
```

### 5. Mono-Lows Invariant (Build Spec §14.5)

**Target:** `ShredDoubleTests` — `CorrectnessTests`

- Sub-crossover sine (e.g. 100 Hz with Xover=180 Hz): assert `L(n) == R(n)` to
  floating-point epsilon at all width, voice-count, and pan settings. Proves the
  shared-LP + M/S crossover design holds in code. The dry mono source `x(n)`
  is low-passed once and summed identically to both channels, so low frequencies
  are always centered (mono-safe by construction).

**Run:**
```bash
cd build && ctest -R ShredDoubleTests -R Correctness
```

### 6. Walk Bounds (Build Spec §14.6)

**Target:** `ShredDoubleTests` — `DSPPrimitiveTests`

- Over a long run (many correlation times): every `τ_v ∈ [τ_min, τ_max]`
  (10–35 ms) and every `ΔC_v ∈ [−C_max, +C_max]` (±12 cents by default).
- Assert mean ≈ nominal delay (no DC drift — the Ornstein-Uhlenbeck process
  is mean-reverting).

**Run:**
```bash
cd build && ctest -R ShredDoubleTests -R DSPPrimitive
```

### 7. Correlation-Time Sanity (Build Spec §14.7)

**Target:** `ShredDoubleTests` — `DSPPrimitiveTests`

- Estimate the autocorrelation time of a captured walk and assert it tracks
  `τ_c(Rate)` within tolerance across the 100–300 ms range.
- Rate=0  ⇒ τ_c ≈ 300 ms; Rate=100 ⇒ τ_c ≈ 100 ms (with linear mapping per D3).

**Run:**
```bash
cd build && ctest -R ShredDoubleTests -R DSPPrimitive
```

### 8. Voice Decorrelation (Build Spec §14.8)

**Target:** `ShredDoubleTests` — `EngineTests`

- Cross-correlation between any two voice outputs stays below a defined
  threshold (proves the allpass + EQ-tilt decorrelation works).
- Summed spectrum has no deep periodic comb notches beyond tolerance.

**Run:**
```bash
cd build && ctest -R ShredDoubleTests -R Engine
```

### 9. Tightness Monotonicity (Build Spec §14.9)

**Target:** `ShredDoubleTests` — `DSPPrimitiveTests`

- RMS excursion of `τ_v` and `ΔC_v` decreases monotonically as Tightness
  sweeps 0 → 100% (proves the D3 depth macro: `(1 − tightness/100)` scales
  both timing and pitch spreads toward zero).

**Run:**
```bash
cd build && ctest -R ShredDoubleTests -R DSPPrimitive
```

### 10. Latency Report (Build Spec §14.10)

**Target:** `ShredDoubleTests` — `EngineTests`

- `getLatencySamples() == L/2` for the active mode:
  - Studio (L=2048) ⇒ 1024 samples.
  - Live (L=512) ⇒ 256 samples.
- Verify `setLatencySamples()` is called and `updateHostDisplay()` triggers
  on mode switch.

**Run:**
```bash
cd build && ctest -R ShredDoubleTests -R Engine
```

### 11. pluginval — VST3 / CLAP Strictness 8+; LV2 / Standalone Smoke
(Build Spec §14.11)

**Script:** `scripts/run_pluginval.sh`

- VST3 and CLAP tested at strictness level 8+ (full strict validation suite).
- LV2 and Standalone load-smoked (`--validate-in-process`) to catch missing
  libraries and basic initialisation failures. The LV2/Standalone host matrix
  is too format-specific for unbounded strict validation — smoke is the
  CI-appropriate gate.

**Invoke (after build):**
```bash
bash scripts/run_pluginval.sh [build-dir] [config]
```

**Environment:**
- `PLUGINVAL` — path to `pluginval` binary (auto-discovered if unset).
- `PLUGINVAL_STRICTNESS` — strictness level for VST3/CLAP (default `8`).

---

## Manual Checks (not CI-gated)

### Ear-Pass ("sounds like a real double-track")

The Build Spec §14 explicitly states: *"The test-oracle question — does it
sound like a real double-track? — still needs an ear pass."* This is a
subjective, perceptual evaluation and cannot be automated. Validate in your DAW:

1. Load a mono rhythm guitar track.
2. Cycle through the factory presets (see [shreddouble_manual.md](../shreddouble_manual.md)).
3. Toggle bypass — the wet signal should sound like a separate performance,
   not a detuned/chorused copy.
4. Verify no periodic wobble (LFO) is audible — the humanization should
   breathe organically.
5. Verify mono fold-down: lows stay centered; no phase cancellation in the
   low-mid range.

### Full Host Matrix

The `pluginval` check (item 11) covers load and basic validation. However,
different DAWs and LV2 hosts may exhibit subtle differences in:

- **PDC / latency reporting** — verify the host respects `setLatencySamples()`.
- **Parameter automation** — confirm all APVTS parameters appear in the host's
  automation lanes (including `voiceCount` and `latencyMode`, though automation
  mid-take is discouraged — see manual).
- **State save/recall** — confirm `masterSeed` persists correctly across
  session load. Same seed + params ⇒ identical render.
- **Re-roll button** — verify the GUI button correctly draws a new `masterSeed`
  and marks state dirty.
- **LV2 port metadata** — confirm port names, ranges, and units appear
  correctly in LV2 hosts (e.g., Jalv, Ardour, Carla).
- **CLAP threading** — verify the plugin behaves correctly under the CLAP
  threading model (if applicable to the host).

Tested DAWs (recommended): Reaper, Studio One, FL Studio, Cubase, Ardour,
Bitwig. This matrix is manual and per-release — the CI suite cannot
substitute for real-host testing.

---

## Quick-Reference: Test Targets & Scripts

| Check | Target / Script | Source File(s) |
|---|---|---|
| RT-safety (static) | `scripts/check_rt_safety.sh` | `Source/PluginProcessor.cpp`, `Source/DSP/ShredDoubleEngine.cpp` |
| Seed-determinism | `ShredDoubleTests` (SeedingTests) | `tests/SeedingTests.cpp` |
| Bypass, Mix=0, Mono-lows | `ShredDoubleTests` (CorrectnessTests) | `tests/CorrectnessTests.cpp` |
| Walk bounds, Corr-time, Tightness | `ShredDoubleTests` (DSPPrimitiveTests) | `tests/DSPPrimitiveTests.cpp` |
| Voice decorr, Latency | `ShredDoubleTests` (EngineTests) | `tests/EngineTests.cpp` |
| pluginval strict/smoke | `scripts/run_pluginval.sh` | — |

**CMake resolution** is handled by `scripts/build_all.sh` (not on `PATH`).
After a successful build, the test binary lives at
`build/linux-relwithdebinfo/ShredDoubleTests` (or your configured `BUILD_DIR`).
Use `cd build && ctest` to run all CI checks together, or target individual
test suites with `-R <regex>`.

> **Note:** `ctest` and the test binary are **not** on `PATH` — run from the
> build directory or provide the full path. The `build_all.sh` script handles
> CMake discovery (CLion-bundled only) and test invocation.