# ShredDouble

**Algorithmic Rhythm Doubler & Double-Tracked Width from One Take**  
Developed by nSpace Systems.

ShredDouble synthesizes convincing **double-tracked rhythm width from a single mono take**. Where ShredWidth widens one lead note with a fixed static detune, ShredDouble fabricates the illusion of independent performances — separate takes with human pitch and timing inconsistency, hard-panned — from one mono guitar.

The mechanism generates `V` virtual voices (2 or 4), each hard-panned, each fed the mono source through an **independent** chain of:
- Bounded random-walk pitch + timing humanization (no LFO — periodic wobble is the one thing this plugin exists to *not* do),
- Per-voice decorrelation (allpass diffusion + subtle EQ tilt) so summed voices are thick rather than hollow/phasey,
- Shared-LP mono-safety crossover so the low end stays centered and tight under mono fold-down.

Formats: **VST3**, **CLAP**, **LV2**, **Standalone** (no AU).

Language standard: **C++20**  
Framework: **JUCE** (pinned tag `8.0.13`)

> For the complete design specification see [`.abracapocus/ShredDouble_Build_Spec.md`](.abracapocus/ShredDouble_Build_Spec.md).

---

## Build Instructions

### Prerequisites

- **CMake** — CLion-bundled CMake is the primary toolchain; `cmake` may not be on `$PATH`.
- **C++20-capable compiler** (GCC 11+, Clang 14+, MSVC 2022+).

### Dependencies

1. **JUCE** — managed as a Git submodule, pinned to tag `8.0.13`.  
2. **clap-juce-extensions** (optional) — required to build the CLAP format. If placed at `clap-juce-extensions/` as a submodule, the build system will automatically enable the CLAP target. Without it, `ShredDouble_CLAP` is skipped with a message.

### Steps

```bash
# 1. Clone and initialize submodules
git clone <repository-url> ShredDouble
cd ShredDouble
git submodule update --init --recursive

# 2. Build all supported format targets
./scripts/build_all.sh
```

The build script (`scripts/build_all.sh`) automatically searches for CLion-bundled CMake in standard locations (Toolbox, snap, flatpak) before falling back to a system `cmake` on `$PATH`.

---

## Submodule Pins

| Submodule | URL | Tag / Commit |
|-----------|-----|-------------|
| `JUCE` | `https://github.com/juce-framework/JUCE.git` | `8.0.13` |

The JUCE pin matches the version used by sibling nSpace plugins (ShredWidth, AuralMaximizerPro) to ensure consistent `drawPopupMenuItem` signatures, LV2 support, and API compatibility. Do not change the pin without coordinating across all nSpace plugin repositories.

`clap-juce-extensions` is **not** pinned as a submodule at this time. If CLAP builds are needed, populate it manually (e.g., `git submodule add https://github.com/free-audio/clap-juce-extensions`), or add it to `.gitmodules` in a future phase.

---

## Documentation

- **[`.abracapocus/ShredDouble_Build_Spec.md`](.abracapocus/ShredDouble_Build_Spec.md)** — Complete build specification: signal flow, DSP math, parameter map, GUI behavior, bus handling, file layout, and verification plan.
- **`scripts/build_all.sh`** — One-shot build script with CLion CMake resolution.