Name:		pciutils
Version:	1.99.2
Release: 	1
Source:		ftp://atrey.karlin.mff.cuni.cz/pub/linux/pci/%{name}-%{version}.tar.gz
Copyright:	GNU GPL
Buildroot: 	/tmp/%{name}-%{version}-root
ExclusiveOS: 	Linux
Summary: 	Linux PCI Utilities
Summary(pl): 	Narzêdzia do manipulacji ustawieniami urz±dzeñ PCI
Group: 		Utilities/System

%description
This package contains various utilities for inspecting and
setting of devices connected to the PCI bus. Requires kernel
version 2.1.82 or newer (supporting the /proc/bus/pci interface).

%description -l pl
Pakiet zawiera narzêdzia do ustawiania i odczytywania informacji
o urz±dzeniach pod³±czonych do szyny PCI w Twoim komputerze.
Wymaga kernela 2.1.82 lub nowszego (udostêpniaj±cego odpowiednie
informacje poprzez /proc/bus/pci).

%prep
%setup -q

%build
make OPT="$RPM_OPT_FLAGS"

%install
rm -rf $RPM_BUILD_ROOT
install -d $RPM_BUILD_ROOT/{sbin,/usr/man/man8,/usr/share}

install -s lspci setpci $RPM_BUILD_ROOT/sbin
install lspci.8 setpci.8 $RPM_BUILD_ROOT/usr/man/man8
install pci.ids $RPM_BUILD_ROOT/usr/share

%files
%defattr(0644, root, root, 0755)
%attr(0644, root, man) /usr/man/man8/*
%attr(0711, root, root) /sbin/*
%config /usr/share/pci.ids
%doc README ChangeLog pciutils.lsm

%clean
rm -rf $RPM_BUILD_ROOT

%changelog
* Tue Sep 29 1998 Krzysztof G. Baranowski <kgb@knm.org.pl>
[1.07-1]
- build from non-root account against glibc-2.0
- written spec from scratch
