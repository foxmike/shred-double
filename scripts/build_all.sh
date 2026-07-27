#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PRESET="linux-relwithdebinfo"

find_cmake() {
    local cmake_path
    local -a clion_cmake_candidates=()

    if command -v cmake >/dev/null 2>&1; then
        command -v cmake
        return
    fi

    shopt -s nullglob
    clion_cmake_candidates=(
        "${HOME}"/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/.local/share/JetBrains/Toolbox/apps/clion/*/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/.local/share/JetBrains/Toolbox/apps/CLion/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/.local/share/JetBrains/Toolbox/apps/CLion/*/bin/cmake/linux/x64/bin/cmake
        /opt/clion/bin/cmake/linux/x64/bin/cmake
        /opt/clion/*/bin/cmake/linux/x64/bin/cmake
        /opt/clion-*/bin/cmake/linux/x64/bin/cmake
        /snap/clion/current/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/snap/clion/current/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/.var/app/com.jetbrains.CLion/data/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/cmake
        "${HOME}"/.var/app/com.jetbrains.CLion/data/JetBrains/Toolbox/apps/clion/*/bin/cmake/linux/x64/bin/cmake
        /var/lib/flatpak/app/com.jetbrains.CLion/current/active/files/extra/clion/bin/cmake/linux/x64/bin/cmake
    )
    shopt -u nullglob

    for cmake_path in "${clion_cmake_candidates[@]}"; do
        if [[ -x "${cmake_path}" ]]; then
            printf '%s\n' "${cmake_path}"
            return
        fi
    done

    printf '%s\n' \
        "ERROR: cmake was not found on PATH or in a supported CLion bundle." >&2
    return 1
}

main() {
    local cmake_bin
    cmake_bin="$(find_cmake)"

    cd "${ROOT_DIR}"

    printf '==> Configuring ShredDouble (preset: %s)\n' "${PRESET}"
    "${cmake_bin}" --preset "${PRESET}"

    local targets=(
        ShredDouble_VST3
        ShredDouble_LV2
        ShredDouble_Standalone
    )
    if [[ -f "${ROOT_DIR}/clap-juce-extensions/CMakeLists.txt" ]]; then
        targets+=(ShredDouble_CLAP)
    fi

    printf '==> Building targets: %s\n' "${targets[*]}"
    "${cmake_bin}" --build --preset "${PRESET}" $(printf -- '--target %s ' "${targets[@]}")

    printf '==> All ShredDouble format targets built successfully\n'
}

main "$@"
