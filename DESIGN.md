# Design and invariants

SQL in C++ is an educational database engine with a deliberately small grammar and checksummed paged local persistence.
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

Version 2 stores a table in one 4096-byte paged `<table>.sql2` file.
Every page has an explicit type, identifier, payload bound, next pointer, and CRC32.
Schema and row streams span checked page chains, so the maximum supported 64-field row can safely exceed one page.
Each mutating statement updates transaction-owned row and B+ tree pages, tracks a deterministic dirty-page set, and builds a complete recovery image before any database page is written.

The checksummed WAL stores that complete image and a commit footer.
The writer fsyncs the WAL before database pages, fsyncs the database, then truncates and fsyncs the WAL.
Recovery validates the whole WAL before redo and rejects incomplete or corrupt records.
Redo is idempotent and the engine never acknowledges a statement before the WAL, database, and cleared-WAL boundaries complete.

The legacy v1 fixed-width pair remains available through a read-only compatibility path and the copy-first migration tool.
Migration never modifies or deletes its v1 source.

Tables support between 1 and 64 uniquely named fields.
Table and field identifiers use letters, digits, and underscores, and cannot begin with a digit.

## Indexes and query semantics

Every field has a persistent page-backed B+ tree ordered by `(value, record_id)`.
Inserts mutate the persisted search path incrementally, preserve untouched page identifiers, split bounded leaf and internal pages, and replace roots through the index directory.
Opening and querying a v2 table validates and reads persisted index entries without inserting them into the legacy in-memory `MMap`.
Equality and range filters return record numbers, while logical expressions combine those sets with union or intersection.

Values are strings.
Ordering is lexical rather than numeric, so `"10"` sorts before `"2"`.
The current query layer loads persistent leaves into its compatibility map and is not a query-planning or logarithmic performance claim.

## Deliberate limits

The engine has atomic single-writer statements and crash recovery, but no multi-statement transactions, concurrency control, joins, updates, deletes, query optimizer, or broader SQL compatibility guarantee.
It is not designed for untrusted multi-user workloads.
The command-line process reads and writes table files in its current working directory.

## Evidence

The CTest suite covers end-to-end SQL behavior, parser safety, persistent randomized B+ tree images, page and WAL corruption, crash recovery, v1 migration equivalence, large rows, and deterministic in-memory B+ tree properties.
CI runs release builds on Linux and macOS plus an AddressSanitizer and UndefinedBehaviorSanitizer build on Linux.
The sanitizer job is a memory-safety regression gate, not evidence of production database readiness.
