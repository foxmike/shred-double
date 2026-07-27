# b

## Goal

New DSP primitives: seeded random walk, allpass decorrelation, EQ tilt, and the per-voice container.

See ShredDouble_Build_Spec.md sections 4.3 (random walk), 4.5 (allpass+tilt), 4.2 (voice architecture), 6 (smoothing), 7 (RT-safety), and 8 (memory). All new DSP classes expose prepare(spec)/process(...)/reset() and allocate nothing outside prepare.

RandomWalk: seeded xoshiro256++ (or xorshift128+) PRNG per stream, deterministic via splitmix64(masterSeed XOR voiceIdx XOR axisTag). One-pole Ornstein-Uhlenbeck walk with alpha = 1 - exp(-1/(tau_c * fs)), variance-normalized by u_tilde = u / sqrt(alpha/(2-alpha)). Generates at control-rate hop H=32 samples and linearly interpolates to audio rate. Tightness scales depth: excursion *= (1-T). Hard clamps: delay [tau_min,tau_max]=[10,35] ms, pitch [-C_max,+C_max]. PRNG is POD, no locks, no std::random_device on audio thread.

AllpassChain: N_ap=4 cascaded first-order Schroeder allpasses per voice. y(n) = -g*x(n) + x(n-M) + g*y(n-M), g~0.6. Per-voice per-stage delays M are mutually coprime and distinct across voices (e.g. V1={13,29,47,71}, V2={17,37,53,79}, V3={19,41,59,83}, V4={23,43,61,89} samples at 48 kHz, scaled by sampleRate). Magnitude-flat, phase-dispersive. Total group delay stays small (a few ms).

TiltFilter: per-voice first-order tilt shelf (low-shelf boost / high-shelf cut of equal small magnitude, pivot ~700 Hz). Distinct per voice (V1 +1.5 dB bright, V2 -1.5 dB dark, V3 +0.8, V4 -0.8). Spectral decorrelation complement to the phase decorrelation.

Voice: container owning 2 RandomWalks (delay axis + pitch axis), 1 PitchShifter (reused), 1 DelayLineFrac (reused), 1 AllpassChain, 1 TiltFilter, pan coefficients (panL, panR via constant-power pan law), and nominalDelayMs. Per-sample process: advance walks (at hop boundaries), feed interpolated deltaC into PitchShifter, feed interpolated tau into DelayLineFrac, chain through AllpassChain and TiltFilter, apply pan and accumulate into wet L/R.

## Notes

- Async: Wave 1: b.1 (RandomWalk), b.2 (AllpassChain), and b.3 (TiltFilter) are all independent (disjoint files). Wave 2: b.4 (Voice) depends on b.1, b.2, b.3 (Voice includes and owns instances of all three). Wave 3: b.5 (TestMain + tests + CMakeLists; sole CMakeLists owner) depends on b.1, b.2, b.3, b.4.
- Risk: b.1 PRNG must be POD with no locks or random_device on audio thread; variance normalization must track tau_c changes. b.2 allpass delays must be coprime within a voice and distinct across voices. b.4 Voice integration must correctly feed interpolated walk values to shifter and delay per sample. b.5 sole CMakeLists owner.
