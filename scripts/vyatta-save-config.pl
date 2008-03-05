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
# Description: script to save the configuration
# 
# **** End License ****

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigOutput;

my $etcdir = "/opt/vyatta/etc";
my $bootpath = $etcdir . "/config";
my $save_file = $bootpath . "/config.boot";

if ($#ARGV > 0) {
  print "Usage: save [config_file_name]\n";
  exit 1;
}

if (defined($ARGV[0])) {
  $save_file = $ARGV[0];
  if (!($save_file =~ /^\//)) {
    # relative path
    $save_file = "$bootpath/$save_file";
  }
}

# this overwrites the file if it exists. we could create a backup first.
if (! open(SAVE, ">$save_file")) {
  print "Cannot open file '$save_file': $!\n";
  exit 1;
}

print "Saving configuration to '$save_file'...";
select SAVE;
VyattaConfigOutput::set_show_all(1);
VyattaConfigOutput::outputActiveConfig();
my $version_str = `/opt/vyatta/sbin/vyatta_current_conf_ver.pl`;
print SAVE $version_str;
select STDOUT;
print "\nDone\n";
close SAVE;
exit 0;
