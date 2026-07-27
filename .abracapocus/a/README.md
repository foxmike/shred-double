# a

## Goal

Project scaffold, reused ShredWidth code, APVTS parameter infrastructure, seeded state, and editor skeleton.

See ShredDouble_Build_Spec.md sections 5 (parameters), 10 (formats), 11 (bus layout), 13 (file structure), 15 (build integration), and 4.10 (seeding).

CMakeLists.txt: juce_add_plugin targeting VST3, CLAP (via clap-juce-extensions), LV2 (JUCE native), and Standalone. Expose the SHREDWIDTH_XOVER_ORDER compile option (default 2 for LR2, shared with ShredWidth). CMakePresets.json and scripts/build_all.sh; build_all.sh must locate CLion's bundled cmake because cmake is not on PATH. Add JUCE as a git submodule pinned to the same tag as shred-width and aural-maximizer-pro; inspect shred-width CMakeLists.txt for the known-working nSpace structure. Register all Source/ files including the reused ones.

Copy the eight reused files verbatim from the ShredWidth project at ~/dev/mike/music/plugins/shred-width/Source/ into this project's Source/DSP/ and Source/GUI/: PitchShifter.h/.cpp, DelayLineFrac.h/.cpp, MonoSafetyCrossover.h/.cpp, WidthProcessor.h/.cpp, CustomLookAndFeel.h/.cpp, RotarySlider.h/.cpp, GoniometerComponent.h/.cpp, CorrelationMeter.h/.cpp. These are unchanged from ShredWidth; do not modify them.

ParameterIDs.h: stable v1 inventory with never-renumbered string IDs: voiceCount ({2,4}, default 2), timingSpread (0-15 ms, 6.0), pitchSpread (0-10 cents, 5.0), humanRate (0-100%, 50, maps to tau_c 300-100 ms), tightness (0-100%, 50, depth macro), monoXover (80-300 Hz, 180, log skew ~0.35), mix (0-100%, 50), panV1 (-100..+100%, -100), panV2 (+100), panV3 (-60), panV4 (+60), width (0-200%, 100), latencyMode ({Studio,Live}, Studio), outTrim (-12..+12 dB, 0), bypass (bool). Non-parameter: masterSeed (uint64) stored in value tree, not host-automatable.

PluginProcessor spine: APVTS with skews; isBusesLayoutSupported accepts mono->stereo and stereo->stereo only; processBlock stub with ScopedNoDenormals; prepareToPlay allocates V=4 worst case (4x pitch buffers L=2048+4, 4x delay lines ceil(35ms*maxSR)+guard, 4x allpass chains, 1x crossover, 1x dry-align 1024, metering FIFO, 8x PRNG POD states); setLatencySamples PDC hooks.

StateManager: APVTS XML getStateInformation/setStateInformation with parameterLayoutVersion, masterSeed uint64 round-trip, Re-roll generates a new seed via splitmix64(std::random_device) and marks state dirty, A/B slots.

PluginEditor skeleton: window, Timer, ResizableCornerComponent, Re-roll button stub.

.gitignore and README.

## Notes

- Async: Wave 1: a.1 (CMakeLists + build + JUCE submodule; sole CMakeLists owner) and a.2 (copy reused ShredWidth files) and a.5 (.gitignore + README) are all independent. Wave 2: a.3 (ParameterIDs + APVTS + bus + Processor spine; sole Processor owner) depends on a.1 and a.2 (reused files must exist for includes). Wave 3: a.4 (StateManager + PluginEditor skeleton) depends on a.3.
- Risk: a.1 sole CMakeLists owner; a.3 sole Processor owner. Reused files must be byte-identical copies from ShredWidth (do not modify). masterSeed round-trip in state is critical for D2 reproducibility. Bus layout: mono->stereo and stereo->stereo only.
