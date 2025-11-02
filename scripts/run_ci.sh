#!/usr/bin/env bash

# Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>
#
# Baseline CI script for Phase 1 builds.

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"

cmake -S . -B "${BUILD_DIR}" -G Ninja -DSYZYGY_BUILD_PROTOTYPES=ON
ninja -C "${BUILD_DIR}" syzygy_app

echo "Build complete. Launch with ${BUILD_DIR}/src/syzygy_app"

