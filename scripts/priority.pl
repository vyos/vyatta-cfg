#!/usr/bin/perl

my %pri;

    # first check if this file exists, and if so ensure this is a config file.
my @files = `find /opt/vyatta/share/vyatta-cfg -name 'node.def'`;
foreach my $f (@files) {
    my $result = `grep 'priority:' $f`;
   if (defined $result && length($result) != 0) {
       my @r = split " ", $result;
       if (defined $r[1]) {
	   #stuff in hash here
	   push @{$pri{$r[1]}},$f;
       }
    }
}


#now sort

foreach my $key ( sort { $a <=> $b } keys %pri ) {
    my @a = @{$pri{$key}};
    foreach my $val (@a) {
	my $loc = substr($val,0,-10);
	my $loc = substr($loc,39);
	print $key," ",$loc,"\n";
    }
} 
