cmake_minimum_required(VERSION 3.24)

project(MakeGain
  VERSION 0.1.0
  DESCRIPTION "MakeGain — chain-wide automatic make-gain plugin (AGPLv3)"
  LANGUAGES C CXX)

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

# --- plugin targets land in C3 (processor) and C4 (formats); ---------------
# --- test targets land in C5. This file stays lean on purpose. -------------
