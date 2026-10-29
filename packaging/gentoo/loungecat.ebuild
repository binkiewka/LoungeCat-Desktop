# Copyright 2026 LoungeCat contributors
# SPDX-License-Identifier: GPL-3.0-or-later

EAPI=8

inherit cmake

DESCRIPTION="Native Qt multi-network IRC client"
HOMEPAGE="https://github.com/binkiewka/LoungeCat-Desktop"
SRC_URI="https://github.com/binkiewka/LoungeCat-Desktop/releases/download/v${PV}/LoungeCat-${PV}-source.tar.gz"
S="${WORKDIR}/LoungeCat-${PV}-source"

LICENSE="GPL-3+ MIT"
SLOT="0"
KEYWORDS="~amd64"
IUSE="+alsa test"
RESTRICT="!test? ( test )"
# Tests only use local synthetic peers. Portage permits loopback during this
# phase; CI additionally runs the entire container with --network none.
PROPERTIES="test? ( test_network )"

RDEPEND="
	>=dev-qt/qtbase-6.4:6[concurrent,gui,network,sql,sqlite,ssl,widgets]
	>=dev-qt/qtsvg-6.4:6
	app-misc/ca-certificates
	alsa? ( media-libs/alsa-lib )
"
DEPEND="${RDEPEND}"
BDEPEND="alsa? ( virtual/pkgconfig )"

src_configure() {
	local mycmakeargs=(
		-DBUILD_TESTING=$(usex test ON OFF)
		-DLOUNGECAT_ALSA=$(usex alsa ON OFF)
	)
	cmake_src_configure
}

src_test() {
	QT_QPA_PLATFORM=offscreen cmake_src_test
}
