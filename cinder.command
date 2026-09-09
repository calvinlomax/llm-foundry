#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
export FOUNDRY_PROJECT_DIR="$PWD"
export FOUNDRY_HOME="$PWD/models/registry"

if [[ ! -x build/release/foundry-gui ]]; then
    printf 'Building Cinder Foundry for its first launch…\n'
    cmake --preset release
    cmake --build --preset release
fi
exec ./build/release/foundry-gui
