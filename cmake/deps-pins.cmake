# Dependency pins — a single source of truth, updated ONLY via deliberate
# commits (see AGENTS.md commit policy). Each pin was verified to exist
# (HTTP 200 on the tag / API HEAD on the commit) before being recorded.
set(MAKEGAIN_JUCE_TAG             "9.0.5")    # latest 9.0.x line (m2-plan: M2c supply-chain bump; JUCE 8 -> 9 owner-approved)
set(MAKEGAIN_CLAP_JUCE_EXT_COMMIT "6bde108329ff9fc24404b79b8b397261a75cf6a8") # free-audio/clap-juce-extensions main HEAD 2026-11-06 (JUCE 9 support #178 + embedded-editor scale fix #183)
set(MAKEGAIN_CATCH2_TAG           "v3.16.1")  # latest release 2026-10-17
