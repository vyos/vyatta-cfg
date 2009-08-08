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

package Vyatta::Misc;
use strict;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(getInterfaces getIP getNetAddIP get_sysfs_value
		 is_address_enabled is_dhcp_enabled
		 isIpAddress is_ip_v4_or_v6);
our @EXPORT_OK = qw(generate_dhclient_intf_files 
		    getInterfacesIPadresses
		    getPortRuleString);

use Vyatta::Config;
use Vyatta::Interface;
use NetAddr::IP;

sub get_sysfs_value {
    my ( $intf, $name ) = @_;

    open( my $statf, '<', "/sys/class/net/$intf/$name" )
      or die "Can't open statistics file /sys/class/net/$intf/$name";

    my $value = <$statf>;
    chomp $value if defined $value;
    close $statf;
    return $value;
}

# check if interface is configured to get an IP address using dhcp
sub is_dhcp_enabled {
    my ( $name, $outside_cli ) = @_;
    my $intf = new Vyatta::Interface($name);
    return unless $intf;

    my $config = new Vyatta::Config;
    $config->{_active_dir_base} = "/opt/vyatta/config/active/"
      if ($outside_cli);

    $config->setLevel( $intf->path() );
    foreach my $addr ( $config->returnOrigValues('address') ) {
        return 1 if ( $addr && $addr eq "dhcp" );
    }

    # return undef
}

# check if any non-dhcp addresses configured
sub is_address_enabled {
    my $name = shift;
    my $intf = new Vyatta::Interface($name);
    $intf or return;

    my $config = new Vyatta::Config;
    $config->setLevel( $intf->path() );
    foreach my $addr ( $config->returnOrigValues('address') ) {
        return 1 if ( $addr && $addr ne 'dhcp' );
    }

    # return undefined (ie false)
}

# return dhclient related files for interface
sub generate_dhclient_intf_files {
    my $intf         = shift;
    my $dhclient_dir = '/var/lib/dhcp3/';

    $intf =~ s/\./_/g;
    my $intf_config_file     = $dhclient_dir . 'dhclient_' . $intf . '.conf';
    my $intf_process_id_file = $dhclient_dir . 'dhclient_' . $intf . '.pid';
    my $intf_leases_file     = $dhclient_dir . 'dhclient_' . $intf . '.leases';
    return ( $intf_config_file, $intf_process_id_file, $intf_leases_file );

}

sub getInterfaces {
    opendir( my $sys_class, '/sys/class/net' )
      or die "can't open /sys/class/net: $!";
    my @interfaces =
      grep { ( !/^\./ ) && ( $_ ne 'bonding_masters' ) } readdir $sys_class;
    closedir $sys_class;
    return @interfaces;
}

# get list of IPv4 and IPv6 addresses
# if name is defined then get the addresses on that interface
# if type is defined then restrict to that type (inet, inet6)
sub getIP {
    my ( $name, $type ) = @_;
    my @args = qw(ip addr show);
    my @addresses;

    push @args, ('dev', $name) if $name;

    open my $ipcmd, '-|'
      or exec @args
      or die "ip addr command failed: $!";

    <$ipcmd>;
    while (<$ipcmd>) {
        my ( $proto, $addr ) = split;
        next unless ( $proto =~ /inet/ );
        if ($type) {
            next if ( $proto eq 'inet6' && $type != 6 );
            next if ( $proto eq 'inet'  && $type != 4 );
        }

        push @addresses, $addr;
    }
    close $ipcmd;

    return @addresses;
}

my %type_hash = (
    'broadcast'    => 'is_broadcast',
    'multicast'    => 'is_multicast',
    'pointtopoint' => 'is_pointtopoint',
    'loopback'     => 'is_loopback',
);

# getInterfacesIPadresses() returns IPv4 addresses for the interface type
# possible type of interfaces : 'broadcast', 'pointtopoint', 'multicast', 'all'
# and 'loopback'
sub getInterfacesIPadresses {
    my $type = shift;
    my $type_func;
    my @ips;

    $type or die "Interface type not defined";

    if ( $type ne 'all' ) {
        $type_func = $type_hash{$type};
        die "Invalid type specified to retreive IP addresses for: $type"
          unless $type_func;
    }

    foreach my $name ( getInterfaces() ) {
        my $intf = new Vyatta::Interface($name);
        next unless $intf;
        if ( defined $type_func ) {
            next unless $intf->$type_func();
        }

        my @addresses = $intf->address(4);
        push @ips, @addresses;
    }
    return @ips;
}

sub getNetAddrIP {
    my $name = shift;
    my $intf = new Vyatta::Interface($name);
    $intf or return;

    foreach my $addr ( $intf->addresses() ) {
        my $ip = new NetAddr::IP $addr;
        next unless ( $ip && ip->version() == 4 );
        return $ip;
    }

    # default return of undefined (ie false)
}

sub is_ip_v4_or_v6 {
    my $addr = shift;

    my $ip = new NetAddr::IP $addr;
    return unless defined $ip;

    my $vers = $ip->version();
    if ( $vers == 4 ) {

        #
        # NetAddr::IP will accept short forms 1.1 and hostnames
        # so check if all 4 octets are defined
        return 4 unless ( $addr !~ /\d+\.\d+\.\d+\.\d+/ );    # undef
    }
    elsif ( $vers == 6 ) {
        return 6;
    }

    # default return of undefined (ie false)
}

sub isIpAddress {
    my $ip = shift;

    return unless $ip =~ /(\d+)\.(\d+)\.(\d+)\.(\d+)/;

    return unless ( $1 > 0  && $1 < 256 );
    return unless ( $2 >= 0 && $2 < 256 );
    return unless ( $3 >= 0 && $3 < 256 );
    return unless ( $4 >= 0 && $4 < 256 );
    return 1;
}

sub isClusterIP {
    my ( $vc, $ip ) = @_;

    return unless $ip;    # undef

    my @cluster_groups = $vc->listNodes('cluster group');
    foreach my $cluster_group (@cluster_groups) {
        my @services =
          $vc->returnValues("cluster group $cluster_group service");
        foreach my $service (@services) {
	    if ($service =~ /\//) {
		$service = substr( $service, 0, index( $service, '/' ));
	    }
            if ( $ip eq $service ) {
                return 1;
            }
        }
    }

    return;
}

sub remove_ip_prefix {
    my @addr_nets = @_;

    s/\/\d+$// for @addr_nets;
    return @addr_nets;
}

sub is_ip_in_list {
    my ( $ip, @list ) = @_;

    @list = remove_ip_prefix(@list);
    my %list_hash = map { $_ => 1 } @list;

    return $list_hash{$ip};
}

sub isIPinInterfaces {
    my ( $vc, $ip_addr, @interfaces ) = @_;

    return unless $ip_addr;    # undef == false

    foreach my $name (@interfaces) {
        return 1 if ( is_ip_in_list( $ip_addr, getIP($name) ) );
    }

    # false (undef)
}

sub isClusteringEnabled {
    my ($vc) = @_;

    return $vc->exists('cluster');
}

# $str: string representing a port number
# returns ($success, $err)
# $success: 1 if success. otherwise undef
# $err: error message if failure. otherwise undef
sub isValidPortNumber {
    my $str = shift;
    return ( undef, "\"$str\" is not a valid port number" )
      if ( !( $str =~ /^\d+$/ ) );
    return ( undef, "invalid port \"$str\" (must be between 1 and 65535)" )
      if ( $str < 1 || $str > 65535 );
    return ( 1, undef );
}

# $str: string representing a port range
# $sep: separator for range
# returns ($success, $err)
# $success: 1 if success. otherwise undef
# $err: error message if failure. otherwise undef
sub isValidPortRange {
    my $str = shift;
    my $sep = shift;
    return ( undef, "\"$str\" is not a valid port range" )
      if ( !( $str =~ /^(\d+)$sep(\d+)$/ ) );
    my ( $start, $end ) = ( $1, $2 );
    my ( $success, $err ) = isValidPortNumber($start);
    return ( undef, $err ) if ( !defined($success) );
    ( $success, $err ) = isValidPortNumber($end);
    return ( undef, $err ) if ( !defined($success) );
    return ( undef, "invalid port range ($end is not greater than $start)" )
      if ( $end <= $start );
    return ( 1, undef );
}

# $str: string representing a port name
# $proto: protocol to check
# returns ($success, $err)
# $success: 1 if success. otherwise undef
# $err: error message if failure. otherwise undef
sub isValidPortName {
    my $str   = shift;
    my $proto = shift;
    return ( undef, "\"\" is not a valid port name for protocol \"$proto\"" )
      if ( $str eq '' );

    my $port = getservbyname( $str, $proto );
    return ( 1, undef ) if $port;

    return ( undef,
        "\"$str\" is not a valid port name for protocol \"$proto\"" );
}

sub getPortRuleString {
    my $port_str     = shift;
    my $can_use_port = shift;
    my $prefix       = shift;
    my $proto        = shift;
    my $negate       = '';
    if ( $port_str =~ /^!(.*)$/ ) {
        $port_str = $1;
        $negate   = '! ';
    }
    $port_str =~ s/(\d+)-(\d+)/$1:$2/g;

    my $num_ports = 0;
    my @port_specs = split /,/, $port_str;
    foreach my $port_spec (@port_specs) {
        my ( $success, $err ) = ( undef, undef );
        if ( $port_spec =~ /:/ ) {
            ( $success, $err ) = isValidPortRange( $port_spec, ':' );
            if ( defined($success) ) {
                $num_ports += 2;
                next;
            }
            else {
                return ( undef, $err );
            }
        }
        if ( $port_spec =~ /^\d/ ) {
            ( $success, $err ) = isValidPortNumber($port_spec);
            if ( defined($success) ) {
                $num_ports += 1;
                next;
            }
            else {
                return ( undef, $err );
            }
        }
        if ($proto eq 'tcp_udp') {
          ( $success, $err ) = isValidPortName( $port_spec, 'tcp' );
          if (defined $success) {
            # only do udp test if the tcp test was a success
            ( $success, $err ) = isValidPortName( $port_spec, 'udp' )
          }
        } else {
          ( $success, $err ) = isValidPortName( $port_spec, $proto );
        }
        if ( defined($success) ) {
            $num_ports += 1;
            next;
        }
        else {
            return ( undef, $err );
        }
    }

    my $rule_str = '';
    if ( ( $num_ports > 0 ) && ( !$can_use_port ) ) {
        return ( undef,
                "ports can only be specified when protocol is \"tcp\" "
              . "or \"udp\" (currently \"$proto\")" );
    }
    if ( $num_ports > 15 ) {
        return ( undef,
                "source/destination port specification only supports "
              . "up to 15 ports (port range counts as 2)" );
    }
    if ( $num_ports > 1 ) {
        $rule_str = " -m multiport --${prefix}ports ${negate}${port_str}";
    }
    elsif ( $num_ports > 0 ) {
        $rule_str = " --${prefix}port ${negate}${port_str}";
    }

    return ( $rule_str, undef );
}

1;
