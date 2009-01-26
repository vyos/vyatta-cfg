#!/usr/bin/perl

# Author: An-Cheng Huang <ancheng@vyatta.com>
# Date: 2007
# Description: Type checking script

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

# Perl module for type validation.
# Usage 1: validate a value of a specific type.
#   use Vyatta::TypeChecker;
#   ...
#   if (validateType('ipv4', '1.1.1.1')) {
#     # valid
#     ...
#   } else {
#     # not valie
#     ...
#   }
#
# Usage 2: find the type of a value (from a list of candidates), returns
# undef if the value is not valid for any of the candidates.
#   $valtype = findType('1.1.1.1', 'ipv4', 'ipv6');
#   if (!defined($valtype)) {
#     # neither ipv4 nor ipv6
#     ...
#   } else {
#     if ($valtype eq 'ipv4') {
#       ...
#     } else {
#       ...
#     }
#   }

package Vyatta::TypeChecker;
use strict;

our @EXPORT = qw(findType validateType);
use base qw(Exporter);

my %type_handler = (
                    'ipv4' => \&validate_ipv4,
                    'ipv4net' => \&validate_ipv4net,
                    'ipv4range' => \&validate_ipv4range,
                    'ipv4_negate' => \&validate_ipv4_negate,
                    'ipv4net_negate' => \&validate_ipv4net_negate,
                    'ipv4range_negate' => \&validate_ipv4range_negate,
                    'iptables4_addr' => \&validate_iptables4_addr,
                    'protocol' => \&validate_protocol,
                    'protocol_negate' => \&validate_protocol_negate,
                    'macaddr' => \&validate_macaddr,
                    'macaddr_negate' => \&validate_macaddr_negate,
                    'ipv6' => \&validate_ipv6,
		    'ipv6_negate' => \&validate_ipv6_negate,
		    'ipv6net' => \&validate_ipv6net,
		    'ipv6net_negate' => \&validate_ipv6net_negate,
                   );

sub validate_ipv4 {
  $_ = shift;
  return 0 if (!/^(\d+)\.(\d+)\.(\d+)\.(\d+)$/);
  return 0 if ($1 > 255 || $2 > 255 || $3 > 255 || $4 > 255);
  return 1;
}

sub validate_ipv4net {
  $_ = shift;
  return 0 if (!/^(\d+)\.(\d+)\.(\d+)\.(\d+)\/(\d+)$/);
  return 0 if ($1 > 255 || $2 > 255 || $3 > 255 || $4 > 255 || $5 > 32);
  return 1;
}

sub validate_ipv4range {
  $_ = shift;
  return 0 if (!/^([^-]+)-([^-]+)$/);
  my ($a1, $a2) = ($1, $2);
  return 0 if (!validate_ipv4($a1) || !validate_ipv4($a2));
  return 1;
}

sub validate_ipv4_negate {
  my $value = shift;
  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_ipv4($value);
}

sub validate_ipv4net_negate {
  my $value = shift;
  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_ipv4net($value);
}

sub validate_ipv4range_negate {
  my $value = shift;
  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_ipv4range($value);
}

sub validate_iptables4_addr {
  my $value = shift;
  return 0 if (!validate_ipv4_negate($value)
               && !validate_ipv4net_negate($value)
               && !validate_ipv4range_negate($value));
  return 1;
}

sub validate_protocol {
  my $value = shift;
  $value = lc $value;
  return 1 if ($value eq 'all');

  if ($value =~ /^\d+$/) {
      # 0 has special meaning to iptables
      return 1 if $value >= 1 and $value <= 255;
  }

  return defined getprotobyname($value);
}

sub validate_protocol_negate {
  my $value = shift;
  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_protocol($value);
}

sub validate_macaddr {
  my $value = shift;
  $value = lc $value;
  my $byte = '[0-9a-f]{2}';
  return 1 if ($value =~ /^$byte(:$byte){5}$/);
}

sub validate_macaddr_negate {
  my $value = shift;
  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_macaddr($value);
}

# IPv6 syntax definition
my $RE_IPV4_BYTE = '((25[0-5])|(2[0-4][0-9])|([01][0-9][0-9])|([0-9]{1,2}))';
my $RE_IPV4 = "$RE_IPV4_BYTE(\.$RE_IPV4_BYTE){3}";
my $RE_H16 = '([a-fA-F0-9]{1,4})';
my $RE_H16_COLON = "($RE_H16:)";
my $RE_LS32 = "(($RE_H16:$RE_H16)|($RE_IPV4))";
my $RE_IPV6_P1 = "($RE_H16_COLON)\{6\}$RE_LS32";
my $RE_IPV6_P2 = "::($RE_H16_COLON)\{5\}$RE_LS32";
my $RE_IPV6_P3 = "($RE_H16)?::($RE_H16_COLON)\{4\}$RE_LS32";
my $RE_IPV6_P4 = "(($RE_H16_COLON)\{0,1\}$RE_H16)?"
                 . "::($RE_H16_COLON)\{3\}$RE_LS32";
my $RE_IPV6_P5 = "(($RE_H16_COLON)\{0,2\}$RE_H16)?"
                 . "::($RE_H16_COLON)\{2\}$RE_LS32";
my $RE_IPV6_P6 = "(($RE_H16_COLON)\{0,3\}$RE_H16)?"
                 . "::($RE_H16_COLON)\{1\}$RE_LS32";
my $RE_IPV6_P7 = "(($RE_H16_COLON)\{0,4\}$RE_H16)?::$RE_LS32";
my $RE_IPV6_P8 = "(($RE_H16_COLON)\{0,5\}$RE_H16)?::$RE_H16";
my $RE_IPV6_P9 = "(($RE_H16_COLON)\{0,6\}$RE_H16)?::";
my $RE_IPV6 = "($RE_IPV6_P1)|($RE_IPV6_P2)|($RE_IPV6_P3)|($RE_IPV6_P4)"
               . "|($RE_IPV6_P5)|($RE_IPV6_P6)|($RE_IPV6_P7)|($RE_IPV6_P8)"
               . "|($RE_IPV6_P9)";

sub validate_ipv6 {
  $_ = shift;
  return 0 if (!/^$RE_IPV6$/);
  return 1;
}

sub validate_ipv6_negate {
  my $value = shift;
  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_ipv6($value);
}

sub validate_ipv6net {
  my $value = shift;
  
  if ($value =~ m/^(.*)\/(.*)$/) {
    my $ipv6_addr = $1;
    my $prefix_length = $2;
    if ($prefix_length < 0 || $prefix_length > 128) {
      print "Invalid prefix length: $prefix_length\n";
      return 0;
    }
    return validate_ipv6($ipv6_addr);
    
  } else {
    print "\"$value\" is not a valid IPv6 prefix\n";
    return 0;
  }
}

sub validate_ipv6net_negate {
  my $value = shift;

  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_ipv6net($value);
}

sub validateType {
  my ($type, $value, $quiet) = @_;
  if (!defined($type) || !defined($value)) {
    return 0;
  }
  if (!defined($type_handler{$type})) {
    print "type \"$type\" not defined\n" if (!defined($quiet));
    return 0;
  }
  if (!&{$type_handler{$type}}($value)) {
    print "\"$value\" is not a valid value of type \"$type\"\n"
      if (!defined($quiet));
    return 0;
  }

  return 1;
}

sub findType {
  my ($value, @candidates) = @_;
  return if (!defined($value) || ((scalar @candidates) < 1)); # undef

  foreach my $type (@candidates) {
    if (!defined($type_handler{$type})) {
      next;
    }
    if (&{$type_handler{$type}}($value)) {
      # the first valid type is returned
      return $type;
    }
  }
}

1;

# Local Variables:
# mode: perl
# indent-tabs-mode: nil
# perl-indent-level: 2
# End:
