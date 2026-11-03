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

Concurrency
-----------
**Chapeau is not thread safe.** It supports one pattern: a database
connection and everything that came from it is used by one thread at a
time. It does not make unsynchronized sharing safe. It does notice, and
refuse, a call that overlaps another call on the same connection (see
*Refusals* below), but that is detection of a mistake, not permission to
share.

A **unit** is one `SQLiteDatabase` (one connection), the result sets
(`executeQuery()` cursors) it produced that have not been destroyed, and the
prepared statements they use (the statement cache and the statements a
cursor owns). A cursor reads and writes its database while it steps, ends or
is destroyed, and the database does the same to its cursors when it closes,
so the parts of a unit share state that nothing protects: the statement cache,
the set of live cursors, the error snapshot, the in-use marker, and the
`sqlite3*` connection.

The rules:

1. At most one thread may access any part of a unit at a time.
2. Different database connections may be used concurrently by different
   threads, also to the same file; SQLite's file locks and the busy timeout
   (`setBusyRetryTimeout()`, 5000 ms by default) make them wait for each other.
3. A unit may move between threads while it is idle.
4. A hand-off needs synchronization **you** provide (a mutex, a condition
   variable, a `join`, a queue). Chapeau provides none.
5. Cursors move with the database they came from: move the whole unit, never
   the database to one thread and its cursors to another.
6. `close()`, the destructor of a database, and the destruction of a cursor
   are accesses like any other, and must not overlap any other access to the
   unit.
7. The error snapshot (`lastErrorKind()`, `lastErrorCode()`,
   `lastErrorMessage()`) belongs to the unit. Read it before the next
   operation replaces it. A cursor's own error belongs to the cursor.
8. A transaction belongs to the connection, not to a thread: it stays open when
   the unit moves, and whichever thread has the unit may commit or roll it back.
9. SQLite callbacks and hooks you install through `sqliteHandle()` must not
   reenter the same unit.
10. Connection pooling is your application's job. Chapeau has no pool.

What this does **not** mean:

- Unsynchronized shared-connection access is not supported, for reads either.
  A violation is a data race (undefined behavior), not an error code.
- SQLite's serialized mode (`sqlite3_threadsafe() == 1`) does not make
  Chapeau thread safe. It protects SQLite's connection object, not the state
  Chapeau keeps around it.
- The in-use marker (`setInUse()`) is not a lock; it is a compatibility flag.
  `inUse()` is a probe: it answers `true` while another call is running on the
  connection (counting and logging nothing), and otherwise as it always did.
- Detection is not safety. Chapeau sees a call that overlaps another one in
  time. It does not see sequential interleaving (two threads taking turns, call
  after call, inside one transaction or on one cursor, with no two calls
  overlapping), and it does not guard what goes around its calls:
  `sqliteHandle()` and the SQLite objects obtained from it, the bind methods
  (the library calls those itself while it holds the connection, as steps of an
  execute), and a cursor's `statement()`, `setStatement()` and `setParentDB()`.
  Those are parts of the unit, and the rules above apply to them in full.
- Three different things are easy to confuse:
  1. **SQLite's compile-time threading support** (`SQLITE_THREADSAFE`, reported
     by `sqlite3_threadsafe()`: 0 none, 1 serialized, 2 multi-thread). Chapeau
     requires it: `open()` returns `false` with `lastErrorKind()` ==
     `DBErrorKind::UnsupportedLibrary` (code 0) when it is 0, before any handle
     is created or any file touched. The message names the SQLite version and
     says to use a build with threading enabled. The object stays valid and
     unopened.
  2. **SQLite's run-time threading configuration** (`sqlite3_config()` with
     `SQLITE_CONFIG_SINGLETHREAD`, `MULTITHREAD` or `SERIALIZED`, and the
     `SQLITE_OPEN_NOMUTEX`/`FULLMUTEX` flags). Chapeau neither sets nor checks
     it. A thread-safe library that the application has switched to
     single-thread mode at run time is not detected.
  3. **Chapeau's own thread-confinement contract** (the rules above). The check
     in 1 is a precondition for it. **It does not make Chapeau thread safe**:
     a library that passes it is still used one thread at a time per unit.
- Errors logged by `setLogsErrors(true)` go to stdout, and diagnostics go
  to Chaudiere's `Logger`; if you install a logger and use several connections
  at once, it must tolerate being called from several threads.

The supported patterns are exercised, as bounded subprocess scenarios, by the
`concurrency_*` cases of the regression harness (`tests/regression/`), and the
suite is run under ThreadSanitizer in CI (the `tsan` job). The same contract
is written in the header comments of `Database.h`, `SQLiteDatabase.h` and
`SQLiteResultSet.h`.

### Refusals

Every public call of `SQLiteDatabase` and `SQLiteResultSet`, and the calls
that change or read the statement cache, take the connection for their
duration. A call that finds it taken (another thread is inside, or the call came
from a SQLite callback of the running call) does not wait and does not touch
the connection: it is **refused** and returns a neutral value. The rules are
written out in the comment of `Database` in `Database.h`; in short:

- `false`, `nullptr`, `0`, `0.0`, `-1` or `ValueTypeNull`, whatever the call
  returns for "nothing"; checked accessors clear their out parameter; setters,
  `close()` and `executeUpdate()` do nothing (`executeUpdate()` also sets its row
  count to 0).
- A refused call changes nothing of the call that is running, of any cursor
  (a refused `next()` consumes no row) or of the error snapshot. A *database* call
  refused because it came from a callback of the running call on the same thread
  reports `DBErrorKind::InUse`, as it always has, to its caller, while the running
  call is still running. That `InUse` is not what the running call leaves behind:
  when it returns, the error snapshot is its own result -- `DBErrorKind::None`
  after a query, update or transaction call that succeeded, SQLite's own error
  after one that failed -- and a successful `next()`, which does not write the
  snapshot, leaves it as it was before the step. The cursor's `status()` and
  error code and message are always those of its own step.
- **A refusal cannot be seen in the return value.** A refused accessor looks
  like a column that is `0` or NULL, and a refused `next()` like the end of the
  data (its `status()` is not `Done`, though). To find out, read
  `Database::refusedCallsOnThisThread()` before and after (the calls the calling
  thread had refused, over all databases) or `db.refusedCalls()` (the calls
  refused on one connection, from every thread, never reset; a cursor that
  outlives its database keeps counting into the same connection state), or turn
  on `setLogsErrors(true)`: the first refusal on a connection and then the 2nd,
  4th, 8th ... are written to stdout as `DB Warning [ConcurrentUse]: ... (unit N,
  <operation>, refusal K on this unit)`. Counting goes on when logging is off.
- **Destructors cannot refuse.** Destroying a database or a cursor while its
  connection is taken, by another thread or from inside one of its own calls,
  prints `Chapeau: fatal: ~SQLiteDatabase ran while its unit ...` (or
  `~SQLiteResultSet`) to stderr and aborts the process. This is a misuse detector:
  a destruction that races with a call, rather than finding it already running,
  can still go wrong before it is noticed. Destroying an idle connection is as
  ever; so is a synchronized hand-off of an idle one.
- None of this makes concurrent use supported, and a program that relies on
  refusals is incorrect. A refusal means a call did not happen.

**ABI.** The `Database` and `SQLiteResultSet` objects each gained one member (a
reference to the connection state they share). The change adds no virtual
function, but the object layouts changed: rebuild everything that uses Chapeau.

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
