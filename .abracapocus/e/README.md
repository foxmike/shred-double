# e

## Goal

Factory presets, correctness tests, verification scripts, and release documentation.

See ShredDouble_Build_Spec.md section 12 (presets), section 14 (verification and test plan), section 15 (build integration), and section 16 (open decisions, all defaulted).

Factory presets as APVTS XML with fixed masterSeed per preset (regression fixtures), embedded via BinaryData: Tight Metal Quad (V4, Timing 4 ms, Pitch 4 c, Rate 40, Tightness 75%, Xover 200 Hz, Pan 100/100/60/60, Mix 60%); Wide Double (V2, 6/5/50/50%, 180 Hz, 100/100, 50%); Loose Garage Double (V2, 12/8/70/20%, 150 Hz, 55%); Subtle Thickener (V2, 3/3/30/80%, 250 Hz, 35%); Live Quad Tracking (Live, V4, 5/4/45/65%, 50%).

Correctness tests: pitch accuracy (FFT shifted output, assert peak within tolerance of f*2^(deltaC/1200)); correlation sanity (Width=200% drops rho; Width=0 rho~1; mono-safe sub-f_cross). Register preset BinaryData and test target in CMakeLists.

Verification scripts: RT-safety grep gate (processBlock forbidden calls + ScopedNoDenormals + assert no std::random_device on audio thread); pluginval at strictness 8+ on VST3/CLAP with LV2/Standalone smoke-load. Scripts resolve cmake via build_all.sh / CLion.

Documentation: verification checklist covering section 14 CI-runnable checks (RT-safety, seed-determinism, bypass, Mix=0, mono-lows, walk bounds, correlation-time sanity, voice decorrelation, tightness monotonicity, latency, pluginval) with honest note that ear-pass and host matrix are manual; user manual covering seeding/Re-roll workflow, D3 Rate/Tightness remap explanation, voice-count and latencyMode automation caveats, LV2 host notes.

## Notes

- Async: Wave 1: e.1 (preset XML assets), e.3 (RT-safety script), e.4 (pluginval script), and e.5 (verification + manual docs) are all independent. Wave 2: e.2 (correctness tests + preset BinaryData + CMakeLists; sole CMakeLists owner) depends on e.1.
- Risk: e.2 sole CMakeLists owner (preset BinaryData + final tests). RT-safety grep gate must also assert no std::random_device on audio thread (D2 PRNG constraint). Presets must include fixed masterSeed values for regression. Verification scripts assume cmake resolved via build_all.sh / CLion.
