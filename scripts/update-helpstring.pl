#!/usr/bin/perl

# Author: Arthur Xiong
# Date: 06/21/2010
# Description: Script to automatically update help string for each 
#              node.def according to the inputs 

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
# Portions created by Vyatta are Copyright (C) 2006-2010 Vyatta, Inc.
# All Rights Reserved.
#
# **** End License ****

use strict;
use warnings;

die "Usage: update-helpstring.pl <helpstring-file> <path-to-template>"
 unless $#ARGV == 1;

my $helpstring;
my $path;
my $comment;
my $help_line;
my $node_def;
my $helpstring_file = $ARGV[0];
my $prefix  = $ARGV[1];

open my $hf, '<', $helpstring_file or die "$helpstring_file can't be opened";
while (<$hf>) {    
    chomp;
    next if /^#.*/ or /^\s*$/;
    die "Syntax Error \"$_\"" unless /^(\S+)\s+\|\s*(\S.*)$/;
    $path = $1;
    $helpstring = $2;

    $help_line = "";
    $help_line = "help: " . $helpstring . "\n";
    print "$help_line";

    $node_def = "";
    $node_def = $prefix . "/" . $path . "/" . "node.def";

    open my $nf, '<', $node_def  or die "$node_def can't be opened";
    open my $nfn, '>', "$node_def.new" or die "$node_def.new can't be opened";
    while (<$nf>) {
        if ( /^help:/ ) {
            print $nfn $help_line;
        } else {
            print $nfn $_;
        }
    }
    close $nfn;
    close $nf;

    rename "$node_def.new", "$node_def"
     or die "$node_def.new can't be renamed to $node_def";

    print "Updated $node_def\n\n";
}
close $hf;
