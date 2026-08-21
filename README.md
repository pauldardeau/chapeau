Chapeau
=======

**Chapeau** provides a simple set of C++ wrapper classes for the SQLite C
library. It is essentially a language port of the excellent FMDB
(Objective-C):

https://github.com/ccgus/fmdb

License
-------
MIT License

This library is a derivative of FMDB whose license and permission is
as follows:

MIT License

Copyright (c) 2008-2014 Flying Meat Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

Dependencies
------------
- **Chaudière** - C++ core functionality (logging, string utilities, date/time),
  included as a git submodule (`chaudiere/`). Required to build both `src/`
  and `tests/`.
- **Poivre** - C++ unit test framework, included as a git submodule
  (`poivre/`). Only needed to build and run `tests/`; the library itself
  (`src/`) has no dependency on it.
- **SQLite3** - the underlying C library (`libsqlite3`), expected to be
  available on the system (e.g. `/usr/local/lib`, `/usr/local/include`).

Clone with `--recurse-submodules`, or after cloning:

```bash
git submodule update --init --recursive
```

Building
--------
```bash
make -C src    # builds libchapeau.so (and the query demo executable)
make -C tests  # builds test_chapeau (needs the poivre submodule)
```

Build `src/` without `-j`/parallel make - `query`'s link step isn't currently declared to
depend on `libchapeau.so` finishing first, so a parallel build can race and fail to find it
(serial `make` always gets the ordering right).

Your own programs need `-I` for `src/` and `chaudiere/src/`, and link
against `src/libchapeau.so`, `chaudiere/src/libchaudiere.so`, and
`-lsqlite3` (see `src/Makefile` and `tests/Makefile` for the exact flags).

### Building with CMake

A `CMakeLists.txt` is also provided (and doesn't have the `-j` caveat above - CMake always
gets target ordering right regardless of parallelism):

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure   # builds and runs test_chapeau
```

To depend on chapeau from another CMake project - vendored as a git submodule:

```cmake
add_subdirectory(chapeau)
target_link_libraries(my_target PRIVATE chapeau)
```

No `-I` needed - the include directory, chaudière and SQLite3 (both linked transitively),
and C++20 all propagate automatically (SQLite3 is located via CMake's built-in `FindSQLite3`
module rather than a hardcoded `-L/usr/local/lib`, so this also works with SQLite installed
from a system package, pkg-config, or vcpkg). The Makefile isn't going anywhere; both build
systems compile the same sources.

Groups of Functionality
------------------------
Chapeau is organized around a small set of pieces, mirroring FMDB's design:

### Database Access

- **`Database`** (interface), **`SQLiteDatabase`** — opens a SQLite file
  and runs `executeQuery`/`executeUpdate`, with or without bound
  parameters. Busy-retry (`SQLITE_BUSY`) is handled internally and
  configurable via `setBusyRetryTimeout()`. Also exposes transactions
  (`beginTransaction()`, `beginDeferredTransaction()`, `commit()`,
  `rollback()`), `lastInsertRowId()`, and error inspection
  (`lastErrorCode()`, `lastErrorMessage()`, `hadError()`).
- **`DBResultSet`** (interface), **`SQLiteResultSet`** — the cursor
  returned by `executeQuery()`. Call `next()` to advance a row at a time,
  then read typed values by column name or index (`intForColumn`,
  `longForColumn`, `boolForColumn`, `doubleForColumn`, `stringForColumn`,
  `dateForColumn`, `dataForColumn`, and the `*ForColumnIndex` equivalents).
- **`DBStatement`** (interface), **`SQLiteStatement`** — wraps a prepared
  `sqlite3_stmt*`. `Database` transparently caches statements by SQL text
  (`setShouldCacheStatements()`) so repeated queries against the same SQL
  reuse an already-prepared statement instead of re-parsing it.

### Typed Parameter Binding

- **`DBStatementArgs`** — an ordered list of bind values for a
  parameterized query; `add()` an argument per `?` placeholder, in order.
- **`DBAbstractDataType`** (interface) and its concrete wrappers —
  **`DBNull`**, **`DBBool`**, **`DBInt`**, **`DBLong`**, **`DBFloat`**,
  **`DBDouble`**, **`DBString`**, **`DBDate`**, **`DBData`** — one class
  per bindable SQLite type. Each knows how to bind itself
  (`bind(Database*, idx, DBStatement*)`), so `executeQuery`/`executeUpdate`
  just walks a `DBStatementArgs` list and lets each element bind itself.

### Values

- **`DBDate`** — a date/time bind value and result-set column type,
  built on chaudière's `DateTime`.
- **`DBData`** — a raw byte-buffer bind value and result-set column type,
  for SQLite `BLOB` columns.

Quick Start
-----------
Create a table, insert a row with bound parameters, and query it back.
This was compiled and run against the real library while writing this
README.

```cpp
#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"
#include "DBStatementArgs.h"
#include "DBInt.h"
#include "DBString.h"

using namespace chapeau;

SQLiteDatabase db("people.sqlite3");
db.open();

unsigned long rowsAffected = 0;
db.executeUpdate("CREATE TABLE people (id INTEGER, name TEXT)", rowsAffected);

DBStatementArgs insertArgs;
insertArgs.add(new DBInt(1));
insertArgs.add(new DBString("Alice"));
db.executeUpdate("INSERT INTO people (id, name) VALUES (?, ?)", insertArgs, rowsAffected);

DBStatementArgs queryArgs;
queryArgs.add(new DBInt(1));
DBResultSet* rs = db.executeQuery("SELECT id, name FROM people WHERE id = ?", queryArgs);

while (rs->next()) {
   const int id = rs->intForColumn("id");
   std::string name;
   rs->stringForColumn("name", name);
   // id == 1, name == "Alice"
}

rs->close();
delete rs;

db.close();
```

Testing
-------
Chapeau's test suite (`tests/`, built on poivre's `TestSuite`/`TEST_CASE`
framework) runs real queries against a real SQLite database file rather
than mocks. Build and run it with:

```bash
make -C src
make -C tests
LD_LIBRARY_PATH=src:chaudiere/src tests/test_chapeau
```

Meaning of Chapeau
-------------------
What does 'Chapeau' mean?  It's the French word for hat.
