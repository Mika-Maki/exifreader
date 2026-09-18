# Security Policy

## Supported versions

| Version | Supported |
|---------|-----------|
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x: |

## Reporting a vulnerability

Please **do not** open a public issue for a security problem.

Report it privately through GitHub's
[private security advisory](https://github.com/OWNER/exifreader/security/advisories/new)
form, or email **security@example.com**.

Include, where you can:

* the version (`exifreader --version`) and the platform/compiler,
* the command line and the output (including the exit code),
* a minimal or reduced input file that triggers the problem,
* whether the issue is a crash, a hang, an out-of-bounds read/write, an
  information leak, or an output-injection problem.

Please strip real metadata from any sample you attach: EXIF frequently carries
GPS coordinates, serial numbers and device identifiers.

## What to expect

| Stage | Target |
|-------|--------|
| Acknowledgement | within 3 business days |
| Initial assessment and severity | within 7 days |
| Fix or mitigation for high severity | within 30 days |
| Public advisory and credit | coordinated with you after a fix ships |

We will credit reporters in the advisory and the changelog unless you ask us
not to.

## Threat model

exifreader parses **untrusted** files. A hostile image is the expected input,
not an edge case, so the security requirements are:

* **No memory unsafety on malformed input.** Every access to the file buffer is
  bounds checked; unsigned offset arithmetic saturates instead of wrapping;
  directory entry counts are validated against the remaining file size before
  being used; out-of-range and negative payload pointers (including negative
  `SLONG`/`SRATIONAL` thumbnail offsets) are rejected rather than wrapped.
* **No unbounded work.** Directory cycles are detected, IFD recursion is capped
  (`kMaxIfdDepth`, `--max-depth` clamps to 256), per-directory entry counts are
  capped, and the total number of visited directories is budgeted.
* **No output injection.** Control characters and invalid UTF-8 in tag values
  are escaped in the tree/tags/summary modes and replaced with U+FFFD in JSON,
  so a crafted payload cannot forge lines or emit terminal escape sequences.
* **Valid JSON always.** Non-finite floats are emitted as the strings `"nan"`,
  `"inf"` and `"-inf"`, never as bare tokens.

The project is expected to build and pass its suite under ASan and UBSan (the
CI configuration does both), and to survive coverage-guided fuzzing of the
parsers. A report that only reproduces under a sanitizer is still a valid
security report.

Out of scope: denial of service caused solely by passing an enormous
*well-formed* file, and problems in data the tool deliberately prints verbatim
(for example thumbnail bytes written by `--extract`).
