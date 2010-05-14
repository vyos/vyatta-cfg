#!/usr/bin/perl

# Author: Michael Larson <mike@vyatta.com>
# Date: 2010
# Description: Perl script for adding comments to portions of the configuration

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
# Portions created by Vyatta are Copyright (C) 2006, 2007, 2008, 2009, 2010 Vyatta, Inc.
# All Rights Reserved.
# **** End License ****

use strict;
use warnings;
use File::Find;
use lib "/opt/vyatta/share/perl5";


sub usage() {
    print "Usage: $0 <path>\n";
    exit 0;
}

if ($#ARGV == 0) {
    exit 0;
}

#adjust for leaf node
my $i = 0;
my @path = @ARGV[0..$#ARGV-1];
foreach my $elem (@path) {
    $elem =~ s/\//%2F/g;
    $elem =~ s/\s+/\//g;
    $path[$i++] = $elem;
}
my $path = join '/', @path;

my $full_path = "$ENV{VYATTA_TEMP_CONFIG_DIR}/$path";

if (! -e $full_path) {
    $path = join '/', @path[0..$#path-1];
    my $leaf = "$ENV{VYATTA_TEMP_CONFIG_DIR}/$path/node.val";
    if (-e $leaf) {
	$full_path = "$ENV{VYATTA_TEMP_CONFIG_DIR}/$path";
    }
    else {
	`echo \"Path is not valid\n\"`;
	exit 0;
    }
}
#else {
#    print "echo \"$ARGV[$#ARGV]\" > $full_path/node.comment\n";
#    `echo \"$ARGV[$#ARGV]\" > $full_path/node.comment`;
#}

if ($ARGV[$#ARGV] eq '') {
    `rm -f $full_path/.comment`;
}
else {
    `echo \"$ARGV[$#ARGV]\" > $full_path/.comment`;
}

#first let's check and ensure that there is not another child .disable node...
#also needs to be enforced when committing
#my $active_dir = "$ENV{VYATTA_ACTIVE_CONFIGURATION_DIR}/$path";
#my $local_dir = $full_path;
#if (-e $active_dir) {
#    find( \&wanted, $active_dir );
#}
#if (-e $local_dir) {
#    find( \&wanted, $local_dir );
#}
#`touch $full_path/node.comment`;

#if this is activate
#  make sure no activate subnodes
#  create .disable file in node
#else
#  ensure .disable file exists
#  remove node

print "Done\n";
exit 0;
