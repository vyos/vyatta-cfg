#!/usr/bin/perl

# Author: An-Cheng Huang <ancheng@vyatta.com>
# Date: 2007
# Description: Script to output the configuration

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
use lib "/opt/vyatta/share/perl5";
use Vyatta::Config::Output;

if ($ARGV[0] eq '-all') {
  shift;
  set_show_all(1);
}

if ($ARGV[0] eq '-active') {
  shift;
  set_hide_password(1);
  outputActiveConfig(@ARGV);
} else {
  outputNewConfig(@ARGV);
}

exit 0;
