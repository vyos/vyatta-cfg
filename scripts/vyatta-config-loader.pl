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

umask 0002;

if (!open(OLDOUT, ">&STDOUT") || !open(OLDERR, ">&STDERR")
    || !open(STDOUT, "|/usr/bin/logger -t config-loader -p local0.notice")
    || !open(STDERR, ">&STDOUT")) {
  print STDERR "Cannot dup STDOUT/STDERR: $!\n";
  exit 1;
}
    
if (!open(WARN, "|/usr/bin/logger -t config-loader -p local0.warning")) {
  print OLDERR "Cannot open syslog: $!\n";
  exit 1;
}

sub restore_fds {
  open(STDOUT, ">&OLDOUT");
  open(STDERR, ">&OLDERR");
}

# get a list of all config statement in the startup config file
my @all_nodes = Vyatta::ConfigLoad::getStartupConfigStatements($ARGV[0]);
if (scalar(@all_nodes) == 0) {
  # no config statements
  restore_fds();
  exit 1;
}

# set up the config environment
my $CWRAPPER = '/opt/vyatta/sbin/vyatta-cfg-cmd-wrapper';
system("$CWRAPPER begin");
if ($? >> 8) {
  print OLDOUT "Cannot set up configuration environment\n";
  print WARN "Cannot set up configuration environment\n";
  restore_fds();
  exit 1;
}

#cmd below is added to debug last set of command ordering
my $commit_cmd = "$CWRAPPER commit";
my $cleanup_cmd = "$CWRAPPER cleanup";
my $ret = 0;
my $rank; #not used
foreach (@all_nodes) {
  my ($path_ref, $rank) = @$_;
  my $cmd = "$CWRAPPER set " . (join ' ', @$path_ref);
  # this debug file should be deleted before release
  system("echo [$cmd] >> /tmp/foo");
  $ret = system("$cmd");
  if ($ret >> 8) {
    $cmd =~ s/^.*?set /set /;
    print OLDOUT "[[$cmd]] failed\n";
    print WARN "[[$cmd]] failed\n";
    # continue after set failure (or should we abort?)
  }
}
$ret = system("$commit_cmd");
if ($ret >> 8) {
  print OLDOUT "Commit failed at boot\n";
  print WARN "Commit failed at boot\n";
  system("$cleanup_cmd");
  # exit normally after cleanup (or should we exit with error?)
}

# really clean up
system("$CWRAPPER end");
restore_fds();

exit 0;

