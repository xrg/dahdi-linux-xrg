%define git_repo dahdi
%define git_head HEAD

%define name dahdi-linux
%define version 2.0
%define release alpha
%define kernelrelease %(uname -r)

%define build_modules 0
%{?_without_modules:	%global build_modules 0}
%{?_with_modules:	%global build_modules 1}

Summary:	Digium Asterisk Hardware Device Interface
Name:		dahdi-linux
Version:	%{version}
Release:	%{release}
License:	GPL
Group:		System/Libraries
URL:		http://www.asterisk.org/
Source0:	%{name}-%{version}.tar.gz
BuildRequires:	newt-devel
#BuildRequires:	libzap-devel >= 1.0.1
# BuildConflicts:	libtonezone-devel # No longer, DAHDI can co-exist I hope.
BuildRoot:	%{_tmppath}/%{name}-%{version}-root


%description
Asterisk Hardware Device Interface

%package devel
Summary:	Development files for the Digium Asterisk Hardware Device Interface
Group:		Development/C

%description devel
This package contains the headers for the kernel part of the DAHDI.


%package  -n kernel-%{name}-%{kernelrelease}
Summary:	DAHDI kernel modules
Group:		System/Kernel and hardware

%description  -n kernel-%{name}-%{kernelrelease}
Kernel modules for Digium Asterisk Hardware Device Interface: drivers
for the DAHDI (aka Zapata) hardware.

%prep
%git_get_source
%setup -q


%build

%if %{build_modules}
%make
%endif


%install
install -d %{buildroot}%{_sysconfdir}/udev/rules.d
install -d %{buildroot}/lib/firmware

pushd drivers/dahdi/firmware
	for TAR in *.tar.gz ; do
		tar -xzf $TAR
	done
popd

%if %{build_modules}
%make DESTDIR=%{buildroot} install
%else
%make DESTDIR=%{buildroot} install-devices install-firmware install-include
%endif


%clean
[ "%{buildroot}" != "/" ] && rm -rf %{buildroot}

%files
%defattr(-,root,root)
%attr(0644,root,root) %config(noreplace) %{_sysconfdir}/udev/rules.d/dahdi.rules
%attr(0644,root,root)		/lib/firmware/*
%exclude			/lib/firmware/.dahdi-fw*
#would be needed after a depmod:
#%exclude			/lib/modules/%{kernelrelease}/modules.*
# /usr/src/%{name}-%{version}-%{release}

%files devel
%defattr(-,root,root)
%{_includedir}/dahdi/*.h


%if %{build_modules}
%files -n kernel-%{name}-%{kernelrelease}
%defattr(-,root,root)
/lib/modules/%{kernelrelease}/dahdi/*.ko
/lib/modules/%{kernelrelease}/dahdi/*/*.ko

%endif