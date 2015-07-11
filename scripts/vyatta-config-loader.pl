#!/usr/bin/perl

# Author: An-Cheng Huang <ancheng@vyatta.com>
# Date: 2007
# Description: configuration loader

# **** License ****
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# This code was originally developed by Vyatta, Inc.
# Portions created by Vyatta are Copyright (C) 2006, 2007, 2008 Vyatta, Inc.
# All Rights Reserved.
# **** End License ****

# Perl script for loading the startup config file.
# $0: startup config file.

use strict;
use lib "/opt/vyatta/share/perl5/";
use Vyatta::ConfigLoad;
use Sys::Syslog qw(:standard :macros);
use POSIX qw(strftime);

my $CWRAPPER = '/opt/vyatta/sbin/vyatta-cfg-cmd-wrapper';
my $CONFIG_LOG = '/var/log/vyatta/vyatta-config-loader.log';
my $COMMIT_CMD  = "$CWRAPPER commit";
my $CLEANUP_CMD = "$CWRAPPER cleanup";
my $BEGIN_CMD   = "$CWRAPPER begin";
my $END_CMD     = "$CWRAPPER end";

umask 0002;

# Set up logging
openlog("config-loader", "nofail", LOG_LOCAL0);

open(STDIN, '<', "/dev/null")
    or die "Can't open /dev/null : $!";
open(STDOUT, '>>', $CONFIG_LOG)
    or die "Can't open $CONFIG_LOG : $!";
open(STDERR, '>&STDOUT')
    or die "Can't redirect stderr: $!";

sub trace {
    my $str = shift;

    printf "%s %s\n", strftime("%F %T ", localtime), $str;
}

# get a list of all config statement in the startup config file
my %cfg_hier = Vyatta::ConfigLoad::getStartupConfigStatements($ARGV[0],'true');
my @all_nodes    = @{$cfg_hier{'set'}};

# empty configuration?
die "Empty configuration!\n"
    if (scalar(@all_nodes) == 0);

# set up the config environment
unless (system($BEGIN_CMD) == 0) {
    syslog(LOG_WARNING, "Cannot set up configuration environment");
    die "Cannot set up configuration environment\n";
}

my $start = time;
trace "-- begin";

#cmd below is added to debug last set of command ordering
foreach (@all_nodes) {
    my ($path_ref, $rank) = @$_;
    my @pr = @$path_ref;

    if ($pr[0] =~ /^comment$/) {
        my $ct = 0;
        my $rel_path;
        foreach my $rp (@pr[1..$#pr]) {
            $ct++;
            my $tmp_path = $rel_path . "/" . $rp;
            my $node_path = "/opt/vyatta/share/vyatta-cfg/templates/". $tmp_path . "/node.def";

            last if ($rp eq '"');
            last if ($rp eq '""');

            if (!-e $node_path) {

                #pop this element
                delete $pr[$ct];
                last;
            }
            $rel_path = $tmp_path;
        }

        my $comment_cmd = "$CWRAPPER " . join(" ",@pr);
        `$comment_cmd`;
        next;
    }

    my $cmd = 'set ' . join(' ', @pr);

    # Show all commands in log
    trace $cmd;
    unless (system("$CWRAPPER $cmd") == 0) {
        warn "*** %s failed: %d\n", $cmd, $?;
        syslog(LOG_NOTICE, "[[%s]] failed", $cmd);
    }
}

my $commit_start = time;
trace "commit";
syslog(LOG_INFO, "Configuration took %d seconds.", $commit_start - $start);

unless (system($COMMIT_CMD) == 0) {
    warn "*** Commit failed: %d\n", $?;
    syslog(LOG_WARNING, "Commit failed at boot");

    system($CLEANUP_CMD);
    system($END_CMD);
    exit 1;
}

my $commit_end = time;
syslog(LOG_INFO, "Commit succeeded took %d seconds.",$commit_end - $commit_start);

# Now process any deactivate nodes
my @deactivate_nodes = @{$cfg_hier{'deactivate'}};
if (@deactivate_nodes) {
    foreach (@deactivate_nodes) {
        my $cmd = "deactivate " . $_;
        trace $cmd;

        unless (system("$CWRAPPER $cmd") == 0) {
            warn "*** %s failed: %d\n", $cmd, $?;
            syslog(LOG_WARNING, "[[%s]] failed", $cmd);
            last;
        }
    }

    unless (system($COMMIT_CMD) == 0) {
        warn  "deactivate commit failed: %d\n", $?;
        syslog(LOG_NOTICE, "Commit deactivate failed at boot");
        system($CLEANUP_CMD);
    }
}

unless (system($END_CMD) == 0) {
    syslog(LOG_WARNING, "Cannot teardown configuration environment");
    die "Cannot teardown configuration environment\n";
}
trace "done.";

exit 0;
