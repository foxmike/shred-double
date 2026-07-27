// =============================================================================
// ShredDouble - DSP Primitive Test Runner Entry Point
// =============================================================================
// Console entry point that runs every registered juce::UnitTest and returns a
// non-zero process exit code if any test fails. No third-party testing
// framework is used; this relies solely on juce::UnitTest / UnitTestRunner,
// mirroring the sibling ShredWidth tests/TestMain.cpp pattern. Catch2 is not
// pulled in over the network — we keep the build hermetic (no FetchContent)
// per the "vegan CMake" guidance from the project brief.
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main()
{
    // Some juce::UnitTest paths touch the message thread / timers, so make
    // sure a MessageManager exists for the lifetime of the runner.
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;

    // The int overload of runAllTests is a truthy gate ("run everything").
    // Pass non-zero so every registered juce::UnitTest executes.
    runner.runAllTests();

    int totalFailures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto *result = runner.getResult(i))
        {
            const auto failures = result->failures;
            const auto passes = result->passes;

            juce::Logger::writeToLog(
                juce::String{result->unitTestName} + " :: "
                + result->subcategoryName + "  passes="
                + juce::String{passes} + " failures="
                + juce::String{failures});

            totalFailures += failures;
        }
    }

    if (totalFailures > 0)
    {
        juce::Logger::writeToLog(
            "ShredDouble DSP primitive tests FAILED ("
            + juce::String{totalFailures} + " failure(s)).");
        return 1;
    }

    juce::Logger::writeToLog("ShredDouble DSP primitive tests passed.");
    return 0;
}