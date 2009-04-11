# Module: Zone.pm
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
# Author: Mohit Mehta
# Date: 2009
# Description: vyatta zone management
#
# **** End License ****
#

package Vyatta::Zone;

use Vyatta::Config;
use Vyatta::Misc;

use strict;
use warnings;

my $debug="false";
my $logger = 'sudo logger -t zone.pm -p local0.warn --';

sub run_cmd {
    my $cmd = shift;

    my $error = system("$cmd");
    if ($debug eq "true") {
        my $func = (caller(1))[3];
        system("$logger [$func] [$cmd] = [$error]");
    }
    return $error;
}

sub get_all_zones {
    my $value_func = shift;
    my $config = new Vyatta::Config;
    return $config->$value_func("zone-policy zone");
}

sub get_zone_interfaces {
    my ($value_func, $zone_name) = @_;
    my $config = new Vyatta::Config;
    return $config->$value_func("zone-policy zone $zone_name interface");
}

sub get_from_zones {
    my ($value_func, $zone_name) = @_;
    my $config = new Vyatta::Config;
    return $config->$value_func("zone-policy zone $zone_name from");
}

sub get_firewall_ruleset {
    my ($value_func, $zone_name, $from_zone, $firewall_type) = @_;
    my $config = new Vyatta::Config;
    return $config->$value_func("zone-policy zone $zone_name from $from_zone
        firewall $firewall_type");
}

sub is_local_zone {
    my ($value_func, $zone_name) = @_;
    my $config = new Vyatta::Config;
    return $config->$value_func("zone-policy zone $zone_name local-zone");
}

sub rule_exists {
    my ($command, $table, $tree, $chain_name, $target, $interface) = @_;
    my $cmd =
        "sudo $command -t $table -L " .
        "$chain_name -v 2>/dev/null | grep \" $target \" ";
    if (defined $interface) {
      $cmd .= "| grep \" $interface \" ";
    }
    $cmd .= "| wc -l";
    my $result = `$cmd`;
    return $result;
}

sub get_zone_chain {
    my ($value_func, $zone, $localout) = @_;
    my $chain = "zone-$zone";
    if (defined(is_local_zone($value_func, $zone))) {
      # local zone
      if (defined $localout) {
        # local zone out chain
        $chain .= "-out";
      } else {
        # local zone in chain
        $chain .= "-in";
      }
    }
    return $chain;
}

sub count_iptables_rules {
    my ($command, $table,$type, $chain) = @_;
    my @lines = `sudo $command -t $table -L $chain -n --line`;
    my $cnt = 0;
    foreach my $line (@lines) {
      $cnt++ if $line =~ /^\d/;
    }
    return $cnt;
}

sub validity_checks {
    my @all_zones = get_all_zones("listNodes");
    my @all_interfaces = ();
    my $num_local_zones = 0;
    my $returnstring;
    foreach my $zone (@all_zones) {
      # get all from zones, see if they exist in config, if not display error
      my @from_zones = get_from_zones("listNodes", $zone);
      foreach my $from_zone (@from_zones) {
        if (scalar(grep(/^$from_zone$/, @all_zones)) == 0) {
          $returnstring = "$from_zone is a from zone under zone $zone\n" . 
		"It is either not defined or deleted from config";
          return ($returnstring, );
        }
      }
      my @zone_intfs = get_zone_interfaces("returnValues", $zone);
      if (scalar(@zone_intfs) == 0) {
        # no interfaces defined for this zone
        if (!defined(is_local_zone("exists", $zone))) {
          $returnstring = "Zone $zone has no interfaces defined " .  
				"and it's not a local-zone";
          return($returnstring, );
        }
        $num_local_zones++;
        # make sure only one zone is a local-zone
        if ($num_local_zones > 1) {
          return ("Only one zone can be defined as a local-zone", );
        }
      } else {
        # zone has interfaces, make sure it is not set as a local-zone
        if (defined(is_local_zone("exists", $zone))) {
          $returnstring = "local-zone cannot have interfaces defined";
          return($returnstring, );
        }
        # make sure an interface is not defined under two zones
        foreach my $interface (@zone_intfs) {
          if (scalar(grep(/^$interface$/, @all_interfaces)) > 0) {
            return ("$interface defined under two zones", );
          } else {
            push(@all_interfaces, $interface);
          }
        }
      }
    }
    return;
}

1;
