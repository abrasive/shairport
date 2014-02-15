# $Header: $

EAPI="5"

inherit flag-o-matic eutils git-2

DESCRIPTION="This program emulates an AirPort Express for the purpose of \
			streaming music from iTunes and compatible iPods and iPhones. \
			It implements a server for the Apple RAOP protocol. ShairPort \
			does not support AirPlay v2 (video and photo streaming)."
HOMEPAGE="https://github.com/abrasive/shairport"
EGIT_REPO_URI="https://github.com/abrasive/shairport.git"

SLOT="0"
KEYWORDS=""
IUSE="ao avahi pulseaudio"

DEPEND="dev-libs/openssl \
		ao? ( media-libs/libao ) \
		avahi? ( net-dns/avahi ) \
		pulseaudio? ( media-sound/pulseaudio )"
RDEPEND="${DEPEND}"

src_install() {
	emake PREFIX="${D}" install

	insinto /etc/conf.d/
	newins scripts/gentoo/openrc/conf.d.cfg shairport

	insinto /etc/init.d/
	newins scripts/gentoo/openrc/init.d.sh shairport
}
