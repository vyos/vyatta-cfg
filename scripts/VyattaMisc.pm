#!/usr/bin/perl

# Module: VyattaMisc.pm
#
# Author: Marat <marat@vyatta.com>
# Date: 2007
# Description: Implements miscellaneous commands

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
# Portions created by Vyatta are Copyright (C) 2006, 2007, 2008 Vyatta, Inc.
# All Rights Reserved.
# **** End License ****

package VyattaMisc;
require Exporter;
@ISA	= qw(Exporter);
@EXPORT	= qw(getNetAddIP isIpAddress is_ip_v4_or_v6);
@EXPORT_OK = qw(getNetAddIP isIpAddress is_ip_v4_or_v6);


use strict;

use VyattaConfig;

# check if interace is configured to get an IP address using dhcp
sub is_dhcp_enabled {
    my ($intf, $outside_cli) = @_;

    my $config = new VyattaConfig;
    if (!($outside_cli eq '')) {
     $config->{_active_dir_base} = "/opt/vyatta/config/active/";
    }

    if ($intf =~ m/^eth/) {
        if ($intf =~ m/(\w+)\.(\d+)/) {
            $config->setLevel("interfaces ethernet $1 vif $2");
        } else {
            $config->setLevel("interfaces ethernet $intf");
        }
    } elsif ($intf =~ m/^br/) {
        $config->setLevel("interfaces bridge $intf");
    } elsif ($intf =~ m/^bond/) {
        if ($intf =~ m/(\w+)\.(\d+)/) {
            $config->setLevel("interfaces bonding $1 vif $2");
        } else {
            $config->setLevel("interfaces bonding $intf");
        }
    } else {
        #
        # add other interfaces that can be configured to use dhcp above
        #
        return 0;
    }
    my @addrs = $config->returnOrigValues("address");
    foreach my $addr (@addrs) {
        if (defined $addr && $addr eq "dhcp") {
            return 1;
        }
    }
    return 0;
}

# return dhclient related files for interface
sub generate_dhclient_intf_files {
    my $intf = shift;
    my $dhclient_dir = '/var/lib/dhcp3/';

    $intf =~ s/\./_/g;
    my $intf_config_file = $dhclient_dir . 'dhclient_' . $intf . '.conf';
    my $intf_process_id_file = $dhclient_dir . 'dhclient_' . $intf . '.pid';
    my $intf_leases_file = $dhclient_dir . 'dhclient_' . $intf . '.leases';
    return ($intf_config_file, $intf_process_id_file, $intf_leases_file);

}

# getInterfacesIPadresses() returns IP addresses for the interface type passed to it
# possible type of interfaces : 'broadcast', 'pointopoint', 'multicast', 'all'
# the loopback IP address is never returned with any of the above parameters
sub getInterfacesIPadresses {

    my $interface_type = shift;
    if (!($interface_type =~ m/broadcast|pointopoint|multicast|all/)) {
        print STDERR "Invalid interface type specified to retrive IP addresses for\n";
        return undef;
    }
    my @interfaces_on_system = `ifconfig -a | awk '\$2 ~ /Link/ {print \$1}'`;
    chomp @interfaces_on_system;
    my @intf_ips = ();
    my $intf_ips_index = 0;
    foreach my $intf_system (@interfaces_on_system) {
     if (!($intf_system eq 'lo')) {
      my $is_intf_interface_type = 0;
       if (!($interface_type eq 'all')) {
       $is_intf_interface_type =
       `ip link show $intf_system 2>/dev/null | grep -i $interface_type | wc -l`;
       } else {
         $is_intf_interface_type = 1;
       }
       if ($is_intf_interface_type > 0) {
        $intf_ips[$intf_ips_index] =
        `ip addr show $intf_system 2>/dev/null | grep inet | grep -v inet6 | awk '{print \$2}'`;
        if (!($intf_ips[$intf_ips_index] eq '')){
         $intf_ips_index++;
        }
       }
     }
    }
    chomp @intf_ips;
    return (@intf_ips);

}


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

sub is_ip_v4_or_v6 {
    my $addr = shift;

    my $ip = NetAddr::IP->new($addr);
    if (defined $ip && $ip->version() == 4) {
	#
	# the call to IP->new() will accept 1.1 and consider
        # it to be 1.1.0.0, so add a check to force all
	# 4 octets to be defined
        #
	if ($addr !~ /\d+\.\d+\.\d+\.\d+/) {
	    return undef;
	}
	return 4;
    }
    $ip = NetAddr::IP->new6($addr);
    if (defined $ip && $ip->version() == 6) {
	return 6;
    }
    
    return undef;
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
