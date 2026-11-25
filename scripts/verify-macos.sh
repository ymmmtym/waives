#!/usr/bin/env bash
#
# Install the macOS bundles on a real Mac and check what a host checks.
#
# Usage:
#   ./scripts/verify-macos.sh                      # newest release from GitHub
#   ./scripts/verify-macos.sh --tarball FILE       # a tarball you already have
#   ./scripts/verify-macos.sh --au-only            # skip VST3 / CLAP / Standalone
#   ./scripts/verify-macos.sh --dry-run            # print the plan, change nothing
#
# Exits non-zero when auval or pluginval rejects the component. The Logic Pro
# checklist that needs a human is in docs/logic-pro-checklist.md.

set -euo pipefail

REPO="ymmmtym/waives"
PLUGIN="MyFirstMasteringEQ"
INSTALL_ROOT="${HOME}/Library/Audio/Plug-Ins"

TARBALL=""
AU_ONLY=0
DRY_RUN=0
KEEP=0

while [ $# -gt 0 ]; do
    case "$1" in
        --tarball) TARBALL="${2:?--tarball needs a path}"; shift 2 ;;
        --au-only) AU_ONLY=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        --keep) KEEP=1; shift ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

log()  { printf '\n== %s\n' "$*"; }
step() { printf '   %s\n' "$*"; }

run() {
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '   [dry-run] %s\n' "$*"
    else
        "$@"
    fi
}

if [ "$(uname -s)" != "Darwin" ]; then
    echo "This script only makes sense on macOS: it installs and validates an AU." >&2
    echo "On Linux, use ./install-linux.sh and the Carla steps in the README instead." >&2
    exit 1
fi

WORK="$(mktemp -d)"
cleanup() { [ "$KEEP" -eq 1 ] || rm -rf "$WORK"; }
trap cleanup EXIT

if [ -z "$TARBALL" ]; then
    log "Downloading the newest release"
    ASSET="${PLUGIN}-macos.tar.gz"

    if command -v gh > /dev/null; then
        run gh release download --repo "$REPO" --pattern "$ASSET" --dir "$WORK" --clobber
        run gh release download --repo "$REPO" --pattern SHA256SUMS --dir "$WORK" --clobber
    else
        url="$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
               | grep -o "https://[^\"]*${ASSET}" | head -n 1)"
        sums="$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
               | grep -o "https://[^\"]*SHA256SUMS" | head -n 1)"
        run curl -fsSL -o "$WORK/$ASSET" "$url"
        run curl -fsSL -o "$WORK/SHA256SUMS" "$sums"
    fi

    TARBALL="$WORK/$ASSET"

    if [ "$DRY_RUN" -eq 0 ] && [ -f "$WORK/SHA256SUMS" ]; then
        log "Checking the download against SHA256SUMS"
        step "$(cd "$WORK" && grep " ${ASSET}\$" SHA256SUMS | shasum -a 256 -c -)"
    fi
fi

log "Unpacking $TARBALL"
run tar -xzf "$TARBALL" -C "$WORK"
RELEASE="$WORK/${PLUGIN}_artefacts/Release"

if [ "$DRY_RUN" -eq 0 ]; then
    for bundle in AU/${PLUGIN}.component VST3/${PLUGIN}.vst3 CLAP/${PLUGIN}.clap; do
        [ -e "$RELEASE/$bundle" ] || { echo "missing from the tarball: $bundle" >&2; exit 1; }
        step "found $bundle"
    done
fi

# The AU's identity is what `auval -v` and Logic look up, so read it from the
# bundle rather than trusting a constant in this script.
COMPONENT_SRC="$RELEASE/AU/${PLUGIN}.component"
if [ "$DRY_RUN" -eq 0 ]; then
    plist="$COMPONENT_SRC/Contents/Info.plist"
    plistbuddy="${PLIST_BUDDY:-/usr/libexec/PlistBuddy}"
    AU_TYPE="$("$plistbuddy" -c 'Print :AudioComponents:0:type' "$plist")"
    AU_CODE="$("$plistbuddy" -c 'Print :AudioComponents:0:subtype' "$plist")"
    AU_MAKER="$("$plistbuddy" -c 'Print :AudioComponents:0:manufacturer' "$plist")"
    step "AU identity: ${AU_TYPE} ${AU_CODE} ${AU_MAKER}"
else
    AU_TYPE=aufx; AU_CODE=Mfmq; AU_MAKER=Myau
fi

log "Signing (ad-hoc) and installing"
DEST="$INSTALL_ROOT/Components/${PLUGIN}.component"
run mkdir -p "$(dirname "$DEST")"
run rm -rf "$DEST"
run cp -R "$COMPONENT_SRC" "$DEST"
# Quarantine survives a copy from Downloads and blocks loading on arm64.
run xattr -dr com.apple.quarantine "$DEST"
run codesign --force --deep -s - "$DEST"
run codesign --verify --deep --strict "$DEST"

if [ "$AU_ONLY" -eq 0 ]; then
    for pair in "VST3/${PLUGIN}.vst3:VST3" "CLAP/${PLUGIN}.clap:CLAP"; do
        src="${pair%%:*}"; kind="${pair##*:}"
        dest="$INSTALL_ROOT/$kind/$(basename "$src")"
        run mkdir -p "$INSTALL_ROOT/$kind"
        run rm -rf "$dest"
        run cp -R "$RELEASE/$src" "$dest"
        run xattr -dr com.apple.quarantine "$dest"
        step "installed $kind bundle"
    done

    standalone="$RELEASE/Standalone/${PLUGIN}.app"
    if [ -e "$standalone" ]; then
        # /Applications is writable for admin accounts, but not for everyone.
        apps="/Applications"
        [ -w "$apps" ] || apps="$HOME/Applications"
        run mkdir -p "$apps"
        run rm -rf "$apps/${PLUGIN}.app"
        run cp -R "$standalone" "$apps/${PLUGIN}.app"
        step "installed the standalone app to $apps"
    fi
fi

log "Restarting the audio component registry"
# auval and Logic read a cached component list; without this the new bundle is
# invisible until the next login.
run killall -9 AudioComponentRegistrar
run sleep 3

log "auval: does the component register?"
if [ "$DRY_RUN" -eq 1 ]; then
    step "[dry-run] auval -a | grep -i ${AU_CODE}"
else
    # Not fatal on its own: the cache can lag, and auval -v below is the check
    # that decides. A missing line here only explains a failure that follows.
    if auval -a 2>&1 | grep -qi "${AU_CODE}"; then
        step "listed by auval -a"
    else
        step "not listed by auval -a yet; the validation below decides"
    fi
fi

log "auval: full validation (the same check Logic's scan does)"
if [ "$DRY_RUN" -eq 1 ]; then
    step "[dry-run] auval -v ${AU_TYPE} ${AU_CODE} ${AU_MAKER}"
else
    for attempt in 1 2 3; do
        if auval -v "$AU_TYPE" "$AU_CODE" "$AU_MAKER"; then
            step "auval passed on attempt $attempt"
            break
        fi
        if [ "$attempt" -eq 3 ]; then
            echo "auval failed three times; see the output above" >&2
            exit 1
        fi
        step "attempt $attempt failed, restarting the registrar and retrying"
        killall -9 AudioComponentRegistrar || true
        sleep 5
    done
fi

log "pluginval on the AU bundle (optional)"
if command -v pluginval > /dev/null; then
    run pluginval --strictness-level 5 --validate-in-process "$DEST"
else
    step "pluginval is not on PATH; install it with: brew install --cask pluginval"
fi

log "Done"
cat <<'EOF'
   The rest needs Logic Pro and a human. Follow docs/logic-pro-checklist.md and
   paste the result into https://github.com/ymmmtym/waives/issues/19.
EOF
