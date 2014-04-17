%define major 3
%define minor 0
%define patchlevel 1

Name:       tel-plugin-vmodem
Version:        %{major}.%{minor}.%{patchlevel}
Release:        1
License:        Apache-2.0
Summary:        Telephony Plug-in for AT communication with AT Virtual Modem (emulator)
Group:          System/Libraries
Source0:    tel-plugin-vmodem-%{version}.tar.gz
Source1001: 	tel-plugin-vmodem.manifest
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  cmake
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(tcore)

%description
Telephony AT Modem library

%prep
%setup -q
cp %{SOURCE1001} .

%build
%cmake .
make %{?jobs:-j%jobs}

%post
/sbin/ldconfig

%postun -p /sbin/ldconfig

%install
%make_install
mkdir -p %{buildroot}/usr/share/license
cp LICENSE %{buildroot}/usr/share/license/%{name}

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/telephony/plugins/vmodem-plugin*
/usr/share/license/%{name}
