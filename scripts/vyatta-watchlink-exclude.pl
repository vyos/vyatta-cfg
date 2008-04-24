#!/usr/bin/perl
#
# Module: vyatta-watchlink-exclude.pl
# 
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
# A copy of the GNU General Public License is available as
# `/usr/share/common-licenses/GPL' in the Debian GNU/Linux distribution
# or on the World Wide Web at `http://www.gnu.org/copyleft/gpl.html'.
# You can also obtain it by writing to the Free Software Foundation,
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
# MA 02110-1301, USA.
# 
# This code was originally developed by Vyatta, Inc.
# Portions created by Vyatta are Copyright (C) 2007 Vyatta, Inc.
# All Rights Reserved.
# 
# Author: Stig Thormodsrud
# Date: March 2008
# Description: Script to update watchlink exclude file
# 
# **** End License ****
#

#
# parameters:
#   --id=""     : owner of exclude line (e.g. vrrp, ha) [required]
#   --action="" : add or remove                         [required]
#   --intf=""   : interface                             [required for add]
#   --ipaddr="" : ip address or network to execlude     [optional]
#   --signal    : should watchlink get signaled         [optional]
#
# Expected format of exclude file:
#
# <interface> [ <ipv4addr> | <ipv4net> ] # id
# 

use Getopt::Long;
use POSIX;

use strict;
use warnings;

my $exclude_file  = '/var/linkstatus/exclude';
my $watchlink_pid = '/var/run/vyatta/quagga/watchlink.pid';

sub read_exclude_file {
    my $FILE;
    my @lines = ();
    if (! -e $exclude_file) {
	return @lines;
    }
    open($FILE, "<", $exclude_file) or die "Error: read() $!";
    @lines = <$FILE>;
    close($FILE);
    chomp @lines;
    return @lines;
}

sub write_exclude_file {
    my @lines = @_;

    my $FILE;
    open($FILE, ">", $exclude_file) or die "Error: write() $!";
    if (scalar(@lines) > 0) {
	print $FILE join("\n", @lines), "\n";
    }
    close($FILE);
}

sub remove_exclude_id {
    my ($id, @lines) = @_;

    my @new_lines;
    my $match = 0;
    foreach my $line (@lines) {
	if ($line =~ /# $id$/) {
	    $match++;
	} else {
	    push @new_lines, $line;
	}
    }
    if ($match < 1) {
	print "$0: no match found for $id";
    }
    return @new_lines;
}

sub remove_exclude_line {
    my ($remove_line, @lines) = @_;

    my @new_lines;
    my $match = 0;
    foreach my $line (@lines) {
	if ($line eq $remove_line) {
	    $match++;
	} else {
	    push @new_lines, $line;
	}
    }
    if ($match < 1) {
	print "$0: no match found for $remove_line";
    }
    return @new_lines;
}

sub is_exclude_dup {
    my ($new_line, @lines) = @_;

    my $frag = substr($new_line, 0, index($new_line, ' #'));
    foreach my $line (@lines) {
	if (substr($line, 0, index($line, ' #')) eq $frag) {
	    return 1;
	}
    }
    return 0;
}


#
# main
#

my ($opt_id, $opt_action, $opt_intf, $opt_ipaddr, $opt_ipnet, $opt_signal);

GetOptions("id=s"     => \$opt_id,
	   "action=s" => \$opt_action,
	   "intf=s"   => \$opt_intf,
	   "ipaddr=s" => \$opt_ipaddr,
	   "signal!"  => \$opt_signal,
    );

if (!(defined $opt_id and defined $opt_action)) {
    die "Error: parameters --id --action must be set";
}

if ($opt_action ne "add" and $opt_action ne "remove") {
    die "Error: --action must be \"add\" or \"remove\" ";
}

if ($opt_action eq "add" and !defined($opt_intf)) {
    die "Error: --intf must be set for \"add\"";
}

my @lines = read_exclude_file();
my $new_line = "$opt_intf ";
if (defined $opt_ipaddr) {
    $new_line .= "$opt_ipaddr ";
}
if (defined $opt_id) {
    $new_line .= "# $opt_id";
}

if ($opt_action eq "add") {
    if (! is_exclude_dup($new_line, @lines)) {
	push @lines, $new_line;
    }
} elsif (defined $opt_intf) {
    @lines = remove_exclude_line($new_line, @lines);
} else {
    @lines = remove_exclude_id($opt_id, @lines);
}
write_exclude_file(@lines);

if (defined $opt_signal) {
    if (! -e $watchlink_pid) {
	#
	# watchlink may have been disabled, so don't treat 
	# this as an error
	#
	exit 0;
    }
    my $pid = `cat $watchlink_pid`;
    chomp $pid;
    system("kill -10 $pid");
}

# end of file
