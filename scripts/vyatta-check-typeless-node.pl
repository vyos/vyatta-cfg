#!/usr/bin/perl
use strict;
use lib "/opt/vyatta/share/perl5/";
use Vyatta::Config;

## Check if a typeless node exists
# this is a lame little script to get around bug 2525 not being fixed.
# i.e. $VAR(./node/) always expands to true.  Once bug 2525 is properly
# fixed, this can go away
my $node = shift;
my $config = new Vyatta::Config;

exit 0 if ($config->exists("$node"));
exit 1;
