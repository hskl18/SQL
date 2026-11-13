# Using SQL in C++

This page holds the details behind the [README](../README.md): the full grammar, the CLI's files and errors, storage inspection, v1 migration, persistence limits, and the sanitizer build.

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

## Sanitizer build

To run the tests under AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -S . -B build-sanitized \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DSQL_ENABLE_SANITIZERS=ON \
  -DCMAKE_COMPILE_WARNING_AS_ERROR=ON
cmake --build build-sanitized --parallel
ctest --test-dir build-sanitized --output-on-failure
```
