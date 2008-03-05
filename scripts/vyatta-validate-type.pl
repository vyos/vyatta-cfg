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
# Description: script to validate types
# 
# **** End License ****

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaTypeChecker;

# validate a value of a specific type
if ($#ARGV < 1) {
  print "usage: vyatta-validate-type.pl [-q] <type> <value>\n";
  exit 1;
}

my $quiet = undef;
if ($ARGV[0] eq '-q') {
  shift;
  $quiet = 1;
}

exit 0 if (VyattaTypeChecker::validateType($ARGV[0], $ARGV[1], $quiet));
exit 1;
