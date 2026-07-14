# SQL in C++

SQL in C++ 2.0 is an educational relational database engine and command-line DSL implemented from first principles.
It parses a focused subset of SQL, persists records in checksummed pages, maintains persistent B+ tree indexes, recovers atomic single-writer statements through a WAL, and evaluates compound filters with a shunting-yard pipeline.

![Command-line preview](image/preview.png)

## What it supports

- `CREATE TABLE` with an ordered field list.
- `INSERT INTO` with quoted or unquoted values.
- `SELECT *` and projected field lists.
- `WHERE` filters using `=`, `!=`, `<`, `<=`, `>`, and `>=`.
- Compound `AND` and `OR` expressions with parentheses.
- Checksummed paged persistence across separate CLI sessions.
- Persistent per-field B+ tree indexes.
- Crash recovery for atomic single-writer statements.
- Copy-first migration from the v1 fixed-width format.
- Batch execution from the sample command files in `batch/`.

This project is intentionally smaller than a production SQL database.
It does not implement joins, multi-statement transactions, schemas, query planning, updates, or deletes.
Values are stored and ordered as strings, so numeric-looking values use lexical comparison semantics.

## Architecture

| Component | Responsibility |
| --- | --- |
| `includes/tokenizer` | Converts command text into lexical tokens with a state machine. |
| `includes/parser` | Validates the supported grammar and builds a parse tree. |
| `includes/shunting_yard` | Converts compound filter expressions to postfix form. |
| `includes/map` | Implements B+ tree-backed maps and multimaps. |
| `includes/binary_files` | Reads the legacy v1 fixed-width format. |
| `includes/storage` | Owns v2 pages, persistent indexes, checksums, WAL, and recovery. |
| `includes/table` | Owns table persistence, indexes, projections, and selections. |
| `includes/sql` | Coordinates parsing and table operations through the public `SQL` interface. |

Storage invariants, query semantics, and deliberate limits are documented in [DESIGN.md](DESIGN.md).
The canonical repository is [hskl18/SQL](https://github.com/hskl18/SQL).

## Build

You need a C++17 compiler and CMake 3.20 or newer.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The build has no downloaded test framework or other third-party dependency.

## Test

```bash
ctest --test-dir build --output-on-failure
```

The test executables verify SQL behavior, persistent incremental B+ tree splits, stable untouched pages, WAL ordering, dynamically enumerated crash boundaries, corruption rejection, idempotent recovery, migration equivalence, maximum-width rows, CLI inspection, parser safety, and both repository batch fixtures.
Tests run in an isolated build directory, so they do not leave database files in the source tree.

To run the same AddressSanitizer and UndefinedBehaviorSanitizer gate used by CI:

```bash
cmake -S . -B build-sanitized \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DSQL_ENABLE_SANITIZERS=ON \
  -DCMAKE_COMPILE_WARNING_AS_ERROR=ON
cmake --build build-sanitized --parallel
ctest --test-dir build-sanitized --output-on-failure
```

## Run the CLI

```bash
./build/run_sql
```

Type `end` to exit and `cls` to clear the terminal.
The CLI writes each table to `<table>.sql2` and uses `<table>.sql2-wal` during a mutating statement.
It reports missing tables, invalid projections, wrong row arity, duplicate schemas, malformed filters, corrupt pages, and corrupt WAL records without terminating the session.

Example session:

```text
SQL>> create table student fields first, last, major, age
SQL>> insert into student values Ada, Lovelace, Math, 36
SQL>> insert into student values Grace, Hopper, CS, 30
SQL>> select first, major from student where major = CS
SQL>> end
```

## Query grammar

```text
create table <table> fields <field> [, <field> ...]
insert into <table> values <value> [, <value> ...]
select <* | field [, field ...]> from <table>
    [where <field> <operator> <value>
    [<and | or> <field> <operator> <value> ...]]
```

Quote values that contain spaces, such as `"Mary Ann"`.
Quoted values may contain spaces, numbers, and punctuation, but escaped double quotes are not supported.

## Version and storage inspection

```bash
./build/run_sql --version
./build/run_sql --inspect-storage student
```

Inspection validates checksums and structural bounds without applying or clearing a pending WAL.

## Migrate a v1 table

Inspect a legacy table without writing anything:

```bash
./build/sql_migrate_v1 --dry-run student
```

Migrate it to `student.sql2`:

```bash
./build/sql_migrate_v1 student
```

The migration reads the v1 `.bin` and `_fields.bin` files read-only, writes an exclusively owned temporary v2 image, reopens and compares every row, rechecks the source bytes, then atomically links the verified destination without replacement.
It never modifies or deletes the v1 source.
Use `--destination <path>` to choose another destination and note that existing destinations are rejected.

## Persistence boundaries

- Tables contain between 1 and 64 uniquely named fields.
- Identifiers contain letters, digits, and underscores and cannot start with a digit.
- Each value is at most 100 bytes, and rows may cross page boundaries.
- An insert must provide exactly one value per field.
- Values longer than 100 bytes are rejected before a write.
- Existing tables cannot be overwritten by another `CREATE TABLE` command.
- Page, index, schema, row, and WAL corruption fail closed when a table is opened.
- V1 tables can be queried read-only and must be migrated before new inserts.
- The durability scope is one process, one writer, and one atomic statement at a time.

The exact byte layout, recovery ordering, checksums, and compatibility boundary are documented in [docs/storage-v2.md](docs/storage-v2.md).

## License

This project is available under the [MIT License](LICENSE).
