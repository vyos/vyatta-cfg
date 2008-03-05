#!/usr/bin/perl

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
# Description: Implements miscellaneous commands
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

sub get_tun_ip_addrs {
    my ($vc, $tun_path) = @_;

    my @addrs      = ();
    my @virt_addrs = ();

    $vc->setLevel("interfaces tunnel $tun_path");
    @addrs = $vc->returnValues("address");

    #
    # check for VIPs
    #
    $vc->setLevel("interfaces tunnel $tun_path vrrp vrrp-group");
    my @vrrp_groups = $vc->listNodes();
    foreach my $group (@vrrp_groups) {
	$vc->setLevel("interfaces tunnel $tun_path vrrp vrrp-group $group");
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
	# tunnel
        if ($intf =~ m/^tun\d+$/) {
	    my @addresses = get_tun_ip_addrs($vc, $intf);
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

# $str: string representing a port number
# returns ($success, $err)
# $success: 1 if success. otherwise undef
# $err: error message if failure. otherwise undef
sub isValidPortNumber {
  my $str = shift;
  return (undef, "\"$str\" is not a valid port number")
    if (!($str =~ /^\d+$/));
  return (undef, "invalid port \"$str\" (must be between 1 and 65535)")
    if ($str < 1 || $str > 65535);
  return (1, undef);
}

# $str: string representing a port range
# $sep: separator for range
# returns ($success, $err)
# $success: 1 if success. otherwise undef
# $err: error message if failure. otherwise undef
sub isValidPortRange {
  my $str = shift;
  my $sep = shift;
  return (undef, "\"$str\" is not a valid port range")
    if (!($str =~ /^(\d+)$sep(\d+)$/));
  my ($start, $end) = ($1, $2);
  my ($success, $err) = isValidPortNumber($start);
  return (undef, $err) if (!defined($success));
  ($success, $err) = isValidPortNumber($end);
  return (undef, $err) if (!defined($success));
  return (undef, "invalid port range ($end is not greater than $start)")
    if ($end <= $start);
  return (1, undef);
}

my %port_name_hash_tcp = ();
my %port_name_hash_udp = ();
sub buildPortNameHash {
  open(IF, "</etc/services") or return 0;
  while (<IF>) {
    s/#.*$//;
    my $is_tcp = /\d\/tcp\s/;
    my @names = grep (!/\//, (split /\s/));
    foreach my $name (@names) {
      if ($is_tcp) {
        $port_name_hash_tcp{$name} = 1;
      } else {
        $port_name_hash_udp{$name} = 1;
      }
    }
  }
  close IF;
  return 1;
}

# $str: string representing a port name
# $proto: protocol to check
# returns ($success, $err)
# $success: 1 if success. otherwise undef
# $err: error message if failure. otherwise undef
sub isValidPortName {
  my $str = shift;
  my $proto = shift;
  return (undef, "\"\" is not a valid port name for protocol \"$proto\"")
    if ($str eq '');
  buildPortNameHash() if ((keys %port_name_hash_tcp) == 0);
  return (1, undef) if ($proto eq 'tcp' && defined($port_name_hash_tcp{$str}));
  return (1, undef) if ($proto eq '6' && defined($port_name_hash_tcp{$str}));
  return (1, undef) if ($proto eq 'udp' && defined($port_name_hash_udp{$str}));
  return (1, undef) if ($proto eq '17' && defined($port_name_hash_udp{$str}));
  return (undef, "\"$str\" is not a valid port name for protocol \"$proto\"");
}

sub getPortRuleString {
  my $port_str = shift;
  my $can_use_port = shift;
  my $prefix = shift;
  my $proto = shift;
  my $negate = '';
  if ($port_str =~ /^!(.*)$/) {
    $port_str = $1;
    $negate = '! ';
  }
  $port_str =~ s/-/:/g;

  my $num_ports = 0;
  my @port_specs = split /,/, $port_str;
  foreach my $port_spec (@port_specs) {
    my ($success, $err) = (undef, undef);
    if ($port_spec =~ /:/) {
      ($success, $err) = isValidPortRange($port_spec, ':');
      if (defined($success)) {
        $num_ports += 2;
        next;
      } else {
        return (undef, $err);
      }
    }
    if ($port_spec =~ /^\d/) {
      ($success, $err) = isValidPortNumber($port_spec);
      if (defined($success)) {
        $num_ports += 1;
        next;
      } else {
        return (undef, $err);
      }
    }
    ($success, $err) = isValidPortName($port_spec, $proto);
    if (defined($success)) {
      $num_ports += 1;
      next;
    } else {
      return (undef, $err);
    }
  }

  my $rule_str = '';
  if (($num_ports > 0) && (!$can_use_port)) {
    return (undef, "ports can only be specified when protocol is \"tcp\" "
                   . "or \"udp\" (currently \"$proto\")");
  }
  if ($num_ports > 15) {
    return (undef, "source/destination port specification only supports "
                   . "up to 15 ports (port range counts as 2)");
  }
  if ($num_ports > 1) {
    $rule_str = " -m multiport --${prefix}ports ${negate}${port_str}";
  } elsif ($num_ports > 0) {
    $rule_str = " --${prefix}port ${negate}${port_str}";
  }

  return ($rule_str, undef);
}

return 1;
