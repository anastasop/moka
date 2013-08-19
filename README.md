moka
====

Moka is a C++ application server. It preforks processes and balances them using select() ala unicorn.

Build
=====

To build moka you must install the google gflags http://code.google.com/p/gflags/ and google logging http://code.google.com/p/google-glog/ Then just type `make` and then run the server with `./srv --help` to see the instructions.

The command line arguments are mostly self-explanatory. This initial version was tested on FreeBSD 9.0 with g++ 4.2.1.

Moka is still work in progress. It purpose is to explore some design issues and architectures for application servers rather than provide a complete solution.
