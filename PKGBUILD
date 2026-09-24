# Maintainer: Dominik Lübben <dominikluebben@googlemail.com>
pkgname=linux-universal-app-center
pkgver=1.8.1
pkgrel=1
pkgdesc="Native application store and system update tool with honest progress for Arch, Fedora, and Debian"
arch=('x86_64')
url="https://github.com/DomiLuebben/linux-app-store"
license=('GPL-3.0-or-later')
depends=('qt6-base' 'qt6-declarative' 'qt6-svg' 'polkit-qt6' 'sqlite' 'pacman' 'pacman-contrib' 'kirigami' 'qqc2-desktop-style' 'appstream-qt' 'archlinux-appstream-data' 'kservice' 'kio')
# dbus liefert dbus-run-session; CMake verlangt es bereits beim Konfigurieren
# für den Bus-Integrationstest, also gehört es zu den Build-Abhängigkeiten.
makedepends=('cmake' 'gcc' 'dbus')
optdepends=('flatpak: Unterstützung für Flatpak-Anwendungen und Updates'
            'git: AUR-Aktualisierungen holen'
            'base-devel: AUR-Pakete mit makepkg bauen')
# fakeroot führt den ALPM-Installationsdurchstich in einem Wegwerf-Wurzelverzeichnis aus.
checkdepends=('desktop-file-utils' 'appstream' 'fakeroot')
# Vorgänger: linux-update-tool (bis 1.2), linux-app-store (bis 1.7). pacman ersetzt beide.
provides=('linux-app-store=1.8.1' 'linux-update-tool=1.8.1')
conflicts=('linux-app-store' 'linux-update-tool')
replaces=('linux-app-store' 'linux-update-tool')
install=linux-universal-app-center.install

build() {
    rm -rf "${srcdir}/build"
    cmake -B "${srcdir}/build" -S "${startdir}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "${srcdir}/build"
}

check() {
    ctest --test-dir "${srcdir}/build" --output-on-failure
    QT_QPA_PLATFORM=offscreen "${srcdir}/build/linux-app-store/linux-universal-app-center" --check-qml --replay "${startdir}/tests/fixtures/small-update.jsonl"
    desktop-file-validate "${startdir}/data/org.linuxuniversalappcenter.desktop"
    appstreamcli validate --no-net "${startdir}/data/org.linuxuniversalappcenter.metainfo.xml"
}

package() {
    DESTDIR="${pkgdir}" cmake --install "${srcdir}/build"
    # Rückwärtskompatible Befehle für bestehende Skripte und Starter
    ln -s linux-universal-app-center "${pkgdir}/usr/bin/linux-app-store"
    ln -s linux-universal-app-center "${pkgdir}/usr/bin/linux-update-tool"
}
