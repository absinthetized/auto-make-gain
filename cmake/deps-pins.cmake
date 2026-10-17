# Dependency pins — a single source of truth, updated ONLY via deliberate
# commits (see AGENTS.md commit policy). Each pin was verified to exist
# (HTTP 200 on the tag / API HEAD on the commit) before being recorded.
set(MAKEGAIN_JUCE_TAG             "8.0.15")   # latest 8.0.x line; JUCE 9 deliberately deferred (m0-plan.md)
set(MAKEGAIN_CLAP_JUCE_EXT_COMMIT "7adee3a1bd4684d4caa5601100e364abccff4b4d") # free-audio/clap-juce-extensions main HEAD 2026-10-17
set(MAKEGAIN_CATCH2_TAG           "v3.16.1")  # latest release 2026-10-17
