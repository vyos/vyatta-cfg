SUBDIRS = . perl_dmod

share_perl5dir = /opt/vyatta/share/perl5/Vyatta
completiondir	 = /etc/bash_completion.d
initddir	 = /etc/init.d
logrotatedir = /etc/logrotate.d
defaultdir = /etc/default
etc_shell_leveldir = $(sysconfdir)/shell/level
dhcphookdir = /etc/dhcp/dhclient-exit-hooks.d
enumdir = $(datadir)/enumeration

if USE_UNIONFSFUSE
USE_UNIONFSFUSE = -DUSE_UNIONFSFUSE=1
endif

AM_CFLAGS = -I src -Wall $(GOBJECT_CFLAGS)
AM_CXXFLAGS = -I src -Wall -Werror $(USE_UNIONFSFUSE)
AM_CXXFLAGS += $(GOBJECT_CFLAGS)
AM_YFLAGS = -d --name-prefix=yy_`basename $* .y`_
AM_LFLAGS = --prefix=yy_`basename $* .l`_ -olex.yy.c

completion_DATA = etc/bash_completion.d/vyatta-cfg
initd_SCRIPTS	= etc/init.d/vyatta-router
dhcphook_SCRIPTS = scripts/vyatta-dhclient-hook
logrotate_DATA = etc/logrotate.d/vyatta-config-logs
logrotate_DATA += etc/logrotate.d/auth
interpdir = $(datadir)/vyatta-cfg/functions/interpreter/
interp_DATA = functions/interpreter/vyatta-cfg-run

wrapperdir = /opt/vyatta/etc/functions/
wrapper_DATA = functions/wrapper/script-template

src/cparse/cparse.cpp: src/cparse/cparse.ypp src/cparse/cparse_def.h
	bison -p cparse_ --defines=src/cparse/cparse.h -o $@ $<

src/cparse/cparse_lex.c: src/cparse/cparse_lex.l src/cparse/cparse.cpp
	flex -P cparse_ -o $@ $<

lib_LTLIBRARIES = src/libvyatta-cfg.la
src_libvyatta_cfg_la_LIBADD = -lglib-2.0
src_libvyatta_cfg_la_LIBADD += -lgio-2.0
src_libvyatta_cfg_la_LIBADD += -lgobject-2.0
src_libvyatta_cfg_la_LIBADD += -lboost_system
src_libvyatta_cfg_la_LIBADD += -lboost_filesystem
src_libvyatta_cfg_la_LIBADD += -lapt-pkg
src_libvyatta_cfg_la_LIBADD += -lperl
src_libvyatta_cfg_la_LDFLAGS = -version-info 1:0:0
src_libvyatta_cfg_la_SOURCES = src/cli_parse.y src/cli_def.l src/cli_val.l
src_libvyatta_cfg_la_SOURCES += src/cli_new.c src/cli_path_utils.c
src_libvyatta_cfg_la_SOURCES += src/common/unionfs.c
src_libvyatta_cfg_la_SOURCES += src/cli_val_engine.c src/cli_objects.c
src_libvyatta_cfg_la_SOURCES += src/cstore/cstore-c.cpp
src_libvyatta_cfg_la_SOURCES += src/cstore/cstore.cpp
src_libvyatta_cfg_la_SOURCES += src/cstore/cstore-varref.cpp
src_libvyatta_cfg_la_SOURCES += src/cstore/unionfs/cstore-unionfs.cpp
src_libvyatta_cfg_la_SOURCES += src/cnode/cnode.cpp
src_libvyatta_cfg_la_SOURCES += src/cnode/cnode-algorithm.cpp
src_libvyatta_cfg_la_SOURCES += src/cparse/cparse.cpp
src_libvyatta_cfg_la_SOURCES += src/cparse/cparse_lex.c
src_libvyatta_cfg_la_SOURCES += src/commit/commit-algorithm.cpp
CLEANFILES = src/cli_parse.c src/cli_parse.h src/cli_def.c src/cli_val.c
CLEANFILES += src/cparse/cparse.cpp src/cparse/cparse.h
CLEANFILES += src/cparse/cparse_lex.c
LDADD = src/libvyatta-cfg.la
LDADD += $(GOBJECT_LIBS)

vincludedir = $(includedir)/vyatta-cfg
vinclude_HEADERS = src/cli_cstore.h

vcincdir = $(vincludedir)/cstore
vcinc_HEADERS = src/cstore/cstore-c.h
vcinc_HEADERS += src/cstore/cstore.hpp
vcinc_HEADERS += src/cstore/cstore-varref.hpp
vcinc_HEADERS += src/cstore/ctemplate.hpp

vcuincdir = $(vcincdir)/unionfs
vcuinc_HEADERS = src/cstore/unionfs/cstore-unionfs.hpp

vnincdir = $(vincludedir)/cnode
vninc_HEADERS = src/cnode/cnode.hpp
vninc_HEADERS += src/cnode/cnode-algorithm.hpp

vpincdir = $(vincludedir)/cparse
vpinc_HEADERS = src/cparse/cparse.hpp

sbin_PROGRAMS = src/priority
sbin_PROGRAMS += src/exe_action
sbin_PROGRAMS += src/dump
sbin_PROGRAMS += src/check_tmpl
sbin_PROGRAMS += src/my_cli_bin
sbin_PROGRAMS += src/my_cli_shell_api

src_priority_SOURCES = src/priority.c
src_exe_action_SOURCES = src/exe_action.c
src_dump_SOURCES = src/dump_session.c
src_check_tmpl_SOURCES = src/check_tmpl.c
src_my_cli_bin_SOURCES = src/cli_bin.cpp
src_my_cli_shell_api_SOURCES = src/cli_shell_api.cpp

sbin_SCRIPTS = scripts/vyatta-cfg-cmd-wrapper
sbin_SCRIPTS += scripts/vyatta-validate-type.pl
sbin_SCRIPTS += scripts/vyatta-find-type.pl
sbin_SCRIPTS += scripts/priority.pl
sbin_SCRIPTS  += scripts/vyatta-boot-config-loader
sbin_SCRIPTS  += scripts/vyatta-cli-expand-var.pl
sbin_SCRIPTS  += scripts/vyatta-output-config.pl
sbin_SCRIPTS  += scripts/vyatta-save-config.pl
sbin_SCRIPTS  += scripts/vyatta-load-config.pl
sbin_SCRIPTS  += scripts/vyatta-cfg-notify
sbin_SCRIPTS  += scripts/vyatta-check-typeless-node.pl
sbin_SCRIPTS  += scripts/vyatta-strip-migration-comments.pl
sbin_SCRIPTS  += scripts/vyatta-log-commit.pl
sbin_SCRIPTS  += scripts/vyos-user-precommit-hooks.sh
sbin_SCRIPTS  += scripts/vyos-user-postcommit-hooks.sh

share_perl5_DATA = lib/Vyatta/Config.pm
share_perl5_DATA += lib/Vyatta/File.pm
share_perl5_DATA += lib/Vyatta/Misc.pm
share_perl5_DATA += lib/Vyatta/ioctl.pm
share_perl5_DATA += lib/Vyatta/Interface.pm
share_perl5_DATA += lib/Vyatta/TypeChecker.pm
share_perl5_DATA += lib/Vyatta/ConfigOutput.pm
share_perl5_DATA += lib/Vyatta/ConfigLoad.pm

enum_SCRIPTS  = scripts/enumeration/existing-interfaces

default_DATA = etc/default/vyatta-cfg
default_DATA += etc/default/vyatta-load-boot

cpiop = find  . ! -regex '\(.*~\|.*\.bak\|.*\.swp\|.*\#.*\#\)' -print0 | \
	cpio -0pd

install-exec-hook:
	mkdir -p $(DESTDIR)$(cfgdir)
	mkdir -p $(DESTDIR)$(etc_shell_leveldir)
	cd etc/shell/level; $(cpiop) $(DESTDIR)$(etc_shell_leveldir)
	cd $(DESTDIR)$(sbindir); \
	  $(LN_S) my_cli_bin my_set; \
	  $(LN_S) my_cli_bin my_delete; \
	  $(LN_S) my_cli_bin my_rename; \
	  $(LN_S) my_cli_bin my_copy; \
	  $(LN_S) my_cli_bin my_comment; \
	  $(LN_S) my_cli_bin my_discard; \
	  $(LN_S) my_cli_bin my_move; \
	  $(LN_S) my_cli_bin my_commit
	mkdir -p $(DESTDIR)/bin
	cd $(DESTDIR)/bin ; \
	  $(LN_S) $(sbindir)/my_cli_shell_api cli-shell-api


