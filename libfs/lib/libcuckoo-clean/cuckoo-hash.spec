%define fullname %{name}-0.3-1

Name:           cuckoo-hash
Version:        0.3
Release:        1%{?dist}
Summary:        Cuckoo hashing C library
Group:          System Environment/Libraries
License:        LGPLv3+
Source:         %{fullname}.tar.bz2

%description
C library implementing cuckoo hashing algorithm.


%prep
%setup -q -n %{fullname}


%build
%configure
make %{?_smp_mflags}


%check
make check


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%{_libdir}/*


%package devel
Summary:        Cuckoo hashing C library headers
Requires:       %{name} == %{version}

%description devel
C library implementing cuckoo hashing algorithm.

This package contains development files.


%files devel
%defattr(-,root,root,-)
%{_includedir}/*


%changelog
