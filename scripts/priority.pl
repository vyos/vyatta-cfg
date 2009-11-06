#!/usr/bin/perl

my %pri;

# Look at all node.def files in the configuration template tree
my @files = `find /opt/vyatta/share/vyatta-cfg -name 'node.def'`;
foreach my $f (@files) {
    my $result = `grep 'priority:' $f`;
    if (defined $result && length($result) != 0) {
	my @r = split " ", $result;
	if (defined $r[1]) {
	    # Strip off trailing "/node.def\n" from file pathname
	    my $line = substr($f, 0, -10);

	    # Strip off leading "/opt/vyatta/share/vyatta-cfg/templates/"
	    $line = substr($line, 39);

	    # See if there is a comment in entry
	    my ($entry, $comment) = split /#/, $result;
	    if (defined $comment) {
		$comment =~ s/\n//;
		$line = $line . " #" . $comment;
	    }
		
	    # stuff resulting line into hash
	    push @{$pri{$r[1]}}, $line;
	}
    }
}


#now sort

foreach my $key ( sort { $a <=> $b } keys %pri ) {
    my @a = @{$pri{$key}};
    foreach my $val (@a) {
	print $key," ",$val,"\n";
    }
} 
