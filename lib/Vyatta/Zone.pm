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
use Vyatta::Interface;

use strict;
use warnings;

my $debug="false";
my $syslog="false";
my $logger = 'sudo logger -t zone.pm -p local0.warn --';

sub run_cmd {
    my $cmd = shift;
    my $error = system("$cmd");

    if ($syslog eq "true") {
        my $func = (caller(1))[3];
        system("$logger [$func] [$cmd] = [$error]");
    }
    if ($debug eq "true") {
        my $func = (caller(1))[3];
        print "[$func] [$cmd] = [$error]\n";
    }
    return $error;
}

sub is_fwruleset_active {
    my ($value_func, $ruleset_type, $fw_ruleset) = @_;
    my $config = new Vyatta::Config;
    return $config->$value_func("firewall $ruleset_type $fw_ruleset");
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

sub get_zone_default_policy {
    my ($value_func, $zone_name) = @_;
    my $config = new Vyatta::Config;
    return $config->$value_func("zone-policy zone $zone_name default-action");
}

sub rule_exists {
    my ($command, $table, $chain_name, $target, $interface) = @_;
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
    my $chain = "VZONE_$zone";
    if (defined(is_local_zone($value_func, $zone))) {
      # local zone
      if (defined $localout) {
        # local zone out chain
        $chain .= "_OUT";
      } else {
        # local zone in chain
        $chain .= "_IN";
      }
    }
    return $chain;
}

sub count_iptables_rules {
    my ($command, $table, $chain) = @_;
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
        # zone defined as a local-zone
        my @zone_intfs_orig = get_zone_interfaces("returnOrigValues", $zone);
        if (scalar(@zone_intfs_orig) != 0) {
          # can't change change transit zone to local-zone on the fly
          $returnstring = "Zone $zone is a transit zone. " .
                "Cannot convert it to local-zone.\n" .
                "Please define another zone to create local-zone";
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
        # make sure you're not converting local-zone to transit zone either
        if (defined(is_local_zone("existsOrig", $zone))) {
          $returnstring = "Cannot convert local-zone $zone to transit zone" .  
				"\nPlease define another zone for it";
          return($returnstring, );
        }
        foreach my $interface (@zone_intfs) {
          # make sure firewall is not applied to this interface
          my $intf = new Vyatta::Interface($interface);
          if ($intf) {
            my $config = new Vyatta::Config;
            $config->setLevel($intf->path());
            if ($config->exists("firewall in name") ||
                $config->exists("firewall out name") ||
                $config->exists("firewall local name")) {
              $returnstring = 
			"interface $interface has firewall configured, " .
			"cannot be defined under a zone";
              return($returnstring, );
            }
          }
          # make sure an interface is not defined under two zones
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
