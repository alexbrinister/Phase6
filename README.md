# EECE.4830 Network Design Phase 6
## Introduction

## Dependencies
This project has few dependencies. These are:

* Some sort of (recent) \*nix (I used Arch Linux with GCC version 8.2.1)
* Compiler that supports C++11 (basically, any GCC 5 and greater)
* CMake version 3.3 or greater

## Compilation
Compiling the program is done using CMake. While in the root directory of the
project, perform the following (assuming release build):

```bash
mkdir -p cbuild/release
cd cbuild/release
cmake -DCMAKE_BUILD_TYPE=Release ../..
make
```
The resulting executable is in the `bin` folder in the root directory of the
project. It will be called `phase5_release.bin`.

## Usage
One can use the program as a server or a client. I performed the tests on two
separate machines. **NOTE**: server must be run first or else the client will
send packets to nowhere. 

_Running from the root directory_:

**Node Receiving First**: 
```
bin/phase6_release.bin \ 
--server \
-m <local port number> \
-i <file to send> \
-o <destination file for transfer back> \
-w <data error percent> \
-x <ack error percent> \
-y <data packet loss percent> \
-z <ack packet loss percent>
```

**Node Sending**: 
```
bin/phase6_release.bin \
--client \
-m <local port number> \
-s <server hostname or IP> \
-d <server port number> \
-i <file to send> \
-o <destination file for transfer back> \
-w <data error percent> \
-x <ack error percent> \
-y <data packet loss percent> \
-z <ack packet loss percent>
```

## Issues

## Test

## Documentation
The rest of this documentation describes the classes, the class structure, and
the files included in this project. You can peruse the webpage using the
buttons/links.

If you are reading this document first, go into the *doc/html* directory and
open the *index.html* page in your browser. I find that the HTML page provides
the most beautiful way of using Doxygen documentation.

## Contributions
* _Alex_:
* _Colin_:
* _Mike_:
* _Yonatan_:

## Credit
We used the following books/websites for information and inspiration, as
well as code samples:

* _Beej's Guide to Network Programming Using Internet Sockets_ for the
socket programming.
* [cppreference.com](https://www.cppreference.com) for standard C/C++ library
  information.
* _The Standard C++ Library Second Edition_ by Nicolai M. Josuttis for
  clarification on how to use std::runtime\_error.
* GNU's page on
  [getopt](https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html)
and
[getopt\_long](https://www.gnu.org/software/libc/manual/html\_node/Getopt-Long-Option-Example.html)
* man7.org
* Linux Die
* Microsoft Desktop API Documentation
* RFCs (mentioned throughout the code reference)
* The book (for RDT 2.2 spec)
