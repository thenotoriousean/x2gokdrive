# %%exclude %%{_prefix}lib/debug/usr/bin/x2gokdrive*
# gets created, but we cannot exclude it under %%files
# without rpmlint throwing an error.
#%%define _debugsource_packages 0

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
# The RPM macro for the linker flags does not exist on EPEL
%{!?__global_ldflags: %global __global_ldflags -Wl,-z,relro}
# disable-static is so we don't get libfoo.a for modules.  now if only we could
# kill the .las.
%configure \
	--libexecdir='%{_prefix}/lib/xorg' \
	--with-module-dir='%{_prefix}/lib/xorg/modules' \
	--with-serverconfig-path='%{_libexecdir}' \
	--disable-static \
	--without-dtrace \
	--disable-strict-compilation \
	--disable-debug \
	--with-int10=x86emu \
	--with-os-vendor="$(lsb_release -i -s)" \
	--with-builderstring="%{name} %{version} (https://wiki.x2go.org)" \
	--with-xkb-path=%{_datadir}/X11/xkb \
	--with-xkb-output=%{_localstatedir}/lib/xkb \
	--with-shared-memory-dir=/dev/shm \
	--enable-mitshm \
	--enable-xres \
	--disable-xcsecurity \
	--disable-tslib \
	--enable-dbe \
	--disable-xf86bigfont \
	--enable-dpms \
	--enable-xorg \
	--disable-linux-acpi \
	--disable-linux-apm \
	--disable-xquartz \
	--disable-xwin \
	--disable-xnest \
	--disable-xfake \
	--disable-xfbdev \
	--disable-install-setuid \
	--disable-xshmfence \
	--disable-config-hal \
	--disable-config-udev \
	--with-default-font-path="catalogue:%{_sysconfdir}/X11/fontpath.d,built-ins" \
	--enable-composite \
	--enable-record \
	--enable-xv \
	--disable-xvmc \
	--disable-dga \
	--enable-screensaver \
	--disable-xdmcp \
	--disable-xdm-auth-1 \
	--enable-glx \
	--enable-present \
	--enable-xinerama \
	--enable-xf86vidmode \
	--enable-xace \
	--disable-xfree86-utils \
	--disable-suid-wrapper \
	--disable-dmx \
	--disable-xvfb \
	--enable-kdrive \
	--enable-x2gokdrive \
	--disable-xephyr \
	--disable-wayland \
	--with-sha1=libgcrypt \
	--enable-xcsecurity \
	--disable-dri3 \
	--disable-xselinux \
	--disable-systemd-logind \
	--without-systemd-daemon \
	--disable-dri \
	--disable-dri2 \
	--disable-glamor \
	--enable-libunwind \
	--disable-libdrm \
	--enable-unit-tests \
	CPPFLAGS="${CPPFLAGS} %{?__global_cppflags} -DPRE_RELEASE=0" \
	LDFLAGS='%{__global_ldflags}'
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
