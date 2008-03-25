#!/usr/bin/perl

# Author: An-Cheng Huang <ancheng@vyatta.com.
# Date: 2007
# Description: Perl script for loading config file at run time.

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

# $0: config file.

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigLoad;

my $etcdir = $ENV{vyatta_sysconfdir};
my $sbindir = $ENV{vyatta_sbindir};
my $bootpath = $etcdir . "/config";
my $load_file = $bootpath . "/config.boot";

if ($#ARGV > 0) {
  print "Usage: load <config_file_name>\n";
  exit 1;
}

if (defined($ARGV[0])) {
   $load_file = $ARGV[0];
   if (!($load_file =~ /^\//)) {
      # relative path
      $load_file = "$bootpath/$load_file";
   }
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
