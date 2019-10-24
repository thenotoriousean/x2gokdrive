# %%exclude %%{_prefix}lib/debug/usr/bin/x2gokdrive*
# gets created, but we cannot exclude it under %%files
# without rpmlint throwing an error.
%define _debugsource_packages 0

%global _hardened_build 1

Name:           x2gokdrive
Version:        0.0.0.1
Release:        0.0x2go1%{?dist}
Summary:        KDrive graphical server backend for X2GoServer

%if 0%{?fedora} || 0%{?rhel}
Group:          Applications/Communications
# Per debian/copyright, only the testscripts folder is GPLv2
License:        GPLv2+ and GPLv2
%else
Group:          Productivity/Networking/Remote Desktop
License:        GPL-2.0+
%endif

URL:            https://www.x2go.org
Source0:        https://code.x2go.org/releases/source/%{name}/%{name}-%{version}.tar.gz

# Required specifically for x2gokdrive
BuildRequires:  libpng-devel
%if 0%{?suse_version}
BuildRequires:  lsb-release
%else
BuildRequires:  redhat-lsb
%endif
# x2gokdrive provides patch dirs for xorg-x11-server-source in quilt format
BuildRequires:  quilt

# XCB bits for Xephyr
# Copied and pasted block from:
# https://src.fedoraproject.org/rpms/xorg-x11-server/blob/master/f/xorg-x11-server.spec

BuildRequires:  pkgconfig(xcb-aux)
BuildRequires:  pkgconfig(xcb-image)
BuildRequires:  pkgconfig(xcb-icccm)
BuildRequires:  pkgconfig(xcb-keysyms)
BuildRequires:  pkgconfig(xcb-renderutil)
# Copied and pasted block from:
# https://src.fedoraproject.org/rpms/tigervnc/blob/master/f/tigervnc.spec
# Copied because almost all of them are required for xorg-x11-server-source
#
# Note that TigerVNC upstream does not provide distro-neutral packaging for us
# to use as a reference, just el6 and el7 specific .spec files.
# https://github.com/TigerVNC/tigervnc/tree/master/contrib/packages/rpm
BuildRequires:  gcc-c++
BuildRequires:  libX11-devel
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  gettext
BuildRequires:  gettext-autopoint
BuildRequires:  libXext-devel
BuildRequires:  xorg-x11-server-source
BuildRequires:  libXi-devel
BuildRequires:  xorg-x11-xtrans-devel
BuildRequires:  xorg-x11-util-macros
BuildRequires:  libXtst-devel
BuildRequires:  libxkbfile-devel
BuildRequires:  openssl-devel
BuildRequires:  libpciaccess-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  libXinerama-devel
BuildRequires:  freetype-devel
BuildRequires:  libXdmcp-devel
BuildRequires:  libxshmfence-devel
BuildRequires:  desktop-file-utils
BuildRequires:  java-devel
BuildRequires:  jpackage-utils
BuildRequires:  libjpeg-turbo-devel
BuildRequires:  gnutls-devel
BuildRequires:  pam-devel
BuildRequires:  libdrm-devel
BuildRequires:  libXt-devel
BuildRequires:  pixman-devel
BuildRequires:  systemd
BuildRequires:  cmake
%if 0%{?fedora} > 24 || 0%{?rhel} >= 7
BuildRequires:  libXfont2-devel
%else
BuildRequires:  libXfont-devel
%endif
BuildRequires:  xorg-x11-server-devel

# Copied and pasted "server" subpackage block from:
# https://github.com/TigerVNC/tigervnc/blob/master/contrib/packages/rpm/el7/SPECS/tigervnc.spec
# (With TigerVNC specific stuff removed, such as Perl for their launch scripts.)
Requires:       xorg-x11-xauth
Requires:       xorg-x11-xinit

# Copied and pasted "server-minimal" subpackage block from above
Requires:       xkeyboard-config
Requires:       xorg-x11-xkb-utils

%if 0%{?rhel} <= 7
Requires:       mesa-dri-drivers
Requires:       x2goserver >= 4.2.0.0
%else
Recommends:     mesa-dri-drivers
Recommends:     x2goserver >= 4.2.0.0
%endif

%description
X2Go is a server based computing environment with
   - session resuming
   - low bandwidth support
   - session brokerage support
   - client-side mass storage mounting support
   - client-side printing support
   - audio support
   - authentication by smartcard and USB stick

This package is built from the X.org xserver module. X2Go KDrive is a
KDrive-based Xserver for X2Go. It provides support for running modern
desktop environments like GNOME, KDE Plasma, Cinnamon, etc. in X2Go
Sessions.

The X2Go KDrive graphical backend is not suitable for low bandwidth WAN
connections between X2Go Client and X2Go Server. It is supposed for X2Go
being used on the local area network.

More information about X.Org can be found at:
<URL:https://www.x.org>

More information about X2Go can be found at:
<URL:https://wiki.x2go.org>


%prep
# %%autosetup creates BUILD subdir
%autosetup

# prepare xorg-server build tree
cp -r '/usr/share/xorg-x11-server-source/'* 'BUILD/'
# Precaution from:
# https://src.fedoraproject.org/rpms/tigervnc/blob/master/f/tigervnc.spec
find 'BUILD/' -type 'f' -perm '-001' -print0 | while read -r -d '' file; do
  chmod -x "${file}"
done
mkdir -p 'BUILD/hw/kdrive/x2gokdrive/'

# inject x2gokdrive into xorg-server build tree
cp 'Makefile.am' *'.c' *'.h' 'BUILD/hw/kdrive/x2gokdrive/'
cp -r 'man/' 'BUILD/hw/kdrive/x2gokdrive/'

# patch xorg-server build tree, so that it will build x2gokdrive
set -x
export XORG_UPSTREAM_VERSION="$(grep 'AC_INIT' 'BUILD/configure.ac' | sed -r 's/^AC_INIT[^,]*, ([^,]+),.*/\\1/')"
cd 'BUILD'
if [ -d "../patches.xorg/${XORG_UPSTREAM_VERSION}" ]; then
  QUILT_PATCHES="../patches.xorg/${XORG_UPSTREAM_VERSION}/" quilt push -a
else
  (
    set +x
    printf '\n##################################################\nERROR: This X2Go KDrive version does not support\nbuilding against X.Org version %s.\n##################################################\n' "${XORG_UPSTREAM_VERSION}"
    exit '1'
  )
fi

%build
%ifarch sparcv9 sparc64 s390 s390x
export CFLAGS="$RPM_OPT_FLAGS -fPIC"
%else
export CFLAGS="$RPM_OPT_FLAGS -fpic"
%endif
export CXXFLAGS="$CFLAGS"

pushd 'BUILD'
autoreconf -fvi
# Another block from tigervnc.spec, except for the 1st option line.
%configure \
        --enable-kdrive --enable-x2gokdrive \
        --disable-xorg --disable-xnest --disable-xvfb --disable-dmx \
        --disable-xwin --disable-xephyr --disable-xwayland \
        --with-pic --disable-static \
        --with-default-font-path="catalogue:%{_sysconfdir}/X11/fontpath.d,built-ins" \
        --with-fontdir=%{_datadir}/X11/fonts \
        --with-xkb-output=%{_localstatedir}/lib/xkb \
        --enable-install-libxf86config \
        --enable-glx --disable-dri --enable-dri2 --disable-dri3 \
        --disable-unit-tests \
        --disable-config-hal \
        --disable-config-udev \
        --with-dri-driver-path=%{_libdir}/dri \
        --without-dtrace \
        --disable-devel-docs \
        --disable-selective-werror
make %{?_smp_mflags}
popd

%install
pushd 'BUILD'
make install DESTDIR='%{buildroot}'
popd
find '%{buildroot}/usr/lib/xorg' -type 'f' -name '*.la' -exec rm -fv '{}' ';'

%files
%defattr(-,root,root)
%{_bindir}/x2gokdrive
%exclude %{_libdir}/xorg/protocol.txt
%exclude %{_mandir}/man1/Xserver.1.gz
%exclude %{_var}/lib/xkb/
%doc %{_mandir}/man1/x2gokdrive.1.gz

%changelog
