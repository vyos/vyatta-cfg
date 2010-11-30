#!/usr/bin/perl

# Author: An-Cheng Huang <ancheng@vyatta.com>
# Date: 2007
# Description: hack of vyatta-config-load.pl to simple generate
#              a list of the "set" commands from a config file.

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

my $conf_file;
my $conf_tmp = "/tmp/config.boot.$$";

if (defined $ARGV[0]) {
    $conf_file = $ARGV[0];
} else {
    system("cli-shell-api showCfg --show-active-only  > $conf_tmp");
    $conf_file = $conf_tmp;
}

# get a list of all config statement in the startup config file
my %cfg_hier = Vyatta::ConfigLoad::getStartupConfigStatements($conf_file);
my @all_nodes    = @{ $cfg_hier{'set'} };
if (scalar(@all_nodes) == 0) {
  # no config statements
  exit 1;
}

my $ret = 0;
foreach (@all_nodes) {
  my ($path_ref) = @$_;
  my $elements = scalar(@$path_ref);
  my @non_leaf = @$path_ref[0 .. ($elements - 2)] ;
  my $path = join(' ', @non_leaf);
  $path =~ s/'//g;
  my $cmd = "set $path " . @$path_ref[($elements - 1)];
  print "$cmd\n";
}

system("rm -f $conf_tmp") if $conf_file eq $conf_tmp;

exit 0;

