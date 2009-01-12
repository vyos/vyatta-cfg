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
    printf "path = '%s'\ndevice=%s\n", $intf->path(), $intf->physicalDevice();

    my @addresses = $intf->address();
    if ($#addresses eq -1) {
	print "address is no set\n";
    } else {
	print "address ", join(' ',@addresses), "\n";
    }

    foreach my $attr (qw(exists configured disabled using up running)) {
	my $val = $intf->$attr();
	if (defined $val) {
	    print "\t$attr = $val\n";
	} else {
	    print "\t$attr is not set\n";
	}
    }
}

exit 0;
