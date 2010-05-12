#!/usr/bin/perl

# Author: Michael Larson <mike@vyatta.com>
# Date: 2010
# Description: Perl script for activating/deactivating portions of the configuration

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


sub wanted {
    return unless ( $_ eq '.disable' );
    print("Cannot set nested deactivated nodes\n");
    exit 1;
}

sub usage() {
    print "Usage: $0 <path>\n";
    exit 0;
}

#adjust for leaf node
my $i = 0;
my @path = @ARGV[1..$#ARGV];
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
        #prevent setting on leaf or multi, check for node.val
#	$full_path = "$ENV{VYATTA_TEMP_CONFIG_DIR}/$path";
	printf("Cannot activate/deactivate end node\n");
	exit 1;
    }
    else {
	printf("This command is not valid: $path\n");
	exit 1;
    }
}


if ($ARGV[0] eq 'activate') {
    $full_path .= "/.disable";
    if (-e $full_path) {
	`rm -f $full_path`;
    }
    else {
	printf("This element is not deactivated\n");
	exit 1;
    }
}
elsif ($ARGV[0] eq 'deactivate') {
    #first let's check and ensure that there is not another child .disable node...
    #also needs to be enforced when committing
    my $active_dir = "$ENV{VYATTA_ACTIVE_CONFIGURATION_DIR}/$path";
    my $local_dir = $full_path;
    if (-e "$active_dir/.disable") {
        printf("This node is already deactivated\n");
        exit 1;
    }
    elsif (-e $active_dir) {
	find( \&wanted, $active_dir );
    }
    if (-e $local_dir) {
	find( \&wanted, $local_dir );
    }
    `touch $full_path/.disable`;
}
elsif ($ARGV[0] eq 'complete') {
    #provide match...
    printf("complete\n");
}
else {
    printf("incoming arg: " . $ARGV[0] . "\n");
    usage();
}

#if this is activate
#  make sure no activate subnodes
#  create .disable file in node
#else
#  ensure .disable file exists
#  remove node

print "Done\n";
exit 0;
