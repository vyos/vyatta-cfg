#!/usr/bin/perl

# Read all the configuration templates in the configuration
# template directory and produce an ordered list of the priority
# of configuration actions

use strict;
use warnings;
use File::Find;

my %priorities;

# Open node file and extract priority and comment if any
sub get_priority {
    open( my $f, '<', $_ )
      or return;
    my $priority;
    my $comment;

    while (<$f>) {
        chomp;
        next unless m/^priority:\s(\d+)/;
        $priority = $1;

        $comment = $1 if (/#(.*)$/);

        last;
    }
    close $f;
    return ( $priority, $comment );
}

# Called by find and returns true iff
#  file is named node.def
#  file contains priority tag
# Side effect: tores resulting line in $priorities hash for display
sub wanted {
    return unless ( $_ eq 'node.def' );

    my ( $priority, $comment ) = get_priority($File::Find::name);
    return unless $priority;

    my $dir = $File::Find::dir;
    $dir =~ s/^.*\/templates\///;

    $dir .= " #" . $comment
      if $comment;

    # append line to list of entries with same priority
    push @{ $priorities{$priority} }, $dir;
    return 1;
}

# main program
my $cfgdir = '/opt/vyatta/share/vyatta-cfg/templates';
die "$cfgdir does not exist!" unless -d $cfgdir;

# walk config file tree
find( \&wanted, $cfgdir );

# display resulting priorities
foreach my $key ( sort { $a <=> $b } keys %priorities ) {
    my @a = @{ $priorities{$key} };
    foreach my $val ( sort @a ) {
        print $key, " ", $val, "\n";
    }
}
