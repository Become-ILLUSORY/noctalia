#!/usr/bin/env bash
# Builds the Arch package for this fork.
#
# Shared by local use and .github/workflows/package-arch.yml so the two paths
# cannot drift. Requires an Arch system (or Arch container) with base-devel.
#
#   ./packaging/arch/build-package.sh          # build
#   ./packaging/arch/build-package.sh -si      # build and install
#
# The finished package lands next to this script.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v makepkg > /dev/null 2>&1; then
  printf 'error: makepkg not found; install base-devel (pacman -S base-devel)\n' >&2
  exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
  printf 'error: makepkg refuses to run as root. Run this as a normal user.\n' >&2
  exit 1
fi

printf '==> building package in %s\n' "$script_dir"
cd "$script_dir"

# -f rebuilds even if a package for this version already exists; extra arguments
# are forwarded, so `-si` also installs the result.
makepkg -f --noconfirm "$@"

printf '\n==> built:\n'
ls -la "$script_dir"/*.pkg.tar.zst
printf '\nInstall with:  sudo pacman -U %s\n' "$script_dir"/*.pkg.tar.zst
