#!/usr/bin/perl

# Read all the configuration templates in the configuration
# template directory and produce an ordered list of the priority
# of configuration actions

use strict;
use warnings;
use File::Find;

my %pri;

sub get_priority {
    open( my $f, '<', $_ )
      or return;
    my $priority;

    while (<$f>) {
        chomp;
        next unless m/^priority:\s(\d+)/;
        $priority = $1;
        last;
    }
    close $f;
    return $priority;
}

sub wanted {
    return unless ( $_ eq 'node.def' );

    my $p = get_priority($File::Find::name);
    return unless $p;

    my $dir = $File::Find::dir;
    $dir =~ s/^.*\/templates\///;

    push @{ $pri{$p} }, $dir;
    return 1;
}

my $cfgdir = '/opt/vyatta/share/vyatta-cfg/templates';
die "$cfgdir does not exist!" unless -d $cfgdir;
find( \&wanted, $cfgdir );

foreach my $key ( sort { $a <=> $b } keys %pri ) {
    my @a = @{ $pri{$key} };
    foreach my $val (@a) {
        print $key, " ", $val, "\n";
    }
}
