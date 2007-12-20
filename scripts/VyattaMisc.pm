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

sub isIPinInterfaces {
    my ($vc, $interfaces, $local_ip) = @_;

    if (!(defined($local_ip))) {
	return 0;
    }

    my @ethernets = $vc->listNodes('interfaces ethernet');
    foreach my $ethernet (@ethernets) {
	if (defined($interfaces->{$ethernet})) {
	    my @addresses = $vc->listNodes("interfaces ethernet $ethernet address");
	    my %addresses_hash = map { $_ => 1 } @addresses;
	    if (defined($addresses_hash{$local_ip})) {
		return 1;
	    }
	    
	    my @vifs = $vc->listNodes("interfaces ethernet $ethernet vif");
	    foreach my $vif (@vifs) {
		my @addresses_vif = $vc->listNodes("interfaces ethernet $ethernet vif $vif address");
		my %addresses_vif_hash = map { $_ => 1 } @addresses_vif;
		if (defined($addresses_vif_hash{$local_ip})) {
		    return 1;
		}
		
		my $virtual_address = $vc->returnValue("interfaces ethernet $ethernet vif $vif vrrp virtual-address");
		if (defined($virtual_address) && $virtual_address eq $local_ip) {
		    return 1;
		}
	    }
	    
	    my $virtual_address = $vc->returnValue("interfaces ethernet $ethernet vrrp virtual-address");
	    if (defined($virtual_address) && $virtual_address eq $local_ip) {
		return 1;
	    }
	}
    }
    
    my @serials = $vc->listNodes('interfaces serial');
    foreach my $serial (@serials) {
	if (defined($interfaces->{$serial})) {
	    my @ppp_vifs = $vc->listNodes("interfaces serial $serial ppp vif");
	    foreach my $ppp_vif (@ppp_vifs) {
		my $local_address = $vc->returnValue("interfaces serial $serial ppp vif $ppp_vif address local-address");
		if (defined($local_address) && $local_address eq $local_ip) {
		    return 1;
		}
	    }
	    
	    my @cisco_hdlc_vifs = $vc->listNodes("interfaces serial $serial cisco-hdlc vif");
	    foreach my $cisco_hdlc_vif (@cisco_hdlc_vifs) {
		my $local_address = $vc->returnValue("interfaces serial $serial cisco-hdlc vif $cisco_hdlc_vif address local-address");
		if (defined($local_address) && $local_address eq $local_ip) {
		    return 1;
		}
	    }
	    
	    my @frame_relay_vifs = $vc->listNodes("interfaces serial $serial frame-relay vif");
	    foreach my $frame_relay_vif (@frame_relay_vifs) {
		my $local_address = $vc->returnValue("interfaces serial $serial frame-relay vif $frame_relay_vif address local-address");
		if (defined($local_address) && $local_address eq $local_ip) {
		    return 1;
		}
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
