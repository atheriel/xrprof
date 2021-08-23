#!/usr/bin/perl -w

# stackcollapse-rprof  Collapse R's Rprof.out format
#
# Example input:
#
#     sample.interval=20000
#     "Sys.sleep"
#     "Sys.sleep"
#     "Sys.sleep"
#     "get" "registerS3methods" "loadNamespace" "getNamespace" "asNamespace" "getExportedValue" "::" "cleanup" "<Anonymous>" "<TopLevel>"
#
# Output:
#
#    <Anonymous>;cleanup;::;getExportedValue;asNamespace;getNamespace;loadNamespace;registerS3methods;get 1
#    Sys.sleep 3

use strict;
use warnings;

my %srcrefs;
my %collapsed;
my $nr = 0;

foreach (<>) {
    # Ignore the "sample.interval" header on the first line.
    if ($nr++ < 1) {
        if (!m/sample\.interval/) {
            warn "warning: input does not appear to be an Rprof.out file\n";
        }
        next;
    }
    chomp;

    # Collect file references into a hash so we can look them up by index later.
    if (m/^#File ([0-9]+): (.*)$/) {
        $srcrefs{$1} = $2;
        next;
    }

    my @stack;
    my $srcref = "";

    for (split / /, $_) {
        # Push any srcref annotations -- e.g. "1#187 func" -- onto a stack for
        # the next function, with a more readable format.
        if (m/([0-9]+)#([0-9]+)/) {
            $srcref = " at $srcrefs{$1}:$2";
            next;
        }

        my $func = $_;
        $func =~ s/"//g;

        # Annotate native code symbols with a _[n] prefix, which is close to the
        # current conventions. This symbol will then be interpreted up by the
        # Flamegraph tool as a semantic colour directive.
        if (m/<Native:([^;]+)>/) {
            if ($1 eq "Missing") {
                $func = "<Missing>_[n]";
            } else {
                $func = "$1_[n]";
            }
        }

        # Append srcref annotations if present.
        if (!$srcref eq "") {
            $func .= "$srcref";
            $srcref = "";
        }

        # Drop stacks when nothing is evaluating.
        if ($func eq "<TopLevel>" || m/^\\s*$/) {
            next
        }

        push @stack, $func;
    }

    $collapsed{join(";", reverse(@stack))} += 1;
}

foreach my $k (sort { $a cmp $b } keys %collapsed) {
    print "$k $collapsed{$k}\n";
}
