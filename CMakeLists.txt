cmake_minimum_required(VERSION 3.24)

project(MakeGain
  VERSION 0.2.1
  DESCRIPTION "MakeGain — chain-wide automatic make-gain plugin (AGPLv3)"
  LANGUAGES C CXX)

# --- build stamp (provenance, not version truth) ---------------------------
# The short git sha is compiled in so a LOADED binary can be identified:
# the editor shows "version · sha" in its title row, and tooling can read
# it back with `strings`. Version truth stays project(VERSION) above
# (`juce_add_plugin` derives JucePlugin_VersionString + bundle metadata
# from it). Outside a git checkout (release tarball) the stamp degrades
# to "unknown" — the build must never depend on git being present.
set(MAKEGAIN_BUILD_SHA "unknown")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _makegain_git_sha
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(_makegain_git_sha)
    set(MAKEGAIN_BUILD_SHA "${_makegain_git_sha}")
    # "+dirty" only for tracked-file modifications — ignored build output
    # and untracked scratch must not make a test build look different.
    execute_process(
      COMMAND git status --porcelain --untracked-files=no
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE _makegain_git_dirty
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(_makegain_git_dirty)
      string(APPEND MAKEGAIN_BUILD_SHA "+dirty")
    endif()
  endif()
endif()
message(STATUS "MakeGain build stamp: ${PROJECT_VERSION} · ${MAKEGAIN_BUILD_SHA}")

# C++20 build mode restricted to C++17-compatible features (TS §1.1).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps-pins.cmake)

# --- dependency declarations (pins in cmake/deps-pins.cmake) --------------
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        ${MAKEGAIN_JUCE_TAG}
  GIT_SHALLOW    TRUE)

# Commit-hash pin: no GIT_SHALLOW (shallow clones can't check out raw hashes).
FetchContent_Declare(clap-juce-extensions
  GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
  GIT_TAG        ${MAKEGAIN_CLAP_JUCE_EXT_COMMIT})

FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        ${MAKEGAIN_CATCH2_TAG}
  GIT_SHALLOW    TRUE)

message(STATUS "MakeGain dependency pins: JUCE=${MAKEGAIN_JUCE_TAG} "
               "clap-juce-extensions=${MAKEGAIN_CLAP_JUCE_EXT_COMMIT} "
               "Catch2=${MAKEGAIN_CATCH2_TAG}")

# --- fetch order matters: JUCE first, then the CLAP extension layer --------
FetchContent_MakeAvailable(JUCE)
FetchContent_MakeAvailable(clap-juce-extensions)
FetchContent_MakeAvailable(Catch2)

message(STATUS "JUCE ${JUCE_VERSION} available; clap_juce_extensions + Catch2 available")

# --- plugin target (C3: VST3; C4 adds CLAP + AU) ----------------------------
juce_add_plugin(MakeGain
  PRODUCT_NAME              "MakeGain"
  COMPANY_NAME              "auto-make-gain"
  BUNDLE_ID                 "org.auto-make-gain.makegain"
  PLUGIN_MANUFACTURER_CODE  Amkg
  PLUGIN_CODE               Mkn1
  FORMATS                   VST3 AU
  IS_SYNTH                  FALSE
  NEEDS_MIDI_INPUT          FALSE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  COPY_PLUGIN_AFTER_BUILD   TRUE)

target_sources(MakeGain PRIVATE
  src/plugin/MakeGainProcessor.cpp
  src/plugin/MakeGainProcessor.h
  src/ui/MakeGainEditor.cpp
  src/ui/MakeGainEditor.h
  # PR-B2 (PB2c): the processor now CALLS the gain machine, so the DSP
  # compilation units join the plugin target (machine + the meter it
  # owns). They stay JUCE-free (the test target still compiles them
  # independently under sanitizers — the two targets sharing the same
  # .cpp files is deliberate: one truth, two lanes).
  src/dsp/gain_computer.cpp
  src/dsp/loudness_meter.cpp
  src/dsp/k_weighting.cpp)

target_include_directories(MakeGain PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(MakeGain PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  # Linux build-dependency reduction (TS §3.6/§6; official JUCE doc
  # juce-framework/JUCE@develop/docs/Linux Dependencies.md):
  # juce_audio_devices and juce_audio_processors probe pkg-config for
  # JACK and LADSPA unless these flags are set — and a plugin never
  # hosts other plugins over JACK/LADSPA. Dropping these deps keeps a
  # fresh Fedora bootstrap to core packages only.
  JUCE_JACK=0
  JUCE_PLUGINHOST_LADSPA=0)

# Build stamp for the editor/status tools (see the capture block at the
# top of this file). The escaped quotes make the C++ side see a string
# literal: -DMAKEGAIN_BUILD_SHA="<sha>".
target_compile_definitions(MakeGain PRIVATE
  "MAKEGAIN_BUILD_SHA=\"${MAKEGAIN_BUILD_SHA}\"")

target_link_libraries(MakeGain PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  # CLAP wrapper layer (free-audio/clap-juce-extensions). NOTE: the target
  # is plain-named (no :: alias) — the repo ships no ALIAS for it.
  clap_juce_extensions)

# CLAP generation (free-audio/clap-juce-extensions): do NOT put "CLAP" in
# FORMATS — the extension layers it onto this target at configure/build.
clap_juce_extensions_plugin(TARGET MakeGain
  CLAP_ID      "org.auto-make-gain.makegain"
  CLAP_FEATURES audio-effect)

# Disclaimer: JUCE 8 (AGPLv3 route) propagates to this program — AGPLv3,
# see LICENSE and tech_specs §2.

# --- test target (C5): JUCE-independent, runs under sanitizers --------------
# (TS §7: sanitizers stay OUT of plugin builds; only the unit-test lane.)
enable_testing()

add_executable(makegain_tests
  tests/dsp/framework_sanity.cpp
  tests/dsp/k_weighting_test.cpp
  tests/dsp/loudness_meter_test.cpp
  tests/dsp/gain_computer_test.cpp
  tests/dsp/fast_level_test.cpp
  src/dsp/k_weighting.cpp
  src/dsp/loudness_meter.cpp
  src/dsp/fast_level.cpp
  src/dsp/gain_computer.cpp)

# dsp/ is JUCE-free and its headers are included by name from the tests
target_include_directories(makegain_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/dsp)

target_link_libraries(makegain_tests PRIVATE
  Catch2::Catch2WithMain)

add_test(NAME framework COMMAND makegain_tests)

if(MAKEGAIN_SANITIZERS)
  # ASan (incl. LeakSanitizer) + UBSan, owner-approved memory-safety lane.
  target_compile_options(makegain_tests PRIVATE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
    -g)
  target_link_options(makegain_tests PRIVATE
    -fsanitize=address,undefined)
endif()
