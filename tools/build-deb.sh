#!/bin/sh
# Builds a .deb of the committed code into build-deb/.
#
# dpkg-buildpackage writes its results into the parent folder of the source
# tree. To keep everything inside the project, this script exports the
# current commit (uncommitted changes are NOT included) to
# build-deb/palang-<version>/ and builds there, so the .deb ends up in
# build-deb/.
#
# Usage: tools/build-deb.sh
set -eu

cd "$(dirname "$0")/.."
version=$(sed -n '1s/^palang (\([^-)]*\).*/\1/p' debian/changelog)
out=build-deb
src="$out/palang-$version"

rm -rf "$out"
mkdir -p "$src"
git archive HEAD | tar -x -C "$src"

cd "$src"
# -b: binary package only; -us -uc: don't sign.
dpkg-buildpackage -b -us -uc

echo
echo "Built: $(ls ../*.deb)"
