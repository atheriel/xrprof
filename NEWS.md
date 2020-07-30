# xrprof devel

* The `-o` option can now be used to write the output directly to a file instead
  of standard output.

# xrprof 0.3.0

* `xrprof` can now be built and run on Windows. (#3)

* Experimental support for "mixed-mode" profiling of both R and C/C++ stacks on
  Linux with `-m`. This also introduces a dependency on `libunwind`. Note that
  the exact output format is not fixed and may change in the future. (#8)

* Various performance improvements.

# xrprof 0.2.0

* `libelf` is now required.

* Support for profiling R processes running in Docker containers. (#6)

* Support for profiling R when built without `libR.so`. (#7)

* Fixes a memory leak.

# xrprof 0.1

* Initial public release. `xrprof` is an external sampling profiler for R on
  Linux.
