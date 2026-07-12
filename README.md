# SQL in C++

SQL in C++ is an educational relational database engine and command-line DSL implemented from first principles.
It parses a focused subset of SQL, persists records to binary files, indexes field values with B+ trees, and evaluates compound filters with a shunting-yard pipeline.

![Command-line preview](image/preview.png)

## What it supports

- `CREATE TABLE` with an ordered field list.
- `INSERT INTO` with quoted or unquoted values.
- `SELECT *` and projected field lists.
- `WHERE` filters using `=`, `!=`, `<`, `<=`, `>`, and `>=`.
- Compound `AND` and `OR` expressions with parentheses.
- Binary persistence across separate CLI sessions.
- Batch execution from the sample command files in `batch/`.

This project is intentionally smaller than a production SQL database.
It does not implement joins, transactions, schemas, query planning, updates, or deletes.
Values are stored and ordered as strings, so numeric-looking values use lexical comparison semantics.

## Architecture

| Component | Responsibility |
| --- | --- |
| `includes/tokenizer` | Converts command text into lexical tokens with a state machine. |
| `includes/parser` | Validates the supported grammar and builds a parse tree. |
| `includes/shunting_yard` | Converts compound filter expressions to postfix form. |
| `includes/map` | Implements B+ tree-backed maps and multimaps. |
| `includes/binary_files` | Reads and writes fixed-format binary records. |
| `includes/table` | Owns table persistence, indexes, projections, and selections. |
| `includes/sql` | Coordinates parsing and table operations through the public `SQL` interface. |

Additional implementation notes are available in [design.txt](design.txt).
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

The test executable verifies inserts, projections, compound filters, persistence, invalid input handling, and both repository batch fixtures.
Tests run in an isolated build directory, so they do not leave database files in the source tree.

## Run the CLI

```bash
./build/run_sql
```

Type `end` to exit and `cls` to clear the terminal.
The CLI writes each table to `<table>.bin` and `<table>_fields.bin` in the current working directory.

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

## License

This project is available under the [MIT License](LICENSE).
