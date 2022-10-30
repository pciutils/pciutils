#!/usr/bin/perl
use strict;
use warnings;
die "Usage: $0 script.ver dllname build.def import.def\n" if @ARGV != 4;
my ($verfile, $dllname, $builddef, $importdef) = @ARGV;
open my $verfh, '<', $verfile or die "Cannot open input file $verfile: $!\n";
my $input = join '', <$verfh>;
close $verfh;
my @syms;
my (%cnt, %last, %ords);
$input =~ s/\/\*.*?\*\///sg; # Remove C comments
while ($input =~ m/(\S+)\s*\{((?:[^\{\}]|\{(?2)\})+)\}\s*;/sg) { # Split {...}
	my ($ver, $block) = ($1, $2);
	while ($block =~ s/(\S+)\s*:((?:[^\{\}:]|\{(?2)\})+)$//sg) { # Split section:
		my ($section, $syms) = ($1, $2);
		next if $section ne 'global';
		$syms =~ s/\s+//g;
		foreach (split /;\s*/, $syms) { # Split symbols
			$cnt{$_}++;
			$last{$_} = $ver;
			push @syms, [$_, $ver];
		}
	}
}
open my $importfh, '>', $importdef or die "Cannot open output file $importdef: $!\n";
open my $buildfh, '>', $builddef or die "Cannot open output file $builddef: $!\n";
print $importfh "LIBRARY \"$dllname\"\n";
print $importfh "EXPORTS\n";
print $buildfh "EXPORTS\n";
my $ord = 1;
foreach (@syms) {
	my ($sym, $ver) = @{$_};
	print $importfh "\"$sym\@$ver\" \@$ord\n";
	if ($last{$sym} ne $ver) {
		print $buildfh "\"$sym\@$ver\" \@$ord\n";
	} else {
		$ords{$sym} = $ord;
		print $buildfh "\"$sym\@$ver\" = " . (($cnt{$sym} > 1) ? "\"$sym\@\@$ver\"" : $sym) . " \@$ord\n"
	}
	$ord++;
}
# GNU dlltool has broken calculation of ordinals for aliased symbols, so specify ordinals explicitly
# GNU LD prior 2.21 has broken handling of symbols with dot character
# Operator == for defining symbol alias is supported since GNU dlltool 2.21
print $importfh "$_ \@$ords{$_} == \"$_\@$last{$_}\"\n" foreach sort keys %last;
close $importfh;
close $buildfh;
