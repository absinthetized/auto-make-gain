// Test framework placeholder (C5 per m0-plan): proves Catch2 is wired and
// the sanitizer lane executes test binaries. Real DSP tests land with the
// loudness/gain-computer milestones (tests/dsp_* per TS §7).
#include <catch2/catch_test_macros.hpp>

TEST_CASE("framework: arithmetic sanity", "[framework]")
{
    REQUIRE (1 + 1 == 2);
}

TEST_CASE("framework: deterministic float compare", "[framework]")
{
    // DSP assertions never use exact equality on computed floats — matches()
    // with tolerances arrives with the real DSP tests (TS §3.1). Here we
    // only prove the toolchain exposes the classic accumulation error to
    // catch2's floating-point helpers.
    const float a = 0.1f + 0.2f;
    CHECK (std::abs (a - 0.3f) < 1.0e-6f);
}
