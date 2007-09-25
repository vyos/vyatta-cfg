package VyattaMisc;
require Exporter;
@ISA	= qw(Exporter);
@EXPORT	= qw(getNetAddIP, isIpAddress);
@EXPORT_OK = qw(getNetAddIP, isIpAddress);

use strict;

sub getNetAddrIP {
  my ($interface);
  ($interface) = @_;

  if ($interface eq '') {
    print STDERR "Error:  No interface specified.\n";
    return undef;
  }


  my $ifconfig_out = `ifconfig $interface`;
  $ifconfig_out =~ /inet addr:(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})/;
  my $ip = $1;
  if ($ip eq '') {
    print STDERR "Error:  Unable to determine IP address for interface \'$interface\'.\n";
    return undef;
  }


  $ifconfig_out =~ /Mask:(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})/;
  my $netmask = $1;
  if ($netmask eq '') {
    print STDERR "Error:  Unable to determine netmask for interface \'$interface\'.\n";
    return undef;
  }

  use NetAddr::IP;  # This library is available via libnetaddr-ip-perl.deb
  my $naip = new NetAddr::IP($ip, $netmask);
  return $naip;
}

sub isIpAddress {
  my $ip = shift;

  $_ = $ip;
  if ( ! /^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$/) {
    return 0;
  }
  else {
    my @ips = split /\./, $ip;
    my $octet = 0;
    my $counter = 0;

    foreach $octet (@ips) {
      if (($octet < 0) || ($octet > 255)) { return 0; }
      if (($counter == 0) && ($octet < 1)) { return 0; }
      $counter++;
    }
  }

  return 1;
}

return 1;
