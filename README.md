# External R Stack Walking Demo

This repository contains a demo sampling profiler for R programs. It will print
out the current stack in the standard `Rprof.out` format, which is widely used
by existing R tools.

```shell
Rscript myscript.R &
# sudo is required to attach to the process
sudo ./rtrace -p <PID> -F 50 | tee Rprof.out
```

How is this different from existing R profiling options, such as `Rprof()` or
the [**profvis**](https://rstudio.github.io/profvis/) package? While these tools
are invaluable for understanding R code inside the current R session, this demo
allows you to extract profiling data from R code *that is already running*. It
is mainly aimed at understanding what R code running in production is doing when
you cannot afford to modify that process. In this regard it joins a large list
of tools for other languages, such as `perf` (the Linux system profiler),
`jstack` (for Java), `rbspy` (for Ruby), `Pyflame` (for Python), and many
others.

Along with the sampling profiler itself, there is also a `stackcollapse-Rprof.R`
script that converts the `Rprof.out` format to one that can be understood by
Brendan Gregg's [FlameGraph](http://www.brendangregg.com/flamegraphs.html) tool.
You can use this to produce graphs like the one below:

```shell
$ stackcollapse-Rprof.R Rprof.out | flamegraph.pl > Rprof.svg
```

![Example FlameGraph](example-flamegraph.svg)

The project was inspired by Julia Evan's blog posts on writing
[`rbspy`](https://rbspy.github.io/) and later by my discovery of Evan Klitzke's
work (and writing) on [Pyflame](https://github.com/uber/pyflame).

## Installation

You must build from source. Clone (or download) the repository and run

```console
$ make
$ sudo make install
```

This will build the binary and install it to `/usr/local/bin`. The `install`
target supports `prefix` and `DESTDIR`.

## Okay, How Does it Work?

Much like other sampling profilers, the program uses Linux's `ptrace` system
calls to attach to running R processes and a mix of `ptrace` and
`process_vm_readv` to read the memory contents of that process, following
pointers along the way.

The R-specific aspect of this is to locate and decode the `R_GlobalContext`
structure inside of the R interpreter that stores information on the currently
executing R code.

In order to "defeat" address space randomization, the profiler will also load
`libR` into memory and then locate the offset of the global context structure.

## Usage

Since this is a demo, the interface is subject to change without notice. For
now, the interface is as follows:

    Usage: ./rtrace [-v] [-F <freq>] [-d <duration>] -p <pid>

## Building

The project contains a simple `Makefile`; just run `make`. This will attempt to
find the R header files automatically. If this fails, you can override them:

``` shell
$ make R_HEADERS=~/src/R-3.5.2/include
```

## Limitations

This project falls firmly in the "demo" stage. It only works on Linux. At
present, it does not support sampling from R programs running inside Docker
containers, although this is planned. Nor does it support reading the C-level
stack, although again this is planned.
