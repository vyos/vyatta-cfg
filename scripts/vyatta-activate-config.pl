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
    print("Cannot deactivate nested elements\n");
    exit 1;
}

sub wanted_local {
    if ( $_ eq '.disable' ) {
	#we'll supercede this .disable by the parent and remove this.
	my $f = $File::Find::name;
	`rm -f $f`;
    }
}

sub check_parents {
    my @p = @_;
    my $l_dir = "$ENV{VYATTA_TEMP_CONFIG_DIR}/";
    my $a_dir = "$ENV{VYATTA_ACTIVE_CONFIGURATION_DIR}/";
    foreach my $sw (@p) {
	$l_dir .= "/$sw";
	$a_dir .= "/$sw";

	if (-e "$l_dir/.disable") {
	    return 1;
	}
	if (-e "$a_dir/.disable") {
	    return 1;
	}
    }
    return 0;
}

sub usage() {
    print "Usage: $0 <activate|deactivate> <path>\n";
    exit 0;
}

my $action = $ARGV[0];

if (!defined $ARGV[1] || $ARGV[1] eq '') {
    print("Cannot activate/deactivate configuration root\n");
    exit 1;
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
	printf("Cannot activate/deactivate end node\n");
	exit 1;
    }
    else {
	printf("This command is not valid: $path\n");
	exit 1;
    }
}

#######################################################
#now check for nesting of the activate/deactivate nodes
#######################################################
if ($action eq 'deactivate') {
    my $active_dir = "$ENV{VYATTA_ACTIVE_CONFIGURATION_DIR}/$path";
    my $local_dir = $full_path;
    if (-e $active_dir && !(-e "$active_dir/.disable")) { #checks active children
	find( \&wanted, $active_dir );
    }
    if (-e $local_dir) { #checks locally commit children, will remove disabled children
	find( \&wanted_local, $local_dir );
    }
    #final check that walks up tree and checks
    if (!(-e "$active_dir/.disable") && check_parents(@path)) { #checks active and locally committed parents
	print("Cannot deactivate nested elements\n");
	exit 1;
    }
}

#######################################################
#now apply the magic
#######################################################
if ($action eq 'activate') {
    $full_path .= "/.disable";
    if (-e $full_path) {
	`rm -f $full_path`;
    }
    else {
	printf("This element has not been deactivated\n");
	exit 1;
    }
}
elsif ($action eq 'deactivate') {
    #first let's check and ensure that there is not another child .disable node...
    #also needs to be enforced when committing
    my $active_dir = "$ENV{VYATTA_ACTIVE_CONFIGURATION_DIR}/$path";
    my $local_dir = $full_path;
    `touch $full_path/.disable`;
}
else {
    printf("bad argument: " . $action . "\n");
    usage();
}

`touch $ENV{VYATTA_TEMP_CONFIG_DIR}/.modified`;

exit 0;
