cfgdir = $(datadir)/vyatta-cfg/templates
share_perl5dir = /opt/vyatta/share/perl5
completiondir	 = /etc/bash_completion.d
initddir	 = /etc/init.d
defaultdir = /etc/default
etc_shell_leveldir = $(sysconfdir)/shell/level
dhcphookdir = /etc/dhcp3/dhclient-exit-hooks.d

AM_CFLAGS = -I src -Wall
AM_YFLAGS = -d --name-prefix=yy_`basename $* .y`_
AM_LFLAGS = --prefix=yy_`basename $* .l`_ -olex.yy.c

completion_SCRIPTS = etc/bash_completion.d/20vyatta-cfg
initd_SCRIPTS	= etc/init.d/vyatta-ofr
dhcphook_SCRIPTS = scripts/vyatta-dhclient-hook

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
sbin_PROGRAMS += src/check_tmpl
src_my_commit_SOURCES = src/commit.c
src_my_delete_SOURCES = src/delete.c
src_my_set_SOURCES = src/set.c
src_check_tmpl_SOURCES = src/check_tmpl.c

sbin_SCRIPTS = scripts/vyatta-cfg-cmd-wrapper
sbin_SCRIPTS += scripts/vyatta-validate-type.pl
sbin_SCRIPTS += scripts/vyatta-find-type.pl
sbin_SCRIPTS  += scripts/vyatta-config-loader.pl
sbin_SCRIPTS  += scripts/vyatta-cli-expand-var.pl
sbin_SCRIPTS  += scripts/vyatta-output-config.pl
sbin_SCRIPTS  += scripts/vyatta-save-config.pl
sbin_SCRIPTS  += scripts/vyatta-load-config.pl
sbin_SCRIPTS  += scripts/vyatta-cfg-notify
sbin_SCRIPTS  += scripts/vyatta-interfaces.pl
sbin_SCRIPTS  += scripts/vyatta-irqaffin
sbin_SCRIPTS  += scripts/vyatta-check-typeless-node.pl

share_perl5_SCRIPTS = scripts/VyattaConfig.pm
share_perl5_SCRIPTS += scripts/VyattaConfigDOMTree.pm
share_perl5_SCRIPTS += scripts/VyattaConfigOutput.pm
share_perl5_SCRIPTS += scripts/VyattaConfigLoad.pm
share_perl5_SCRIPTS += scripts/VyattaMisc.pm
share_perl5_SCRIPTS += scripts/VyattaTypeChecker.pm
share_perl5_SCRIPTS += scripts/VyattaIpTablesAddressFilter.pm

default_DATA = etc/default/vyatta-cfg

cpiop = find  . ! -regex '\(.*~\|.*\.bak\|.*\.swp\|.*\#.*\#\)' -print0 | \
	cpio -0pd

install-exec-hook:
	mkdir -p $(DESTDIR)$(cfgdir)
	cd templates; $(cpiop) $(DESTDIR)$(cfgdir)
	mkdir -p $(DESTDIR)$(etc_shell_leveldir)
	cd etc/shell/level; $(cpiop) $(DESTDIR)$(etc_shell_leveldir)
