SUBDIRS = . perl_dmod

share_perl5dir = /opt/vyatta/share/perl5/Vyatta
completiondir	 = /etc/bash_completion.d
initddir	 = /etc/init.d
commit_run_dir = /etc/commit
defaultdir = /etc/default
etc_shell_leveldir = $(sysconfdir)/shell/level
dhcphookdir = /etc/dhcp3/dhclient-exit-hooks.d
enumdir = $(datadir)/enumeration

AM_CFLAGS = -I src -Wall -I /usr/include/glib-2.0 -I /usr/lib/glib-2.0/include
AM_CXXFLAGS = -I src -Wall -Werror
AM_YFLAGS = -d --name-prefix=yy_`basename $* .y`_
AM_LFLAGS = --prefix=yy_`basename $* .l`_ -olex.yy.c

completion_SCRIPTS = etc/bash_completion.d/20vyatta-cfg
initd_SCRIPTS	= etc/init.d/vyatta-router
dhcphook_SCRIPTS = scripts/vyatta-dhclient-hook

lib_LTLIBRARIES = src/libvyatta-cfg.la
src_libvyatta_cfg_la_LIBADD = /usr/lib/libglib-2.0.la
src_libvyatta_cfg_la_LIBADD += /usr/lib/libgio-2.0.la
src_libvyatta_cfg_la_LIBADD += -lboost_system
src_libvyatta_cfg_la_LIBADD += -lboost_filesystem
src_libvyatta_cfg_la_LIBADD += -lapt-pkg
src_libvyatta_cfg_la_LDFLAGS = -version-info 1:0:0
src_libvyatta_cfg_la_SOURCES = src/cli_parse.y src/cli_def.l src/cli_val.l
src_libvyatta_cfg_la_SOURCES += src/cli_new.c src/cli_path_utils.c
src_libvyatta_cfg_la_SOURCES += src/common/common.c src/common/unionfs.c
src_libvyatta_cfg_la_SOURCES += src/cli_val_engine.c src/cli_objects.c
src_libvyatta_cfg_la_SOURCES += src/cstore/cstore-c.cpp
src_libvyatta_cfg_la_SOURCES += src/cstore/cstore.cpp
src_libvyatta_cfg_la_SOURCES += src/cstore/cstore-varref.cpp
src_libvyatta_cfg_la_SOURCES += src/cstore/cstore-node.cpp
src_libvyatta_cfg_la_SOURCES += src/cstore/unionfs/cstore-unionfs.cpp
CLEANFILES = src/cli_parse.c src/cli_parse.h src/cli_def.c src/cli_val.c
LDADD = src/libvyatta-cfg.la
LDADD += /usr/lib/libglib-2.0.la

vincludedir = $(includedir)/vyatta-cfg
vinclude_HEADERS = src/cli_cstore.h

vcincdir = $(vincludedir)/cstore
vcinc_HEADERS = src/cstore/cstore-c.h
vcinc_HEADERS += src/cstore/cstore.hpp

vcuincdir = $(vcincdir)/unionfs
vcuinc_HEADERS = src/cstore/unionfs/cstore-unionfs.hpp

sbin_PROGRAMS = src/priority
sbin_PROGRAMS += src/my_commit
sbin_PROGRAMS += src/exe_action
sbin_PROGRAMS += src/dump
sbin_PROGRAMS += src/check_tmpl
sbin_PROGRAMS += src/net_set
sbin_PROGRAMS += src/my_cli_bin
sbin_PROGRAMS += src/my_cli_shell_api

src_priority_SOURCES = src/priority.c
src_my_commit_SOURCES = src/commit2.c
src_exe_action_SOURCES = src/exe_action.c
src_dump_SOURCES = src/dump_session.c
src_check_tmpl_SOURCES = src/check_tmpl.c
src_net_set_SOURCES = src/net_set.c
src_my_cli_bin_SOURCES = src/cli_bin.cpp
src_my_cli_shell_api_SOURCES = src/cli_shell_api.cpp

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
sbin_SCRIPTS  += scripts/vyatta-check-typeless-node.pl
sbin_SCRIPTS  += scripts/vyatta-strip-migration-comments.pl

share_perl5_DATA = lib/Vyatta/Config.pm
share_perl5_DATA += lib/Vyatta/Misc.pm
share_perl5_DATA += lib/Vyatta/Interface.pm
share_perl5_DATA += lib/Vyatta/TypeChecker.pm
share_perl5_DATA += lib/Vyatta/ConfigOutput.pm
share_perl5_DATA += lib/Vyatta/ConfigLoad.pm
share_perl5_DATA += lib/Vyatta/Keepalived.pm

enum_SCRIPTS  = scripts/enumeration/existing-interfaces

default_DATA = etc/default/vyatta-cfg
default_DATA += etc/default/vyatta-load-boot

cpiop = find  . ! -regex '\(.*~\|.*\.bak\|.*\.swp\|.*\#.*\#\)' -print0 | \
	cpio -0pd

install-exec-hook:
	mkdir -p $(DESTDIR)$(cfgdir)
	mkdir -p $(DESTDIR)$(commit_run_dir)
	mkdir -p $(DESTDIR)$(etc_shell_leveldir)
	cd etc/shell/level; $(cpiop) $(DESTDIR)$(etc_shell_leveldir)
	cd $(DESTDIR)$(sbindir); \
	  $(LN_S) my_cli_bin my_set; \
	  $(LN_S) my_cli_bin my_delete; \
	  $(LN_S) my_cli_bin my_activate; \
	  $(LN_S) my_cli_bin my_deactivate; \
	  $(LN_S) my_cli_bin my_rename; \
	  $(LN_S) my_cli_bin my_copy; \
	  $(LN_S) my_cli_bin my_comment; \
	  $(LN_S) my_cli_bin my_discard; \
	  $(LN_S) my_cli_bin my_move
	mkdir -p $(DESTDIR)/bin
	cd $(DESTDIR)/bin ; \
	  $(LN_S) $(sbindir)/my_cli_shell_api cli-shell-api

