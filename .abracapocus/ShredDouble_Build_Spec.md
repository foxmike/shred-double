# ShredDouble — Complete Build Specification
**Algorithmic Rhythm Doubler**
nSpace Systems · C++20 / JUCE · VST3 · CLAP · LV2 · Standalone

---

## 1. Executive Summary

ShredDouble synthesizes convincing **double-tracked rhythm width from a single take**. Where ShredWidth widens *one lead note* with a fixed static detune, ShredDouble solves a different problem: it fabricates the illusion of **independent performances** — separate takes with human pitch and timing inconsistency, hard-panned — from one mono guitar. That is the sound metal rhythm tracks are built on, and it cannot be produced by static detune alone; you must *synthesize decorrelated performers*, not just spread the original.

The mechanism: generate `V` virtual voices (2 or 4), each hard-panned, each fed the mono source through an **independent** chain of (a) bounded random-walk pitch + timing humanization — **no LFO**, because an LFO reintroduces exactly the periodic wobble we avoid — (b) per-voice decorrelation (allpass diffusion + subtle EQ tilt) so summed voices are thick rather than hollow/phasey, and (c) the same shared-LP mono-safety crossover as ShredWidth so the low end stays centered and tight under mono fold-down.

Distinguishing constraints:

- **Human, not periodic.** Each voice drifts via a slow mean-reverting random walk (correlation time ~100–300 ms), so the double *breathes* like a second take instead of chorusing.
- **Decorrelated, not combed.** Independent allpass diffusion + spectral tilt per voice prevent the comb-filter hollowness you get from simply summing delayed copies.
- **Mono-safe by construction.** Same crossover trick as ShredWidth: only the highs spread, sub-lows stay dead-center.
- **Recallable performance.** The randomness is seeded and stored — reopen the session and you get *the* double you dialed in, bit-for-bit. A **Re-roll** button generates a new performance on demand.

This document specifies everything needed to build the plugin end-to-end. It reuses ShredWidth's `PitchShifter` and `MonoSafetyCrossover` verbatim; the new DSP is the humanization engine and the per-voice decorrelation.

---

## 2. Design Principles & Locked Decisions

Each is a **Decision** with a default; override any before plan kickoff — none block starting.

- **D1 — No LFO. Bounded, mean-reverting random walk only.** Voice motion is stochastic and non-periodic. Periodic modulation is the one thing this plugin exists to *not* do.
- **D2 — Deterministic given a seed (not fully deterministic).** Unlike ShredWidth (bit-identical from params alone), ShredDouble's output depends on PRNG state. We make it **reproducible** by seeding every voice/axis PRNG from a single `masterSeed` stored in plugin state. Same seed + same params ⇒ bit-identical render. This is what makes save/recall trustworthy for a doubler and keeps null-testing possible. **Re-roll** picks a new `masterSeed`.
- **D3 — Control-list redundancy resolved.** A mean-reverting random walk per axis has exactly **two** degrees of freedom: *excursion* (how far it wanders) and *correlation time* (how fast/smoothly it wanders). The source control list carries four time/depth controls — Timing Spread, Pitch Spread, Humanization Rate, **and** Tightness — but "Humanization Rate" and "Tightness (correlation-time)" are the same physical axis. Resolution: **Humanization Rate → correlation time / motion speed**; **Tightness → a master depth macro** that scales both spreads toward zero (0% = loose/wandering, 100% = locked-tight). Timing/Pitch Spread remain the per-axis excursion amounts. This yields four *orthogonal* controls that each do a distinct job and matches how a mix engineer actually reaches for a "tightness" knob. (Source defined Tightness as correlation-time; this remap is the deliberate change — flagged here to override.)
- **D4 — Per-voice nominal delays are pre-spread; the walk adds wander on top.** Even at zero spread the voices sit at distinct nominal delays (10–35 ms band), so the double is decorrelated in time before any randomness. The random walk supplies the *human* drift, not the base separation.
- **D5 — Random-walk generation runs at control rate, interpolated to audio rate.** The walk's correlation time is ≥100 ms, so generating new values every 32–64 samples and linearly interpolating is inaudible and cheap. Keeps CPU sane at V=4.
- **D6 — Buffers sized for V=4 at `prepareToPlay`; voice-count change never reallocates.** Switching 2↔4 activates/deactivates pre-allocated voice objects with a short masked crossfade. No audio-thread allocation ever.
- **D7 — Mono-safety by shared-LP, identical to ShredWidth.** Lows taken from the dry mono source and shared to both channels; only the summed highs spread. Reuse `MonoSafetyCrossover`.
- **D8 — No oversampling.** No nonlinearity anywhere (allpass, IIR, interpolation are all linear). Not needed.
- **D9 — Latency Mode carried over as parity feature.** Rhythm doublers are tracked live too. Studio/Live toggles the shared pitch-shifter buffer size exactly as in ShredWidth. Default Studio.

---

## 3. Signal Flow

```
                              ShredDouble signal path (V = 4 shown)
                              =====================================

  in (stereo or mono)
        │
        ▼
  ┌───────────────┐   x(n)  (mono source: 0.5*(L+R) if stereo, else L)
  │ Mono summing  │──────────────────────────────────────────────────────────┐
  └──────┬────────┘                                                            │ x → DRY
         │ x(n)  (fanned to every voice)                                       │ (aligned to PDC)
         ├───────────────┬───────────────┬───────────────┐                    │
         ▼               ▼               ▼               ▼                     │
   ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐                │
   │ Voice 1  │    │ Voice 2  │    │ Voice 3  │    │ Voice 4  │  each voice:    │
   │          │    │          │    │          │    │          │                │
   │ RW-walk  │    │ RW-walk  │    │ RW-walk  │    │ RW-walk  │  τ_v(n),ΔC_v(n) │
   │  ↓       │    │  ↓       │    │  ↓       │    │  ↓       │  (§4.3)         │
   │ Pitch    │    │ Pitch    │    │ Pitch    │    │ Pitch    │  shifter(ΔC_v)  │
   │ Shifter  │    │ Shifter  │    │ Shifter  │    │ Shifter  │  (§4.4)         │
   │  ↓       │    │  ↓       │    │  ↓       │    │  ↓       │                 │
   │ Delay τ_v│    │ Delay τ_v│    │ Delay τ_v│    │ Delay τ_v│  drifting       │
   │  ↓       │    │  ↓       │    │  ↓       │    │  ↓       │                 │
   │ Allpass  │    │ Allpass  │    │ Allpass  │    │ Allpass  │  decorrelation  │
   │ + EQtilt │    │ + EQtilt │    │ + EQtilt │    │ + EQtilt │  (§4.5)         │
   └────┬─────┘    └────┬─────┘    └────┬─────┘    └────┬─────┘                 │
        │ pan_1         │ pan_2         │ pan_3         │ pan_4                 │
        └──────┬────────┴───────┬───────┴───────────────┘                      │
               ▼                ▼                                              │
        wetL = Σ v·panL_v    wetR = Σ v·panR_v                                 │
               │                │                                              │
               ▼                ▼                                              │
        ┌──────────────┐  ┌──────────────┐   ┌──────────────┐                 │
        │  LR2 HP @fc  │  │  LR2 HP @fc  │   │  LR2 LP @fc  │◄─ shared LP from  │
        └──────┬───────┘  └──────┬───────┘   └──────┬───────┘   x(n) (centered)│
               │ hpL             │ hpR              │ lp                       │
               ▼                 ▼                  │                          │
        L_wet = lp + hpL   R_wet = lp + hpR ◄───────┘                          │
               │                                                               │
               ▼                                                               │
        ┌──────────────┐  M/S width (lows stay in Mid → centered)              │
        │ Width (opt.) │                                                       │
        └──────┬───────┘                                                       │
               ▼                                                               ▼
        ┌────────────────────────────────────────────────────────────────────────┐
        │  Dry/Wet Mix:  out = (1−mix)·dry + mix·wet                               │
        └────────────────────────────────────────────────────────────────────────┘
               │
               ▼
        out (stereo) ──► [tap L/R → Goniometer FIFO + Correlation estimator]
                     ──► [per-voice τ_v, ΔC_v atomics → Drift Display]
```

Everything runs sample-accurate inside `processBlock`. Thread crossings (metering, drift display) are all lock-free.

---

## 4. DSP Architecture (Detailed)

### 4.1 Input conditioning / mono source

Identical to ShredWidth §4.1. The effect is defined on one mono excitation `x(n)`:

- Stereo in: `x(n) = 0.5·(L_in + R_in)`.
- Mono in: `x(n) = L_in`.

Every voice is fed the *same* `x(n)`; decorrelation is synthesized downstream, not taken from stereo content. The preserved **dry** in the mix is the original input (stereo untouched at Mix=0).

### 4.2 Voice architecture overview

A voice is a self-contained object:

```
Voice = { RandomWalk delayWalk, RandomWalk pitchWalk,
          PitchShifter shifter,          // reused from ShredWidth (dual-head granular)
          DelayLineFrac delay,           // drifting Haas delay
          AllpassChain diffusion,        // per-voice coprime allpasses
          TiltFilter tilt,               // subtle per-voice EQ tilt
          float nominalDelayMs, panL, panR, seedDelay, seedPitch }
```

`V ∈ {2, 4}`. Objects for all 4 exist always (D6); at V=2 voices 3–4 are muted/ramped out.

### 4.3 Per-voice humanization — bounded random walk (the new DSP)

Each voice owns two independent stochastic processes: one for its delay, one for its pitch offset. Both are **mean-reverting** (Ornstein-Uhlenbeck-style) so they wander around a target without drifting to the clamp and sticking.

**Driving noise → low-passed, unit-variance.** Per voice `v`, per axis, draw white noise `η_v(n) ∼ 𝒩(0,1)` from a dedicated PRNG stream and low-pass it with a one-pole:

$$
u_v(n) = (1-\alpha)\,u_v(n-1) + \alpha\,\eta_v(n),
\qquad
\alpha = 1 - e^{-1/(\tau_c\, f_s)}
$$

`τ_c` is the **correlation time** (motion speed), set by **Humanization Rate** and constrained to the 100–300 ms band (Rate high → τ_c short → livelier wander; Rate low → τ_c long → glacial drift). Normalize `u_v` to unit variance so the spread controls read in real units regardless of `τ_c`:

$$
\tilde{u}_v(n) = u_v(n)\,\Big/\,\sqrt{\tfrac{\alpha}{2-\alpha}}
$$

**Map to delay and pitch.** With `T = Tightness ∈ [0,1]` scaling depth (D3):

$$
\tau_v(n) = \operatorname{clamp}\!\Big(\tau_{nom,v} + S_\tau\,(1-T)\,\tilde{u}^{\,d}_v(n),\; \tau_{min},\; \tau_{max}\Big)
$$

$$
\Delta C_v(n) = \operatorname{clamp}\!\Big(S_c\,(1-T)\,\tilde{u}^{\,p}_v(n),\; -C_{max},\; +C_{max}\Big)
$$

where `S_τ` = **Timing Spread** (ms), `S_c` = **Pitch Spread** (cents), `τ_nom,v` are the pre-spread per-voice nominal delays (D4), and the clamps are hard safety rails (`[τ_min,τ_max] = [10,35] ms`, `C_max` set slightly above Pitch Spread's max, e.g. ±12 cents).

**Nominal delay layout (D4)** across the 10–35 ms band, spread so voices never collide at zero randomness:

| V | Voice 1 | Voice 2 | Voice 3 | Voice 4 |
|---|---|---|---|---|
| 2 | 14 ms | 22 ms | — | — |
| 4 | 12 ms | 19 ms | 26 ms | 33 ms |

**Control-rate generation (D5):** advance the walk every `H = 32` samples (a "hop"), then linearly interpolate `τ_v` and `ΔC_v` to audio rate. Because `τ_c ≥ 100 ms ≫ H/f_s`, this is inaudible and cuts PRNG/one-pole cost by ~32×. The pitch shifter's `Δv` and the delay read-distance both consume the interpolated per-sample values.

**PRNG (D2, RT-safe):** each voice/axis stream is a `xoshiro256++` (or `xorshift128+`) seeded deterministically: `seed_{v,axis} = splitmix64(masterSeed ⊕ voiceIndex ⊕ axisTag)`. No locks, no allocation, fully reproducible.

### 4.4 Per-voice pitch shifter + interpolation

Each voice reuses ShredWidth's dual-head granular `PitchShifter` verbatim, driven by that voice's time-varying `ΔC_v(n)`:

$$
r_v = 2^{\Delta C_v/1200}, \quad \Delta v_v = 1 - r_v
$$

with the two-head `L/2`-offset read pointers, unity-gain raised-cosine crossfade, and 3rd-order (4-point) Lagrange fractional reads exactly as ShredWidth §4.2–4.3. The only difference: `ΔC_v` is now a slowly-drifting value from the random walk rather than a static parameter.

### 4.5 Per-voice decorrelation — allpass diffusion + EQ tilt

Summing delayed copies of one source combs. To get *thick* rather than *hollow*, each voice passes through a distinct phase-decorrelating chain **after** its Haas delay:

**A. Allpass diffusion chain.** `N_ap = 4` cascaded first-order Schroeder allpasses per voice:

$$
y(n) = -g\,x(n) + x(n-M) + g\,y(n-M)
$$

Magnitude-flat, phase-dispersive. Per-voice, per-stage delays `M` are **mutually coprime** and *distinct across voices* (e.g. voice-1 = {13, 29, 47, 71}, voice-2 = {17, 37, 53, 79}, … samples, scaled by sample rate to keep the *time* consistent), `g ≈ 0.6`. Coprime lengths avoid resonant reinforcement; distinct-per-voice lengths ensure the four voices don't share a phase signature. Keep total group delay small (a few ms) — this is decorrelation, not slap.

**B. Per-voice EQ tilt.** A gentle first-order tilt (low-shelf boost / high-shelf cut of equal small magnitude, pivoting ~700 Hz), **different per voice** (e.g. V1 +1.5 dB tilt bright, V2 −1.5 dB dark, V3 +0.8, V4 −0.8). Spectral decorrelation on top of the phase decorrelation, so the summed image is wide and full instead of phasey. Subtle by design — it should not be an obvious tone change.

The pairing (phase + spectral decorrelation) is what separates a convincing algorithmic double from an obvious "delayed copy."

### 4.6 Panning & voice summation

Each voice has independent pan (`panL_v, panR_v` via constant-power pan law). Defaults:

| V | Voice 1 | Voice 2 | Voice 3 | Voice 4 |
|---|---|---|---|---|
| 2 | 100% L | 100% R | — | — |
| 4 | 100% L | 100% R | 60% L | 60% R |

Classic metal quad-track is hard L/R; the inner pair at 60% adds density without collapsing the width. Summation into wet accumulators:

$$
\text{wetL}(n) = \sum_{v} \text{panL}_v \cdot \text{voice}_v(n),
\qquad
\text{wetR}(n) = \sum_{v} \text{panR}_v \cdot \text{voice}_v(n)
$$

### 4.7 Mono-safety crossover (shared-LP) + optional width

Identical structure to ShredWidth §4.5–4.7. The LR2 lowpass is taken from the **dry mono source** `x(n)` (single instance, shared to both channels), the LR2 highpass from each wet accumulator:

$$
L_{wet} = H_{LP}[x] + H_{HP}[\text{wetL}], \qquad
R_{wet} = H_{LP}[x] + H_{HP}[\text{wetR}]
$$

Shared `lp` ⇒ sub-band bit-identical L=R ⇒ centered and mono-safe. Optional master **Width** (M/S, 0–200%) applies afterward; by the same cancellation proof as ShredWidth §4.7 the shared `lp` contributes zero to Side, so width can never move the lows. Width is secondary here (per-voice pan is the primary width control) — included for parity, default 100%. LR2 default; LR4 available via the same `#define` build option.

### 4.8 Dry/wet mix

$$
\text{out}_{L/R}(n) = (1-\text{mix})\cdot d_{L/R}(n) + \text{mix}\cdot [L/R]_{wet}(n)
$$

- **Dry `d`** = original input, delayed by the reported PDC latency (§4.9) so dry and wet stay coherent at the crossover hand-off.
- Equal-gain crossfade (wet is derived from and largely correlated with dry).
- Default **50% wet** — a doubler is typically pushed further wet than a lead widener; verify against presets.

### 4.9 Latency & PDC

Same policy as ShredWidth (D4/§4.9 there):

| Mode | Buffer `L` | Reported latency | Character |
|---|---|---|---|
| Studio | 2048 | `L/2 = 1024` samples | Smoothest voices, cleanest sustain |
| Live | 512 | `L/2 = 256` samples | Lower latency for tracking; shorter grains, slightly more warble |

- Report the **nominal** shifter delay `L/2` via `setLatencySamples()`; align the dry path to it.
- The drifting Haas delays `τ_v` (10–35 ms) are **effect**, not latency — never PDC-compensated.
- All voices share the same buffer size, so the reported latency is single-valued. Mode change → recompute `L`, `setLatencySamples()`, `updateHostDisplay()`, masked ~10 ms crossfade.

### 4.10 Determinism, seeding & Re-roll (workflow-critical)

- **`masterSeed` (uint64)** is stored in plugin state. All voice/axis PRNG streams derive from it via `splitmix64`. Save/recall reproduces the exact performance bit-for-bit (D2).
- **Re-roll** (GUI button + optional host-visible momentary param) draws a fresh `masterSeed`, resets all walks, and marks state dirty — a new take on demand.
- On `prepareToPlay`, seed all streams from the stored `masterSeed`; reset walk states to their means so the double starts settled, not mid-excursion.
- This is the feature that makes an algorithmic doubler usable in production: the engineer dials in *a* double they like and it stays that double forever, until they choose to re-roll.

---

## 5. Parameters (APVTS)

Single `AudioProcessorValueTreeState`, stable string IDs, `parameterLayoutVersion` for migration.

| GUI Name | Param ID | Type | Range | Default | Skew / Taper | Unit | Smoothing |
|---|---|---|---|---|---|---|---|
| Voice Count | `voiceCount` | choice | {2, 4} | 2 | — | voices | special (masked) |
| Timing Spread | `timingSpread` | float | 0 … 15 | 6.0 | linear | ms | 50 ms |
| Pitch Spread | `pitchSpread` | float | 0 … 10 | 5.0 | linear | cents | 50 ms |
| Humanization Rate | `humanRate` | float | 0 … 100 | 50 | linear → τ_c 300→100 ms | % | block-rate |
| Tightness | `tightness` | float | 0 … 100 | 50 | linear | % (depth macro) | 50 ms |
| Low Mono Crossover | `monoXover` | float | 80 … 300 | 180 | log (skew ≈ 0.35) | Hz | block-rate coeff |
| Mix | `mix` | float | 0 … 100 | 50 | linear | % wet | 20 ms |
| Pan V1 | `panV1` | float | −100 … 100 | −100 | linear | % | 20 ms |
| Pan V2 | `panV2` | float | −100 … 100 | +100 | linear | % | 20 ms |
| Pan V3 | `panV3` | float | −100 … 100 | −60 | linear | % | 20 ms |
| Pan V4 | `panV4` | float | −100 … 100 | +60 | linear | % | 20 ms |
| Stereo Width | `width` | float | 0 … 200 | 100 | linear | % | 20 ms |
| Latency Mode | `latencyMode` | choice | {Studio, Live} | Studio | — | — | special (§4.9) |
| Output Trim | `outTrim` | float | −12 … +12 | 0.0 | linear | dB | 20 ms |
| Bypass | `bypass` | bool | — | false | — | — | host-managed |

Non-parameter state (stored in the value tree, not host-automatable):
- `masterSeed` (uint64) — the performance seed (§4.10).
- Editor size.

Notes:
- **Humanization Rate** maps linearly to correlation time `τ_c ∈ [300 ms (0), 100 ms (100)]` (D3).
- **Tightness** scales `(1 − tightness/100)` onto both spreads (D3). At 100% the double is nearly locked; at 0% maximally loose.
- Pan V3/V4 are ignored when Voice Count = 2 (voices muted).
- **Stereo Width** and **Output Trim** are parity additions (like ShredWidth), default no-op.

---

## 6. Parameter Smoothing & Coefficient Updates

- `SmoothedValue<float, Linear>` for gain-like params (`mix`, panning, `width`, `outTrim`, spreads).
- Spread changes ramp (50 ms) so the excursion envelope doesn't step.
- `humanRate`/`tightness`: recompute `α` and depth scalars per block (dirty flag), not per sample.
- Crossover coeffs: recompute on `monoXover` change only, at block start.
- Random walk itself is the deliberate modulation — it is *not* smoothed away; it is generated at hop rate and linearly interpolated (D5).
- All smoothers `reset(sampleRate, ramp)` in `prepareToPlay`.

---

## 7. Real-Time Safety (`processBlock` contract)

Same hard rules as ShredWidth §7, plus PRNG concerns:

- `juce::ScopedNoDenormals` at top of `processBlock`.
- **Zero heap allocation** on the audio thread. All voice objects, buffers, allpass lines, delay lines, dry-align delay allocated in `prepareToPlay` sized for **V=4 / Studio** worst case.
- **PRNG is RT-safe:** stack/POD state, no locks, no `std::random_device` on the audio thread. Seed only in `prepareToPlay` / on Re-roll (both non-audio-thread or guarded).
- **No locks.** Metering (goniometer pairs) via `juce::AbstractFifo`; correlation `ρ` and per-voice `τ_v`/`ΔC_v` via `std::atomic<float>`.
- No `String`, logging, GUI, or file I/O on the audio thread.
- Voice-count and Latency-Mode switches repoint pre-allocated storage with a masked crossfade — no allocation (D6).

---

## 8. Memory & Buffer Sizing

Allocated once in `prepareToPlay(sampleRate, maxBlock)` for the worst case (V=4, Studio):

- 4 × pitch circular buffers, `2048 + 4` floats each.
- 4 × drifting delay lines, `ceil(35 ms × maxSR) + Lagrange guard` each (≈ 6800 + 4 @ 192 kHz).
- 4 × allpass chains (4 stages each; longest stage ≈ 100 samples → trivial).
- 4 × tilt-filter states (tiny).
- 1 × shared crossover (small).
- 1 × dry-alignment delay (`1024` samples Studio × channels).
- Metering FIFO (~4096 stereo frames), decimated writes.
- PRNG state: 8 streams (4 voices × 2 axes), POD, negligible.

Steady-state audio-thread footprint is well under 1 MB and never grows.

---

## 9. GUI & Visualizations

### 9.1 Layout

Single dark window, custom `LookAndFeel`. Regions:

- **Header:** name, preset selector, A/B, **Re-roll** button, bypass LED.
- **Voice section:** Voice Count segmented (2/4); a small per-voice strip (Pan) for the active voices.
- **Humanization section:** Timing Spread, Pitch Spread, Humanization Rate, Tightness.
- **Global section:** Low Mono Crossover, Stereo Width, Mix, Output Trim, Latency Mode.
- **Metering column:** Goniometer (top), Correlation Meter (below), **Per-Voice Drift Display** (bottom, the signature ShredDouble readout).
- Resizable, size stored in state.

### 9.2 Goniometer

Identical to ShredWidth §9.2 — output L/R rotated 45° (`x = (R−L)/√2`, `y = (L+R)/√2`), phosphor decay, fed from the lock-free FIFO. Vertical = mono, healthy fat vertical spread = good double, horizontal smear = anti-correlation danger.

### 9.3 Correlation meter

Identical to ShredWidth §9.3 — audio-thread one-pole Pearson correlation

$$
\rho = \frac{\overline{L R}}{\sqrt{\overline{L^2}\,\overline{R^2}} + \varepsilon}, \quad \rho \in [-1,1]
$$

(~300 ms time constant), crossed to UI as a single `std::atomic<float>`. Green/yellow/red bar with a toward-`−1` warning flash. For a doubler you expect `ρ` to sit low-positive/near-zero (wide, decorrelated) without diving to `−1`.

### 9.4 Per-voice timing / pitch drift display (new)

The signature diagnostic. A 2-D field:

- **X-axis** = each voice's current timing offset relative to its nominal (ms).
- **Y-axis** = each voice's current pitch offset (cents).
- One color-coded marker per active voice, each trailing a short fading history so you *see* the four performers breathing independently.
- Optional companion: two thin strip charts (timing and pitch vs. time) if the scatter is too abstract for quick reads — build-time selectable.

Fed by `2V` `std::atomic<float>` values (`τ_v`, `ΔC_v`) written on the audio thread at hop rate, polled by the UI `Timer` at 30–60 Hz. The walks are slow, so atomics (no FIFO) are sufficient and cheap. This display is also the fastest way to sanity-check that Tightness/Rate/Spread are doing what the ear expects.

### 9.5 Audio → UI transport

- Goniometer: `juce::AbstractFifo` (~4096 stereo frames), decimated pushes, UI drains at frame rate.
- Correlation `ρ` and per-voice `τ_v`/`ΔC_v`: `std::atomic<float>`.
- Never block/lock/allocate in the push path.

### 9.6 LookAndFeel

Reuse the `CustomLookAndFeel` from AuralMaximizerPro/ShredWidth. **Implement `drawPopupMenuItem` with the `LookAndFeel_V4` individual-parameter signature** (`isSeparator, isActive, isHighlighted, isTicked, hasSubMenu, text, shortcutKeyText, icon, textColour`), **not** the newer `PopupMenu::Item&` overload — that overload is absent in the pinned JUCE version. Carry the corrected version across from the start.

---

## 10. Plugin Formats & Connectivity

Same as ShredWidth §10:

- **Targets:** VST3, CLAP (`clap-juce-extensions`), LV2 (native JUCE, confirm pinned-version support), Standalone. AU optional if macOS in scope — flag.
- All APVTS params host-automatable; `latencyMode`/`voiceCount`/`bypass` included (latency- or voice-count automation mid-render discouraged in the manual).
- State: full APVTS save/restore **including `masterSeed`** via `getStateInformation`/`setStateInformation`; `parameterLayoutVersion` for migration.
- `setLatencySamples` verified per format.

---

## 11. Bus Layout & Channel Handling

Same as ShredWidth §11:

- Supported: **mono→stereo**, **stereo→stereo**. Reject others in `isBusesLayoutSupported`.
- Internal processing always stereo out; mono input summed to the single source `x`.
- Sample-rate agnostic; ms→sample, Hz→coeff, and allpass stage times use live `sampleRate`. Re-`prepareToPlay` on rate change reallocates delay lines (not on audio thread).

---

## 12. Presets

Factory bank doubles as regression fixtures (fixed `masterSeed` per preset so they recall identically):

- **Tight Metal Quad** — V4, Timing 4 ms, Pitch 4 c, Rate 40, Tightness 75%, Xover 200 Hz, Pan 100/100/60/60, Mix 60%.
- **Wide Double (default-ish)** — V2, Timing 6 ms, Pitch 5 c, Rate 50, Tightness 50%, 180 Hz, Pan 100/100, Mix 50%.
- **Loose Garage Double** — V2, Timing 12 ms, Pitch 8 c, Rate 70, Tightness 20%, 150 Hz, Mix 55%.
- **Subtle Thickener** — V2, Timing 3 ms, Pitch 3 c, Rate 30, Tightness 80%, 250 Hz, Mix 35% (correlation stays high, mono-safe).
- **Live Quad Tracking** — Live mode, V4, Timing 5 ms, Pitch 4 c, Rate 45, Tightness 65%, Mix 50% (low latency).

Stored as APVTS XML (seed included), embedded via `BinaryData`.

---

## 13. File & Class Structure

Matches the nSpace `Source/` convention; reuses ShredWidth DSP classes verbatim:

```
shred-double/                          (repo: foxmike/shred-double)
├── CMakeLists.txt
├── scripts/build_all.sh
├── JUCE/                              (git submodule, same pinned tag as ShredWidth)
└── Source/
    ├── PluginProcessor.h / .cpp       APVTS, processBlock, voice mgmt, PDC, seeding, bus layout
    ├── PluginEditor.h  / .cpp         layout, timer, metering + drift-display wiring, Re-roll
    ├── DSP/
    │   ├── PitchShifter.h  / .cpp     REUSED from ShredWidth (dual-head granular + Lagrange)
    │   ├── DelayLineFrac.h / .cpp     REUSED from ShredWidth (fractional delay)
    │   ├── MonoSafetyCrossover.h/.cpp REUSED from ShredWidth (LR2 shared-LP)
    │   ├── WidthProcessor.h / .cpp    REUSED from ShredWidth (M/S width)
    │   ├── RandomWalk.h    / .cpp     NEW: seeded PRNG + one-pole OU walk (§4.3)
    │   ├── AllpassChain.h  / .cpp     NEW: per-voice coprime Schroeder allpasses (§4.5A)
    │   ├── TiltFilter.h    / .cpp     NEW: per-voice subtle EQ tilt (§4.5B)
    │   ├── Voice.h         / .cpp     NEW: one virtual performer (walks+shifter+delay+decorr+pan)
    │   └── ShredDoubleEngine.h/.cpp   NEW: owns V voices, crossover, width, mix, metering, seed
    └── GUI/
        ├── CustomLookAndFeel.h / .cpp (corrected drawPopupMenuItem signature)
        ├── GoniometerComponent.h/.cpp REUSED from ShredWidth
        ├── CorrelationMeter.h  / .cpp REUSED from ShredWidth
        ├── DriftDisplay.h      / .cpp NEW: per-voice timing/pitch scatter + trails (§9.4)
        └── RotarySlider.h      / .cpp (if factored out)
```

`ShredDoubleEngine` owns the voice array, the shared `MonoSafetyCrossover`, `WidthProcessor`, dry-align delay, `masterSeed`, and the metering taps. `PluginProcessor` stays thin. All DSP classes expose `prepare(spec)` / `process(...)` / `reset()`; no allocation outside `prepare`. Sharing the four DSP files with ShredWidth means one fix (e.g. the shifter) propagates to both plugins — factor them into a small shared static lib or a common submodule if both projects live in the same workspace.

---

## 14. Verification & Test Plan

CI-runnable, adapted from ShredWidth §14 with seeding-aware determinism:

1. **RT-safety checks.** No audio-thread allocation (allocator hook around a `processBlock` loop); `ScopedNoDenormals` present; grep gate on `new`/`malloc`/`String`/`lock` in `processBlock` paths; assert PRNG never touches `std::random_device` on the audio thread.
2. **Reproducibility-from-seed.** Same `masterSeed` + same params over two runs ⇒ bit-identical output (checksum). Different seeds ⇒ different output. Guards D2.
3. **Bypass null test.** True-bypass output == input (sample-exact, minus reported latency).
4. **Mix=0 passthrough.** Output == dry input delayed by reported PDC (exact if align delay is integer).
5. **Mono-lows invariant (key correctness).** Sub-`f_cross` sine (e.g. 100 Hz, Xover 180 Hz): assert `L(n) == R(n)` to FP epsilon at all width/voice/pan settings — proves the shared-LP + M/S design holds in code.
6. **Walk bounds.** Over a long run assert every `τ_v ∈ [τ_min,τ_max]` and every `ΔC_v ∈ [−C_max,+C_max]`; assert mean ≈ nominal (no DC drift).
7. **Correlation-time sanity.** Estimate the autocorrelation time of a captured walk; assert it tracks `τ_c(Rate)` within tolerance across the 100–300 ms range.
8. **Voice decorrelation.** Cross-correlation between any two voice outputs stays below a threshold (proves the allpass+tilt decorrelation works); summed-spectrum has no deep periodic comb notches beyond tolerance.
9. **Tightness monotonicity.** RMS of `τ_v`/`ΔC_v` excursion decreases monotonically as Tightness 0→100% (proves the D3 depth macro).
10. **Latency report.** `getLatencySamples() == L/2` for the active mode; updates on mode switch.
11. **pluginval** strictness 8+ on VST3/CLAP; LV2/Standalone smoke-load.

The test-oracle question — *does it sound like a real double-track?* — still needs an ear pass, but 1–11 catch every mechanical/statistical failure first.

---

## 15. Build Integration (nSpace workflow)

Input document for the abracapocus plan; not the build script. When materializing:

- Set `ABRACAPOCUS_WORKING_ROOT` and `PROJECT_NAME` **before** any run.
- Route the math-heavy new DSP (`RandomWalk`, `AllpassChain`, `Voice`, `ShredDoubleEngine`) to `codex_cli`; GUI/`DriftDisplay`/editor to `antigravity_cli`; glue/boilerplate to `pi_cli` + OpenRouter (GLM). Never `opencode_cli` for plan-materialize.
- **Reuse, don't regenerate,** the four shared DSP files from ShredWidth (`PitchShifter`, `DelayLineFrac`, `MonoSafetyCrossover`, `WidthProcessor`) — pull them in as-is (shared lib/submodule preferred) so a fix propagates to both plugins.
- Generate `CMakeLists.txt` + `scripts/build_all.sh` complete, into the project dir, no placeholders. JUCE submodule pinned to the same tag as ShredWidth (keeps the `drawPopupMenuItem` signature and LV2 support consistent).
- CMake is CLion-bundled only (not on PATH) — `build_all.sh` locates it accordingly or build from CLion.
- After each backend step: verify `changed_files` non-empty (exit_code=0 with empty changes = silent no-op; do not assume success).

---

## 16. Open Decisions for Sign-Off

All decided with defaults; call out any to change before kickoff — none block starting:

- **D3** — Humanization Rate = correlation time, Tightness = depth macro (remapped from source's "tightness = correlation-time"). Confirm the remap or revert to the literal source meaning.
- **Default Voice Count** — 2 vs. 4. (Default 2.)
- **D9 / Latency Mode** — include Studio/Live parity toggle. (Default: include, Studio.)
- **Stereo Width & Output Trim** — parity additions. (Default: include, no-op at default.)
- **Re-roll surfacing** — GUI button only, or also a host-visible momentary parameter. (Default: GUI button; state stores `masterSeed`.)
- **Default Mix** — 50% (doubler) vs. lower. (Default 50%.)
- **AU target** — in scope? (Default: no; VST3/CLAP/LV2/Standalone.)
- **Shared-DSP packaging** — copy files vs. shared static lib/submodule across ShredWidth + ShredDouble. (Default: shared submodule if both live in one workspace.)

---

*End of ShredDouble specification. Companion to ShredWidth_Build_Spec.md — same template, shared DSP core.*
