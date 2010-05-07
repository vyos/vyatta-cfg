#!/usr/bin/perl

# Author: Arthur Xiong
# Date: 04/15/2010
# Description: Script to automatically update/add priority tag with 
#              some value for each node.def according to the inputs 

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

die "Usage: update-priority.pl <priority-file> <path-to-template>"
 unless $#ARGV == 1;

my $priority;
my $path;
my $comment;
my $priority_line;
my $node_def;
my $priority_file = $ARGV[0];
my $prefix  = $ARGV[1];

open my $pf, '<', $priority_file or die "$priority_file can't be opened";
while (<$pf>) {    
    chomp;
    next if /^#.*/ or /^$/;
    die "Syntax Error \"$_\"" unless /^(\d+)\s+(\S+)(|\s+|\s+#.*)$/;
    $priority = $1;
    $path = $2;
    $comment = $3;

    $priority_line = "";
    $priority_line = "priority: " . $priority . "\t" . $comment . "\n";
    print "priority_line: $priority_line";

    $node_def = "";
    $node_def = $prefix . "/" . $path . "/" . "node.def";
    print "node_def: ", $node_def, "\n";

    open my $nf, '<', $node_def  or die "$node_def can't be opened";
    open my $nfn, '>', "$node_def.new" or die "$node_def.new can't be opened";
    while (<$nf>) {
        last unless /^#.*/ or /^$/;
        print $nfn $_;
    }
    print $nfn $_ if /^(tag|multi):/;
    print $nfn $priority_line if $priority != 0;
    print $nfn $_ unless /^priority:\s(\d+)/ or /^(tag|multi):/;
    while (<$nf>) {
        print $nfn $_ unless /^priority:\s(\d+)/;
    }
    close $nfn;
    close $nf;

    rename "$node_def.new", "$node_def"
     or die "$node_def.new can't be renamed to $node_def";

    print "Updated $node_def\n\n";
}
close $pf;
