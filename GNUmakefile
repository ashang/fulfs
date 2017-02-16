
ifneq ($(CXXFLAGS),)
CMAKEOPTS += "-DCMAKE_CXX_FLAGS=$(CXXFLAGS)" 
else
CMAKEOPTS += "-DCMAKE_CXX_FLAGS=-g -O3 -Wall" 
endif

ifneq ($(LDFLAGS),)
CMAKEOPTS += "-DCMAKE_EXE_LINKER_FLAGS=$(LDFLAGS)"
endif

DBTMP=dbgen/tmp

all: config
	@$(MAKE) -C build

#apt-cacher-ng in.acng acngfs: config
#	@$(MAKE) -C build $@

config: build/.config-stamp

build/.config-stamp: build/.dir-stamp
	cd build && cmake .. $(CMAKEOPTS)
	@>$@

build/.dir-stamp:
	@test -d build || mkdir build
	@>$@

clean: build/.config-stamp
	$(MAKE) -C build clean

distclean:
	rm -rf build $(DBTMP)

VERSION=$(shell cat VERSION)
DISTNAME=fulfs-$(VERSION)
DEBSRCNAME=fulfs_$(shell echo $(VERSION) | sed -e "s,pre,~pre,").orig.tar.xz

tarball:
#	if test "$(shell svn status | grep -v -i make)" ; then echo Uncommited files found. Run \"svn status\" to display them. ; exit 1 ; fi
	@if test -f ../$(DISTNAME).tar.xz ; then echo ../$(DISTNAME).tar.xz exists, not overwritting ; exit 1; fi
	-svn up
	rm -rf tmp
	mkdir tmp
	svn export . tmp/$(DISTNAME)
	rm -rf tmp/$(DISTNAME)/debian tmp/$(DISTNAME)/tmp tmp/$(DISTNAME)/orig tmp/$(DISTNAME)/trash
	tar -f - -c -C tmp $(DISTNAME) | xz -9 > ../$(DISTNAME).tar.xz
	rm -rf tmp
	test -e /etc/debian_version && ln -f ../$(DISTNAME).tar.xz ../$(DEBSRCNAME) || true
	test -e ../tarballs && ln -f ../$(DISTNAME).tar.xz ../tarballs/$(DEBSRCNAME) || true
	test -e ../build-area && ln -f ../$(DISTNAME).tar.xz ../build-area/$(DEBSRCNAME) || true

tarball-remove:
	rm -f ../$(DISTNAME).tar.xz ../tarballs/$(DEBSRCNAME) ../$(DEBSRCNAME) ../build-area/$(DEBSRCNAME)

SVNBASE=$(shell svn info | grep URL: | cut -f2 -d' ' | xargs dirname)
release: tarball
#release:
	svn ci
	svn cp $(SVNBASE)/trunk $(SVNBASE)/tags/release_$(shell cat VERSION)

unrelease: tarball-remove
#unrelease:
	svn rm $(SVNBASE)/tags/release_$(shell cat VERSION)

# execute them always and consider done afterwards
.PHONY: clean distclean config

# no MT, cmake doesn't like it
.NOTPARALLEL:
