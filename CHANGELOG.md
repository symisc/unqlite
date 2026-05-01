# Changelog

All notable changes to this project are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
This project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] - 2026-04-30

### Changed

- Simplified the internal `SyMemBackend` pool path in both the split source tree and the amalgamation.
  `SyMemBackendPoolAlloc()` and `SyMemBackendPoolFree()` now route through the normal backend allocator/free path instead of the bucket-pool allocator.
- Removed the pool-only allocator machinery and bookkeeping from the internal runtime layer.
  This includes the old pool macros, pool header structure, pool-only helper functions, and stale pool realloc declarations.
- Refreshed the project README for the current site and current embedding workflow.
  The new README now documents the amalgamation-first integration path, sample programs, official documentation links, and working GCC/MSVC build examples.
- Modernized the root CMake project for current CMake and Visual Studio toolchains.
  The top-level build now requires CMake `3.10`, declares `C` explicitly as the project language, and uses cleaner option/conditional handling.
- Updated root test/sample wiring to prefer `samples/` when present while still tolerating older checkouts that may use `example/`.

### Fixed

- Fixed configuration failure with newer CMake releases that no longer support compatibility modes below CMake `3.5`.
- Fixed static zero-initializers that still assumed the removed pool bookkeeping fields were present.

### Notes

- No public C API change was introduced in this pass.
- The recommended production embed path remains the amalgamation pair:
  `unqlite.c` and `unqlite.h`.

## [1.2.1] - 2024-05

### Changed

- Moved the official project homepage and documentation links to `https://unqlite.symisc.net/`.

### Fixed

- Fixed the shared-database corruption issue reported in [issue #137](https://github.com/symisc/unqlite/issues/137).

## [1.1.8] - 2019-05

### Fixed

- Fixed the minor file handle leak described in [issue #74](https://github.com/symisc/unqlite/issues/74).

### State

- No known data corruption bug had been reported since December 2017 at the time of this release.

## [1.1.8] - 2018-01

### Fixed

- Synced the database first if a dirty commit had already been applied.
- Fixed a header inclusion issue.
