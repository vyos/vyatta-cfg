package XorpConfigParser;

use lib "/opt/vyatta/share/perl5/";
use strict;

my %data;

my %fields = (
	_data => \%data
);

sub new {
  my $that = shift;
  my $class = ref ($that) || $that;
  my $self = {
    %fields,
  };

  bless $self, $class;
  return $self;
}


sub copy_node {
	my ($self, $from, $to, $name) = @_;
	if (!defined($from) || !defined($to) || !defined($name)) {
		return;
	}

	foreach my $node (@$from) {
		my $stringNodeNameHere = $node->{'name'};
		if ($stringNodeNameHere =~ /^$name.*/) {
			foreach my $nodeCheck (@$to) {
				my $stringCheck = $nodeCheck->{'name'};
				if ($name eq $stringCheck) {
					$nodeCheck->{'value'} = $node->{'value'};
					$nodeCheck->{'children'} = $node->{'children'};
					$nodeCheck->{'comment'} = $node->{'comment'};
					return;
				}
			}
			push(@$to, $node);
		}
	}
}
sub copy_multis {
	my ($self, $nodes, $name) = @_;
	if (!defined($nodes) || !defined($name)) {
		return undef;
	}

	my @multis;

	foreach my $node (@$nodes) {
		my $stringNodeNameHere = $node->{'name'};
		if ($stringNodeNameHere =~ /$name\s(\S+)/) {
			my $stringNameHere = $1;
			my %multi = (
				'name' => $stringNameHere,
				'comment' => $node->{'comment'},
				'value' => $node->{'value'},
				'children' => $node->{'children'}
			);
			push(@multis, \%multi);
		}
	}

	return @multis;
}
sub comment_out_child {
	my ($self, $children, $name, $comment) = @_;
	if (!defined($children) || !defined($name)) {
		return;
	}

	for (my $i = 0; $i < @$children; $i++) {
		my $stringNodeNameHere = @$children[$i]->{'name'};
		if ($name eq $stringNodeNameHere) {
			@$children[$i]->{'comment_out'} = "1";
      if (defined($comment)) {
        @$children[$i]->{'comment_out'} = $comment;
      }
		}
	}
}
sub create_node {
	my ($self, $path) = @_;

	my $hash = \%data;
	foreach my $segment (@$path) {
		my $children = $hash->{'children'};
		if (!defined($children)) {
			my @new_children;
			$hash->{'children'} = \@new_children;
			$children = \@new_children;
		}
		my $child_found = 0;
		foreach my $child (@$children) {
			if ($child->{'name'} eq $segment) {
				$child_found = 1;
				$hash = $child;
				last;
			}
		}
		if ($child_found == 0) {
			my %new_hash = (
				'name' => $segment
			);
			push(@$children, \%new_hash);
			$hash = \%new_hash;
		}
	}
	return $hash;
}
sub delete_child {
	my ($self, $children, $name) = @_;
	if (!defined($children) || !defined($name)) {
		return;
	}

	for (my $i = 0; $i < @$children; $i++) {
		my $stringNodeNameHere = @$children[$i]->{'name'};
		if ($name eq $stringNodeNameHere) {
			@$children[$i] = undef;
		}
	}
}
sub find_child {
	my ($self, $children, $name) = @_;
	if (!defined($children) || !defined($name)) {
		return undef;
	}

	foreach my $child (@$children) {
		my $stringNodeNameHere = $child->{'name'};
		if ($name eq $stringNodeNameHere) {
			return $child;
		}
	}
	return undef;
}
sub get_node {
	my ($self, $path) = @_;

	my $hash = $self->{_data};
	foreach my $segment (@$path) {
		my $children = $hash->{'children'};
		if (!defined($children)) {
			return undef;
		}

		my $child_found = 0;
		foreach my $child (@$children) {
			if ($child->{'name'} eq $segment) {
				$child_found = 1;
				$hash = $child;
				last;
			}
		}

		if ($child_found == 0) {
			return undef;
		}
	}
	return $hash;
}

sub push_comment {
	my ($self, $path, $comment) = @_;

	my $hash = \%data;
	foreach my $segment (@$path) {
		my $children = $hash->{'children'};
		if (!defined($children)) {
			my @children;
			$hash->{'children'} = \@children;
			$children = \@children;
		}

		my $child_found = 0;
		foreach my $child (@$children) {
			if ($child->{'name'} eq $segment) {
				$child_found = 1;
				$hash = $child;
				last;
			}
		}

		if ($child_found == 0) {
			my %new_hash = (
				'name' => $segment
			);
			push(@$children, \%new_hash);
			$hash = \%new_hash;
		}
	}

	my %new_comment = (
		'comment' => $comment
	);
	my $childrenPush = $hash->{'children'};
	if (!defined($childrenPush)) {
		my @new_children;
		$hash->{'children'} = \@new_children;
		$childrenPush = \@new_children;
	}
	push(@$childrenPush, \%new_comment);
}
sub set_value {
	my ($self, $path, $value) = @_;

	my $hash = $self->create_node($path);
	if (defined($hash)) {
		$hash->{'value'} = $value;
	}
}
sub output {
	my ($self, $depth, $hash) = @_;

	if (!defined($hash)) {
		$hash = $self->{_data};
	}

	if ($hash->{'comment'} ne '') {
		print '/*' . $hash->{'comment'} . "*/\n";
	}
	my $children = $hash->{'children'};
	foreach my $child (@$children) {
		if (defined($child)) {
			if (defined($child->{'comment_out'})) {
				print "\n";
        if ($child->{'comment_out'} ne "1") {
          print "/*   --- $child->{'comment_out'} ---   */\n";
        }
				print "/*   --- CONFIGURATION COMMENTED OUT DURING MIGRATION BELOW ---\n";
			}

			print "    " x $depth;
			if ($child->{'value'} ne '') {
				print "$child->{'name'}: $child->{'value'}";
				print "\n";
			} else {
				my $print_brackets = 0;
				my $children = $child->{'children'};
				if (defined($children) && @$children > 0) {
					$print_brackets = 1;
				} elsif ($child->{'name'} ne '' && !($child->{'name'} =~ /\s/))  {
					$print_brackets = 1;
				}

				if ($child->{'name'} ne '') {
					print "$child->{'name'}";
					if ($print_brackets) {
						print " {";
					}
					print "\n";
				}

				$self->output($depth+1, $child);
				if ($print_brackets) {
					print "    " x $depth;
					print "}\n";
				}
			}

			if (defined($child->{'comment_out'})) {
				print "     --- CONFIGURATION COMMENTED OUT DURING MIGRATION ABOVE ---  */\n\n";
			}

		}
	}
}
sub parse {
	my ($self, $file) = @_;
	open(INPUT, "< $file") or die "Error!  Unable to open file \"$file\".  $!";

	my $contents = "";
	while (<INPUT>) {$contents .= $_}
	close INPUT;

	my @array_contents = split('', $contents);
#	print scalar(@array_contents) . "\n";

	my $length_contents = @array_contents;
	my $colon = 0;
	my $colon_quote = 0;
	my $name = '';
	my $value = undef;
	my @path;
	my %tree;
	for (my $i = 0; $i < $length_contents;) {
		my $c = $array_contents[$i];
		my $cNext = $array_contents[$i+1];

		if ($c eq '/' && $cNext eq '*') {
			my $comment_text = '';
			my $comment_end = index($contents, '*/', $i+2);
			if ($comment_end == -1) {
				$comment_text = substr($contents, $i+2);
			} else {
				$comment_text = substr($contents, $i+2, $comment_end - $i - 2);
				$i = $comment_end + 2;
			}
#			print 'Comment is: "' . $comment_text . "\"\n";
			$self->push_comment(\@path, $comment_text);
		} elsif ($colon == 0 && ($c eq '{' || $c eq ':' || $c eq "\n")) {
			$name =~ s/^\s+|\s$//g;
			if (length($name) > 0) {
				push(@path, $name);
#				print "Path is: \"@path\"    Name is: \"$name\"\n";
				$self->set_value(\@path, $value);
				$name = '';

				if ($c eq "\n") {
					pop(@path);
				}
				if ($c eq ':') {
					$colon = 1;
				}
			}
			$i++;
		} elsif ($c eq '}') {
			pop(@path);
			$name = '';
			$i++;
		} elsif ($c eq ';') {
			$i++;
		} elsif ($colon == 1) {
			my $value_end = 0;
			if ($c eq '"') {
				$value .= $c;
				if ($colon_quote == 1) {
					$value_end = 1;
				} else {
					$colon_quote = 1;
				}
			} elsif ($c eq '\\' && $cNext eq '"') {
				$value .= '\\"';
				$i++;
			} else {
				if ((length($value) > 0) || (!($c =~ /\s/))) {
					$value .= $c;
				}
			}

			if ($colon_quote == 0 && ($cNext eq '}' || $cNext eq ';' || $cNext =~ /\s/)) {
				$value_end = 1;
			}
			$i++;

			if ($value_end == 1) {
				if (length($value) > 0) {
#					print "Path is: \"@path\"    Value is: $value\n";
					$self->set_value(\@path, $value);
					$value = undef;
				}
				pop(@path);
				$colon_quote = 0;
				$colon = 0;
			}
		} else {
			$name .= $c;
			$i++;
		}
	}
}


