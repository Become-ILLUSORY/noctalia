#!/usr/bin/env bash
# Builds the Arch package for this fork.
#
# Used both locally and by .github/workflows/package-arch.yml, so the two paths
# cannot drift. Requires an Arch system (or Arch container) with base-devel.
#
#   ./packaging/arch/build-package.sh
#
# The finished package lands next to this script.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

version="$(<"$repo_root/VERSION")"
srcname="noctalia-immersive-$version"
tarball="$script_dir/$srcname.tar.gz"

for tool in makepkg tar; do
  if ! command -v "$tool" > /dev/null 2>&1; then
    printf 'error: %s is required (install base-devel on Arch)\n' "$tool" >&2
    exit 1
  fi
done

if [[ "$(id -u)" -eq 0 ]]; then
  printf 'error: makepkg refuses to run as root. Build as a normal user.\n' >&2
  exit 1
fi

printf '==> staging source tree %s\n' "$srcname"
staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT

mkdir -p "$staging/$srcname"
tar -C "$repo_root" \
  --exclude='.git' \
  --exclude='build*' \
  --exclude='packaging/arch/*.pkg.tar.zst' \
  --exclude='packaging/arch/*.tar.gz' \
  -cf - . | tar -C "$staging/$srcname" -xf -

printf '==> creating %s\n' "$(basename "$tarball")"
tar -C "$staging" -czf "$tarball" "$srcname"

printf '==> running makepkg\n'
cd "$script_dir"
# Extra arguments are forwarded, so `build-package.sh -si` also installs the
# package and its dependencies (makepkg's -s pulls them via sudo).
makepkg -f --noconfirm "$@"

printf '\n==> built:\n'
ls -la "$script_dir"/*.pkg.tar.zst
printf '\nInstall with:  sudo pacman -U %s\n' "$script_dir"/*.pkg.tar.zst
