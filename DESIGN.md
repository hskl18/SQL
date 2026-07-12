# Design and invariants

SQL in C++ is an educational database engine with a deliberately small grammar and fixed-width local persistence.
The implementation favors inspectable data structures over production database features.

## Command path

1. `STokenizer` emits lexical tokens from a command string.
2. `Parser` validates the supported grammar and builds a parse tree.
3. `SQL` validates command-level identifiers and converts failures into CLI errors.
4. `Table` validates schemas, projections, filters, and row arity before storage access.
5. Compound filters pass through `ShuntingYard` and are evaluated against per-field indexes.

Invalid syntax, unknown tables or fields, unmatched quotes or parentheses, and corrupt files fail closed.
An invalid `WHERE` expression never falls back to an unfiltered query.

## Storage format

Each field occupies one zero-padded 100-byte slot.
The schema file stores the field count followed by one slot per field name.
The data file stores each row as exactly `field_count * 100` bytes.

The format retains the repository's existing fixed-width layout.
Writes now use explicit zero-initialized buffers instead of reading beyond a string allocation.
Values longer than 100 bytes, rows with the wrong number of values, duplicate fields, partial rows, and invalid metadata are rejected before they can be treated as valid data.

Tables support between 1 and 64 uniquely named fields.
Table and field identifiers use letters, digits, and underscores, and cannot begin with a digit.

## Indexes and query semantics

Every field has an in-memory `MMap` backed by the repository's B+ tree implementation.
Indexes are rebuilt from the data file when a table is opened.
Equality and range filters return record numbers, while logical expressions combine those sets with union or intersection.

Values are strings.
Ordering is lexical rather than numeric, so `"10"` sorts before `"2"`.
The current `lower_bound` and `upper_bound` implementations scan the linked leaf sequence and are not a logarithmic query-planning claim.

## Deliberate limits

The engine has no transactions, concurrency control, write-ahead log, schema migration, joins, updates, deletes, query optimizer, or SQL compatibility guarantee.
It is not designed for untrusted multi-user workloads.
The command-line process reads and writes table files in its current working directory.

## Evidence

The CTest suite covers end-to-end SQL behavior, parser safety, and deterministic B+ tree insertion and lookup properties.
CI runs release builds on Linux and macOS plus an AddressSanitizer and UndefinedBehaviorSanitizer build on Linux.
The sanitizer job is a memory-safety regression gate, not evidence of production database readiness.
