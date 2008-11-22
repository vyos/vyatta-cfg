#!/usr/bin/perl

# Author: An-Cheng Huang <ancheng@vyatta.com>
# Date: 2007
# Description: script to validate types

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
use Vyatta::TypeChecker;

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

exit 0 if (Vyatta::TypeChecker::validateType($ARGV[0], $ARGV[1], $quiet));
exit 1;
