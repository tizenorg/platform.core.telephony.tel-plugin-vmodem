%define major 0
%define minor 1
%define patchlevel 23

Name:           tel-plugin-vmodem
Version:        %{major}.%{minor}.%{patchlevel}
Release:        1
License:        Apache-2.0
Summary:        Telephony AT Virtual Modem library
Group:          System/Libraries
Source0:        tel-plugin-vmodem-%{version}.tar.gz

%if %{_with_emulator}
%else
ExcludeArch: %{arm} aarch64
%endif

BuildRequires:  cmake
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(tcore)
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
Telephony AT Modem library

%prep
%setup -q

%build
versionint=$[%{major} * 1000000 + %{minor} * 1000 + %{patchlevel}]
cmake . -DCMAKE_INSTALL_PREFIX=%{_prefix} \
	-DLIB_INSTALL_DIR=%{_libdir} \
	-DVERSION=$versionint
make %{?_smp_mflags}

%post
/sbin/ldconfig

%postun -p /sbin/ldconfig

%install
%make_install
mkdir -p %{buildroot}/usr/share/license

%files
%manifest tel-plugin-vmodem.manifest
%defattr(644,system,system,-)
#%doc COPYING
%{_libdir}/telephony/plugins/vmodem-plugin*
/usr/share/license/tel-plugin-vmodem
