#!/usr/bin/perl

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
# Portions created by Vyatta are Copyright (C) 2006, 2007 Vyatta, Inc.
# All Rights Reserved.
# 
# Author: An-Cheng Huang
# Date: 2007
# Description: IP tables address filter
# 
# **** End License ****

package VyattaIpTablesAddressFilter;

use VyattaConfig;
use VyattaMisc;
use VyattaTypeChecker;

my %_protocolswithports = (
  tcp => 1,
  udp => 1,
  6   => 1,
  17  => 1,
);

my %fields = (
  _srcdst	   => undef,
  _range_start     => undef,
  _range_stop      => undef,
  _network         => undef,
  _address         => undef,
  _port            => undef,
  _protocol        => undef,
  _src_mac         => undef,
);

sub new {
  my $that = shift;
  my $class = ref ($that) || $that;
  my $self = {
    %fields,
  };

  bless $self, $class;
  return $self;
}

sub setup {
  my ($self, $level) = @_;
  my $config = new VyattaConfig;

  $config->setLevel("$level");

  # setup needed parent nodes
  $self->{_srcdst}          = $config->returnParent("..");
  $self->{_protocol}        = $config->returnValue(".. protocol");

  # setup address filter nodes
  $self->{_address}         = $config->returnValue("address");
  $self->{_network} = undef;
  $self->{_range_start} = undef;
  $self->{_range_stop} = undef;
  if (defined($self->{_address})) {
    if ($self->{_address} =~ /\//) {
      $self->{_network} = $self->{_address};
      $self->{_address} = undef;
    } elsif ($self->{_address} =~ /^([^-]+)-([^-]+)$/) {
      $self->{_range_start} = $1;
      $self->{_range_stop} = $2;
      $self->{_address} = undef;
    }
  }

  $self->{_port}         = $config->returnValue("port");
  $self->{_src_mac}  = $config->returnValue("mac-address");

  return 0;
}

sub setupOrig {
  my ($self, $level) = @_;
  my $config = new VyattaConfig;

  $config->setLevel("$level");

  # setup needed parent nodes
  $self->{_srcdst}          = $config->returnParent("..");
  $self->{_protocol}        = $config->returnOrigValue(".. protocol");

  # setup address filter nodes
  $self->{_address}         = $config->returnOrigValue("address");
  $self->{_network} = undef;
  $self->{_range_start} = undef;
  $self->{_range_stop} = undef;
  if (defined($self->{_address})) {
    if ($self->{_address} =~ /\//) {
      $self->{_network} = $self->{_address};
      $self->{_address} = undef;
    } elsif ($self->{_address} =~ /^([^-]+)-([^-]+)$/) {
      $self->{_range_start} = $1;
      $self->{_range_stop} = $2;
      $self->{_address} = undef;
    }
  }

  $self->{_port}         = $config->returnOrigValue("port");
  $self->{_src_mac}  = $config->returnValue("mac-address");

  return 0;
}

sub print {
  my ($self) = @_;

  print "srcdst: $self->{_srcdst}\n"           	 	if defined $self->{_srcdst};
  print "range start: $self->{_range_start}\n"          if defined $self->{_range_start};
  print "range stop: $self->{_range_stop}\n"            if defined $self->{_range_stop};
  print "network: $self->{_network}\n"                  if defined $self->{_network};
  print "address: $self->{_address}\n"                  if defined $self->{_address};
  print "port: $self->{_port}\n" if defined $self->{_port};
  print "protocol: $self->{_protocol}\n"		if defined $self->{_protocol};
  print "src-mac: $self->{_src_mac}\n"		if defined $self->{_src_mac};

  return 0;
}

sub rule {
  my ($self) = @_;
  my $rule = "";
  my $can_use_port = 1;
  
  if (!defined($self->{_protocol})
      || !defined($_protocolswithports{$self->{_protocol}})) {
    $can_use_port = 0;
  }

  if (($self->{_srcdst} eq "source") && (defined($self->{_src_mac}))) {
    # handle src mac
    my $str = $self->{_src_mac};
    $str =~ s/^\!(.*)$/! $1/;
    $rule .= "-m mac --mac-source $str ";
  }

  # set the address filter parameters
  if (defined($self->{_network})) {
    my $str = $self->{_network};
    return (undef, "\"$str\" is not a valid IP subnet")
      if (!VyattaTypeChecker::validateType('ipv4net_negate', $str, 1));
    $str =~ s/^\!(.*)$/! $1/;
    $rule .= "--$self->{_srcdst} $str ";
  } elsif (defined($self->{_address})) {
    my $str = $self->{_address};
    return (undef, "\"$str\" is not a valid IP address")
      if (!VyattaTypeChecker::validateType('ipv4_negate', $str, 1));
    $str =~ s/^\!(.*)$/! $1/;
    $rule .= "--$self->{_srcdst} $str ";
  } elsif ((defined $self->{_range_start}) && (defined $self->{_range_stop})) {
    my $start = $self->{_range_start};
    my $stop = $self->{_range_stop};
    return (undef, "\"$start-$stop\" is not a valid IP range")
      if (!VyattaTypeChecker::validateType('ipv4_negate', $start, 1)
          || !VyattaTypeChecker::validateType('ipv4', $stop, 1));
    my $negate = '';
    if ($self->{_range_start} =~ /^!(.*)$/) {
      $start = $1;
      $negate = '! '
    }
    if ("$self->{_srcdst}" eq "source") { 
      $rule .= ("-m iprange $negate--src-range $start-$self->{_range_stop} ");
    }
    elsif ("$self->{_srcdst}" eq "destination") { 
      $rule .= ("-m iprange $negate--dst-range $start-$self->{_range_stop} ");
    }
  }

  my ($port_str, $port_err)
    = VyattaMisc::getPortRuleString($self->{_port}, $can_use_port,
                                    ($self->{_srcdst} eq "source") ? "s" : "d",
                                    $self->{_protocol});
  return (undef, $port_err) if (!defined($port_str));
  $rule .= $port_str;
  return ($rule, undef);
}

sub outputXmlElem {
  my ($name, $value, $fh) = @_;
  print $fh "    <$name>$value</$name>\n";
}

sub outputXml {
  my ($self, $prefix, $fh) = @_;
  outputXmlElem("${prefix}_addr", $self->{_address}, $fh);
  outputXmlElem("${prefix}_net", $self->{_network}, $fh);
  outputXmlElem("${prefix}_addr_start", $self->{_range_start}, $fh);
  outputXmlElem("${prefix}_addr_stop", $self->{_range_stop}, $fh);
  outputXmlElem("${prefix}_port", $self->{_port}, $fh);
}

