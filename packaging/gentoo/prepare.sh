#!/usr/bin/env bash
set -euo pipefail
python3 packaging/check-source.py
version=$(sed -n 's/^project(LoungeCat VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
mkdir -p build/gentoo dist-gentoo
source="$PWD/build/gentoo/LoungeCat-${version}-source.tar.gz"
git archive --format=tar.gz --prefix="LoungeCat-${version}-source/" --output="$source" HEAD
python3 packaging/gentoo/make-overlay.py --version "$version" --source "$source" --output dist-gentoo
python3 packaging/write-manifest.py --kind gentoo --dist dist-gentoo --source "$source"
