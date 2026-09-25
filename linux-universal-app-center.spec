Name:           linux-universal-app-center
Version:        1.8.1
Release:        6%{?dist}
Summary:        Natives Anwendungszentrum und Systemaktualisierung mit ehrlichem Fortschritt

License:        GPL-3.0-or-later
URL:            https://github.com/DomiLuebben/linux-universal-app-center
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.24
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  polkit-qt6-1-devel
BuildRequires:  sqlite-devel
BuildRequires:  desktop-file-utils
BuildRequires:  appstream
BuildRequires:  appstream-devel
BuildRequires:  appstream-qt-devel
BuildRequires:  kf6-kirigami-devel
BuildRequires:  kf6-kservice-devel
BuildRequires:  kf6-kio-devel
BuildRequires:  systemd-rpm-macros

Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       qt6-qtsvg
Requires:       polkit-qt6-1
Requires:       sqlite
Requires:       kf6-kirigami
Requires:       kf6-kservice
Requires:       kf6-kio
Requires:       appstream
Requires:       appstream-qt
Requires:       polkit
Requires:       systemd
Requires:       hicolor-icon-theme

Provides:       linux-app-store = %{version}-%{release}
Provides:       linux-update-tool = %{version}-%{release}
Obsoletes:      linux-app-store < %{version}-%{release}
Obsoletes:      linux-update-tool < %{version}-%{release}

%description
Nativer Anwendungs-Store und Systemaktualisierungswerkzeug mit ehrlicher
Fortschrittsanzeige für Fedora, Arch und Debian. Unterstützt DNF5,
Flatpak, Snap, Pacstall und Repository-Verwaltung.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install
ln -s %{name} %{buildroot}%{_bindir}/linux-app-store
ln -s %{name} %{buildroot}%{_bindir}/linux-update-tool

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/org.linuxuniversalappcenter.desktop
appstreamcli validate --no-net %{buildroot}%{_datadir}/metainfo/org.linuxuniversalappcenter.metainfo.xml

%post
%systemd_post lutd.service

%preun
%systemd_preun lutd.service

%postun
%systemd_postun_with_restart lutd.service

%files
%doc docs
%{_bindir}/linux-universal-app-center
%{_bindir}/linux-app-store
%{_bindir}/linux-update-tool
%{_libexecdir}/lutd
%dir %{_libexecdir}/linux-update-tool
%{_libexecdir}/linux-update-tool/lut-apt-guard
%{_unitdir}/lutd.service
%{_datadir}/applications/org.linuxuniversalappcenter.desktop
%{_datadir}/metainfo/org.linuxuniversalappcenter.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/org.linuxuniversalappcenter.svg
%{_datadir}/dbus-1/system.d/org.linuxupdatetool.Daemon1.conf
%{_datadir}/dbus-1/system-services/org.linuxupdatetool.Daemon1.service
%{_datadir}/polkit-1/actions/org.linuxupdatetool.policy

%changelog
* Fri Sep 25 2026 Dominik Lübben <dominikluebben@googlemail.com> - 1.8.1-6
- Store: Icon-Themenprüfung über einen einmaligen Namensindex statt
  QIcon::hasThemeIcon je App (Katalog lädt in ~7 statt ~16-35 s)
- Store: Nach Paketaktionen nur den Installationsstatus neu ermitteln statt
  den kompletten AppStream-Katalog neu einzulesen
- DNF5: RPM-Dateiliste je Ladevorgang nur noch einmal lesen
- DNF5: Installieren/Entfernen ohne --refresh aller Repositories

* Fri Sep 25 2026 Dominik Lübben <dominikluebben@googlemail.com> - 1.8.1-5
- Store: Wurde lutd durch die Installation neu gestartet, band sich die GUI
  an die eigene Planung wieder an und öffnete das Fortschrittsfenster; die
  Vorschau ließ sich nicht mehr bestätigen
- D-Bus-Aufrufe warten bis zu 5 Minuten auf die Polkit-Anmeldung statt 25 s
- Keine zweite Polkit-Abfrage über die Altschnittstelle nach abgelehnter
  Freigabe

* Fri Sep 25 2026 Dominik Lübben <dominikluebben@googlemail.com> - 1.8.1-4
- Store: Paketzuordnung der Desktop-/Metainfo-Dateien über eine einzige
  RPM-Abfrage statt je Datei einem dnf5-Aufruf; der Katalog blieb dadurch
  minutenlang leer
- DNF5: lesende Abfragen nur aus dem Cache (--cacheonly)

* Fri Sep 25 2026 Dominik Lübben <dominikluebben@googlemail.com> - 1.8.1-3
- lutd: Planrevision für DNF5 an die GUI senden (Installieren/Entfernen im
  Store scheiterte mit "Die bestätigte Planrevision fehlt")
- lutd: Plan und Absicht je Transaktion zurücksetzen; Wiederanbinden meldet
  laufende Commits und Systemupdate-Pläne korrekt
- Store: DNF5-Abfragen nur aus dem System-Cache, letzter Paketbestand bleibt
  bei fehlgeschlagener Abfrage erhalten
- DNF5: Vorbereitungs-Timeout auf 60 Minuten erhöht

* Thu Sep 24 2026 Dominik Lübben <dominikluebben@googlemail.com> - 1.8.1-2
- lutd.service: Sandbox entfernt, DNF5 verweigerte darin den Commit

* Thu Sep 24 2026 Dominik Lübben <dominikluebben@googlemail.com> - 1.8.1-1
- Initial RPM release of linux-universal-app-center 1.8.1
