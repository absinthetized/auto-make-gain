# docker/fedora-dev.Dockerfile — Tier-1 Linux lane image (doc/human/build.md §8).
#
# Purpose: reproduce the exact Fedora compile/test environment that
# scripts/bootstrap.sh provisions on a real Fedora Asahi machine, inside an
# image. The dependency list is deliberately NOT duplicated here: the image
# build runs the repo's own bootstrap script, so a future dependency change
# automatically ships to both the bare-metal lane and this container lane.
#
# Pinned to fedora:44 (the release the Asahi Remix tracks) rather than
# :latest — see build.md §8 "version-skew guard": the container must not
# drift away from the bare-metal box mid-milestone.
FROM fedora:44

# bootstrap.sh prefixes its package install with `sudo`. The base image
# already runs as root but carries no sudo binary; installing it lets the
# script's own install path work unchanged inside the container (root's
# sudo is passwordless by definition).
RUN dnf install -y sudo && dnf clean all

# Single source of truth: the package list lives in scripts/bootstrap.sh
# (linux_packages, dnf branch). Running the repo's own bootstrap inside the
# image build guarantees the two lanes never diverge. Note: it first warns
# "missing: gcc git cmake ninja ..." — expected and harmless on a bare
# image; the warning precedes its own install.
COPY scripts/bootstrap.sh /tmp/bootstrap.sh
RUN chmod +x /tmp/bootstrap.sh \
    && /tmp/bootstrap.sh --yes \
    && dnf clean all

# The repo itself is bind-mounted at run time by scripts/docker-check.sh;
# only the working directory is declared here.
WORKDIR /work
