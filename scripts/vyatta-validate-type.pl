#!/usr/bin/perl

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaTypeChecker;

# validate a value of a specific type
if ($#ARGV != 1) {
  print "usage: vyatta-validate-type.pl <type> <value>\n";
  exit 1;
}

exit 0 if (VyattaTypeChecker::validateType($ARGV[0], $ARGV[1]));
exit 1;

