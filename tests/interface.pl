#! /usr/bin/perl

# Standalone test for Vyatta::Interface, not intended to be used
# directly


use strict;
use warnings;
use Vyatta::Interface;

foreach my $arg (@ARGV) {
    print "$arg : ";
    my $intf = new Vyatta::Interface($arg);

    if (! $intf) {
	print "undefined\n";
	next;
    }
    
    my $vif = $intf->vif();
    print "vif=$vif " if $vif;
    printf "path = '%s' device=%s\n", $intf->path(), $intf->physicalDevice();

    foreach my $attr (qw(exists configured disabled dhcp address up running)) {
	my $val = $intf->$attr();

	if ($val) {
	    print "\t$attr = $val\n";
	} else {
	    print "\t$attr is not set\n";
	}
    }
}

exit 0;
