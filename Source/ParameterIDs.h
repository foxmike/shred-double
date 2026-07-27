#pragma once

namespace ShredDouble::ParameterIDs {
// Parameter IDs are persisted by hosts and presets. Never rename or renumber
// an existing ID; add migrations when a future schema requires changes.
inline constexpr int parameterLayoutVersion = 1;
inline constexpr int schemaVersion = parameterLayoutVersion;

inline constexpr auto voiceCount = "voiceCount";
inline constexpr auto timingSpread = "timingSpread";
inline constexpr auto pitchSpread = "pitchSpread";
inline constexpr auto humanRate = "humanRate";
inline constexpr auto tightness = "tightness";
inline constexpr auto monoXover = "monoXover";
inline constexpr auto mix = "mix";
inline constexpr auto panV1 = "panV1";
inline constexpr auto panV2 = "panV2";
inline constexpr auto panV3 = "panV3";
inline constexpr auto panV4 = "panV4";
inline constexpr auto width = "width";
inline constexpr auto latencyMode = "latencyMode";
inline constexpr auto outTrim = "outTrim";
inline constexpr auto bypass = "bypass";

// ValueTree-only state property. This is deliberately not a host-automatable
// APVTS parameter: recalling it restores the deterministic performance seed.
inline constexpr auto masterSeed = "masterSeed";
} // namespace ShredDouble::ParameterIDs
