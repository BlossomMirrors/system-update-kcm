#!/bin/bash
set -e

PACKAGE_NAME=blossom-kcm-software-update
VERSION=0.1.0
RELEASE=1
BUILDROOT=$(pwd)/rpmbuild
SPECS_DIR=$BUILDROOT/SPECS
SOURCES_DIR=$BUILDROOT/SOURCES

rm -rf $BUILDROOT
mkdir -p $SPECS_DIR $SOURCES_DIR

# Create source tarball (exclude build artifacts and git metadata)
tar -czf $SOURCES_DIR/$PACKAGE_NAME-$VERSION.tar.gz \
    --transform "s|^|$PACKAGE_NAME-$VERSION/|" \
    CMakeLists.txt kcm/ po/

SPECFILE=$SPECS_DIR/$PACKAGE_NAME.spec

cat > $SPECFILE <<EOF
Name:           $PACKAGE_NAME
Version:        $VERSION
Release:        $RELEASE%{?dist}
Summary:        BlossomOS KDE System Settings module for OS image updates
License:        MIT
URL:            https://codeberg.org/BlossomOS/system-update-kcm

Source0:        $PACKAGE_NAME-$VERSION.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  extra-cmake-modules
BuildRequires:  pkgconfig(Qt6Core)
BuildRequires:  pkgconfig(Qt6DBus)
BuildRequires:  pkgconfig(Qt6Qml)
BuildRequires:  pkgconfig(Qt6Quick)
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kcmutils-devel

Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       kf6-kcoreaddons
Requires:       kf6-ki18n
Requires:       kf6-kcmutils
Requires:       plasma-workspace

%description
BlossomOS KDE System Settings module (KCM) for managing OS image updates
on immutable Linux systems using rpm-ostree. Provides upgrade, rollback,
and automatic update scheduling via a Kirigami-based interface.

%prep
%setup -q

%build
cmake -B build -S . \\
    -GNinja \\
    -DCMAKE_BUILD_TYPE=Release \\
    -DCMAKE_INSTALL_PREFIX=/usr

ninja -C build

%install
DESTDIR=%{buildroot} ninja -C build install

%find_lang kcm_software_update

%files -f kcm_software_update.lang
%{_libdir}/qt6/plugins/plasma/kcms/systemsettings/kcm_software_update.so
%{_datadir}/kservices6/kcm_software_update.json

%changelog
* $(LANG=C date +"%a %b %d %Y") Leonie Ain <me@koyu.space> - $VERSION-$RELEASE
- Initial Release
EOF

rpmbuild -bb $SPECFILE \
    --define "_topdir $BUILDROOT" \
    --define "_sourcedir $SOURCES_DIR"

echo ""
echo "Build complete! RPM package is available at:"
echo "$BUILDROOT/RPMS/$(uname -m)/$PACKAGE_NAME-$VERSION-$RELEASE.$(uname -m).rpm"
