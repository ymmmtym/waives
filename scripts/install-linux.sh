#!/usr/bin/env bash
# Installs the built plugin into the per-user search paths that Linux hosts scan.
# No sudo needed. Re-run after every build to pick up the new binaries.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artefacts="${1:-$repo_root/build/MyFirstMasteringEQ_artefacts/Release}"

vst3_source="$artefacts/VST3/MyFirstMasteringEQ.vst3"
clap_source="$artefacts/CLAP/MyFirstMasteringEQ.clap"
standalone_source="$artefacts/Standalone/MyFirstMasteringEQ"

vst3_target="$HOME/.vst3"
clap_target="$HOME/.clap"
bin_target="$HOME/.local/bin"

if [ ! -d "$artefacts" ]; then
    echo "Build output not found: $artefacts" >&2
    echo "Build first:  cmake --build build -j\"\$(nproc)\"" >&2
    exit 1
fi

mkdir -p "$vst3_target" "$clap_target" "$bin_target"

# A copy is used instead of a symlink: some hosts resolve the bundle path and
# refuse anything that leaves the search directory.
if [ -d "$vst3_source" ]; then
    rm -rf "${vst3_target:?}/MyFirstMasteringEQ.vst3"
    cp -r "$vst3_source" "$vst3_target/"
    echo "VST3  -> $vst3_target/MyFirstMasteringEQ.vst3"
else
    echo "skip: $vst3_source (not built)"
fi

if [ -f "$clap_source" ]; then
    cp "$clap_source" "$clap_target/"
    echo "CLAP  -> $clap_target/MyFirstMasteringEQ.clap"
else
    echo "skip: $clap_source (not built)"
fi

if [ -x "$standalone_source" ]; then
    cp "$standalone_source" "$bin_target/"
    echo "Standalone -> $bin_target/MyFirstMasteringEQ"
else
    echo "skip: $standalone_source (not built)"
fi

echo
echo "Done. Restart the host, or rescan plugins in it."
echo "Standalone: $bin_target/MyFirstMasteringEQ  (make sure ~/.local/bin is on PATH)"
