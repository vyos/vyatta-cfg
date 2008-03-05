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
# Description: bash completion for Vyatta configuration commands
# 
# **** End License ****

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaTypeChecker;

# find the type of a value (from a list of candidates)
if ($#ARGV < 1) {
  print "usage: vyatta-find-type.pl <value> <type> [<type> ...]\n";
  exit 1;
}

if (my $type = VyattaTypeChecker::findType(@ARGV)) {
  # type found
  print "$type";
  exit 0;
}

# value not valid for any of the candidates
exit 1;
