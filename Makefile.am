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

lrelease:
if HAVE_GUI
	lrelease lang/*.ts
endif

qrc_img_resources.py: imgList.xml
if HAVE_GUI
	pyrcc4 -o qrc_img_resources.py imgList.xml
endif

copy-script:
	cp cppForSwig/ArmoryDB ./ArmoryDB

# SWIG code and requirements.
if HAVE_GUI
	cp cppForSwig/CppBlockUtils.py ./CppBlockUtils.py
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
	install_name_tool -change /usr/local/lib/liblmdb.0.dylib @loader_path/../liblmdb.0.dylib ./cppForSwig/.libs/libCppBlockUtils.dylib
	cp cppForSwig/.libs/libCppBlockUtils.dylib ./_CppBlockUtils.so
else
	cp cppForSwig/.libs/libCppBlockUtils.so ./_CppBlockUtils.so
endif
endif

.PHONY: copy-script lrelease qrc_img_resources.py

all-local: copy-script lrelease qrc_img_resources.py


#target to clean up pre autotools installation left overs
uninstall-old:
	rm -f $(DESTDIR)$(prefix)/bin/ArmoryDB

install-exec-hook: uninstall-old
if HAVE_GUI
	mkdir -p $(DESTDIR)$(prefix)/lib/armory/ui
	mkdir -p $(DESTDIR)$(prefix)/lib/armory/armoryengine
	mkdir -p $(DESTDIR)$(prefix)/lib/armory/lang
	mkdir -p $(DESTDIR)$(prefix)/share/applications
	cp *.py *.so README.md $(DESTDIR)$(prefix)/lib/armory
	cp -r ui/* $(DESTDIR)$(prefix)/lib/armory/ui
	cp -r armoryengine/* $(DESTDIR)$(prefix)/lib/armory/armoryengine
	cp lang/*.qm $(DESTDIR)$(prefix)/lib/armory/lang
endif

	mkdir -p $(DESTDIR)$(prefix)/bin
# Sometimes, uninstall-old deletes a valid ArmoryDB. Copy again to be safe.
	cp ArmoryDB $(DESTDIR)$(prefix)/bin

# No need to install test binaries.
if BUILD_TESTS
	rm -f $(DESTDIR)$(prefix)/bin/ContainerTests
	rm -f $(DESTDIR)$(prefix)/bin/CppBlockUtilsTests
	rm -f $(DESTDIR)$(prefix)/bin/DB1kIterTest
endif

# Skip Linux-specific steps on OSX.
if ! BUILD_DARWIN
if HAVE_GUI
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
	rm -f CppBlockUtils.py
	rm -f _CppBlockUtils.so
	rm -f CppBlockUtils.pyc
