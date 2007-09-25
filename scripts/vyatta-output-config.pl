#!/usr/bin/perl

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigOutput;

VyattaConfigOutput::outputNewConfig(@ARGV);
exit 0;

