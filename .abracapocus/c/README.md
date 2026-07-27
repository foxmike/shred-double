# c

## Goal

Engine orchestration, metering tap, processBlock integration, and comprehensive tests.

See ShredDouble_Build_Spec.md sections 3 (signal flow), 4.1 (mono source), 4.6 (panning/summation), 4.7 (crossover + width), 4.8 (dry/wet mix), 4.9 (latency/PDC), 4.10 (seeding/Re-roll), 7 (RT-safety), 9.5 (audio->UI transport), and 14 (verification).

MeterTap: audio-thread Pearson correlation rho of output L/R, one-pole smoothed ~300 ms, published as std::atomic<float>; decimated output L/R pairs pushed into juce::AbstractFifo (~4096 frames) for goniometer; per-voice tau_v and deltaC_v published as 2*V std::atomic<float> values written at hop rate for DriftDisplay. No lock, allocation, or String on push path.

ShredDoubleEngine: owns V=4 Voice objects (voices 3-4 muted/ramped when voiceCount==2 via masked crossfade, D6), one shared MonoSafetyCrossover (shared LP from dry mono x, independent HP per wet accumulator, inverted HP polarity), one WidthProcessor, dry-align delay, and MeterTap. masterSeed management: seed(uint64) reseeds all voice/axis PRNGs via splitmix64 and resets walks to nominal means; Re-roll calls seed(newSeed). Per-sample path: mono source x -> fan to V voices (each: walk advance at hop, pitch shift, drifting delay, allpass, tilt, pan accumulate) -> wet L/R summation -> HP(wet) + shared LP(x) assembly -> M/S width -> equal-gain dry/wet mix against dry delayed by PDC. Latency-mode switch repoints pre-allocated buffers with masked crossfade (no realloc, D6/D9).

PluginProcessor processBlock: ScopedNoDenormals; read smoothed parameters (crossover coeffs per block; spreads, pans, width, mix per sample; humanRate/tightness recompute alpha/depth per block); call engine.process; push final output to MeterTap; report latency = L/2 via setLatencySamples, updateHostDisplay on mode switch. Register new sources in CMakeLists.

Tests split into two tasks: (1) seeding and determinism (same seed = bit-identical, different seed = different, Re-roll generates distinct seed); (2) integration checks (bypass null, Mix=0 passthrough, mono-lows invariant, walk bounds, tightness monotonicity, decorrelation, latency).

## Notes

- Async: Wave 1: c.2 (MeterTap) is standalone. Wave 2: c.1 (ShredDoubleEngine, which holds a MeterTap member) depends on c.2. Wave 3: c.3 (processBlock wiring + latency + CMakeLists; sole Processor/CMakeLists owner) depends on c.1. Wave 4: c.4 (seeding tests) and c.5 (integration tests) depend on c.1 and are independent of each other.
- Risk: c.3 is the Processor fan-in and sole CMakeLists owner. Voice-count switch must ramp voices 3-4 with a masked crossfade (no realloc). Latency-mode switch repoints pre-allocated buffers (D6/D9). masterSeed must be seeded in prepareToPlay and on Re-roll, never on the audio thread via random_device.
