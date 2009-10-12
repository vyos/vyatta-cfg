#!/usr/bin/perl
#
# Module: vyatta-auto-irqaffin.pl
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
# This code was originally developed by Vyatta, Inc.
# Portions created by Vyatta are Copyright (C) 2009 Vyatta, Inc.
# All Rights Reserved.
#
# Author: Bob Gilligan (gilligan@vyatta.com)
# Date: October 2009
# Description: Script to configure optimal IRQ affinity for NICs.
#
# **** End License ****
#

# This script attempts to perform a static affinity assignment for network
# interfaces.  It is primarily targeted at supporting multi-queue NICs. 
# Since different NICs may have different queue organizations, and
# because there is no standard API for learning the mapping between
# queues and IRQ numbers, different code is required for each driver.
#
# The general strategy includes:
#  - Spread the receive load among as many CPUs as possible.
#  - For NICs that provide both rx and tx queue, keep the tx queue
#    on the same CPU as the corresponding rx queue.
#  - For all multi-queue NICs in the system, the same tx and rx queue
#    numbers should interrupt the same CPUs.  I.e. tx and rx queue 0
#    of all NICs should interrupt the same CPU.
#  - If hyperthreading is supported and enabled, avoid assigning
#    queues to both CPUs of a hyperthreaded pair if there are enough
#    CPUs available to do that.
#


use lib "/opt/vyatta/share/perl5";
use Getopt::Long;

use warnings;
use strict;

# Send output of shell commands to syslog for debugging and so that
# the user is not confused by it.  Log at debug level, which is supressed
# by default, so that we don't unnecessarily fill up the syslog file.
my $logger = 'logger -t firewall-cfg -p local0.debug --';

# Enable printing debug output to stdout.
my $debug_flag = 0;
my $syslog_flag = 0;

my $setup_ifname;

GetOptions("setup=s"		=> \$setup_ifname,
	   "debug"		=> \$debug_flag
    );

sub log_msg {
    my $message = shift;

    print "DEBUG: $message" if $debug_flag;
    system("$logger DEBUG: \"$message\"") if $syslog_flag;
}


# Affinity strategy function for the igb driver.  NICs using this
# driver have an equal number of rx and tx queues.  The first part of
# the strategy for optimal performance is to assign irq of each queue
# in a pair of tx and rx queues that have the same queue number to the
# same CPU.  I.e., assign queue 0 to CPU X, queue 1 to CPU Y, etc.
# The second part is to avoid assigning any queues to the second CPU
# in a hyper-threaded pair, if posible.  I.e., if CPU 0 and 1 are
# hyper-threaded pairs, then assign a queue to CPU 0, but try to avoid
# assigning one to to CPU 1.  But if we have more queues than CPUs, then
# it is OK to assign some to the second CPU in a hyperthreaded pair.
# 
sub igb_func{
    my ($ifname, $numcpus, $numcores) = @_;
    my $rx_queues;	# number of rx queues
    my $tx_queues;	# number of tx queues
    my $ht_factor;	# 2 if HT enabled, 1 if not

    log_msg("igb_func was called.\n");

    if ($numcpus > $numcores) {
	$ht_factor = 2;
    } else {
	$ht_factor = 1;
    }

    log_msg("ht_factor is $ht_factor.\n");

    # Figure out how many queues we have

    $rx_queues=`grep "$ifname-rx-" /proc/interrupts | wc -l`;
    $rx_queues =~ s/\n//;

    $tx_queues=`grep "$ifname-tx-" /proc/interrupts | wc -l`;
    $tx_queues =~ s/\n//;

    log_msg("rx_queues is $rx_queues.  tx_queues is $tx_queues\n");
    
    if ($rx_queues != $tx_queues) {
	printf("Error: rx and tx queues don't match for igb driver.\n");
	exit 1;
    }

    # For i = 0 to number of queues:
    #    Affinity of rx and tx queue $i gets CPU ($i * (2 if HT, 1 if no HT)) 
    #                                   % number_of_cpus
    for (my $queue = 0, my $cpu = 0; ($queue < $rx_queues) ; $queue++) {
	# Generate the hex string for the bitmask representing this CPU
	my $cpu_bit = 1 << $cpu;
	my $cpu_hex = sprintf("%x", $cpu_bit);
	log_msg ("queue=$queue cpu=$cpu cpu_bit=$cpu_bit cpu_hex=$cpu_hex\n");
	
	# Get the IRQ number for RX queue
	my $rx_irq=`grep "$ifname-rx-$queue" /proc/interrupts | awk -F: '{print \$1}'`;
	$rx_irq =~ s/\n//;
	$rx_irq =~ s/ //g;

	# Get the IRQ number for TX queue
	my $tx_irq=`grep "$ifname-tx-$queue" /proc/interrupts | awk -F: '{print \$1}'`;
	$tx_irq =~ s/\n//;
	$tx_irq =~ s/ //g;

	log_msg("rx_irq = $rx_irq.  tx_irq = $tx_irq\n");

	# Assign CPU affinity for both IRQs
	system "echo $cpu_hex > /proc/irq/$rx_irq/smp_affinity";
	system "echo $cpu_hex > /proc/irq/$tx_irq/smp_affinity";

	$cpu += $ht_factor;

	if ($cpu >= $numcpus) {
	    # Must "wrap"
	    $cpu %= $numcpus;

	    if ($ht_factor > 1) {
		# Next time through, select the other CPU in a hyperthreaded 
		# pair.
		if ($cpu == 0) {
		    $cpu++;
		} else {
		    $cpu--;
		}
	    }
	}
    }
};

# Similar strategy as for igb driver, but Broadcom NICs do not have
# separate receive and transmit queues.
sub bnx2_func{
    my ($ifname, $numcpus, $numcores) = @_;
    my $num_queues;	# number of queues
    my $ht_factor;	# 2 if HT enabled, 1 if not

    log_msg("bnx2_func was called.\n");

    # Figure out how many queues we have
    $num_queues=`grep "$ifname-" /proc/interrupts | wc -l`;
    $num_queues =~ s/\n//;

    log_msg("num_queues=$num_queues\n");

    if ($num_queues <=0) {
	printf("ERROR: No queues found for $ifname\n");
	exit 1;
    }

    if ($numcpus > $numcores) {
	$ht_factor = 2;
    } else {
	$ht_factor = 1;
    }

    log_msg("ht_factor is $ht_factor.\n");

    for (my $queue = 0, my $cpu = 0; ($queue < $num_queues) ; $queue++) {
	# Generate the hex string for the bitmask representing this CPU
	my $cpu_bit = 1 << $cpu;
	my $cpu_hex = sprintf("%x", $cpu_bit);
	log_msg ("queue=$queue cpu=$cpu cpu_bit=$cpu_bit cpu_hex=$cpu_hex\n");
	
	# Get the IRQ number for the queue
	my $irq=`grep "$ifname-$queue" /proc/interrupts | awk -F: '{print \$1}'`;
	$irq =~ s/\n//;
	$irq =~ s/ //g;

	log_msg("irq = $irq.\n");

	# Assign CPU affinity for this IRQs
	system "echo $cpu_hex > /proc/irq/$irq/smp_affinity";

	$cpu += $ht_factor;
	if ($cpu >= $numcpus) {
	    # Must "wrap"
	    $cpu %= $numcpus;

	    if ($ht_factor > 1) {
		# Next time through, select the other CPU in a hyperthreaded
		# pair.
		if ($cpu == 0) {
		    $cpu++;
		} else {
		    $cpu--;
		}
	    }
	}
    }
}

my %driver_hash = ( 'igb' => \&igb_func,
		    'ixbg' => \&igb_func,
		    'bnx2' =>\&bnx2_func );

if (defined $setup_ifname) {
    # Set up automatic IRQ affinity for the named interface

    log_msg("setup $setup_ifname\n");

    my $ifname = $setup_ifname;	# shorter variable name
    my $drivername;	# Name of the NIC driver, e.g. "igb".
    my $numcpus;	# Number of Linux "cpus"
    my $numcores;	# Number of unique CPU cores
    my $driver_func;	# Pointer to fuction specific to a driver

    # Determine how many CPUs the machine has
    $numcpus=`grep "^processor" /proc/cpuinfo | wc -l`;
    $numcpus =~ s/\n//;

    log_msg("numcpus is $numcpus\n");

    if ($numcpus == 1) {
	# Nothing to do if we only have one CPU, so just exit quietly.
	exit 0;
    }

    # Verify that interface exists
    if (! (-e "/proc/sys/net/ipv4/conf/$ifname")) {
	printf("Error: Interface $ifname does not exist\n");
	exit 1;
    }

    # Figure out what driver this NIC is using.
    $drivername=`ethtool -i $ifname | grep "^driver" | awk '{print \$2}'`;
    $drivername =~ s/\n//;

    log_msg("drivername is $drivername\n");

    $driver_func = $driver_hash{$drivername};

    # We only support a couple of drivers at this time, so just exit
    # if its not one we support.
    if (! defined($driver_func)) {
	printf("Automatic SMP affinity not supported for NICs using the $drivername driver.\n");
	exit 0;	# not an error
    }

    # Determine whether machine has hyperthreading enabled
    $numcores=`grep "^core id" /proc/cpuinfo | uniq | wc -l`;
    $numcores =~ s/\n//;
    
    log_msg("numcores is $numcores.\n");

    &$driver_func($ifname, $numcpus, $numcores);

    exit 0;
}

printf("Must specify options.\n");
exit(1);


