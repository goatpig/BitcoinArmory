# Makefile for Bitcoin Armory - Root Makefile
SUBDIRS = cppForSwig
bin_PROGRAMS =
noinst_PROGRAMS =

EXTRA_DIST = *.py *.md LICENSE LICENSE-ATI LICENSE-MIT \
	nginx_example.conf imgList.xml autogen.sh \
	armoryengine/*.py ui/*.py \
	img/* lang/* dpkgfiles/* \
	windowsbuild/* linuxbuild/* osxbuild/*

ACLOCAL_AMFLAGS = -I build-aux/m4

if BUILD_GUI
lrelease:
	lrelease lang/*.ts
endif

if BUILD_GUI
qrc_img_resources.py: imgList.xml
	pyrcc4 -o qrc_img_resources.py imgList.xml
endif

copy-script:
	cp cppForSwig/ArmoryDB ./ArmoryDB
	cp cppForSwig/BIP150KeyManager ./BIP150KeyManager

if BUILD_DARWIN
# Autotools insists on looking for shared libraries (-install_name flag during
# compilation) in /usr/local/lib. Unsure how to safely override the setting.
# We also can't rely on the user installing libraries elsewhere. So, by default,
# CppBlockUtils will choke when it attempts to load any shared library to which
# it's linked. The workaround: Let Automake do its thing and then use an
# external tool (installed by Xcode) to tell CppBlockUtils.so where to actually
# find shared libraries it needs.
# SOURCE 1: https://www.mikeash.com/pyblog/friday-qa-2009-11-06-linking-and-install-names.html
# SOURCE 2: https://github.com/conda/conda-build/issues/279
# Alt target name (useful for debugging?): @executable_path/py/usr/local/lib/liblmdb.0.dylib
	cp cppForSwig/.libs/libCppBlockUtils.dylib ./_CppBlockUtils.so
else
	cp cppForSwig/.libs/libCppBlockUtils.so ./_CppBlockUtils.so
endif

.PHONY: copy-script lrelease qrc_img_resources.py

all-local: copy-script lrelease qrc_img_resources.py


#target to clean up pre autotools installation left overs
uninstall-old:
	rm -f $(DESTDIR)$(prefix)/bin/ArmoryDB
	rm -f $(DESTDIR)$(prefix)/bin/BIP150KeyManager

install-exec-hook: uninstall-old
if BUILD_CLIENT
if BUILD_GUI
	mkdir -p $(DESTDIR)$(prefix)/lib/armory/ui
endif
	mkdir -p $(DESTDIR)$(prefix)/lib/armory/armoryengine
	mkdir -p $(DESTDIR)$(prefix)/share/applications
	cp *.py *.so README.md $(DESTDIR)$(prefix)/lib/armory
	cp -r armoryengine/* $(DESTDIR)$(prefix)/lib/armory/armoryengine
if BUILD_GUI
	mkdir -p $(DESTDIR)$(prefix)/lib/armory/lang
	cp -r ui/* $(DESTDIR)$(prefix)/lib/armory/ui
	cp lang/*.qm $(DESTDIR)$(prefix)/lib/armory/lang
endif
endif

	mkdir -p $(DESTDIR)$(prefix)/bin
# Sometimes, uninstall-old deletes a valid ArmoryDB. Copy again to be safe.
	cp ArmoryDB $(DESTDIR)$(prefix)/bin
	cp BIP150KeyManager $(DESTDIR)$(prefix)/bin

# No need to install test binaries.
if BUILD_TESTS
	rm -f $(DESTDIR)$(prefix)/bin/*Tests
endif

# Skip Linux-specific steps on OSX.
if ! BUILD_DARWIN
if BUILD_CLIENT
	rsync -rupE --exclude="img/.DS_Store" img $(DESTDIR)$(prefix)/share/armory/
	sed "s: /usr: $(prefix):g" < dpkgfiles/armory > $(DESTDIR)$(prefix)/bin/armory
	chmod +x $(DESTDIR)$(prefix)/bin/armory
endif
endif

uninstall-hook: uninstall-old
	rm -rf $(DESTDIR)$(prefix)/lib/armory
	rm -rf $(DESTDIR)$(prefix)/bin/armory
	rm -f  $(DESTDIR)$(prefix)/share/applications/armory*
	rm -rf $(DESTDIR)$(prefix)/share/armory

clean-local:
	rm -f ArmoryDB
	rm -f BIP150KeyManager
	rm -f lib/*
if BUILD_CLIENT
	rm -f CppBlockUtils.py
	rm -f _CppBlockUtils.so
	rm -f CppBlockUtils.pyc
endif
