#!/usr/bin/perl

# Author: An-Cheng Huang <ancheng@vyatta.com>
# Date: 2007
# Description: bash tyep checking for Vyatta configuration commands

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
