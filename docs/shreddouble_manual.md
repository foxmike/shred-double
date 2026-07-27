# ShredDouble — User Manual

**nSpace Systems** · Algorithmic Rhythm Doubler  
VST3 · CLAP · LV2 · Standalone

---

## What Is ShredDouble?

ShredDouble synthesises a convincing **double-tracked rhythm guitar** from a
single mono or stereo take. Instead of copying the input with a fixed detune
(which sounds like a chorus or flanger), it generates **virtual performers** —
up to four independent voices, each with its own human timing and pitch drift,
hard-panned, and phase-decorrelated. The result is the wide, thick sound of
multiple takes stacked in a metal or rock production, without requiring a
second performance.

**Key design principles:**

- **Stochastic humanisation, not LFO.** Each voice drifts via a
  mean-reverting random walk (Ornstein-Uhlenbeck process). There is no
  periodic modulation — the double *breathes* like a real take.
- **Decorrelated voices.** Each voice runs through independent allpass
  diffusion chains and subtle EQ tilts, preventing the comb-filter hollowness
  of simple delayed copies.
- **Mono-safe by construction.** A shared Linkwitz-Riley lowpass feeds the
  **same** mono lows into both channels. Under mono fold-down the low end
  stays centred and tight — no phase cancellation.

---

## Parameter Reference

Single `AudioProcessorValueTreeState`; all parameters are host-automatable
unless noted.

| Control | Param ID | Range | Default | Description |
|---|---|---|---|---|
| Voice Count | `voiceCount` | 2, 4 | 2 | Number of virtual performers. |
| Timing Spread | `timingSpread` | 0–15 ms | 6.0 ms | Per-voice timing wander excursion. |
| Pitch Spread | `pitchSpread` | 0–10 ¢ | 5.0 ¢ | Per-voice pitch wander excursion. |
| Humanisation Rate | `humanRate` | 0–100 % | 50 % | Correlation time of the random walk (300 → 100 ms). |
| Tightness | `tightness` | 0–100 % | 50 % | Master depth macro — scales both spreads toward zero at 100 %. |
| Low Mono Crossover | `monoXover` | 80–300 Hz | 180 Hz | Crossover frequency for the mono-safety lowpass. |
| Pan V1–V4 | `panV1`…`panV4` | −100–+100 % | −100, +100, −60, +60 | Per-voice pan (V3/V4 ignored at Voice Count=2). |
| Stereo Width | `width` | 0–200 % | 100 % | Master M/S width (lows unaffected — see mono-safety above). |
| Latency Mode | `latencyMode` | Studio, Live | Studio | Pitch-shifter buffer size (see Latency §). |
| Mix | `mix` | 0–100 % | 50 % | Dry/wet balance. |
| Output Trim | `outTrim` | −12–+12 dB | 0 dB | Master output gain. |
| Bypass | `bypass` | On/Off | Off | Host-managed true bypass. |

**Non-automatable state (stored with preset / session):**

- `masterSeed` (uint64) — the seed governing all random processes.
- Editor size / window geometry.

---

## Seeding & Re-roll Workflow

### Deterministic Render

Unlike a typical random modulation effect, ShredDouble is **reproducible**:

1. Every voice and axis (timing, pitch) draws from a PRNG stream seeded by a
   single **`masterSeed`** (a 64-bit unsigned integer stored in plugin state).
2. Same `masterSeed` + same parameters ⇒ **bit-identical** output on every
   playback. There is no hidden randomness — open the session tomorrow and the
   double sounds exactly the same.

This is critical for production use: you dial in *a* double you like, save the
session, and it stays that double forever.

### Re-roll

Click the **Re-roll** button on the GUI to draw a new `masterSeed`. This
generates an entirely new set of timing/pitch walks — effectively a "new take"
from the same virtual performers. The plugin marks its state as dirty so your
DAW prompts to save on close.

Re-rolling does **not** change any parameter value; only the random seed
changes. You can re-roll as many times as you like until the double sounds
right.

### What Re-roll Is Not

- Re-roll is **not** a host-automatable parameter by default. It is a GUI
  button only. The `masterSeed` is stored state, not a real-time automation
  target. (For power users: see the SDK notes below on exposing a momentary
  seed-trigger parameter — this is off by design choice to keep the automation
  lane list clean.)

---

## D3: Humanisation Rate & Tightness Remap

These two controls map to the **same physical axis** of the random walk but
do different jobs:

### Humanisation Rate → Correlation Time (Motion Speed)

Rate controls how *fast* the wander moves:

| Value | τ_c (correlation time) | Character |
|---|---|---|
| 0 (min) | ≈ 300 ms | Slow, glacial drift — the double barely moves |
| 50 (default) | ≈ 150 ms | Moderate — audible but natural breathing |
| 100 (max) | ≈ 100 ms | Lively, quick wander — most "human" jitter |

The underlying noise is normalised to unit variance so that changing Rate does
**not** change the excursion depth — only the speed.

### Tightness → Depth Macro

Tightness is a **master depth** that scales both `timingSpread` and
`pitchSpread` toward zero:

```
effectiveSpread = spread × (1 − tightness/100)
```

| Tightness | Effect |
|---|---|
| 0 % | Full wander — loosest, widest double |
| 50 % (default) | Half the spreads — moderate tightness |
| 100 % | Zero excursion — voices sit at their nominal delays (10–35 ms) with no pitch drift; the double is as "tight" as it gets |

**What Tightness is NOT:** It is not a second LFO rate, not an independent
correlation-time control, and not a "width" knob. It strictly reduces the
amount of timing and pitch excursion. For tighter metal riffing, turn
Tightness up; for loose, spacious doubling, turn it down.

### Quick Visual

```
   Rate (low) ──→ slow wander ──→ feels "locked"
   Rate (high) ──→ fast wander ──→ feels "live"

   Tightness 0% ──→ full spreads ──→ wide, loose
   Tightness 100% ──→ no spreads ──→ tight, locked
```

For a tight metal quad-track: **moderate Rate (40–50), high Tightness
(70–80 %)**. For a wide, lazy garage double: **high Rate (70+), low
Tightness (20 %)**.

---

## Latency & Latency Mode

ShredDouble uses dual-head granular pitch shifters with a fixed buffer per
voice. The buffer size determines both latency and voice quality:

| Mode | Buffer | Reported Latency | Use Case |
|---|---|---|---|
| **Studio** | 2048 samples | 1024 samples | Smoother grains, cleanest sustain. Use for mixing. |
| **Live** | 512 samples | 256 samples | Lower latency for tracking. Slightly more audible grain warble. |

The reported latency is exactly `L/2` (half the buffer). The dry path is
delayed by the same amount so dry and wet stay coherent. The drifting Haas
delays (10–35 ms) are **effect**, not latency — they are never PDC-compensated.

**Default:** Studio (1024 samples @ 48 kHz ≈ 21 ms).

---

## Automation Caveats

### voiceCount (2 ↔ 4)

Changing the voice count is handled by activating/deactivating pre-allocated
voice objects with a **short masked crossfade** (~10 ms). No audio-thread
allocation occurs. However:

- **It is not sample-accurate.** The crossfade may produce a brief audible
  transition (a few ms).
- **Do not automate voiceCount mid-take** in a tracking or live-recording
  context. Set it once per track and leave it.

If you must switch voice count, do so between takes or during a silence.

### latencyMode (Studio ↔ Live)

Switching latency mode triggers:
1. A buffer-size recompute (reports the new latency to the host).
2. `setLatencySamples()` and `updateHostDisplay()` — the host may pause
   processing briefly.
3. A masked crossfade similar to voice-count switching.

**Automation of `latencyMode` is technically possible but strongly
discouraged mid-render.** Some hosts (e.g., those that honour real-time PDC
changes) may handle it gracefully; others may glitch. Set it per-track and
leave it.

### General Advice

- `voiceCount` and `latencyMode` are included in the APVTS for preset recall
  convenience. Automating them is at your own risk.
- All other parameters (`timingSpread`, `pitchSpread`, `humanRate`,
  `tightness`, `monoXover`, `mix`, `panV*`, `width`, `outTrim`, `bypass`)
  are safe to automate and smooth naturally.

---

## LV2 Host Notes

### Port Metadata

When loaded in an LV2 host (e.g., Ardour, Jalv, Carla), ShredDouble exposes
the full APVTS parameter set as LV2 control ports with proper:
- Port names (human-readable, matching the GUI labels).
- Ranges (min/max/default) matching the spec table.
- Unit hints (ms, cents, Hz, dB, %) where applicable.

### Latency / PDC

- The `latencyMode` switch calls `setLatencySamples()` → LV2 hosts receive a
  latency port notification. The host should adjust its delay compensation
  accordingly.
- Some LV2 hosts (notably Jalv and early Ardour versions) may not re-read the
  latency port after the initial activation. If you change latency mode during
  a session, consider a re-instantiation (remove and re-add the plugin) to
  force the host to re-read the port.

### State Save / Recall

LV2 state extension is handled through JUCE's native LV2 wrapper. The full
plugin state — including `masterSeed` — is serialised via the LV2 State
extension. Preset files (`.lv2preset`) should capture all parameter values
and the seed.

### Plugin Validation

For testing LV2 builds, use `scripts/run_pluginval.sh` in **smoke** mode
(`--validate-in-process`). The strict validation suite is reserved for VST3
and CLAP; the LV2 host landscape is varied enough that a load/smoke check is
the CI-appropriate gate. Real-host validation (Ardour, Carla, Jalv) is a
manual step.

---

## Factory Presets

Five factory presets are embedded in the plugin. Each includes a fixed
`masterSeed` so they recall **identically** every time — useful both as
starting points and as regression fixtures.

### Tight Metal Quad

| Param | Value |
|---|---|
| Voice Count | 4 |
| Timing Spread | 4 ms |
| Pitch Spread | 4 ¢ |
| Humanisation Rate | 40 % |
| Tightness | 75 % |
| Crossover | 200 Hz |
| Pan | 100 L / 100 R / 60 L / 60 R |
| Mix | 60 % |
| Latency | Studio |

Chunky, tight quad-track for heavy rhythm work. High tightness keeps the
excursion small; the inner pair (60 % pan) adds density without collapsing
width.

### Wide Double (default-ish)

| Param | Value |
|---|---|
| Voice Count | 2 |
| Timing Spread | 6 ms |
| Pitch Spread | 5 ¢ |
| Humanisation Rate | 50 % |
| Tightness | 50 % |
| Crossover | 180 Hz |
| Pan | 100 L / 100 R |
| Mix | 50 % |
| Latency | Studio |

The default starting point. Balanced timing/pitch wander with moderate
tightness. Hard-panned L/R, 50 % wet — a versatile rock/metal double.

### Loose Garage Double

| Param | Value |
|---|---|
| Voice Count | 2 |
| Timing Spread | 12 ms |
| Pitch Spread | 8 ¢ |
| Humanisation Rate | 70 % |
| Tightness | 20 % |
| Crossover | 150 Hz |
| Pan | 100 L / 100 R |
| Mix | 55 % |
| Latency | Studio |

Wide and messy — evokes a live room, two takes, no grid. High Rate + low
Tightness maximises wander. Great for lo-fi, stoner rock, or anything that
shouldn't sound polished.

### Subtle Thickener

| Param | Value |
|---|---|
| Voice Count | 2 |
| Timing Spread | 3 ms |
| Pitch Spread | 3 ¢ |
| Humanisation Rate | 30 % |
| Tightness | 80 % |
| Crossover | 250 Hz |
| Pan | 100 L / 100 R |
| Mix | 35 % |
| Latency | Studio |

Minimal wander, high tightness, low mix. Adds a touch of width and density
without calling attention to itself. Correlation stays high — good for clean
or single-note lines where you want thickness, not separation.

### Live Quad Tracking

| Param | Value |
|---|---|
| Voice Count | 4 |
| Timing Spread | 5 ms |
| Pitch Spread | 4 ¢ |
| Humanisation Rate | 45 % |
| Tightness | 65 % |
| Crossover | 180 Hz |
| Pan | 100 L / 100 R / 60 L / 60 R |
| Mix | 50 % |
| Latency | **Live** (256 samples) |

Designed for low-latency tracking (256 samples reported). Four voices, moderate
tightness, 50 % wet. Record through it and commit the double tone to tape.

---

## Mono-Safe Design (for Technically Curious Users)

The mono-safety crossover works identically to ShredWidth's proven design:

1. The mono source `x(n)` (summed from stereo input, or L if mono input) is
   split through a shared Linkwitz-Riley lowpass at the crossover frequency.
2. The wet sum (all voices, hard-panned or otherwise) passes through a
   complementary highpass.
3. The LP output — purely from `x(n)` — is summed identically to **both**
   L and R output channels. Because it is the same signal, it cancels to zero
   in the Side channel under any M/S analysis.

**Result:** No matter how you set the per-voice pans, stereo width, or voice
count, everything below `monoXover` is dead-centre. Mono fold-down preserves
low-end punch and phase coherence. The highs spread freely per the pan
settings — that is where the width lives.

---

## Format Notes

| Format | Status | Notes |
|---|---|---|
| VST3 | ✓ Full support | |
| CLAP | ✓ Full support | via `clap-juce-extensions` |
| LV2 | ✓ Full support | Native JUCE LV2; see LV2 notes above |
| Standalone | ✓ Full support | JUCE standalone app (ASIO/CoreAudio/ALSA) |
| AU | ✗ Not supported | macOS AU is out of scope for this release |

---

## Support & Troubleshooting

- **No sound / silence?** Check `Mix` is not at 0 % and bypass is off. Verify
  the host's PDC is active — ShredDouble reports non-zero latency.
- **Voices sound too similar?** Increase Timing Spread and/or Pitch Spread.
  Verify Voice Count is set to 4. Try Re-roll to get a different seed.
- **Double sounds phasey or hollow?** This is usually a symptom of insufficient
  decorrelation. Increase Timing Spread, reduce Tightness, or try the Wide
  Double preset as a baseline.
- **Mono fold-down sounds bad?** Check the `monoXover` frequency — if it is
  set very low (below 100 Hz), the mono-safety band is too narrow. Default
  180 Hz is safe for most material.
- **Host reports wrong latency after mode switch?** Re-insert the plugin or
  restart the host's audio engine. Some hosts cache latency at plugin load and
  do not re-read `setLatencySamples()` mid-session.
- **Seed not persisting across session load?** Ensure your DAW saves plugin
  state. Some hosts in "offline" or "crash-recovery" mode may not call
  `getStateInformation()` correctly. No data loss is expected under normal
  operation.