#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test "$(uname -m)" = x86_64
python3 packaging/check-source.py
build="$PWD/build/rpm"
dist="$PWD/dist-rpm"
mkdir -p "$dist/test-support"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr -DLOUNGECAT_ALSA=ON \
    '-DLOUNGECAT_RPM_REQUIRES=qt6-qtbase, qt6-qtbase-gui, openssl-libs, ca-certificates'
cmake --build "$build" --parallel "${BUILD_JOBS:-2}"
desktop-file-validate packaging/io.github.binkiewka.LoungeCat.desktop
cpack --config "$build/CPackConfig.cmake" -B "$dist" -G RPM
c++ -std=c++20 -fPIC packaging/runtime-probe.cpp -o "$dist/test-support/runtime-probe" \
    $(pkg-config --cflags --libs Qt6Widgets Qt6Sql Qt6Network Qt6Svg)
cp packaging/smoke.py tests/fixtures/localhost-{cert,key}.pem "$dist/test-support/"
rpm -qa --qf '%{NAME}\t%{VERSION}-%{RELEASE}\n' | sort > "$dist/rpm-build-dependencies.tsv"
python3 packaging/write-manifest.py --kind rpm --dist "$dist"
