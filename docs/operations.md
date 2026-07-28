# ShredDouble — Operations Guide

## Purpose
ShredDouble is an algorithmic rhythm doubler designed to synthesize a realistic double-tracked rhythm guitar sound from a single monophonic input. Unlike traditional chorus units that introduce periodic LFO pitch wobbles, ShredDouble emulates human timing and pitch inconsistency per voice using a slow random walk (Ornstein-Uhlenbeck process). This results in hard-panned, phase-decorrelated virtual performers that deliver the thickness of multiple physical takes. A low-frequency mono crossover ensures that sub-bass remains aligned and centered to prevent phase cancellation under mono fold-down.

## Signal Flow
The internal audio signal path runs from input to output through the following sequential stages:
1. **Mono Extractor**: Combines incoming channels to form a mono signal `x(n)` (using `0.5 * (L + R)` or Left-only extraction).
2. **Crossover Filter Bank (`monoXover`)**: Splits the mono signal into Low and High frequency bands using a 2nd-order Linkwitz-Riley crossover filter.
   - *Low Band Path*: Guided directly and identically to both output channels, remaining centered.
   - *High Band Path (Wet)*: Fed into the virtual double-tracking engine.
3. **Ornstein-Uhlenbeck Random Walk Generator**: Calculates slowly drifting human timing and pitch offsets for up to four voices based on `humanRate`, `timingSpread`, and `pitchSpread`, scaled by the `tightness` factor. Governed deterministically by `masterSeed`.
4. **Fractional Delay & Pitch Shifters (per voice)**: Generates $V$ (2 or 4) virtual voices. Each voice is pitch-shifted and delayed dynamically according to its timing and pitch walks.
5. **Voice Decorrelation**: Each voice passes through a dedicated allpass diffusion chain and a mild EQ tilt to suppress comb filtering.
6. **Pan Matrix (`panV1` to `panV4`)**: Pans each virtual voice across the stereo field. (V3/V4 are ignored if Voice Count is 2).
7. **M/S Width Scaler (`width`)**: Controls output Mid/Side gain scaling applied post-crossover to high-band signals.
8. **Master Mix (`mix`)**: Crossfades between the dry input signal and the wet processed signal.
9. **Output Trim (`outTrim`)**: Applies final output gain.

---

## Controls Reference

All parameters are located on a single main interface panel:

### Voice Count
- **Parameter ID:** `voiceCount`
- **Type:** choice
- **Range:** 2 (0), 4 (1), default 2 (0)
- **Unit:** none
- **What it does:** Selects the number of virtual performers to generate. Voices 3 and 4 are panned according to `panV3` and `panV4`. Reference DSP stage: Fractional Delay & Pitch Shifters.

### Timing Spread
- **Parameter ID:** `timingSpread`
- **Type:** float
- **Range:** 0.0 to 15.0 ms, default 6.0 ms
- **Unit:** ms
- **What it does:** Sets the maximum range of timing humanization wander per voice. Reference DSP stage: Ornstein-Uhlenbeck Random Walk Generator.

### Pitch Spread
- **Parameter ID:** `pitchSpread`
- **Type:** float
- **Range:** 0.0 to 10.0 cents, default 5.0 cents
- **Unit:** cents
- **What it does:** Sets the maximum range of pitch humanization wander per voice. Reference DSP stage: Ornstein-Uhlenbeck Random Walk Generator.

### Humanization Rate
- **Parameter ID:** `humanRate`
- **Type:** float
- **Range:** 0.0% to 100.0%, default 50.0%
- **Unit:** %
- **What it does:** Controls the motion speed (correlation time $\tau_c$) of the humanization random walk, varying between slow drift (0%) and fast jitter (100%). Reference DSP stage: Ornstein-Uhlenbeck Random Walk Generator.

### Tightness
- **Parameter ID:** `tightness`
- **Type:** float
- **Range:** 0.0% to 100.0%, default 50.0%
- **Unit:** %
- **What it does:** Master humanization depth scaler. Reduces both timing and pitch spreads toward zero as tightness approaches 100%. Reference DSP stage: Ornstein-Uhlenbeck Random Walk Generator.

### Low Mono Crossover
- **Parameter ID:** `monoXover`
- **Type:** float
- **Range:** 80.0 to 300.0 Hz, default 180.0 Hz
- **Unit:** Hz
- **What it does:** Sets the crossover frequency below which all low frequencies are kept centered. Reference DSP stage: Crossover Filter Bank.

### Pan V1 / V2 / V3 / V4
- **Parameter ID:** `panV1` / `panV2` / `panV3` / `panV4`
- **Type:** float
- **Range:** -100.0% (L) to +100.0% (R)
- **Defaults:** -100% (V1), +100% (V2), -60% (V3), +60% (V4)
- **Unit:** %
- **What it does:** Sets the static stereo position of each virtual voice. (V3/V4 parameters do nothing if Voice Count is 2). Reference DSP stage: Pan Matrix.

### Stereo Width
- **Parameter ID:** `width`
- **Type:** float
- **Range:** 0.0% to 200.0%, default 100.0%
- **Unit:** %
- **What it does:** Controls global M/S stereo width scaling. Low-frequency signals remain centered. Reference DSP stage: M/S Width Scaler.

### Latency Mode
- **Parameter ID:** `latencyMode`
- **Type:** choice
- **Range:** Studio (0), Live (1), default Studio (0)
- **Unit:** none
- **What it does:** Selects the granular buffer size. Studio uses 2048 samples for cleaner sustain; Live uses 512 samples to reduce tracking latency. Reference DSP stage: Fractional Delay & Pitch Shifters.

### Mix
- **Parameter ID:** `mix`
- **Type:** float
- **Range:** 0.0% to 100.0%, default 50.0%
- **Unit:** % wet
- **What it does:** Crossfades between the dry input signal and the wet processed signal. Reference DSP stage: Master Mix.

### Output Trim
- **Parameter ID:** `outTrim`
- **Type:** float
- **Range:** -12.0 to +12.0 dB, default 0.0 dB
- **Unit:** dB
- **What it does:** Adjusts the final output gain. Reference DSP stage: Output Trim.

### Bypass
- **Parameter ID:** `bypass`
- **Type:** bool
- **Range:** Off (0.0) to On (1.0), default Off (0.0)
- **Unit:** none
- **What it does:** Bypasses audio processing.

---

## Metering and Visualization
- **Goniometer (`GoniometerComponent`)**: Renders a vectorscope phase display representing the stereo image width and distribution.
- **Correlation Meter (`CorrelationMeter`)**: Displays the L/R channel correlation coefficient from -1.0 to +1.0 to monitor mono compatibility.

## Preset System
Five factory presets are compiled into the binary:
1. `Tight Metal Quad`: Chunky 4-voice tracking setup, timing spread at $4$ ms, pitch spread at $4$ cents, tightness at $75\%$.
2. `Wide Double`: Default 2-voice rock double, timing spread at $6$ ms, pitch spread at $5$ cents, tightness at $50\%$.
3. `Loose Garage Double`: Intentionally loose 2-voice setup, timing spread at $12$ ms, pitch spread at $8$ cents, tightness at $20\%$.
4. `Subtle Thickener`: Narrow 2-voice setup, timing spread at $3$ ms, pitch spread at $3$ cents, tightness at $80\%$, mix at $35\%$.
5. `Live Quad Tracking`: 4-voice tracking preset in low-latency Live Mode.
Presets contain a fixed `masterSeed` parameter which ensures they produce identical waveforms on recall. Users can click the **Re-roll** GUI button to generate a new 64-bit random seed for timing/pitch walks without changing preset parameter values. DAW state saving/loading uses standard APVTS XML serialization.

## Latency
- Studio Mode introduces **1024 samples** of latency ($\approx 21.3$ ms at 48 kHz).
- Live Mode introduces **256 samples** of latency ($\approx 5.3$ ms at 48 kHz).
- The dry path is delayed internally to align with the nominal shifting delay. Latency changes are reported to the host via `setLatencySamples()`.

## Supported Formats
- VST3, CLAP, LV2, Standalone.

## Known Limitations
- **LV2 Latency Updates**: Certain LV2 host environments do not dynamically process PDC updates when `latencyMode` changes, requiring session reload or plugin re-instantiation.
- **Live Automation Glitches**: Live automation of `latencyMode` or `voiceCount` causes brief audio crossfades (10 ms) and PDC recalculation, which can cause host glitches. It is recommended to set these parameters while stopped.
- **Master Seed Range**: The `masterSeed` property is not a real-time automatable parameter but a persistent state parameter.
