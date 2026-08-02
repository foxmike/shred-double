# ShredDouble — Operations Manual

## Plugin Overview
ShredDouble is an algorithmic rhythm doubler designed to synthesize a realistic double-tracked rhythm guitar sound from a single monophonic input. Unlike traditional chorus units that introduce periodic LFO pitch wobbles, ShredDouble emulates human timing and pitch inconsistency per voice using a slow random walk (Ornstein-Uhlenbeck process). This results in hard-panned, phase-decorrelated virtual performers that deliver the thickness of multiple physical takes. A low-frequency mono crossover ensures that sub-bass remains aligned and centered to prevent phase cancellation under mono fold-down.

## Intended Use
ShredDouble is intended to be inserted on a single mono or stereo guitar/bass track to create the effect of multiple virtual performers tracking in unison, hard-panned across the stereo field.

## Signal Flow
The internal audio signal path runs from input to output through the following sequential stages:
1. **Mono Extractor**: Combines incoming channels to form a mono signal `x(n)` (using `0.5 * (L + R)` or Left-only extraction).
2. **Crossover Filter Bank (`monoXover`)**: Splits the mono signal into Low and High frequency bands using a 2nd-order Linkwitz-Riley crossover filter.
   - *Low Band Path*: Routed directly and identically to both output channels, remaining centered.
   - *High Band Path (Wet)*: Fed into the virtual double-tracking engine.
3. **Ornstein-Uhlenbeck Random Walk Generator**: Calculates slowly drifting human timing and pitch offsets for up to four voices based on `humanRate`, `timingSpread`, and `pitchSpread`, scaled by the `tightness` factor. Governed deterministically by `masterSeed`.
4. **Fractional Delay & Pitch Shifters (per voice)**: Generates $V$ (2 or 4) virtual voices. Each voice is pitch-shifted and delayed dynamically according to its timing and pitch walks.
5. **Voice Decorrelation**: Each voice passes through a dedicated allpass diffusion chain and a mild EQ tilt to suppress comb filtering.
6. **Pan Matrix (`panV1` to `panV4`)**: Pans each virtual voice across the stereo field. (V3/V4 are ignored if Voice Count is 2).
7. **M/S Width Scaler (`width`)**: Controls output Mid/Side gain scaling applied post-crossover to high-band signals.
8. **Master Mix (`mix`)**: Crossfades between the dry input signal and the wet processed signal.
9. **Output Trim (`outTrim`)**: Applies final output gain.

## Controls and Parameters

| Control | Parameter ID | Range | Default | Unit | Description & DSP Reference |
|:---|:---|:---|:---|:---|:---|
| **Voice Count** | `voiceCount` | 2 (0) / 4 (1) | 2 (0) | - | Selects number of virtual performers to generate. (Fractional Delay & Pitch Shifters) |
| **Timing Spread** | `timingSpread` | 0.0 to 15.0 | 6.0 | ms | Maximum range of timing humanization wander per voice. (Ornstein-Uhlenbeck Walk) |
| **Pitch Spread** | `pitchSpread` | 0.0 to 10.0 | 5.0 | cents | Maximum range of pitch humanization wander per voice. (Ornstein-Uhlenbeck Walk) |
| **Humanisation Rate** | `humanRate` | 0.0% to 100.0% | 50.0% | % | Motion speed (correlation time $\tau_c$) of the humanization random walk. (Ornstein-Uhlenbeck Walk) |
| **Tightness** | `tightness` | 0.0% to 100.0% | 50.0% | % | Master humanization depth scaler. Reduces spreads toward zero as tightness approaches 100%. |
| **Low Mono Crossover** | `monoXover` | 80.0 to 300.0 | 180.0 | Hz | Crossover frequency below which all low frequencies are kept centered. (Crossover Filter Bank) |
| **Pan V1** | `panV1` | -100.0% to +100.0% | -100.0% | % | Stereo pan position for Virtual Voice 1. (Pan Matrix) |
| **Pan V2** | `panV2` | -100.0% to +100.0% | +100.0% | % | Stereo pan position for Virtual Voice 2. (Pan Matrix) |
| **Pan V3** | `panV3` | -100.0% to +100.0% | -60.0% | % | Stereo pan position for Virtual Voice 3 (ignored at 2 voices). (Pan Matrix) |
| **Pan V4** | `panV4` | -100.0% to +100.0% | +60.0% | % | Stereo pan position for Virtual Voice 4 (ignored at 2 voices). (Pan Matrix) |
| **Stereo Width** | `width` | 0.0% to 200.0% | 100.0% | % | Controls global M/S width scaling post-crossover. Lows remain centered. (M/S Width Scaler) |
| **Latency Mode** | `latencyMode` | Studio (0) / Live (1) | Studio (0) | - | Selects granular buffer size (2048 vs 512 samples) to trade grain smoothness for lower latency. |
| **Mix** | `mix` | 0.0% to 100.0% | 50.0% | % | Crossfades between dry input and wet processed signal. (Master Mix) |
| **Output Trim** | `outTrim` | -12.0 to +12.0 | 0.0 | dB | Adjusts final output gain. (Output Trim) |
| **Bypass** | `bypass` | Off (0.0) / On (1.0) | Off (0.0) | - | Bypasses all processing. |

### Non-Automatable State
- **`masterSeed`**: 64-bit unsigned integer governing all random processes, stored in preset/session state.

## Seeding & Re-roll Workflow
ShredDouble is fully deterministic and reproducible:
1. Every voice and axis (timing, pitch) draws from a PRNG stream seeded by a single `masterSeed`.
2. Same `masterSeed` + same parameters = bit-identical output on every playback.
3. Click the **Re-roll** button on the GUI to draw a new `masterSeed` to generate a "new take" from the virtual performers without changing parameter values. The plugin marks its state as dirty so the DAW prompts to save.
4. The `masterSeed` state value is persisted in session and preset data; it is not a host-automatable control. The Re-roll button generates and stores a new seed.

## Humanisation Rate & Tightness Behavior
Rate controls how fast the wander moves:
- **0% (Min)**: $\tau_c \approx 300$ ms. Slow, glacial drift.
- **50% (Default)**: $\tau_c \approx 150$ ms. Moderate, natural breathing.
- **100% (Max)**: $\tau_c \approx 100$ ms. Lively, quick human jitter.

The underlying noise is normalized to unit variance so changing Rate does not change excursion depth—only the speed.

Tightness is a master depth macro scaling both `timingSpread` and `pitchSpread` toward zero:
$$\text{effectiveSpread} = \text{spread} \times \left(1 - \frac{\text{tightness}}{100}\right)$$
- **0%**: Full wander excursion.
- **50% (Default)**: Spreads are scaled to 50%.
- **100%**: Zero excursion. Voices sit at their nominal delays (10–35 ms) with no pitch drift.

## Stereo and Mono Behavior
Low frequency audio is centered below the crossover point (`monoXover`, 80 to 300 Hz) using a Linkwitz-Riley low-pass filter. The LP output is routed identically to L and R channels, ensuring it cancels to zero in the Side channel. Highs are processed independently per channel and can be panned or widened as desired, remaining completely mono-safe.

## Latency and Lookahead (PDC)
Nominal latency is reported to the host based on the Latency Mode:
- **Studio Mode**: Buffer size $L = 2048$ samples. Reports **1024 samples** of latency ($\approx 21.3$ ms @ 48 kHz). Use for mixing and cleaner sustain.
- **Live Mode**: Buffer size $L = 512$ samples. Reports **256 samples** of latency ($\approx 5.3$ ms @ 48 kHz). Use for real-time tracking.

The dry path is delayed internally to align with the nominal shifting delay. Latency changes are reported to the host via `setLatencySamples()`.

## Bypass Behavior
Controlled via the `bypass` parameter. Bypasses all dynamic and delay processing, passing audio transparently.

## Automation Behavior
- All parameters except `masterSeed` are host-automatable.
- **Voice Count switching (2 ↔ 4)**: Newly enabled or disabled voices transition with a smooth gain crossfade. Voice Count is a structural choice rather than a sample-accurate modulation target.
- **Latency Mode switching**: Changing between Studio and Live reconfigures the shifter buffer, uses a short transition, reports the new latency, and requests a host display/PDC refresh.
- For Voice Count and Latency Mode changes, prefer a transport-stopped workflow so the host can apply structural and delay-compensation changes cleanly. If alignment does not refresh immediately, restart transport or reset the host audio engine.
- Other parameters (`timingSpread`, `pitchSpread`, `humanRate`, `tightness`, `monoXover`, `mix`, `panV*`, `width`, `outTrim`, `bypass`) are safe to automate and smooth naturally.

## Metering and Visual Feedback
- **Goniometer (`GoniometerComponent`)**: Renders a vectorscope phase display representing output stereo image width and distribution.
- **Correlation Meter (`CorrelationMeter`)**: Displays the L/R channel correlation coefficient from -1.0 to +1.0 to monitor mono compatibility.

## Presets and State Recall
Five factory presets are compiled into the binary with fixed seeds:

### 1. Tight Metal Quad
*Chunky 4-voice tracking setup for heavy rhythm work. High tightness keeps excursion small; the inner pair (60% pan) adds density.*
- **Voice Count**: 4
- **Timing Spread**: 4.0 ms
- **Pitch Spread**: 4.0 cents
- **Humanisation Rate**: 40%
- **Tightness**: 75%
- **Crossover**: 200 Hz
- **Pan**: -100% (V1), +100% (V2), -60% (V3), +60% (V4)
- **Width**: 100%
- **Mix**: 60%
- **Latency Mode**: Studio
- **Output Trim**: 0 dB

### 2. Wide Double (Default)
*Balanced timing/pitch wander with moderate tightness. Hard-panned L/R, 50% wet.*
- **Voice Count**: 2
- **Timing Spread**: 6.0 ms
- **Pitch Spread**: 5.0 cents
- **Humanisation Rate**: 50%
- **Tightness**: 50%
- **Crossover**: 180 Hz
- **Pan**: -100% (V1), +100% (V2)
- **Width**: 100%
- **Mix**: 50%
- **Latency Mode**: Studio
- **Output Trim**: 0 dB

### 3. Loose Garage Double
*Wide and messy, evoking two live takes. High rate and low tightness maximize wander.*
- **Voice Count**: 2
- **Timing Spread**: 12.0 ms
- **Pitch Spread**: 8.0 cents
- **Humanisation Rate**: 70%
- **Tightness**: 20%
- **Crossover**: 150 Hz
- **Pan**: -100% (V1), +100% (V2)
- **Width**: 100%
- **Mix**: 55%
- **Latency Mode**: Studio
- **Output Trim**: 0 dB

### 4. Subtle Thickener
*Minimal wander, high tightness, low mix. Adds subtle width and density without separation.*
- **Voice Count**: 2
- **Timing Spread**: 3.0 ms
- **Pitch Spread**: 3.0 cents
- **Humanisation Rate**: 30%
- **Tightness**: 80%
- **Crossover**: 250 Hz
- **Pan**: -100% (V1), +100% (V2)
- **Width**: 100%
- **Mix**: 35%
- **Latency Mode**: Studio
- **Output Trim**: 0 dB

### 5. Live Quad Tracking
*Designed for low-latency tracking. Four voices, moderate tightness, 50% wet.*
- **Voice Count**: 4
- **Timing Spread**: 5.0 ms
- **Pitch Spread**: 4.0 cents
- **Humanisation Rate**: 45%
- **Tightness**: 65%
- **Crossover**: 180 Hz
- **Pan**: -100% (V1), +100% (V2), -60% (V3), +60% (V4)
- **Width**: 100%
- **Mix**: 50%
- **Latency Mode**: Live
- **Output Trim**: 0 dB

## DAW and Format Notes
- **Supported Formats**: VST3, CLAP, LV2, Standalone.
- **Host PDC refresh**: Parameters map directly to host controls, and state serialization includes `masterSeed`. Hosts that cache latency metadata may require a transport restart, audio-engine reset, or plugin re-instantiation after a Latency Mode change.
