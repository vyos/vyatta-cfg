#!/usr/bin/perl

# Author: An-Cheng Huang <ancheng@vyatta.com>
# Date: 2007
# Description: script to save the configuration

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
use Vyatta::ConfigOutput;

my $etcdir = "/opt/vyatta/etc";
my $bootpath = $etcdir . "/config";
my $save_file = $bootpath . "/config.boot";
my $url_tmp_file = $bootpath . "/config.boot.$$";


if ($#ARGV > 0) {
  print "Usage: save [config_file_name]\n";
  exit 1;
}

if (defined($ARGV[0])) {
  $save_file = $ARGV[0];
}

my $mode = 'local';
my $proto;

if ($save_file =~ /^[^\/]\w+:\//) {
  if ($save_file =~ /^(\w+):\/\/\w/) {
    $mode = 'url';
    $proto = lc($1);
    if ($proto eq 'tftp') {
    } elsif ($proto eq 'ftp') {
    } elsif ($proto eq 'scp') {
    } else {
      print "Invalid url protocol [$proto]\n";
      exit 1;
    }
  } else {
    print "Invalid url [$save_file]\n";
    exit 1;
  }
}

if ($mode eq 'local' and !($save_file =~ /^\//)) {
  # relative path
  $save_file = "$bootpath/$save_file";
}

my $version_str = `/opt/vyatta/sbin/vyatta_current_conf_ver.pl`;
print "Saving configuration to '$save_file'...\n";

my $save;
if ($mode eq 'local') {
    # this overwrites the file if it exists. we could create a backup first.
    open $save, '>', $save_file
	or die "Can not open file '$save_file': $!\n";
} elsif ($mode eq 'url') {
    die "Package [curl] not installed\n" unless ( -f '/usr/bin/curl');

    open $save, '>', $url_tmp_file
	or die "Cannot open file '$url_tmp_file': $!\n";
}

select $save;
set_show_all(1);
outputActiveConfig();
print $version_str;
close $save;

select STDOUT;

if ($mode eq 'url') {
  my $rc = system("curl -# -T $url_tmp_file $save_file");
  system("rm -f $url_tmp_file");
  if ($rc) {
    print "Error saving $save_file\n";
    exit 1;
  }
}

print "Done\n";
exit 0;
