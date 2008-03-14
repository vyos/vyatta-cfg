#!/usr/bin/perl
use lib "/opt/vyatta/share/perl5/";
use VyattaConfig;
use VyattaMisc;
use Getopt::Long;

## Check if a typeless node exists
# this is a lame little script to get around bug 2525 not being fixed.
# i.e. $VAR(./node/) always expands to true.  Once bug 2525 is properly
# fixed, this can go away
my $node = shift;
my $config = new VyattaConfig;

if ($config->exists("$node")) {
  exit 0;
}
else {
  exit 1;
}

exit 0;
