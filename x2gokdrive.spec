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
BuildRequires:  xorg-x11-server-source
# Selecting libjpeg-turbo is quite difficult.
%if 0%{?suse_version}
%if 0%{?sle_version} >= 120200
# Recent *SuSE versions call this "libturbojpeg".
BuildRequireS:  pkgconfig(libturbojpeg)
%else
# Older ones have either libjpeg8-devel or libjpeg62-devel and don't define
# any pkgconfig() virtual provide. Pick libjpeg62-devel since that's most
# likely the turbo variant.
BuildRequires:  libjpeg62-devel
%endif
%else
# RHEL/CentOS 6+ and all supported Fedora versions ship libjpeg-turbo, but call
# it libjpeg.
BuildRequires:  pkgconfig(libjpeg)
%endif
BuildRequires:  libpng-devel
BuildRequires:  quilt
%if 0%{?suse_version}
BuildRequires:  lsb-release
%else
BuildRequires:  redhat-lsb
%endif
# x2gokdrive provides patch dirs for xorg-x11-server-source in quilt format

# XCB bits for Xephyr
# Copied/synced with debian/control.
BuildRequires:  pkgconfig(xcb) >= 1
BuildRequires:  pkgconfig(xcb-xkb)
BuildRequires:  pkgconfig(xcb-shape)
BuildRequires:  pkgconfig(xcb-render)
BuildRequires:  pkgconfig(xcb-renderutil)
BuildRequires:  pkgconfig(xcb-util)
BuildRequires:  pkgconfig(xcb-image)
BuildRequires:  pkgconfig(xcb-icccm)
BuildRequires:  pkgconfig(xcb-shm)
BuildRequires:  pkgconfig(xcb-keysyms)
BuildRequires:  pkgconfig(xcb-randr)
BuildRequires:  pkgconfig(xcb-xv)
BuildRequires:  pkgconfig(xcb-glx)
BuildRequires:  pkgconfig(xcb-xf86dri)

# Dependencies for xorg-x11-server.
BuildRequires:  gcc-c++
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  gettext
%if 0%{?suse_version}
BuildRequires:  gettext-tools
%else
# This is really just a virtual provides for gettext-devel.
# We might want to spell it out directly and only once, also for *SuSE, but
# then we would be pulling in a "real" package on non-*SuSE, but a virtual
# one on *SuSE, since *SuSE only has gettext-{runtime,tools} as "real" packages
# with gettext-tools providing the virtual gettext-devel package.
BuildRequires:  gettext-autopoint
%endif
BuildRequires:  bison
BuildRequires:  flex
BuildRequires:  imake
BuildRequires:  pkgconfig(fontutil)
BuildRequires:  pkgconfig(xproto)
BuildRequires:  pkgconfig(damageproto)
BuildRequires:  pkgconfig(fixesproto)
BuildRequires:  pkgconfig(fontsproto)
BuildRequires:  pkgconfig(kbproto)
BuildRequires:  pkgconfig(xineramaproto)
BuildRequires:  pkgconfig(randrproto)
BuildRequires:  pkgconfig(recordproto)
BuildRequires:  pkgconfig(renderproto)
BuildRequires:  pkgconfig(resourceproto)
BuildRequires:  pkgconfig(scrnsaverproto)
BuildRequires:  pkgconfig(videoproto)
BuildRequires:  pkgconfig(xcmiscproto)
BuildRequires:  pkgconfig(xextproto)
BuildRequires:  pkgconfig(xf86bigfontproto)
BuildRequires:  pkgconfig(xf86dgaproto)
BuildRequires:  pkgconfig(xf86vidmodeproto)
BuildRequires:  pkgconfig(presentproto)
BuildRequires:  pkgconfig(bigreqsproto)
BuildRequires:  pkgconfig(compositeproto)
BuildRequires:  pkgconfig(glproto)
BuildRequires:  pkgconfig(xtrans)
BuildRequires:  pkgconfig(xau)
BuildRequires:  pkgconfig(xdmcp)
%if 0%{?fedora} > 24 || 0%{?rhel} >= 7 || 0%{?sle_version} >= 120300
BuildRequires:  pkgconfig(xfont2)
%else
BuildRequires:  pkgconfig(xfont)
%endif
BuildRequires:  pkgconfig(xkbfile)
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(pciaccess)
%if 0%{?fedora} > 28 || 0%{?rhel} > 7 || 0%{?suse_version} > 1500
BuildRequires:  pkgconfig(libgcrypt)
%else
# Older libgcrypt-devel packages do not have a pkgconfig(libgcrypt) provides
# line.
BuildRequires:  libgcrypt-devel
%endif
BuildRequires:  pkgconfig(nettle)
BuildRequires:  pam-devel
# We probably won't need some libudev-devel equivalent because we disable that
# feature anyway.
# Same goes for pkgconfig(libselinux).
BuildRequires:  pkgconfig(audit)
BuildRequires:  pkgconfig(auparse)
# Same goes for pkgconfig(libdrm).
BuildRequires:  pkgconfig(gl)
BuildRequires:  pkgconfig(libunwind)
BuildRequires:  pkgconfig(xmuu)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xrender)
BuildRequires:  pkgconfig(xi)
BuildRequires:  pkgconfig(xpm)
BuildRequires:  pkgconfig(xaw7)
BuildRequires:  pkgconfig(xt)
BuildRequires:  pkgconfig(xmu)
BuildRequires:  pkgconfig(xtst)
BuildRequires:  pkgconfig(xres)
BuildRequires:  pkgconfig(xfixes)
BuildRequires:  pkgconfig(xv)
BuildRequires:  pkgconfig(xinerama)
# RPM-specific... probably?
BuildRequires:  pkgconfig(xorg-macros)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(xshmfence)

%description
X2Go is a server based computing environment with
   - session resuming
   - low bandwidth support
   - session brokerage support
   - client-side mass storage mounting support
   - client-side printing support
   - audio support
   - authentication by smartcard and USB stick

X2Go KDrive is a KDrive-based Xserver for X2Go. It provides support for
running modern desktop environments like GNOME, KDE Plasma, Cinnamon, etc.
in X2Go Sessions.

The X2Go KDrive graphical backend is not suitable for low bandwidth WAN
connections between X2Go Client and X2Go Server. It is supposed for X2Go
being used on the local area network.

More information about X.Org can be found at:
<URL:https://www.x.org>

More information about X2Go can be found at:
<URL:https://wiki.x2go.org>


%package -n xorg-x11-server-x2gokdrive
Summary:        KDrive graphical server backend for X2GoServer
Requires:       xorg-x11-server-common >= 1.20.3
%if 0%{?rhel} > 8 || 0%{?suse_version}
Recommends:     mesa-dri-drivers
Recommends:     x2goserver >= 4.2.0.0
%else
Requires:       mesa-dri-drivers
Requires:       x2goserver >= 4.2.0.0
%endif

%description -n xorg-x11-server-x2gokdrive
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
%if 0%{?fedora} >= 19 || 0%{?suse_version} >= 1310 || 0%{?sle_version} >= 120400 || 0%{?rhel} >= 7
%autosetup
%else
%setup
%endif

# prepare xorg-server build tree
mkdir -p 'BUILD'
%if 0%{?suse_version}
cp -av '/usr/src/xserver/'* 'BUILD/'
%else
cp -av '/usr/share/xorg-x11-server-source/'* 'BUILD/'
%endif
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
export XORG_UPSTREAM_VERSION="$(grep 'AC_INIT' 'BUILD/configure.ac' | sed -r 's/^AC_INIT[^,]*, ([^,]+),.*/\1/')"
pushd 'BUILD'
if [ -d "../patches.xorg/${XORG_UPSTREAM_VERSION}" ]; then
  QUILT_PATCHES="../patches.xorg/${XORG_UPSTREAM_VERSION}/" quilt push -a
  if [ -d "../patches.xorg/${XORG_UPSTREAM_VERSION}/missing" ]; then
    # Hack used to install missing files.
    # This is actually working around problems in the upstream
    # xorg-server-source packages, which will have to be reported upstream.
    cp -av "../patches.xorg/${XORG_UPSTREAM_VERSION}/missing/"* 'BUILD/'
  fi
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
# Clean up old configure files that might have had the executable bit set.
# Will be regenerated later on.
rm -f 'configure' 'config.'{sub,guess} 'depcomp' 'install-sh' 'compile' 'ltmain.sh' 'missing' 'ylwrap'
autoreconf -fvi
# The RPM macro for the linker flags does not exist on EPEL
%{!?__global_ldflags: %global __global_ldflags -Wl,-z,relro}
# disable-static is so we don't get libfoo.a for modules.  now if only we could
# kill the .las.
%configure \
	--libexecdir='%{_prefix}/lib/xorg' \
	--with-module-dir='%{_prefix}/lib/xorg/modules' \
	--with-serverconfig-path='%{_libexecdir}' \
	--disable-silent-rules \
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
	--disable-xorg \
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
# find '%{buildroot}/usr/lib/xorg' -type 'f' -name '*.la' -exec rm -fv '{}' ';'

%files -n xorg-x11-server-x2gokdrive
%defattr(-,root,root)
%{_bindir}/x2gokdrive
# Exclude protocol.txt, it's shipped by xorg-x11-server-common and we don't
# want to conflict with that package.
%exclude %{_libexecdir}/protocol.txt
%exclude %{_mandir}/man1/Xserver.1.gz
%exclude %{_var}/lib/xkb/
%doc %{_mandir}/man1/x2gokdrive.1.gz

%changelog
