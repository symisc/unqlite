<p align="center">
  <a href="https://unqlite.symisc.net/">
    <img src="https://unqlite.symisc.net/images/symisc-unqlite.png" alt="UnQLite" width="320">
  </a>
</p>

# UnQLite

UnQLite is a self-contained, serverless, transactional database engine for C and C++ applications.

It runs in-process, stores data in a single portable file, and ships as a small embed-friendly codebase with no external runtime dependency. UnQLite exposes two layers:

- a raw key/value store for binary-safe records
- a document store powered by the Jx9 embedded scripting language

Official website: https://unqlite.symisc.net/

Current public release: `1.2.1`

## Why UnQLite

- Embedded: no separate daemon, no socket protocol, no service to deploy
- Transactional: ACID semantics for local storage workloads
- Simple distribution: a database is typically one file on disk
- Portable: the file format is cross-platform
- Flexible: use key/value APIs directly, or use the document store and Jx9 layer
- Small integration surface: the recommended embed path is the amalgamation
- Optional threading support: enable it at compile time with `UNQLITE_ENABLE_THREADS`

## Recommended Integration Path

If you want the simplest and most stable embed story, use the amalgamation files at the repository root:

- `unqlite.c`
- `unqlite.h`

That is the intended drop-in path for production embedding.

The `src/` directory contains the split source tree used to build and maintain the amalgamation.

## Repository Layout

- `unqlite.c`: amalgamated implementation
- `unqlite.h`: public header for the amalgamation build
- `src/`: split source tree and internal headers
- `samples/`: small example programs
- `CHANGELOG.md`: release notes
- `LICENSE`: 2-Clause BSD license

## Quick Start

The fastest way to embed UnQLite is to compile your application together with `unqlite.c`.

### GCC or Clang

```sh
cc -O2 -std=c99 -I. your_app.c unqlite.c -o your_app
```

Compile the bundled key/value intro sample:

```sh
cc -O2 -std=c99 -I. samples/1.c unqlite.c -o unqlite_kv_intro
```

### MSVC

```bat
cl /nologo /TC /I. your_app.c unqlite.c
```

Compile the bundled key/value intro sample:

```bat
cl /nologo /TC /I. samples\1.c unqlite.c /link /OUT:unqlite_kv_intro.exe
```

### Threading Support

If your application needs UnQLite compiled with thread support, define `UNQLITE_ENABLE_THREADS` when building:

```sh
cc -O2 -std=c99 -DUNQLITE_ENABLE_THREADS -I. your_app.c unqlite.c -o your_app
```

```bat
cl /nologo /TC /DUNQLITE_ENABLE_THREADS /I. your_app.c unqlite.c
```

## Minimal Example

```c
#include "unqlite.h"
#include <stdio.h>

static int print_value(const void *data, unsigned int len, void *user_data) {
    (void)user_data;
    fwrite(data, 1, len, stdout);
    return UNQLITE_OK;
}

int main(void) {
    unqlite *db = 0;

    if (unqlite_open(&db, ":mem:", UNQLITE_OPEN_CREATE) != UNQLITE_OK) {
        return 1;
    }

    if (unqlite_kv_store(db, "hello", -1, "world", 5) != UNQLITE_OK) {
        unqlite_close(db);
        return 1;
    }

    if (unqlite_kv_fetch_callback(db, "hello", -1, print_value, 0) != UNQLITE_OK) {
        unqlite_close(db);
        return 1;
    }

    putchar('\n');
    return unqlite_close(db) == UNQLITE_OK ? 0 : 1;
}
```

If you only need embedded key/value storage, you can stay entirely within the `unqlite_kv_*` API family and ignore the document-store layer.

## What UnQLite Includes

- On-disk and in-memory databases
- Key/value CRUD APIs
- Cursor APIs for linear traversal
- A pluggable storage engine model
- A document store built on Jx9
- Foreign function and constant binding for Jx9 code
- A virtual file system abstraction for portability

## Samples

The `samples/` directory contains small, focused examples for common integration paths.

Useful starting points:

- `samples/1.c`: key/value introduction
- `samples/2.c` through `samples/6.c`: broader API usage
- `samples/unqlite_huge.c`: large-value handling
- `samples/unqlite_tar.c`: archive-oriented example
- `samples/unqlite_mp3.c`: metadata-oriented example

## Documentation

The official documentation lives on the new site:

- Homepage: https://unqlite.symisc.net/
- Downloads: https://unqlite.symisc.net/downloads.html
- UnQLite in 5 Minutes or Less: https://unqlite.symisc.net/intro.html
- C/C++ API introduction: https://unqlite.symisc.net/api_intro.html
- C/C++ API reference: https://unqlite.symisc.net/c_api.html
- Cursor reference: https://unqlite.symisc.net/c_api/unqlite_kv_cursor.html
- Architecture overview: https://unqlite.symisc.net/arch.html
- Jx9 overview: https://unqlite.symisc.net/jx9.html
- FAQ: https://unqlite.symisc.net/faq.html

## Notes

- UnQLite is not a client/server database.
- The amalgamation is the easiest way to consume the library.
- If you are evaluating the engine for a KV-only use case, start with `samples/1.c`.
- If you need document-oriented scripting, move next to the Jx9 documentation and samples.

## License

UnQLite is distributed under the 2-Clause BSD license.

- License text in this repository: [LICENSE](LICENSE)
- Official licensing page: https://unqlite.symisc.net/licensing.html
