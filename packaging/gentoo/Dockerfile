FROM gentoo/portage@sha256:33c8c36bb0b7c26cb0c5b4cdece4e47424cfd4da643746c76b5b00a7d1b12c21 AS portage
FROM gentoo/stage3@sha256:1ca7647d1ce968d02c2858ee4201e2173675fd3fc0754a8acd583d9288b75478 AS base
COPY --from=portage /var/db/repos/gentoo /var/db/repos/gentoo
RUN printf '\nABI_X86="64"\n' >> /etc/portage/make.conf
FROM base AS build
RUN mkdir -p /etc/portage/package.use && printf 'x11-base/xorg-server xvfb\n' > /etc/portage/package.use/loungecat-ci
# Only dependencies are binary packages. LoungeCat itself is built by Portage
# from the exact release source archive during the offline test job.
RUN emerge --getbinpkg --usepkgonly --binpkg-respect-use=y --oneshot --verbose \
    dev-build/cmake dev-build/ninja dev-util/pkgconf dev-vcs/git \
    dev-qt/qtbase:6 dev-qt/qtsvg:6 media-libs/alsa-lib \
    dev-util/desktop-file-utils x11-base/xorg-server[xvfb] x11-misc/xdotool \
    media-fonts/dejavu
RUN emerge --getbinpkg --oneshot dev-util/pkgcheck
WORKDIR /src
