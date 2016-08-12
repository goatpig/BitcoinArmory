# All the actual Makefiles are deeper in the directory tree.  
# I am just calling them, here.

PREFIX=/usr
DESTDIR=
UNAME_S := $(shell uname -s)
VERSION := $(shell PYTHONPATH=. extras/getVersionString.py)

all:
	$(MAKE) -C cppForSwig

clean :
	$(MAKE) -C cppForSwig clean
	rm -f osxbuild/build-app.log.txt
	rm -f osxbuild/armory_*.tar.gz
ifeq ($(UNAME_S),Darwin)
		$(MAKE) -C osxbuild/objc_armory distclean
endif
	rm -rf osxbuild/workspace/
	rm -f CppBlockUtils.py
	rm -f qrc_img_resources.py
	rm -f _CppBlockUtils.so
	rm -f cppForSwig/cryptopp/a.out
	rm -f *.pyc bitcoinrpc_jsonrpc/*.pyc ui/*.pyc
	rm -f armoryengine/*.pyc dialogs/*.pyc	
	rm -f pytest/*.pyc txjsonrpc/*.pyc jsonrpc/*.pyc txjsonrpc/web/*.pyc
	rm -f BitcoinArmory.spec
	rm -f BitcoinArmory-*.tar.gz
	rm -fr dist

install : all
	mkdir -p $(DESTDIR)$(PREFIX)/share/armory/img
	mkdir -p $(DESTDIR)$(PREFIX)/lib/armory/extras
	mkdir -p $(DESTDIR)$(PREFIX)/lib/armory/bitcoinrpc_jsonrpc
	mkdir -p $(DESTDIR)$(PREFIX)/lib/armory/txjsonrpc
	mkdir -p $(DESTDIR)$(PREFIX)/lib/armory/txjsonrpc/web
	mkdir -p $(DESTDIR)$(PREFIX)/lib/armory/ui
	mkdir -p $(DESTDIR)$(PREFIX)/lib/armory/pytest
	mkdir -p $(DESTDIR)$(PREFIX)/lib/armory/urllib3
	mkdir -p $(DESTDIR)$(PREFIX)/lib/armory/urllib3
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	sed "s: /usr: $(PREFIX):g" < dpkgfiles/armory > $(DESTDIR)$(PREFIX)/bin/armory
	chmod +x $(DESTDIR)$(PREFIX)/bin/armory
	cp *.py *.so README.md $(DESTDIR)$(PREFIX)/lib/armory/
	rsync -rupE armoryengine $(DESTDIR)$(PREFIX)/lib/armory/
	rsync -rupE --exclude="img/.DS_Store" img $(DESTDIR)$(PREFIX)/share/armory/
	cp extras/*.py $(DESTDIR)$(PREFIX)/lib/armory/extras
	cp bitcoinrpc_jsonrpc/*.py $(DESTDIR)$(PREFIX)/lib/armory/bitcoinrpc_jsonrpc
	cp -r txjsonrpc/*.py $(DESTDIR)$(PREFIX)/lib/armory/txjsonrpc
	cp -r txjsonrpc/web/*.py $(DESTDIR)$(PREFIX)/lib/armory/txjsonrpc/web
	cp ui/*.py $(DESTDIR)$(PREFIX)/lib/armory/ui
	cp pytest/*.py $(DESTDIR)$(PREFIX)/lib/armory/pytest
	cp -r urllib3/* $(DESTDIR)$(PREFIX)/lib/armory/urllib3
	mkdir -p $(DESTDIR)$(PREFIX)/share/applications
	sed "s:python /usr:python $(PREFIX):g" < dpkgfiles/armory.desktop > $(DESTDIR)$(PREFIX)/share/applications/armory.desktop
	sed "s:python /usr:python $(PREFIX):g" < dpkgfiles/armoryoffline.desktop > $(DESTDIR)$(PREFIX)/share/applications/armoryoffline.desktop
	sed "s:python /usr:python $(PREFIX):g" < dpkgfiles/armorytestnet.desktop > $(DESTDIR)$(PREFIX)/share/applications/armorytestnet.desktop
	#$(MAKE) -C po install DESTDIR=$(DESTDIR) PREFIX=$(PREFIX)

all-test-tools: all
	$(MAKE) -C cppForSwig/gtest

gtest: all-test-tools
	(cd cppForSwig/gtest && ./CppBlockUtilsTests)

pytest: all
	python -m unittest discover

test: gtest pytest

mo: messages
	$(MAKE) -C po mo

messages:
	xgettext -o po/messages.pot --from-code=UTF-8 -L Python -ktr *.py armoryengine/*.py ui/*.py

osx :
	chmod +x osxbuild/deploy.sh
	cd osxbuild; ./deploy.sh

BitcoinArmory.spec: BitcoinArmory.spec.in
	sed 's/@VERSION@/$(VERSION)/g' < BitcoinArmory.spec.in > BitcoinArmory.spec

BitcoinArmory-$(VERSION).tar.gz: BitcoinArmory.spec
	extras/maketarball $(VERSION)

dist: BitcoinArmory-$(VERSION).tar.gz
	true

srpm: dist
	tmpdir=`mktemp -d` ; rpmbuild --define "_topdir $$tmpdir" -ts BitcoinArmory-$(VERSION).tar.gz && { mv -f "$$tmpdir"/SRPMS/* . ; } ; rm -rf "$$tmpdir"

rpm: dist
	tmpdir=`mktemp -d` ; rpmbuild --define "_topdir $$tmpdir" -ta BitcoinArmory-$(VERSION).tar.gz && { mv -f "$$tmpdir"/SRPMS/* "$$tmpdir"/RPMS/*/* . ; } ; rm -rf "$$tmpdir"
