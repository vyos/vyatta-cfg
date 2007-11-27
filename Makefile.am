cfgdir = $(datadir)/vyatta-cfg/templates
share_perl5dir = /opt/vyatta/share/perl5
completiondir	 = /etc/bash_completion.d
initddir	 = /etc/init.d

AM_CFLAGS = -I src -Wall
AM_YFLAGS = -d --name-prefix=yy_`basename $* .y`_
AM_LFLAGS = --prefix=yy_`basename $* .l`_ -olex.yy.c

completion_DATA	= etc/bash_completion.d/20vyatta-cfg
initd_SCRIPTS	= etc/init.d/vyatta-ofr

lib_LTLIBRARIES = src/libvyatta-cfg.la
src_libvyatta_cfg_la_LDFLAGS = -version-info 1:0:0
src_libvyatta_cfg_la_SOURCES = src/cli_parse.y src/cli_def.l src/cli_val.l \
												src/cli_new.c src/cli_path_utils.c \
												src/cli_val_engine.c src/cli_objects.c
CLEANFILES = src/cli_parse.c src/cli_parse.h src/cli_def.c src/cli_val.c
LDADD = src/libvyatta-cfg.la

sbin_PROGRAMS = src/my_commit
sbin_PROGRAMS += src/my_delete
sbin_PROGRAMS += src/my_set
src_my_commit_SOURCES = src/commit.c
src_my_delete_SOURCES = src/delete.c
src_my_set_SOURCES = src/set.c

sbin_SCRIPTS = scripts/xorp_tmpl_tool
sbin_SCRIPTS += scripts/vyatta-validate-type.pl
sbin_SCRIPTS += scripts/vyatta-find-type.pl
sbin_SCRIPTS  += scripts/vyatta-config-loader.pl
sbin_SCRIPTS  += scripts/vyatta-cli-expand-var.pl
sbin_SCRIPTS  += scripts/vyatta-output-config.pl
sbin_SCRIPTS  += scripts/vyatta-save-config.pl
sbin_SCRIPTS  += scripts/vyatta-load-config.pl
sbin_SCRIPTS  += scripts/vyatta-cfg-notify
sbin_SCRIPTS  += scripts/vyatta-interfaces.pl

share_perl5_DATA = scripts/VyattaConfig.pm
share_perl5_DATA += scripts/VyattaConfigDOMTree.pm
share_perl5_DATA += scripts/VyattaConfigOutput.pm
share_perl5_DATA += scripts/VyattaConfigLoad.pm
share_perl5_DATA += scripts/VyattaMisc.pm
share_perl5_DATA += scripts/VyattaTypeChecker.pm

cpiop = find  . ! -regex '\(.*~\|.*\.bak\|.*\.swp\|.*\#.*\#\)' -print0 | \
	cpio -0pd

install-exec-hook:
	mkdir -p $(DESTDIR)$(cfgdir)
	cd templates; $(cpiop) $(DESTDIR)$(cfgdir)
