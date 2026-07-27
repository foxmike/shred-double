cmake_minimum_required(VERSION 3.22)

project(ShredDouble VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    SHREDWIDTH_CONFIGURE_ONLY
    "Configure the top-level project without requiring the JUCE submodule"
    OFF
)

set(
    SHREDWIDTH_XOVER_ORDER
    2
    CACHE STRING
    "Linkwitz-Riley crossover order (2 or 4)"
)
set_property(CACHE SHREDWIDTH_XOVER_ORDER PROPERTY STRINGS 2 4)

if(NOT SHREDWIDTH_XOVER_ORDER MATCHES "^(2|4)$")
    message(FATAL_ERROR "SHREDWIDTH_XOVER_ORDER must be 2 or 4")
endif()

if(SHREDWIDTH_CONFIGURE_ONLY)
    message(STATUS "ShredDouble configure-only mode enabled")
    return()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(
        FATAL_ERROR
        "The pinned JUCE 8.0.13 submodule is missing. Populate JUCE before configuring a build."
    )
endif()

add_subdirectory(JUCE)

# clap-juce-extensions is optional at configure time. When populated at this
# path, add_subdirectory() provides clap_juce_extensions_plugin(), which creates
# the ShredDouble_CLAP target below.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/clap-juce-extensions/CMakeLists.txt")
    add_subdirectory(clap-juce-extensions EXCLUDE_FROM_ALL)
else()
    message(
        STATUS
        "clap-juce-extensions is not populated; ShredDouble_CLAP will not be generated"
    )
endif()

juce_add_plugin(ShredDouble
    PRODUCT_NAME ShredDouble
    COMPANY_NAME "nSpace Systems"
    VERSION 1.0.0
    PLUGIN_MANUFACTURER_CODE NSPC
    PLUGIN_CODE ShDb
    BUNDLE_ID com.nspacesystems.shreddouble
    LV2URI urn:nspacesystems:plugin:shreddouble
    FORMATS VST3 CLAP LV2 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
)

if(COMMAND clap_juce_extensions_plugin)
    clap_juce_extensions_plugin(
        TARGET ShredDouble
        CLAP_ID com.nspacesystems.shreddouble
        CLAP_FEATURES audio-effect stereo
    )
endif()

target_sources(ShredDouble
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/StateManager.cpp
        Source/DSP/PitchShifter.cpp
        Source/DSP/DelayLineFrac.cpp
        Source/DSP/MonoSafetyCrossover.cpp
        Source/DSP/WidthProcessor.cpp
        Source/DSP/RandomWalk.cpp
        Source/DSP/AllpassChain.cpp
        Source/DSP/TiltFilter.cpp
        Source/DSP/Voice.cpp
        Source/DSP/MeterTap.cpp
        Source/DSP/ShredDoubleEngine.cpp
        Source/GUI/CustomLookAndFeel.cpp
        Source/GUI/RotarySlider.cpp
        Source/GUI/GoniometerComponent.cpp
        Source/GUI/CorrelationMeter.cpp
        Source/GUI/DriftDisplay.cpp
)

juce_add_binary_data(ShredDoublePresets
    SOURCES
        Resources/Presets/TightMetalQuad.xml
        Resources/Presets/WideDouble.xml
        Resources/Presets/LooseGarageDouble.xml
        Resources/Presets/SubtleThickener.xml
        Resources/Presets/LiveQuadTracking.xml
)

target_compile_definitions(ShredDouble
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SHREDWIDTH_XOVER_ORDER=${SHREDWIDTH_XOVER_ORDER}
)

target_link_libraries(ShredDouble
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        ShredDoublePresets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

enable_testing()

# -----------------------------------------------------------------------------
# DSP primitive test target. Sole owner of tests/* in phase B (task b.5).
# Registers a console executable that exercises the public APIs of the
# DSP primitives produced by b.1-b.4 (RandomWalk, AllpassChain, TiltFilter,
# Voice) plus the reused shared primitives (PitchShifter, DelayLineFrac)
# that Voice transitively depends on. Does not instantiate a full plugin
# host. The harness is juce::UnitTest-based (mirrors the sibling ShredWidth
# pattern) — no Catch2 / FetchContent is pulled in over the network so the
# build remains hermetic.
# -----------------------------------------------------------------------------
set(SHREDDOUBLE_TEST_SOURCES
    tests/TestMain.cpp
    tests/DSPPrimitiveTests.cpp
    tests/CorrectnessTests.cpp
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/SeedingTests.cpp")
    list(APPEND SHREDDOUBLE_TEST_SOURCES tests/SeedingTests.cpp)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineTests.cpp")
    list(APPEND SHREDDOUBLE_TEST_SOURCES tests/EngineTests.cpp)
endif()

add_executable(ShredDoubleTests
    ${SHREDDOUBLE_TEST_SOURCES}
    Source/DSP/PitchShifter.cpp
    Source/DSP/DelayLineFrac.cpp
    Source/DSP/RandomWalk.cpp
    Source/DSP/AllpassChain.cpp
    Source/DSP/TiltFilter.cpp
    Source/DSP/Voice.cpp
    Source/DSP/MonoSafetyCrossover.cpp
    Source/DSP/WidthProcessor.cpp
    Source/DSP/MeterTap.cpp
    Source/DSP/ShredDoubleEngine.cpp
)


target_compile_definitions(ShredDoubleTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    SHREDWIDTH_XOVER_ORDER=${SHREDWIDTH_XOVER_ORDER}
)

target_include_directories(ShredDoubleTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ShredDoubleTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        ShredDoublePresets
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME ShredDoubleTests COMMAND ShredDoubleTests)
