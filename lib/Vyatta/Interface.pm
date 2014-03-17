# Author: Stephen Hemminger <shemminger@vyatta.com>
# Date: 2009
# Description: vyatta interface management

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
# Portions created by Vyatta are Copyright (C) 2008 Vyatta, Inc.
# All Rights Reserved.
# **** End License ****

package Vyatta::Interface;

use strict;
use warnings;

use Vyatta::Misc;
use Vyatta::ioctl;
use Vyatta::Config;
use base 'Exporter';

our @EXPORT = qw(IFF_UP IFF_BROADCAST IFF_DEBUG IFF_LOOPBACK
	          IFF_POINTOPOINT IFF_RUNNING IFF_NOARP
		  IFF_PROMISC IFF_MULTICAST);

use constant {
    IFF_UP		=> 0x1,		# interface is up
    IFF_BROADCAST	=> 0x2,		# broadcast address valid
    IFF_DEBUG		=> 0x4,		# turn on debugging
    IFF_LOOPBACK	=> 0x8,		# is a loopback net
    IFF_POINTOPOINT	=> 0x10,	# interface is has p-p link
    IFF_NOTRAILERS	=> 0x20,	# avoid use of trailers
    IFF_RUNNING		=> 0x40,	# interface RFC2863 OPER_UP
    IFF_NOARP		=> 0x80,	# no ARP protocol
    IFF_PROMISC		=> 0x100,	# receive all packets
    IFF_ALLMULTI	=> 0x200,	# receive all multicast packets
    IFF_MASTER		=> 0x400,	# master of a load balancer
    IFF_SLAVE		=> 0x800,	# slave of a load balancer
    IFF_MULTICAST	=> 0x1000,	# Supports multicast
    IFF_PORTSEL		=> 0x2000,      # can set media type
    IFF_AUTOMEDIA	=> 0x4000,	# auto media select active
    IFF_DYNAMIC		=> 0x8000,	# dialup device with changing addresses
    IFF_LOWER_UP	=> 0x10000,	# driver signals L1 up
    IFF_DORMANT		=> 0x20000,	# driver signals dormant
    IFF_ECHO		=> 0x40000,	# echo sent packets
};

# Build list of known interface types
my $NETDEV    = '/opt/vyatta/etc/netdevice';

# Hash of interface types
# ex: $net_prefix{"eth"} = "ethernet"
my %net_prefix;

sub parse_netdev_file {
    my $filename = shift;

    open (my $in, '<', $filename)
	or return;

    while (<$in>) {
	chomp;

	# remove text after # as comment
	s/#.*$//;

	my ($prefix, $type) = split;

	# ignore blank lines or missing patterns
	next unless defined($prefix) && defined($type);

	$net_prefix{$prefix} = $type;
    }
    close $in;
}

# read /opt/vyatta/etc/netdevice
parse_netdev_file($NETDEV);

# look for optional package interfaces in /opt/vyatta/etc/netdevice.d
my $dirname = $NETDEV . '.d';
if (opendir(my $netd, $dirname)) {
    foreach my $pkg (sort readdir $netd) {
	parse_netdev_file($dirname . '/' . $pkg);
    }
    closedir $netd;
}


# get list of interface types (only used in usage function)
sub interface_types {
    return values %net_prefix;
}

# new interface description object
sub new {
    my $that  = shift;
    my $name   = pop;
    my $class = ref($that) || $that;

    my ($vif, $vrid);
    my $dev = $name;

    # remove VRRP id suffix
    if ($dev =~ /^(.*)v(\d+)$/) {
	$dev = $1;
	$vrid = $2;
    }

    # remove VLAN suffix
    if ($dev =~ /^(.*)\.(\d+)/) {
	$dev = $1;
	$vif = $2;
    }

    return unless ($dev =~ /^(l2tpeth|[a-z]+)/);

    # convert from prefix 'eth' to type 'ethernet'
    my $type = $net_prefix{$1};
    return unless $type;	# unknown network interface type

    my $self = {
	name => $name,
	type => $type,
	dev  => $dev,
	vif  => $vif,
	vrid => $vrid,
    };
    bless $self, $class;
    return $self;
}

## Field accessors
sub name {
    my $self = shift;
    return $self->{name};
}

sub path {
    my $self = shift;
    my $config = new Vyatta::Config;

    if ($self->{name} =~ /^(pppo[ae])(\d+)/) {
	# For ppp need to look in config file to find where used
	my $type = $1;
	my $id = $2;

	my $intf = _ppp_intf($self->{name});
	return unless $intf;

	if ($type eq 'pppoe') {
	    return "interfaces ethernet $intf pppoe $id";
	}

	my $adsl = "interfaces adsl $intf pvc";
	my $config = new Vyatta::Config;
	foreach my $pvc ($config->listNodes($adsl)) {
	    my $path = "$adsl $pvc $type $id";
	    return $path if $config->exists($path);
	}
    }
    elsif ($self->{name} =~ /^(wan\d+)\.(\d+)/) {
	# guesswork for wan devices
	my $dev = $1;
	my $vif = $2;
	foreach my $type (qw(cisco-hdlc ppp frame-relay)) {
	    my $path = "interfaces serial $dev $type vif $vif";
	    return $path if $config->exists($path);
	}
    }
    else {
	# normal device
	my $path = "interfaces $self->{type} $self->{dev}";
	$path .= " vrrp $self->{vrid}" if $self->{vrid};
	$path .= " vif $self->{vif}" if $self->{vif};

	return $path;
    }

    return;     # undefined (not in config)
}

sub type {
    my $self = shift;
    return $self->{type};
}

sub vif {
    my $self = shift;
    return $self->{vif};
}

sub vrid {
    my $self = shift;
    return $self->{vrid};
}

sub physicalDevice {
    my $self = shift;
    return $self->{dev};
}

# Read ppp config to fine associated interface for ppp device
sub _ppp_intf {
    my $dev = shift;
    my $intf;

    open (my $ppp, '<', "/etc/ppp/peers/$dev")
	or return;	# no such device

    while (<$ppp>) {
	chomp;
	# looking for line like:
	# pty "/usr/sbin/pppoe -m 1412 -I eth1"
	next unless /^pty\s.*-I\s*(\w+)"/;
	$intf = $1;
	last;
    }
    close $ppp;

    return $intf;
}

## Configuration checks

sub configured {
    my $self   = shift;
    my $config = new Vyatta::Config;

    return $config->exists( $self->{path} );
}

sub disabled {
    my $self   = shift;
    my $config = new Vyatta::Config;

    $config->setLevel( $self->{path} );
    return $config->exists("disable");
}

sub mtu {
    my $self  = shift;
    my $config = new Vyatta::Config;

    $config->setLevel( $self->{path} );
    return $config->returnValue("mtu");
}

sub using_dhcp {
    my $self   = shift;
    my $config = new Vyatta::Config;
    $config->setLevel( $self->{path} );

    my @addr = grep { $_ eq 'dhcp' } $config->returnOrigValues('address');

    return if ($#addr < 0);
    return $addr[0];
}

sub bridge_grp {
    my $self  = shift;
    my $config = new Vyatta::Config;

    $config->setLevel( $self->{path} );
    return $config->returnValue("bridge-group bridge");
}

## System checks

# return array of current addresses (on system)
sub address {
    my ($self, $type) = @_;

    return Vyatta::Misc::getIP($self->{name}, $type);
}

# Do SIOCGIFFLAGS ioctl in perl
sub flags {
  my $self = shift;
  return Vyatta::ioctl::get_interface_flags($self->{name});
}

sub exists {
    my $self = shift;
    my $flags = $self->flags();
    return defined($flags);
}

sub hw_address {
    my $self = shift;

    open my $addrf, '<', "/sys/class/net/$self->{name}/address"
	or return;
    my $address = <$addrf>;
    close $addrf;

    chomp $address if $address;
    return $address;
}

sub is_broadcast {
    my $self = shift;
    return $self->flags() & IFF_BROADCAST;
}

sub is_multicast {
    my $self = shift;
    return $self->flags() & IFF_MULTICAST;
}

sub is_pointtopoint {
    my $self = shift;
    return $self->flags() & IFF_POINTOPOINT;
}

sub is_loopback {
    my $self = shift;
    return $self->flags() & IFF_LOOPBACK;
}

# device exists and is online
sub up {
    my $self  = shift;
    my $flags = $self->flags();

    return defined($flags) && ( $flags & IFF_UP );
}

# device exists and is running (ie carrier present)
sub running {
    my $self  = shift;
    my $flags = $self->flags();

    return defined($flags) && ( $flags & IFF_RUNNING );
}

# device description information in kernel (future use)
sub description {
    my $self = shift;

    return interface_description($self->{name});
}

## Utility functions

# enumerate vrrp slave devices
sub get_vrrp_interfaces {
    my ($cfg, $vfunc, $dev, $path) = @_;
    my @ret_ifs;

    foreach my $vrid ($cfg->$vfunc("$path vrrp vrrp-group")) {
	my $vrdev = $dev."v".$vrid;
	my $vrpath = "$path vrrp vrrp-group $vrid interface";

	push @ret_ifs, { name => $vrdev,
			 type => 'vrrp',
			 path => $vrpath,
	};
    }

    return @ret_ifs;
}

# enumerate vif devies
sub get_vif_interfaces {
    my ($cfg, $vfunc, $dev, $type, $path) = @_;
    my @ret_ifs;

    foreach my $vnum ($cfg->$vfunc("$path vif")) {
	my $vifdev = "$dev.$vnum";
	my $vifpath = "$path vif $vnum";
	push @ret_ifs, { name => $vifdev,
			 type => $type,
			 path => $vifpath };
	push @ret_ifs, get_vrrp_interfaces($cfg, $vfunc, $vifdev, $vifpath);
    }

    return @ret_ifs;
}

sub get_pppoe_interfaces {
    my ($cfg, $vfunc, $dev, $path) = @_;
    my @ret_ifs;

    foreach my $ep ($cfg->$vfunc("$path pppoe")) {
	my $pppdev = "pppoe$ep";
	my $ppppath = "$path pppoe $ep";

	push @ret_ifs, { name => $pppdev,
			 type => 'pppoe',
			 path => $ppppath };
    }

    return @ret_ifs;
}

# special cases for adsl
sub get_adsl_interfaces {
    my ($cfg, $vfunc) = @_;
    my @ret_ifs;

    for my $p ($cfg->$vfunc("interfaces adsl $a $a pvc")) {
	for my $t ($cfg->$vfunc("interfaces adsl $a $a pvc $p")) {
	    if ($t eq 'classical-ipoa' or $t eq 'bridged-ethernet') {
		# classical-ipoa or bridged-ethernet
		push @ret_ifs, { name => $a,
				 type => 'adsl',
				 path => "interfaces adsl $a $a pvc $p $t" };
		next;
	    }

	    # pppo[ea]
	    for my $i ($cfg->$vfunc("interfaces adsl $a $a pvc $p $t")) {
		push @ret_ifs, { name => "$t$i",
				 type => 'adsl-pppo[ea]',
				 path => "interfaces adsl $a $a pvc $p $t $i" };
	    }
	}
    }
    return @ret_ifs;
}

# get all configured interfaces from configuration
# parameter is virtual function (see Config.pm)
#
# return a hash of:
#   name => ethX
#   type => "ethernet"
#   path => "interfaces ethernet ethX"
#
# Don't use this function directly, use wrappers below instead
sub get_config_interfaces {
    my $vfunc = shift;
    my $cfg = new Vyatta::Config;
    my @ret_ifs;

    foreach my $type ($cfg->$vfunc("interfaces")) {
	if ($type eq 'adsl') {
	    push @ret_ifs, get_adsl_interfaces($cfg, $vfunc);
	    next;
	}

	foreach my $dev ($cfg->$vfunc("interfaces $type")) {
	    my $path = "interfaces $type $dev";

	    push @ret_ifs, { name => $dev,
			     type => $type,
			     path => $path };
	    push @ret_ifs, get_vrrp_interfaces($cfg, $vfunc, $dev, $path);
	    push @ret_ifs, get_vif_interfaces($cfg, $vfunc, $dev, $type, $path);

	    push @ret_ifs, get_pppoe_interfaces($cfg, $vfunc, $dev, $path)
		if ($type eq 'ethernet');
	}

    }

    return @ret_ifs;
}

# get array of hash for interfaces in working config
sub get_interfaces {
    return get_config_interfaces('listNodes');
}

# get array of hash for interfaces in configuration
# when used outside of config mode.
sub get_effective_interfaces {
    return get_config_interfaces('listEffectiveNodes');
}

# get array of hash for interfaces in original config
# only makes sense in configuration mode
sub get_original_interfaces {
    return get_config_interfaces('listOrigNodes');
}

# get map of current addresses on the system
# returns reference to hash of form:
#   ( "192.168.1.1" => { 'eth0', 'eth2' } )
sub get_cfg_addresses {
    my $config = new Vyatta::Config;
    my @cfgifs = get_interfaces();
    my %ahash;

    foreach my $intf ( @cfgifs ) {
	my $name = $intf->{'name'};

	# workaround openvpn wart
	my @addrs;
	$config->setLevel($intf->{'path'});
	if ($name =~ /^vtun/) {
	    @addrs = $config->listNodes('local-address');
	} else {
	    @addrs = $config->returnValues('address');
	}

	foreach my $addr ( @addrs ){
	    next if ($addr =~ /^dhcp/);

	    # put interface into
	    my $aif = $ahash{$addr};
	    if ($aif) {
		push @{$aif}, $name;
	    } else {
		$ahash{$addr} = [ $name ];
	    }
	}
    }

    return \%ahash;
}

1;
