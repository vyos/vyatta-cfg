# Perl module for type validation.
# Usage 1: validate a value of a specific type.
#   use VyattaTypeChecker;
#   ...
#   if (VyattaTypeChecker::validateType('ipv4', '1.1.1.1')) {
#     # valid
#     ...
#   } else {
#     # not valie
#     ...
#   }
#
# Usage 2: find the type of a value (from a list of candidates), returns
# undef if the value is not valid for any of the candidates.
#   $valtype = VyattaTypeChecker::findType('1.1.1.1', 'ipv4', 'ipv6');
#   if (!defined($valtype)) {
#     # neither ipv4 nor ipv6
#     ...
#   } else {
#     if ($valtype eq 'ipv4') {
#       ...
#     } else {
#       ...
#     }
#   }

package VyattaTypeChecker;

use strict;

my %type_handler = (
                    'ipv4' => \&validate_ipv4,
                    'ipv4net' => \&validate_ipv4net,
                    'ipv4_negate' => \&validate_ipv4_negate,
                    'ipv4net_negate' => \&validate_ipv4net_negate,
                    'protocol' => \&validate_protocol,
                    'protocol_negate' => \&validate_protocol_negate,
                    'macaddr' => \&validate_macaddr,
                    'macaddr_negate' => \&validate_macaddr_negate,
                    'ipv6' => \&validate_ipv6,
                   );

sub validate_ipv4 {
  $_ = shift;
  return 0 if (!/^(\d+)\.(\d+)\.(\d+)\.(\d+)$/);
  return 0 if ($1 > 255 || $2 > 255 || $3 > 255 || $4 > 255);
  return 1;
}

sub validate_ipv4net {
  $_ = shift;
  return 0 if (!/^(\d+)\.(\d+)\.(\d+)\.(\d+)\/(\d+)$/);
  return 0 if ($1 > 255 || $2 > 255 || $3 > 255 || $4 > 255 || $5 > 32);
  return 1;
}

sub validate_ipv4_negate {
  my $value = shift;
  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_ipv4($value);
}

sub validate_ipv4net_negate {
  my $value = shift;
  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_ipv4net($value);
}

sub validate_protocol {
  my $value = shift;
  $value = lc $value;
  return 1 if ($value eq 'all');
  if (!open(IN, "</etc/protocols")) {
    print "can't open /etc/protocols";
    return 0;
  }
  my $ret = 0;
  while (<IN>) {
    s/^([^#]*)#.*$/$1/;
    if ((/^$value\s/) || (/^\S+\s+$value\s/)) {
      $ret = 1;
      last;
    }
  }
  close IN;
  return $ret;
}

sub validate_protocol_negate {
  my $value = shift;
  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_protocol($value);
}

sub validate_macaddr {
  my $value = shift;
  $value = lc $value;
  my $byte = '[0-9a-f]{2}';
  return 1 if ($value =~ /^$byte(:$byte){5}$/);
}

sub validate_macaddr_negate {
  my $value = shift;
  if ($value =~ m/^\!(.*)$/) {
    $value = $1;
  }
  return validate_macaddr($value);
}

# IPv6 syntax definition
my $RE_IPV4_BYTE = '((25[0-5])|(2[0-4][0-9])|([01][0-9][0-9])|([0-9]{1,2}))';
my $RE_IPV4 = "$RE_IPV4_BYTE(\.$RE_IPV4_BYTE){3}";
my $RE_H16 = '([a-fA-F0-9]{1,4})';
my $RE_H16_COLON = "($RE_H16:)";
my $RE_LS32 = "(($RE_H16:$RE_H16)|($RE_IPV4))";
my $RE_IPV6_P1 = "($RE_H16_COLON)\{6\}$RE_LS32";
my $RE_IPV6_P2 = "::($RE_H16_COLON)\{5\}$RE_LS32";
my $RE_IPV6_P3 = "($RE_H16)?::($RE_H16_COLON)\{4\}$RE_LS32";
my $RE_IPV6_P4 = "(($RE_H16_COLON)\{0,1\}$RE_H16)?"
                 . "::($RE_H16_COLON)\{3\}$RE_LS32";
my $RE_IPV6_P5 = "(($RE_H16_COLON)\{0,2\}$RE_H16)?"
                 . "::($RE_H16_COLON)\{2\}$RE_LS32";
my $RE_IPV6_P6 = "(($RE_H16_COLON)\{0,3\}$RE_H16)?"
                 . "::($RE_H16_COLON)\{1\}$RE_LS32";
my $RE_IPV6_P7 = "(($RE_H16_COLON)\{0,4\}$RE_H16)?::$RE_LS32";
my $RE_IPV6_P8 = "(($RE_H16_COLON)\{0,5\}$RE_H16)?::$RE_H16";
my $RE_IPV6_P9 = "(($RE_H16_COLON)\{0,6\}$RE_H16)?::";
my $RE_IPV6 = "($RE_IPV6_P1)|($RE_IPV6_P2)|($RE_IPV6_P3)|($RE_IPV6_P4)"
               . "|($RE_IPV6_P5)|($RE_IPV6_P6)|($RE_IPV6_P7)|($RE_IPV6_P8)"
               . "|($RE_IPV6_P9)";

sub validate_ipv6 {
  $_ = shift;
  return 0 if (!/^$RE_IPV6$/);
  return 1;
}

sub validateType {
  my ($type, $value, $quiet) = @_;
  if (!defined($type) || !defined($value)) {
    return 0;
  }
  if (!defined($type_handler{$type})) {
    print "type \"$type\" not defined\n" if (!defined($quiet));
    return 0;
  }
  if (!&{$type_handler{$type}}($value)) {
    print "\"$value\" is not a valid value of type \"$type\"\n"
      if (!defined($quiet));
    return 0;
  }

  return 1;
}

sub findType {
  my ($value, @candidates) = @_;
  if (!defined($value) || ((scalar @candidates) < 1)) {
    return undef;
  }
  foreach my $type (@candidates) {
    if (!defined($type_handler{$type})) {
      next;
    }
    if (&{$type_handler{$type}}($value)) {
      # the first valid type is returned
      return $type;
    }
  }
  return undef;
}

1;

