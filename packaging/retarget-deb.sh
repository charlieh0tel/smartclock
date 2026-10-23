#!/bin/sh
# Rewrite a package's relations to its sibling from one version to
# another, in place:
#   packaging/retarget-deb.sh DEB FROM TO
# smartclockmon and smartclock-utils name each other's version in
# Depends, Breaks and Replaces, written in Cargo.toml as the release
# version, since cargo-deb has no variable for it.  A snapshot is
# versioned by its build stamp instead, and its relations have to name
# that, or the two snapshots refuse each other.  `make deb` runs this on
# each snapshot it builds; a release needs nothing.
set -eu

deb=$1
from=$2
to=$3

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# A version's dots are the only characters in it a regular expression
# reads specially; the replacement's ~ and + are literal.
from_pattern=$(printf '%s' "$from" | sed 's/\./\\./g')

dpkg-deb -R "$deb" "$work/root"
sed -i -E "/^(Depends|Breaks|Replaces):/ s/\((=|<<) $from_pattern\)/(\1 $to)/g" \
    "$work/root/DEBIAN/control"
# cargo-deb owns every file by root, which an extraction by anyone else
# cannot keep; --root-owner-group puts it back.
dpkg-deb --root-owner-group -Zxz -b "$work/root" "$deb" >/dev/null
