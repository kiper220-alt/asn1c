%define _unpackaged_files_terminate_build 1

Name:    asn1c
Version: 0.9.28
Release: alt1
Summary: An ASN.1 Compiler
License: BSD-2-Clause
Group:   Development/C
URL:     https://github.com/vlm/asn1c
Vcs:     https://github.com/vlm/asn1c

BuildRequires: rpm-macros-make
BuildRequires: gcc
BuildRequires: make
BuildRequires: perl-devel

Source0: %name-%version.tar

%description
Compiles ASN.1 data structures into C source structures that can be
simply marshalled to/unmarshalled from: BER, DER, CER, BASIC-XER,
CXER, EXTENDED-XER, PER.

%prep
%setup -q

%build
%autoreconf
%configure --docdir=%{_docdir}/%{name}
%make_build

%install
%makeinstall_std

%files
%doc %{_docdir}/%{name}
%doc --no-dereference LICENSE
%{_bindir}/*
%{_datadir}/asn1c
%{_mandir}/man1/*
%{_datadir}/%{name}

%changelog
* Wed Oct 16 2024 Korney Gedert <kiper@altlinux.org> 0.9.28-alt1 
- Initial build.
