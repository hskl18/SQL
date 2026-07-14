# Version 2.0.0 release checklist

This checklist prepares the repository for a later release without creating a tag or GitHub Release.

- [ ] Confirm the CMake project version is `2.0.0`.
- [ ] Confirm `run_sql --version` reports CLI `2.0.0` and storage format `2`.
- [ ] Run the complete Release CTest suite on Linux and macOS.
- [ ] Run AddressSanitizer and UndefinedBehaviorSanitizer with warnings as errors.
- [ ] Migrate the canonical v1 fixtures and compare schemas, rows, and indexed range-query results.
- [ ] Run every deterministic write and recovery crash point.
- [ ] Run superblock, page, WAL, child-pointer, and partial-record corruption tests.
- [ ] Confirm persistent indexes reopen without a row-derived rebuild.
- [ ] Confirm the v1 source files remain byte-identical after migration.
- [ ] Review the documented single-process and single-writer compatibility boundary.
- [ ] Review the pull request CI evidence before a maintainer creates `v2.0.0`.
