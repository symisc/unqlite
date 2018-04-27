## UnQLite - Transactional Embedded Database Engine https://unqlite.org
[![Build Status](https://travis-ci.org/symisc/unqlite.svg?branch=master)](https://travis-ci.org/symisc/unqlite) [![Maintenance](https://img.shields.io/badge/Maintained%3F-yes-green.svg)](https://github.com/symisc/unqlite/pulse) [![GitHub license](https://img.shields.io/pypi/l/Django.svg)](https://unqlite.org/licensing.html) 

**Release 1.1.9** (April 2018): Fixed memory leak in _unqlite_commit()_ that caused data loss under some circumstances.

As of January 2018 - Symisc Systems has decided to **revive the UnQLite project**. All known data corruption bugs have been fixed, and expect to see new features (LZ compression), performance improvements, etc to be pushed here.
You should rely for your production build on the amalgamation file and its header file available here or to be downloaded directly from https://unqlite.org/downloads.html


UnQLite is a in-process software library which implements a self-contained, serverless, zero-configuration, transactional NoSQL database engine. UnQLite is a document store database similar to MongoDB, Redis, CouchDB etc. as well a standard Key/Value store similar to BerkeleyDB, LevelDB, etc.


UnQLite is an embedded NoSQL (Key/Value store and Document-store) database engine. Unlike most other NoSQL databases, UnQLite does not have a separate server process. UnQLite reads and writes directly to ordinary disk files. A complete database with multiple collections, is contained in a single disk file. The database file format is cross-platform, you can freely copy a database between 32-bit and 64-bit systems or between big-endian and little-endian architectures. UnQLite features includes:


    Serverless, NoSQL database engine.
    Transactional (ACID) database.
    Zero configuration.
    Single database file, does not use temporary files.
    Cross-platform file format.
    UnQLite is a Self-Contained C library without dependency.
    Standard Key/Value store.
    Document store (JSON) database via Jx9.
    Support cursors for linear records traversal.
    Pluggable run-time interchangeable storage engine.
    Support for on-disk as well in-memory databases.
    Built with a powerful disk storage engine which support O(1) lookup.
    Thread safe and full reentrant.
    Simple, Clean and easy to use API.
    Support Terabyte sized databases.
    BSD licensed product.
    Amalgamation: All C source code for UnQLite and Jx9 are combined into a single source file.



UnQLite is a self-contained C library without dependency. It requires very minimal support from external libraries or from the operating system. This makes it well suited for use in embedded devices that lack the support infrastructure of a desktop computer. This also makes UnQLite appropriate for use within applications that need to run without modification on a wide variety of computers of varying configurations.

UnQLite is written in ANSI C, Thread-safe, Full reentrant, compiles unmodified and should run in most platforms including restricted embedded devices with a C compiler. UnQLite is extensively tested on Windows and UNIX systems especially Linux, FreeBSD, Oracle Solaris and Mac OS X.


http://unqlite.org
