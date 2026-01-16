# Copyright 2024 LoungeCat
# Distributed under the terms of the MIT License

EAPI=8

inherit unpacker xdg

DESCRIPTION="Modern IRC Client for Linux"
HOMEPAGE="https://github.com/binkiewka/LoungeCat-Desktop"
SRC_URI="https://github.com/binkiewka/LoungeCat-Desktop/releases/download/v${PV}/LoungeCat_${PV}_amd64.deb"

LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64"

RDEPEND="
    virtual/jre
    media-libs/alsa-lib
    x11-libs/libX11
    x11-libs/libXext
    x11-libs/libXi
    x11-libs/libXrender
    x11-libs/libXtst
"

S="${WORKDIR}"

src_unpack() {
    unpack_deb ${A}
}

src_install() {
    # unpack_deb extracts content to ${S}
    # We copy binaries and share folders to ${D}
    
    # Typical structure of our deb: /opt/LoungeCat or /usr/bin/LoungeCat
    # We need to preserve the hierarchy found in the deb.
    
    # Assuming the deb installs to /opt/LoungeCat
    # We just copy everything from S to D
    
    cp -a "${S}"/* "${D}"/ || die
}
