#########################################################################
#
# Makefile for readiso for *nix environments
#
#
Version = 0.3alpha
PKGNAME = readiso

# Compile Options:
#  -DLINUX    for Linux
#  -DSGI      for Silicon Graphics

DEFINES = -DSGI -DDEFAULT_DEV=\"/dev/scsi/sc1d3l0\"

PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin
MANDIR  = $(PREFIX)/man/man1
USER	= root
GROUP	= root

# if necessary define where jpeglib and it's headers are located
LIBDIR  = # -L/usr/local/lib
INCDIR  = # -I/usr/src/linux/include/scsi


CC     = gcc 
CFLAGS = -O2 $(DEFINES) $(INCDIR)  # -N
LIBS   = -lds $(LIBDIR)
STRIP  = strip


# should be no reason to modify lines below this
#########################################################################

DIRNAME = $(shell basename `pwd`) 
DISTNAME  = $(PKGNAME)-$(Version)


$(PKGNAME):	$(PKGNAME).c Makefile
	$(CC) $(CFLAGS) -o $(PKGNAME) $(PKGNAME).c $(LIBS) 

all:	$(PKGNAME) 

strip:
	$(STRIP) $(PKGNAME)

clean:
	rm -f *~ *.o core a.out make.log $(PKGNAME)

dist:	clean
	(cd .. ; tar cvzf $(DISTNAME).tar.gz $(DIRNAME))

backup:	dist

zip:	clean	
	(cd .. ; zip -r9 $(DISTNAME).zip $(DIRNAME))

install: all  install.man
	install -m 755 -u $(USER) -g $(GROUP) -f $(BINDIR) $(PKGNAME)

printable.man:
	groff -Tps -mandoc ./$(PKGNAME).1 >$(PKGNAME).ps
	groff -Tascii -mandoc ./$(PKGNAME).1 | tee $(PKGNAME).prn | sed 's/.//g' >$(PKGNAME).txt

install.man:
	install -m 644 -u $(USER) -g $(GROUP) -f $(MANDIR) $(PKGNAME).1 

# a tradition !
love:	
	@echo "Not War - Eh?"
# eof

