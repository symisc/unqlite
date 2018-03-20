# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html).

### [1.1.9] - 2018-03-15

### Fixed

- Added pre-processor switch to detect `mingw` cross-compiler and correct the inclusion of `<Windows.h>` header for cross-compilation. This allows compiling `unqlite` for target `Windows` from a `Linux` host.

## [1.1.8] - Januari 2018

### Fixed

- Sync database first if a dirty commit has been applied.
- Header inclusion fix.
