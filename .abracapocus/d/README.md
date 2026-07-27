# d

## Goal

GUI: per-voice drift display and full editor layout.

See ShredDouble_Build_Spec.md section 9 (GUI and visualizations) and section 13 (file structure). CustomLookAndFeel, RotarySlider, GoniometerComponent, and CorrelationMeter are reused from ShredWidth (copied in phase a). The only new GUI component is DriftDisplay.

DriftDisplay: the signature ShredDouble diagnostic. A 2-D scatter field with X-axis = each voice's current timing offset relative to nominal (ms) and Y-axis = current pitch offset (cents). One color-coded marker per active voice, each trailing a short fading history so you see the four performers breathing independently. Polls 2*V std::atomic<float> values (tau_v, deltaC_v) from MeterTap at 30-60 Hz via the UI Timer. Voices 3-4 markers hidden when voiceCount==2.

PluginEditor full layout: header (plugin name, preset selector, A/B compare, Re-roll button wired to StateManager::reroll(), bypass LED); voice section (voiceCount segmented 2/4 with per-voice pan strips for active voices); humanization section (Timing Spread, Pitch Spread, Humanization Rate, Tightness rotaries); global section (Low Mono Crossover, Stereo Width, Mix, Output Trim, Latency Mode segmented); metering column (GoniometerComponent top, CorrelationMeter middle, DriftDisplay bottom). Wire APVTS SliderAttachments, Timer at 30-60 Hz, ResizableCornerComponent with constrained aspect, window size in value tree state. Apply CustomLookAndFeel. Register GUI sources in CMakeLists.

## Notes

- Async: Wave 1: d.1 (DriftDisplay) is standalone. Wave 2: d.2 (PluginEditor full layout + CMakeLists; sole Editor/CMakeLists owner) depends on d.1.
- Risk: d.2 is the Editor fan-in and sole CMakeLists owner. DriftDisplay must hide V3/V4 markers when voiceCount==2. Re-roll button must call StateManager::reroll() which reseeds the engine. drawPopupMenuItem uses the LookAndFeel_V4 individual-parameter signature (carried from reused CustomLookAndFeel).
