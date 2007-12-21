#
# Module: VyattaMisc.pm
#
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
# Portions created by Vyatta are Copyright (C) 2005, 2006, 2007 Vyatta, Inc.
# All Rights Reserved.
#
# Author: Marat
# Date: 2007
# Description:
#
# **** End License ****
#

package VyattaMisc;
require Exporter;
@ISA	= qw(Exporter);
@EXPORT	= qw(getNetAddIP isIpAddress);
@EXPORT_OK = qw(getNetAddIP isIpAddress);


use strict;

use VyattaConfig;

sub getNetAddrIP {
    my ($interface);
    ($interface) = @_;

    if ($interface eq '') {
	print STDERR "Error:  No interface specified.\n";
	return undef;
    }

    my $ifconfig_out = `ifconfig $interface`;
    $ifconfig_out =~ /inet addr:(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})/;
    my $ip = $1;
    if ($ip eq '') {
	print STDERR "Error:  Unable to determine IP address for interface \'$interface\'.\n";
	return undef;
    }

    $ifconfig_out =~ /Mask:(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})/;
    my $netmask = $1;
    if ($netmask eq '') {
	print STDERR "Error:  Unable to determine netmask for interface \'$interface\'.\n";
	return undef;
    }
    
    use NetAddr::IP;  # This library is available via libnetaddr-ip-perl.deb
    my $naip = new NetAddr::IP($ip, $netmask);
    return $naip;
}

sub isIpAddress {
  my $ip = shift;

  $_ = $ip;
  if ( ! /^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$/) {
    return 0;
  }
  else {
    my @ips = split /\./, $ip;
    my $octet = 0;
    my $counter = 0;

    foreach $octet (@ips) {
      if (($octet < 0) || ($octet > 255)) { return 0; }
      if (($counter == 0) && ($octet < 1)) { return 0; }
      $counter++;
    }
  }

  return 1;
}

sub isClusterIP {
    my ($vc, $ip) = @_;
    
    if (!(defined($ip))) {
	return 0;
    }
    
    my @cluster_groups = $vc->listNodes('cluster group');
    foreach my $cluster_group (@cluster_groups) {
	my @services = $vc->returnValues("cluster group $cluster_group service");
	foreach my $service (@services) {
	    if ($ip eq $service) {
		return 1;
	    }
	}
    }
    
    return 0;
}

sub remove_ip_prefix {
    my @addr_nets = @_;

    s/\/\d+$//  for @addr_nets;    
    return @addr_nets;
}

sub is_ip_in_list {
    my ($ip, @list) = @_;
    
    if (!defined($ip) || scalar(@list) == 0) {
	return 0;
    }

    @list = remove_ip_prefix(@list);
    my %list_hash = map { $_ => 1 } @list;
    if (defined($list_hash{$ip})) {
	return 1;
    } else {
	return 0;
    }
}

sub get_eth_ip_addrs {
    my ($vc, $eth_path) = @_;

    my @addrs      = ();
    my @virt_addrs = ();

    $vc->setLevel("interfaces ethernet $eth_path");
    @addrs = $vc->returnValues("address");

    #
    # check for VIPs
    #
    $vc->setLevel("interfaces ethernet $eth_path vrrp vrrp-group");
    my @vrrp_groups = $vc->listNodes();
    foreach my $group (@vrrp_groups) {
	$vc->setLevel("interfaces ethernet $eth_path vrrp vrrp-group $group");
	@virt_addrs = $vc->returnValues("virtual-address");
    }
    return (@addrs, @virt_addrs);
}

sub get_serial_ip_addrs {
    #
    # Todo when serial is added
    #
}

sub isIPinInterfaces {
    my ($vc, $ip_addr, @interfaces) = @_;

    if (!(defined($ip_addr))) {
	return 0;
    }

    foreach my $intf (@interfaces) {
	# regular ethernet
	if ($intf =~ m/^eth\d+$/) {
	    my @addresses = get_eth_ip_addrs($vc, $intf);
	    if (is_ip_in_list($ip_addr, @addresses)) {
		return 1;
	    }
	}
	# ethernet vlan
	if ($intf =~ m/^eth(\d+).(\d+)$/) {
	    my $eth = "eth$1";
	    my $vif = $2;
	    my @addresses = get_eth_ip_addrs($vc, "$eth vif $vif");
	    if (is_ip_in_list($ip_addr, @addresses)) {
		return 1;
	    }
	}
	# serial
	if ($intf =~ m/^wan(\d+).(\d+)$/) {
	    my @addresses = get_serial_ip_addrs($vc, $intf);
	    if (is_ip_in_list($ip_addr, @addresses)) {
		return 1;
	    }
	}
    }
    
    return 0;
}

sub isClusteringEnabled {
    my ($vc) = @_;
    
    if ($vc->exists('cluster')) {
	return 1;
    } else {
	return 0;
    } 
}

return 1;
