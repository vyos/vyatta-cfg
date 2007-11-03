#!/usr/bin/perl

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigOutput;

if ($ARGV[0] eq '-all') {
  shift;
  VyattaConfigOutput::set_show_all(1);
}
VyattaConfigOutput::outputNewConfig(@ARGV);
exit 0;

