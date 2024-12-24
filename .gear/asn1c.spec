%define _unpackeged_files_terminate_build 1

Name: asn1c
Version: 0.29.9
Release: alt1

Summary: The ASN.1 Compiler
License: BSD-2-Clause
Group: Development/C
Url: https://github.com/vlm/asn1c
Vcs: https://github.com/vlm/asn1c

BuildRequires: rpm-macros-make autoconf-common make gcc

Source0: %name-%version.tar

%description
ASN.1 to C compiler takes the ASN.1 module files (example) and generates the C++ compatible C source code. That code can be used to serialize the native C structures into compact and unambiguous BER/OER/PER/XER-based data files, and deserialize the files back.
Various ASN.1 based formats are widely used in the industry, such as to encode the X.509 certificates employed in the HTTPS handshake, to exchange control data between mobile phones and cellular networks, to perform car-to-car communication in intelligent transportation networks.
The ASN.1 family of standards is large and complex, and no open source compiler supports it in its entirety. The asn1c is arguably the most evolved open source ASN.1 compiler.

%prep
%setup -q

%build
%autoreconf
%configure
%make_build

%install
%makeinstall_std

%files
%_bindir/*

%changelog
* Tue Dec 24 2024 Gedert Korney <kiper@altlinux.org> 0.29.9-alt1
- Initial build.
