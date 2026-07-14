# Storage format v2

## Scope

Storage format v2 is an inspectable, checksummed, paged format for the existing SQL in C++ command set.
It provides atomic single-statement `CREATE TABLE` and `INSERT INTO` persistence for one process and one writer.
It does not provide multi-process locking, concurrent transactions, rollback, update, delete, or broader SQL compatibility.

The v1 fixed-width files remain readable by the migration tool and are never modified by migration.
The SQL runtime opens a v2 table when `<table>.sql2` exists.
It otherwise opens a v1 table read-only through the compatibility path.

## Integer encoding and limits

All multi-byte integers are unsigned and encoded in little-endian byte order.
The page size is 4096 bytes.
Page identifiers are unsigned 32-bit integers.
Record identifiers and generations are unsigned 64-bit integers.
Strings are byte strings with an unsigned 16-bit length and a maximum length of 100 bytes.
Tables contain between 1 and 64 fields.

## Page envelope

Every page begins with a 32-byte header.

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 4 | ASCII magic `SQL2` |
| 4 | 1 | Page type |
| 5 | 1 | Flags, currently zero |
| 6 | 2 | Format version, currently 2 |
| 8 | 4 | Page identifier |
| 12 | 4 | Payload byte count |
| 16 | 4 | Next page or `0xffffffff` |
| 20 | 4 | CRC32 of the full page with this field zeroed |
| 24 | 8 | Reserved, must be zero |

Readers reject a wrong magic, version, page identifier, reserved value, payload bound, page type, or checksum.
Reads must be page-aligned and bounded by the page count in the superblock and by the physical file size.
The numeric page types are `1` for the superblock, `2` for schema, `3` for rows, `4` for the index directory, `5` for an index leaf, and `6` for an index internal node.
Unused payload bytes through byte 4095 are zero.
The physical database size must equal `superblock.page_count * 4096` with no partial or trailing pages.

The checksum is reflected IEEE CRC32.
It uses polynomial `0xedb88320`, initial value `0xffffffff`, and final XOR `0xffffffff`.
The checksum covers all 4096 page bytes after bytes 20 through 23 are set to zero.

## Superblock

Page zero has type `superblock`.
Its payload contains the database generation, page size, total page count, schema chain head, row chain head, index-directory chain head, free-page head, field count, and row count.
The free-page head is `0xffffffff` when no reusable page exists.
Page allocation is deterministic and append-only.
The initial image allocates schema, row, index-directory, leaf, and internal pages in that order.
Incremental inserts retain existing page identifiers and append only overflow row pages and split index pages.
Because v2 has no update or delete command, no live page becomes free and the free list remains empty.

The WAL carries a complete target image for simple idempotent recovery, while normal statement application writes only the buffer's dirty page set.
This makes buffer ownership, flush ordering, crash recovery, and corruption behavior directly inspectable without claiming a production cache or eviction policy.

The superblock payload begins at physical byte 32 and is exactly 44 bytes.

| Payload offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 8 | Generation |
| 8 | 4 | Page size, exactly 4096 |
| 12 | 4 | Physical page count |
| 16 | 4 | Schema chain head |
| 20 | 4 | Row chain head |
| 24 | 4 | Index-directory chain head |
| 28 | 4 | Free-page head, currently `0xffffffff` |
| 32 | 4 | Field count |
| 36 | 8 | Row count |

The in-memory transaction buffer owns the target image and an explicit ordered dirty-page set until the WAL record is durable.
No database page is written from that buffer before WAL fsync.
The crash suite observes the database bytes at the durable-WAL boundary to verify this no-steal flush behavior.

## Chained payload pages

Schema, row, and index-directory values are encoded as logical byte streams and split across chained pages.
Each chain is acyclic, terminates with `0xffffffff`, and may contain only its declared page type.
Readers reject cycles, out-of-range links, duplicate page ownership, missing pages, and trailing or truncated logical records.

The schema stream contains the field count followed by length-prefixed field names.
The row stream contains the row count followed by each row's field count and length-prefixed values.
Rows may cross page boundaries, including a 64-field row containing 100-byte values.

The index directory stores one root page identifier per field.
An empty field index uses `0xffffffff` as its root.

The schema stream encoding is `u32 field_count`, followed by exactly `field_count` values encoded as `u16 byte_length` and that many bytes.
The row stream encoding is `u64 row_count`, followed for each row by `u32 field_count`, then exactly that many `u16 byte_length` and byte-string values.
Each sequential row encoding is a logical record slot whose zero-based position is its stable record identifier.
One logical slot may span chained row pages, so the 64-field maximum row does not depend on fitting inside one physical page.
The index-directory stream encoding is `u32 field_count`, followed by exactly that many `u32 root_page_id` values.
Any missing or trailing logical bytes are corruption.

## Persistent B+ tree pages

Every indexed field has an independent B+ tree.
Leaf entries are ordered by `(key, record_id)` and contain a length-prefixed key plus a record identifier.
Duplicate keys therefore remain individually addressable and deterministic.
Leaf pages store their previous sibling in the payload and their next sibling in the common page header.

Internal pages store their tree level, a first child, and ordered composite-separator and right-child pairs.
A separator is the smallest `(key, record_id)` entry reachable through its right child.
The root page identifier is stored in the index directory.
Initial construction fills bounded leaf pages, links siblings, then repeatedly creates bounded parent levels until one root remains.
An insert descends through persisted internal pages, modifies one leaf, splits an overflowing leaf by encoded bytes, repairs adjacent sibling links, propagates composite separators, splits overflowing internal pages, and replaces the root when required.
Untouched page identifiers and bytes remain stable.

An index-leaf payload starts with `u16 key_count`, `u32 previous_leaf`, and a zero `u16 reserved` value.
Each leaf entry is `u16 key_byte_length`, the key bytes, and `u64 record_id`.
The next-leaf identifier is the common page header's next field.

An index-internal payload starts with `u16 level`, `u16 key_count`, and `u32 first_child`.
It then contains `key_count` entries encoded as `u16 separator_byte_length`, separator bytes, `u64 separator_record_id`, and `u32 right_child`.
All children of an internal page have level exactly one less than their parent.
Separators may compare equal when duplicate values span leaves, but child ranges may never move backward.

On open, the runtime validates key counts, entry bounds, ordering, child identifiers, separator ranges, levels, sibling symmetry, leaf-chain completeness, and the exact set of record identifiers.
Queries load the persisted leaf entries and do not rebuild indexes from table rows.

## Write-ahead log

The WAL path is `<table>.sql2-wal`.
A WAL transaction is one complete database after-image with this structure:

1. A fixed header containing magic `SQL2WAL`, WAL version 1, image byte count, target generation, and an image CRC32.
2. The complete page-aligned database image.
3. A commit footer containing magic `COMMIT2` and a CRC32 of the header and image.

The WAL header is exactly 40 bytes.

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 8 | `SQL2WAL` plus a zero byte |
| 8 | 4 | WAL version, exactly 1 |
| 12 | 4 | Header size, exactly 40 |
| 16 | 8 | Image byte count |
| 24 | 8 | Target database generation |
| 32 | 4 | Image CRC32 |
| 36 | 4 | Reserved, exactly zero |

The WAL footer is exactly 16 bytes at offset `40 + image_byte_count`.
It contains `COMMIT2` plus a zero byte, a CRC32 of the complete header and image, and a zero `u32` reserved value.
The database image must be non-empty and page aligned, and every contained page must validate before redo begins.

The writer performs these boundaries in order:

1. Truncate and write the complete WAL record.
2. Flush the stream and fsync the WAL file.
3. Fsync the parent directory if the WAL was newly created.
4. Extend the database to the target length when pages were appended and write dirty pages in page-id order.
5. Flush the stream and fsync the database file.
6. Truncate and fsync the WAL file.

The statement is acknowledged only after step 6.
Deterministic crash injection is available at the partial-WAL, WAL-write, WAL-fsync, per-page, data-fsync, WAL-truncate, and WAL-clear boundaries.
The crash harness uses abrupt process termination and proves recovery logic and write ordering with the kernel page cache intact.
It does not simulate physical media failure or loss of the operating system page cache, so power-loss durability rests on the documented fsync and directory-fsync contract plus Linux and macOS CI behavior.

## Recovery

An empty or absent WAL needs no recovery.
A structurally complete committed WAL with valid checksums is redone by writing every page in page-id order, fsyncing the database, and clearing the WAL.
Redo is idempotent because the WAL contains the complete target image and generation.
A WAL target generation lower than a separately valid database generation is rejected so recovery cannot roll a database backward.
A WAL generation equal to or higher than the valid database generation is safe to redo.
A torn database image is replaced from a fully validated committed WAL without trusting its damaged superblock.

An incomplete WAL record, invalid commit footer, invalid WAL checksum, non-page-aligned image, or corrupt contained page fails closed with an explicit corruption error.
Recovery never interprets an incomplete record as committed and never silently discards it.

## Migration and compatibility

`sql_migrate_v1` accepts a v1 table name and supports dry-run inspection.
Migration validates both v1 files, reads empty strings and exact 100-byte values, writes a v2 image to an exclusively created temporary destination, reopens it, compares every field and row, and rechecks the source bytes.
It then creates the final destination with a same-filesystem hard link that fails if the destination exists, fsyncs the directory, removes the temporary link, and fsyncs the directory again.
The source `.bin` and `_fields.bin` files are never opened for writing, renamed, truncated, or deleted.

The v2 runtime prefers `<table>.sql2` when both formats exist.
The migration tool refuses to replace an existing destination.
There is no in-place upgrade and no downgrade writer.

## Inspection

`run_sql --version` reports the CLI and storage format versions.
`run_sql --inspect-storage <table>` reports the format, generation, page count, row count, field count, persistent index roots, and WAL state without changing the table.
Inspection applies the same bounded-read and checksum validation as a normal open.
When a valid committed WAL is pending, inspection validates and reports the target image from the WAL without applying it or clearing the WAL.

## Durability boundary

The fsync implementation uses the native POSIX file descriptor on Linux and macOS.
Directory fsync is performed after first database creation and migration promotion so the directory entry is durable.
The format makes no durability claim for unsupported filesystems or platforms where these primitives are unavailable.
