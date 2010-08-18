PERL_MODS = Cstore

# nop for all-local. make install will do a build anyway, so don't repeat
# the build here.
all-local: ;

install-exec-local:
	for pm in $(PERL_MODS); do \
		(cd $$pm; \
		 perl Makefile.PL; \
		 $(MAKE) $(AM_MAKEFLAGS) install); \
	done

clean-local:
	for pm in $(PERL_MODS); do \
		(cd $$pm; \
		 perl Makefile.PL; \
		 $(MAKE) $(AM_MAKEFLAGS) realclean); \
	done

# nops
check-local: ;
install-data-local: ;
uninstall-local: ;

