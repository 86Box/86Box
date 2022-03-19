Name:		86Box
Version:	3.3
Release:	1%{?dist}
Summary:	Classic PC emulator
License:	GPLv2
URL:		https://86box.net

Source0:	https://github.com/86Box/86Box/archive/refs/tags/v%%{version}.tar.gz

BuildRequires: cmake
BuildRequires: desktop-file-utils
BuildRequires: extra-cmake-modules
BuildRequires: gcc-c++
BuildRequires: libFAudio-devel
BuildRequires: libappstream-glib
BuildRequires: libevdev-devel
BuildRequires: libXi-devel
BuildRequires: ninja-build
BuildRequires: qt5-linguist
BuildRequires: qt5-qtconfiguration-devel
BuildRequires: qt5-qtbase-private-devel
BuildRequires: qt5-qtbase-static
BuildRequires: rtmidi-devel
BuildRequires: wayland-devel
BuildRequires: SDL2-devel

Requires: hicolor-icon-theme
Requires: fluid-soundfont-gm

%description
86Box is a hypervisor and IBM PC system emulator that specializes in
running old operating systems and software designed for IBM
PC systems and compatibles from 1981 through fairly recent
system designs based on the PCI bus.

It supports various models of PCs, graphics and sound cards, and CPUs.

%prep
%autosetup -p1

%build
%ifarch x86_64
%cmake -DRELEASE=on
%else
%cmake -DRELEASE=on -DDYNAREC=off
%endif
%cmake_build

%install
%cmake_install

# install icons
for i in 48 64 72 96 128 192 256 512; do
  mkdir -p $RPM_BUILD_ROOT%{_datadir}/icons/hicolor/${i}x${i}/apps
  cp src/unix/assets/${i}x${i}/net.86box.86Box.png $RPM_BUILD_ROOT%{_datadir}/icons/hicolor/${i}x${i}/apps/net.86box.86Box.png
done

desktop-file-install --dir=%{buildroot}%{_datadir}/applications src/unix/assets/net.86box.86Box.desktop
mkdir -p %{buildroot}%{_metainfodir}
cp src/unix/assets/net.86box.86Box.metainfo.xml %{buildroot}%{_metainfodir}
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/net.86box.86Box.metainfo.xml

%files
%license COPYING
%{_bindir}/86Box
%{_datadir}/applications/net.86box.86Box.desktop
%{_metainfodir}/net.86box.86Box.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/net.86box.86Box.png

%changelog
* Thu Mar 17 2022 Robert de Rooy <robert.de.rooy[AT]gmail.com> 3.2.1-1
- Initial RPM release
