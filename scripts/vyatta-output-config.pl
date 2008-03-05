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
# Description: Script5 to output the configuration`
# 
# **** End License ****

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigOutput;

if ($ARGV[0] eq '-all') {
  shift;
  VyattaConfigOutput::set_show_all(1);
}
if ($ARGV[0] eq '-active') {
  shift;
  VyattaConfigOutput::set_hide_password(1);
  VyattaConfigOutput::outputActiveConfig(@ARGV);
} else {
  VyattaConfigOutput::outputNewConfig(@ARGV);
}
exit 0;
