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
# Description: Perl script for loading config file at run time.
# 
# **** End License ****

# $0: config file.

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigLoad;

my $etcdir = $ENV{vyatta_sysconfdir};
my $sbindir = $ENV{vyatta_sbindir};
my $bootpath = $etcdir . "/config";

if ($#ARGV != 0) {
  print "Usage: load <config_file_name>\n";
  exit 1;
}

my $load_file = $ARGV[0];
if (!($load_file =~ /^\//)) {
  # relative path
  $load_file = "$bootpath/$load_file";
}

# do config migration
system("$sbindir/vyatta_config_migrate.pl $load_file");

print "Loading config file $load_file...\n";
my %cfg_hier = VyattaConfigLoad::loadConfigHierarchy($load_file);
if (scalar(keys %cfg_hier) == 0) {
  print "The specified file does not contain any configuration.\n";
  print "Do you want to remove everything in the running configuration? [no] ";
  my $resp = <STDIN>;
  if (!($resp =~ /^yes$/i)) {
    print "Configuration not loaded\n";
    exit 1;
  }
}

my %cfg_diff = VyattaConfigLoad::getConfigDiff(\%cfg_hier);

my @delete_list = @{$cfg_diff{'delete'}};
my @set_list = @{$cfg_diff{'set'}};

foreach (@delete_list) {
  my ($cmd_ref, $rank) = @{$_};
  my @cmd = ( "$sbindir/my_delete", @{$cmd_ref} );
  my $cmd_str = join ' ', @cmd;
  system("$cmd_str");
  if ($? >> 8) {
    $cmd_str =~ s/^$sbindir\/my_//;
    print "\"$cmd_str\" failed\n";
  }
}

foreach (@set_list) {
  my ($cmd_ref, $rank) = @{$_};
  my @cmd = ( "$sbindir/my_set", @{$cmd_ref} );
  my $cmd_str = join ' ', @cmd;
  system("$cmd_str");
  if ($? >> 8) {
    $cmd_str =~ s/^$sbindir\/my_//;
    print "\"$cmd_str\" failed\n";
  }
}

system("$sbindir/my_commit");
if ($? >> 8) {
  print "Load failed (commit failed)\n";
  exit 1;
}

print "Done\n";
exit 0;
