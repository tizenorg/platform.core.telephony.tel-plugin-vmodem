#sbs-git:slp/pkgs/t/tel-plugin-vmodem
Name:       tel-plugin-vmodem
Summary:    Telephony AT Virtual Modem library
Version: 0.1.6
Release:    1
Group:      System/Libraries
License:    Apache
Source0:    tel-plugin-vmodem-%{version}.tar.gz
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  cmake
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(tcore)

%description
Telephony AT Modem library

%prep
%setup -q

%build
cmake . -DCMAKE_INSTALL_PREFIX=%{_prefix}
make %{?jobs:-j%jobs}

%post
/sbin/ldconfig

%postun -p /sbin/ldconfig

%install
rm -rf %{buildroot}
%make_install

%files
%defattr(-,root,root,-)
#%doc COPYING
%{_libdir}/telephony/plugins/vmodem-plugin*
