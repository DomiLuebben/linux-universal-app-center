# Maintainer: Dominik Lübben <dominikluebben@googlemail.com>
pkgname=linux-update-tool
pkgver=1.0.0
pkgrel=1
pkgdesc="Native system update tool with honest byte-weighted progress for DNF5, APT, and Pacman"
arch=('x86_64')
url="https://github.com/DomiLuebben/linux-update-tool"
license=('GPL-3.0-or-later')
depends=('qt6-base' 'qt6-declarative' 'qt6-svg' 'polkit-qt6' 'sqlite' 'pacman' 'pacman-contrib' 'kirigami' 'qqc2-desktop-style')
makedepends=('cmake' 'gcc')
optdepends=('linux-package-installer>=1.4.0: AUR-Aktualisierungen auf Arch und Derivaten')
checkdepends=('desktop-file-utils' 'appstream')

build() {
    rm -rf "${srcdir}/build"
    cmake -B "${srcdir}/build" -S "${startdir}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "${srcdir}/build"
}

check() {
    ctest --test-dir "${srcdir}/build" --output-on-failure
    QT_QPA_PLATFORM=offscreen "${srcdir}/build/linux-update-tool/linux-update-tool" --check-qml
    desktop-file-validate "${startdir}/data/org.linuxupdatetool.desktop"
    appstreamcli validate --no-net "${startdir}/data/org.linuxupdatetool.metainfo.xml"
}

package() {
    DESTDIR="${pkgdir}" cmake --install "${srcdir}/build"
}
