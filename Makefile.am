share_perl5dir = /opt/vyatta/share/perl5/Vyatta
completiondir	 = /etc/bash_completion.d
initddir	 = /etc/init.d
defaultdir = /etc/default
modprobedir = /etc/modprobe.d
etc_shell_leveldir = $(sysconfdir)/shell/level
dhcphookdir = /etc/dhcp3/dhclient-exit-hooks.d

AM_CFLAGS = -I src -Wall -I /usr/include/glib-2.0 -I /usr/lib/glib-2.0/include
AM_YFLAGS = -d --name-prefix=yy_`basename $* .y`_
AM_LFLAGS = --prefix=yy_`basename $* .l`_ -olex.yy.c

completion_SCRIPTS = etc/bash_completion.d/20vyatta-cfg
initd_SCRIPTS	= etc/init.d/vyatta-ofr
dhcphook_SCRIPTS = scripts/vyatta-dhclient-hook
modprobe_SCRIPTS = etc/modprobe.d/no-copybreak.conf

lib_LTLIBRARIES = src/libvyatta-cfg.la
src_libvyatta_cfg_la_LIBADD = /usr/lib/libglib-2.0.la
src_libvyatta_cfg_la_LIBADD += /usr/lib/libgio-2.0.la
src_libvyatta_cfg_la_LDFLAGS = -version-info 1:0:0
src_libvyatta_cfg_la_SOURCES = src/cli_parse.y src/cli_def.l src/cli_val.l \
												src/cli_new.c src/cli_path_utils.c \
												src/common/common.c src/common/unionfs.c \
												src/cli_val_engine.c src/cli_objects.c
CLEANFILES = src/cli_parse.c src/cli_parse.h src/cli_def.c src/cli_val.c
LDADD = src/libvyatta-cfg.la
LDADD += /usr/lib/libglib-2.0.la

sbin_PROGRAMS = src/priority
sbin_PROGRAMS += src/my_commit2
sbin_PROGRAMS += src/exe_action
sbin_PROGRAMS += src/dump
sbin_PROGRAMS += src/my_delete
sbin_PROGRAMS += src/my_set
sbin_PROGRAMS += src/check_tmpl
src_priority_SOURCES = src/priority.c
src_my_commit2_SOURCES = src/commit2.c
src_exe_action_SOURCES = src/exe_action.c
src_dump_SOURCES = src/dump_session.c
src_my_delete_SOURCES = src/delete.c
src_my_set_SOURCES = src/set.c
src_check_tmpl_SOURCES = src/check_tmpl.c

sbin_SCRIPTS = scripts/vyatta-cfg-cmd-wrapper
sbin_SCRIPTS += scripts/vyatta-validate-type.pl
sbin_SCRIPTS += scripts/vyatta-find-type.pl
sbin_SCRIPTS += scripts/priority.pl
sbin_SCRIPTS  += scripts/vyatta-config-loader.pl
sbin_SCRIPTS  += scripts/vyatta-config-gen-sets.pl
sbin_SCRIPTS  += scripts/vyatta-cli-expand-var.pl
sbin_SCRIPTS  += scripts/vyatta-output-config.pl
sbin_SCRIPTS  += scripts/vyatta-save-config.pl
sbin_SCRIPTS  += scripts/vyatta-load-config.pl
sbin_SCRIPTS  += scripts/vyatta-cfg-notify
sbin_SCRIPTS  += scripts/vyatta-irqaffin
sbin_SCRIPTS  += scripts/vyatta-auto-irqaffin.pl
sbin_SCRIPTS  += scripts/vyatta-check-typeless-node.pl
sbin_SCRIPTS  += scripts/vyatta-exists

share_perl5_DATA = lib/Vyatta/Config.pm
share_perl5_DATA += lib/Vyatta/Misc.pm
share_perl5_DATA += lib/Vyatta/Interface.pm
share_perl5_DATA += lib/Vyatta/TypeChecker.pm
share_perl5_DATA += lib/Vyatta/ConfigDOMTree.pm
share_perl5_DATA += lib/Vyatta/ConfigOutput.pm
share_perl5_DATA += lib/Vyatta/ConfigLoad.pm
share_perl5_DATA += lib/Vyatta/Keepalived.pm
share_perl5_DATA += lib/Vyatta/Zone.pm

default_DATA = etc/default/vyatta-cfg

cpiop = find  . ! -regex '\(.*~\|.*\.bak\|.*\.swp\|.*\#.*\#\)' -print0 | \
	cpio -0pd

install-exec-hook:
	mkdir -p $(DESTDIR)$(cfgdir)
	mkdir -p $(DESTDIR)$(etc_shell_leveldir)
	cd etc/shell/level; $(cpiop) $(DESTDIR)$(etc_shell_leveldir)
	cd $(DESTDIR)/opt/vyatta/sbin; ln -s my_commit2 my_commit
	
