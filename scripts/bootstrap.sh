#!/usr/bin/env bash
# MakeGain developer bootstrap — the only entrypoint a fresh machine needs.
#
# What it does (per docs/m0-plan.md C1):
#   - checks the C/C++ toolchain needed to build MakeGain
#   - macOS: offers to trigger the Xcode CLT installer; downloads official
#     cmake/ninja binaries into ~/.local/bin if Homebrew is absent/unused
#   - Fedora (dnf) / Debian-likes (apt) / Arch (pacman): prints — and
#     installs with `--yes` — the exact package list
#   - JUCE and other dependencies are NOT downloaded here; CMake fetches
#     them (pinned) at first `cmake --preset ...` configure.
#
# Usage:
#   ./scripts/bootstrap.sh          # check + print what is missing
#   ./scripts/bootstrap.sh --yes    # allow installing what is missing
set -u

YES=0
[ "${1:-}" = "--yes" ] && YES=1
PREFIX="${HOME}/.local/bin"

say()  { printf '\n\033[1;36m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m ✓\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m !\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m✗\033[0m %s\n' "$*"; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
say "Detecting platform"
OS="$(uname -s)"
ARCH="$(uname -m)"
if [ "$OS" = "Darwin" ]; then
  PLATFORM="macos"
elif [ "$OS" = "Linux" ]; then
  PLATFORM="linux"
else
  die "Unsupported OS '$OS' — MakeGain v0 targets macOS and Linux only."
fi
ok "platform: ${PLATFORM} (${ARCH})"

# ---------------------------------------------------------------------------
# Tool availability map (name -> why we need it)
check_tools() {
  MISSING=""
  # compiler
  case "$PLATFORM" in
    macos) if have clang && have git; then ok "clang + git (Xcode CLT) present"; else
             MISSING="${MISSING} xcode-clt"
           fi ;;
    linux) if have g++ && have git; then ok "g++ + git present"; else
             MISSING="${MISSING} gcc git"
           fi ;;
  esac
  # cmake + ninja
  have cmake   || MISSING="${MISSING} cmake"
  have ninja   || MISSING="${MISSING} ninja"
  if [ -n "${MISSING}" ]; then warn "missing:${MISSING}"; else ok "cmake + ninja present"; fi
}

# ---------------------------------------------------------------------------
macos_xcode_clt() {
  if have clang && have git; then return 0; fi
  say "Xcode Command Line Tools are required (provides clang + git)"
  if [ "$YES" = "1" ]; then
    warn "launching installer — after it finishes, re-run this script"
    xcode-select --install || die "could not start the Xcode CLT installer"
  else
    warn "run me again with --yes, or manually:  xcode-select --install"
  fi
  return 2   # signals "rerun after install"
}

# Download official upstream binaries into ~/.local/bin (no Homebrew needed).
# Uses the GitHub releases API so we always get the latest official build.
macos_fetch_tool() {
  tool="$1"; repo="$2"; asset_grep="$3"
  have curl || die "curl missing (built-in on macOS — did the CLT install finish?)"
  URL="$(curl -fsSL "https://api.github.com/repos/${repo}/releases/latest" \
        | grep browser_download_url | grep -E "${asset_grep}" \
        | head -n1 | cut -d'"' -f4)"
  [ -n "${URL}" ] || die "could not resolve a release asset for ${tool}"
  say "Downloading ${tool} -> ${PREFIX}"
  mkdir -p "$PREFIX" || die "cannot create ${PREFIX}"
  TMP="$(mktemp -d)"
  case "$URL" in
    *.tar.gz) curl -fLSs "$URL" | tar -xz -C "$TMP" ;;
    *.zip)    curl -fLSs "$URL" -o "$TMP/a.zip" && (cd "$TMP" && unzip -q a.zip) ;;
    *) die "unexpected asset type for ${tool}: $URL" ;;
  esac
  find "$TMP" -type f \( -name cmake -o -name ninja \) -exec chmod +x {} \; -exec cp {} "$PREFIX/" \; \
    || die "installing ${tool} failed"
  rm -rf "$TMP"
  ok "${tool} installed at ${PREFIX}/${tool}"
}

macos_tools() {
  if have cmake && have ninja; then return 0; fi
  if have brew; then
    say "Homebrew detected"
    warn "then:  brew install cmake ninja"
    warn "(excluded at M0: keeping the bootstrap Homebrew-free)"
    die "installing via Homebrew is therefore left explicit to you"
  fi
  have cmake || macos_fetch_tool cmake     Kitware/CMake "macos.*universal\.tar\.gz"
  have ninja || macos_fetch_tool ninja     ninja-build/ninja "ninja-mac\.zip"
  warn "make sure ${PREFIX} is on your PATH, e.g.:"
  warn "  echo 'export PATH=\"${PREFIX}:\$PATH\"' >> ~/.zshrc  &&  source ~/.zshrc"
}

# ---------------------------------------------------------------------------
linux_packages() {
  case "$PLATFORM" in
    linux)
      if have dnf; then
        PKGS="git gcc-c++ cmake ninja-build alsa-lib-devel"
        MGR="dnf"; MGR_ARGS="-y"
      elif have apt-get; then
        PKGS="git g++ cmake ninja-build libasound2-dev"
        MGR="apt-get"; MGR_ARGS="-y"
      elif have pacman; then
        PKGS="git gcc cmake ninja alsa-lib"
        MGR="pacman"; MGR_ARGS="--noconfirm"
      else
        die "no dnf/apt/pacman found — install manually: a C++17 compiler, git, cmake(>=3.24), ninja, ALSA headers"
      fi ;;
  esac

  say "Required packages (${MGR}): ${PKGS}"
  if [ "$YES" = "1" ]; then
    warn "installing with ${MGR} — sudo may ask for your password"
    # shellcheck disable=SC2086
    sudo "$MGR" install $MGR_ARGS $PKGS || die "package install failed"
    ok "packages installed"
  else
    warn "dry run: re-run with --yes to let me install them, or paste:"
    warn "  sudo ${MGR} install ${MGR_ARGS} ${PKGS}"
  fi
}

# ---------------------------------------------------------------------------
say "Checking tool prerequisites"
if [ "$PLATFORM" = "macos" ]; then
  macos_xcode_clt || { say "RE-RUN ME after the Xcode CLT install finishes"; exit 0; }
  check_tools
  macos_tools
else
  check_tools
  linux_packages
fi

say "Next step"
echo "  cmake --preset $([ $PLATFORM = macos ] && echo macos || echo linux-arm64)"
echo "  (first configure also downloads JUCE ~ pinned — a few minutes on slow links)"
say "bootstrap complete — happy build path ready"
