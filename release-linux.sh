VERSION=1.0.0
rm -rf ./release-linux
mkdir release-linux

cp ./src/clore_blockchaind ./release-linux/
cp ./src/clore-cli ./release-linux/
cp ./src/qt/clore-qt ./release-linux/
cp ./CLORECOIN_small.png ./release-linux/

cd ./release-linux/
strip clore_blockchaind
strip clore-cli
strip clore-qt

#==========================================================
# prepare for packaging deb file.

mkdir clorecoin-$VERSION
cd clorecoin-$VERSION
mkdir -p DEBIAN
echo 'Package: clorecoin
Version: '$VERSION'
Section: base 
Priority: optional 
Architecture: all 
Depends:
Maintainer: Clore
Description: Clore coin wallet and service.
' > ./DEBIAN/control
mkdir -p ./usr/local/bin/
cp ../clore_blockchaind ./usr/local/bin/
cp ../clore-cli ./usr/local/bin/
cp ../clore-qt ./usr/local/bin/

# prepare for desktop shortcut
mkdir -p ./usr/share/icons/
cp ../CLORECOIN_small.png ./usr/share/icons/
mkdir -p ./usr/share/applications/
echo '
#!/usr/bin/env xdg-open

[Desktop Entry]
Version=1.0
Type=Application
Terminal=false
Exec=/usr/local/bin/clore-qt
Name=clorecoin
Comment= clore coin wallet
Icon=/usr/share/icons/CLORECOIN_small.png
' > ./usr/share/applications/clorecoin.desktop

cd ../
# build deb file.
dpkg-deb --build clorecoin-$VERSION

#==========================================================
# build rpm package
rm -rf ~/rpmbuild/
mkdir -p ~/rpmbuild/{RPMS,SRPMS,BUILD,SOURCES,SPECS,tmp}

cat <<EOF >~/.rpmmacros
%_topdir   %(echo $HOME)/rpmbuild
%_tmppath  %{_topdir}/tmp
EOF

#prepare for build rpm package.
rm -rf clorecoin-$VERSION
mkdir clorecoin-$VERSION
cd clorecoin-$VERSION

mkdir -p ./usr/bin/
cp ../clore_blockchaind ./usr/bin/
cp ../clore-cli ./usr/bin/
cp ../clore-qt ./usr/bin/

# prepare for desktop shortcut
mkdir -p ./usr/share/icons/
cp ../CLORECOIN_small.png ./usr/share/icons/
mkdir -p ./usr/share/applications/
echo '
[Desktop Entry]
Version=1.0
Type=Application
Terminal=false
Exec=/usr/bin/clore-qt
Name=clorecoin
Comment= clore coin wallet
Icon=/usr/share/icons/CLORECOIN_small.png
' > ./usr/share/applications/clorecoin.desktop
cd ../

# make tar ball to source folder.
tar -zcvf clorecoin-$VERSION.tar.gz ./clorecoin-$VERSION
cp clorecoin-$VERSION.tar.gz ~/rpmbuild/SOURCES/

# build rpm package.
cd ~/rpmbuild

cat <<EOF > SPECS/clorecoin.spec
# Don't try fancy stuff like debuginfo, which is useless on binary-only
# packages. Don't strip binary too
# Be sure buildpolicy set to do nothing

Summary: Clore wallet rpm package
Name: clorecoin
Version: $VERSION
Release: 1
License: MIT
SOURCE0 : %{name}-%{version}.tar.gz
URL: https://www.clorecoin.net/

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
%{summary}

%prep
%setup -q

%build
# Empty section.

%install
rm -rf %{buildroot}
mkdir -p  %{buildroot}

# in builddir
cp -a * %{buildroot}


%clean
rm -rf %{buildroot}


%files
/usr/share/applications/clorecoin.desktop
/usr/share/icons/CLORECOIN_small.png
%defattr(-,root,root,-)
%{_bindir}/*

%changelog
* Tue Aug 24 2021  Clore Project Team.
- First Build

EOF

rpmbuild -ba SPECS/clorecoin.spec



