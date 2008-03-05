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
# Description: bash expand script
# 
# **** End License ****

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfig;

# expand a variable reference
if ($#ARGV != 0) {
  print STDERR "usage: vyatta-cli-expand-var.pl '<var-ref>'\n";
  exit 1;
}

$_ = $ARGV[0];

# basic format check:
# '(' ')' not allowed in reference.
# only allow absolute path for now.
if (!/^\$\(\/([^()]+)\)$/) {
  print STDERR "invalid variable reference (invalid format)\n";
  exit 1;
}
$_ = $1;

my $multi_val = 1;
if (s/^(.*)\/\@\@$/$1/) {
  # return list of multi-node values
  $multi_val = 1;
} elsif (s/^(.*)\/\@$/$1/) {
  # return single value
  $multi_val = 0;
} else {
  # only allow the above 2 forms for now.
  print STDERR "invalid variable reference (invalid value specification)\n";
  exit 1;
}

if (/\@/) {
  # '@' not allowed anywhere else in the reference for now.
  print STDERR "invalid variable reference (extra value specification)\n";
  exit 1;
}

my $config = new VyattaConfig;
my $path_str = join ' ', (split /\//);
my $val_str = "";
if ($multi_val) {
  my @tmp = $config->returnOrigValues($path_str);
  if (scalar(@tmp) > 0) {
    # we got multiple values back
    $val_str = join ' ', @tmp;
  } else {
    # this node may be a 'tag' node. try listing children.
    $config->setLevel($path_str);
    @tmp = $config->listOrigNodes();
    $val_str = join ' ', @tmp;
  }
} else {
  $val_str = $config->returnOrigValue($path_str);
}

# expanded string is printed on stdout (multiple values separated by ' ').
print "$val_str";
exit 0;

