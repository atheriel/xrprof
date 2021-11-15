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
use File::Basename;

my %srcrefs;
my %addrs;
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
        if (m/([0-9]+)#([abcdefx0-9]+)/) {
            my $file = $1;
            my $line = $2;
            if (!exists $srcrefs{$file}) {
                # Avoid undefined hash errors, just in case.
                next;
            }
            # Detect when "line numbers" are actually addresses.
            if (not ($line =~ m:^0x:)) {
                $srcref = " at $srcrefs{$file}:$line";
                next;
            }
            my @a2l_entries;
            if (exists $addrs{"$srcrefs{$file}:$line"}) {
                @a2l_entries = @{ $addrs{"$srcrefs{$file}:$line"} };
                push @stack, @a2l_entries;
                $srcref = $a2l_entries[$#a2l_entries];
                # warn "current entries: $srcref, @a2l_entries\n\n";
                next;
            }
            # Run the address through addr2line to see if we can recover the
            # source reference. Note that there might be multiple entries in the
            # result, e.g.
            #
            #    futex_reltimed_wait_cancelable
            #    futex-internal.h:142
            #    __pthread_cond_wait_common
            #    pthread_cond_wait.c:533
            #    __pthread_cond_timedwait
            #    pthread_cond_wait.c:667
            my $a2l = `addr2line $line -e $srcrefs{$file} -i -f -s -C`;
            # warn "output: ", $a2l;
            my $previous = "";
            my $entry = "";
            my $libname = basename($srcrefs{$file});
            for (split /^/, $a2l) {
                chomp $_;

                if ($_ =~ m/\?\?/ and $previous eq "") {
                    warn "found no entry for $line in $srcrefs{$file}: $_\n";
                    next;
                }

                # if ($_ =~ m/\?\?/ and not $previous eq "") {
                #     # warn "found $_ with previous line: $previous\n";
                #     $entry = "$previous";
                #     $entry .= "_[n] at $libname:$_";
                #     push @a2l_entries, $entry;
                #     $srcref = $entry;
                #     $previous = "";
                #     next;
                # }

                $_ =~ s/ \(discriminator \S+\)//;

                # if ($_ =~ m/^[0-9]+/) {
                #     warn "strange entry: $a2l\n";
                # }

                if ($previous eq "") {
                    # warn "set previous: $_\n";
                    $previous = $_;
                    next;
                }
                $entry = "$previous";
                $entry .= "_[n] at $libname:$_";

                push @a2l_entries, $entry;
                $srcref = $entry;
                $previous = "";
                # warn "pushed @a2l_entries\n";
            }
            # if (not $previous eq "") {
            #     warn "dangling entry: $previous;\nin: $a2l\n";
            # }
            $addrs{"$srcrefs{$file}:$line"} = \@a2l_entries;
            push @stack, @a2l_entries;
            # warn "\npushed to @stack\n\n";
            next;
            # 72#0x176151 "<Native:Rf_allocFormalsList6>" 72#0x176288 "<Native:R_mkEVPROMISE>" "pmax" "LimitToRange" "CalculateGoalProbs" "eval" "eval" "[.data.table" "[" "JasisModelCompute" "eval" "eval" "withVisible" "<Unknown>" "source" "<TopLevel>"
        }

        my $func = $_;
        $func =~ s/"//g;

        # Annotate native code symbols with a _[n] prefix, which is close to the
        # current conventions. This symbol will then be interpreted up by the
        # Flamegraph tool as a semantic colour directive.
        if (m/<Native:([^;]+)>/) {
            if (not $srcref eq "") {
                # warn "srcref for native symbol $1: $srcref\n";
                # The addr2line output should already cover this symbol.
                $srcref = "";
                next;
            }
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

        if ($func =~ m/^[0-9]+/) {
            warn "strange entry: $func\n";
        }

        push @stack, $func;
    }

    $collapsed{join(";", reverse(@stack))} += 1;
}

foreach my $k (sort { $a cmp $b } keys %collapsed) {
    print "$k $collapsed{$k}\n";
}
