#!/usr/bin/env bash
# Installs every built plugin into the per-user search paths that Linux hosts
# scan. No sudo needed. Re-run after a build to pick up the new binaries.
#
#   ./scripts/install-linux.sh [build-dir]

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${1:-$repo_root/build}"

vst3_target="$HOME/.vst3"
clap_target="$HOME/.clap"
bin_target="$HOME/.local/bin"

if [ ! -d "$build_root" ]; then
    echo "Build output not found: $build_root" >&2
    echo "Build first:  cmake --build build -j\"\$(nproc)\"" >&2
    exit 1
fi

mkdir -p "$vst3_target" "$clap_target" "$bin_target"

installed=0

# Every plugin builds into <build>/<plugin>/<target>_artefacts/Release, so the
# script discovers plugins instead of hard-coding their names.
while IFS= read -r -d '' release_dir; do
    # A copy is used instead of a symlink: some hosts resolve the bundle path
    # and refuse anything that leaves the search directory.
    for bundle in "$release_dir"/VST3/*.vst3; do
        [ -d "$bundle" ] || continue
        name="$(basename "$bundle")"
        rm -rf "${vst3_target:?}/$name"
        cp -r "$bundle" "$vst3_target/"
        echo "VST3       -> $vst3_target/$name"
        installed=$((installed + 1))
    done

    for clap in "$release_dir"/CLAP/*.clap; do
        [ -f "$clap" ] || continue
        cp "$clap" "$clap_target/"
        echo "CLAP       -> $clap_target/$(basename "$clap")"
        installed=$((installed + 1))
    done

    for exe in "$release_dir"/Standalone/*; do
        [ -f "$exe" ] && [ -x "$exe" ] || continue
        cp "$exe" "$bin_target/"
        echo "Standalone -> $bin_target/$(basename "$exe")"
        installed=$((installed + 1))
    done
done < <(find "$build_root" -type d -name Release -path "*_artefacts*" -print0 | sort -z)

if [ "$installed" -eq 0 ]; then
    echo "Nothing to install under $build_root. Did the build finish?" >&2
    exit 1
fi

echo
echo "Done ($installed item(s)). Restart the host, or rescan plugins in it."
echo "Standalone binaries are in $bin_target (make sure it is on PATH)."
