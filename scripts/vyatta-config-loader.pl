#!/usr/bin/perl

# **** License ****
# Version: VPL 1.0
# 
# The contents of this file are subject to the Vyatta Public License
# Version 1.0 ("License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://www.vyatta.com/vpl
# 
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
# the License for the specific language governing rights and limitations
# under the License.
# 
# This code was originally developed by Vyatta, Inc.
# Portions created by Vyatta are Copyright (C) 2006, 2007 Vyatta, Inc.
# All Rights Reserved.
# 
# Author: An-Cheng Huang
# Date: 2007
# Description: configuration loader
# 
# **** End License ****

# Perl script for loading the startup config file.
# $0: startup config file.

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigLoad;

umask 0002;

if (!open(OLDOUT, ">&STDOUT") || !open(OLDERR, ">&STDERR")
    || !open(STDOUT, "|/usr/bin/logger -t config-loader -p local0.debug")
    || !open(STDERR, ">&STDOUT")) {
  print STDERR "Cannot dup STDOUT/STDERR: $!\n";
  exit 1;
}

sub restore_fds {
  open(STDOUT, ">&OLDOUT");
  open(STDERR, ">&OLDERR");
}

# get a list of all config statement in the startup config file
# (sorted by rank).
my @all_nodes = VyattaConfigLoad::getStartupConfigStatements($ARGV[0]);
if (scalar(@all_nodes) == 0) {
  # no config statements
  restore_fds();
  exit 1;
}
my $cur_rank = ${$all_nodes[0]}[1];

# set up the config environment
my $CWRAPPER = '/opt/vyatta/sbin/vyatta-cfg-cmd-wrapper';
system("$CWRAPPER begin");
if ($? >> 8) {
  print OLDOUT "Cannot set up configuration environment\n";
  print STDOUT "Cannot set up configuration environment\n";
  restore_fds();
  exit 1;
}

my $commit_cmd = "$CWRAPPER commit";
my $cleanup_cmd = "$CWRAPPER cleanup";
my $ret = 0;
# higher-ranked statements committed before lower-ranked.
foreach (@all_nodes) {
  my ($path_ref, $rank) = @$_;
  if ($rank != $cur_rank) {
    # commit all nodes with the same rank together.
    $ret = system("$commit_cmd");
    if ($ret >> 8) {
      print OLDOUT "Commit failed at rank $cur_rank\n";
      print STDOUT "Commit failed at rank $cur_rank\n";
      system("$cleanup_cmd");
      # continue after cleanup (or should we abort?)
    }
    $cur_rank = $rank;
  }
  my $cmd = "$CWRAPPER set " . (join ' ', @$path_ref);
  $ret = system("$cmd");
  if ($ret >> 8) {
    $cmd =~ s/^.*?set /set /;
    print OLDOUT "[[$cmd]] failed\n";
    print STDOUT "[[$cmd]] failed\n";
    # continue after set failure (or should we abort?)
  }
}
$ret = system("$commit_cmd");
if ($ret >> 8) {
  print OLDOUT "Commit failed at rank $cur_rank\n";
  print STDOUT "Commit failed at rank $cur_rank\n";
  system("$cleanup_cmd");
  # exit normally after cleanup (or should we exit with error?)
}

# really clean up
system("$CWRAPPER end");
restore_fds();

exit 0;

