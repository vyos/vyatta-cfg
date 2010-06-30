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
my $edit_level = "$ENV{VYATTA_EDIT_LEVEL}";

my $path = $edit_level . join '/', @path;

my $full_path = "$ENV{VYATTA_TEMP_CONFIG_DIR}/$path";

if (! -e $full_path) {
    $path = $edit_level . join '/', @path[0..$#path-1];
    my $leaf = "$ENV{VYATTA_TEMP_CONFIG_DIR}/$path/node.val";
    if (-e $leaf) {
	$full_path = "$ENV{VYATTA_TEMP_CONFIG_DIR}/$path";
    }
    else {
	`echo \"Path is not valid\n\"`;
	exit 0;
    }
}

#scan for illegal characters here: '/*', '*/'
if ($ARGV[$#ARGV] =~ /\/\*|\*\//) {
    print "illegal characters found in comment\n";
    exit 1;
}


if ($ARGV[$#ARGV] eq '') {
    `rm -f $full_path/.comment`;
}
else {
    `echo \"$ARGV[$#ARGV]\" > $full_path/.comment`;
}

print "Done\n";
exit 0;
