# Build Your Own Git: Project Summary

## 1. Project Overview

This project is a miniature Git implementation developed for the CodeCrafters **Build Your Own Git** challenge. The implementation recreates the essential internal behavior of Git rather than calling the system Git executable. It supports repository initialization, Git object creation and inspection, tree construction, commit creation, and cloning a public GitHub repository.

The implementation is intentionally centered in one source file, [`src/main.cpp`](src/main.cpp), because all challenge commands are routed through the same executable. The project is built as a C++23 application named `git`. It uses:

- An embedded **SHA-1** implementation for Git object hashing.
- **zlib** for Git object compression and packfile decompression.
- **C++ filesystem APIs** for repository and working-tree manipulation.
- **curl**, invoked from C++, for HTTP communication with GitHub.

The executable is compiled and launched through [`your_program.sh`](your_program.sh).

## 2. Git's Object Model

Git stores repository data as objects inside `.git/objects`. Every object contains a header followed by its content:

```text
<type> <content-size>\0<content>
```

For example, a blob containing `hello` is represented as:

```text
blob 5\0hello
```

The complete header and content are hashed together using SHA-1. A 40-character hexadecimal object ID is produced. Git stores the object using the first two characters as a directory name and the remaining 38 characters as the filename:

```text
.git/objects/ab/cdef...
```

Before storage, the complete object is compressed with zlib. The shared `sha1Hex()` and `writeGitObject()` functions implement this format for fetched and locally-created objects.

This creates the central Git relationship:

```text
commit -> tree -> tree/blob objects
```

A commit identifies a root tree. A tree represents a directory and points to blobs or other trees. A blob contains the contents of a file.

## 3. Implemented Local Git Commands

### `init`

The `init` command creates the minimum repository structure:

- `.git`
- `.git/objects`
- `.git/refs`
- `.git/HEAD`

The `HEAD` file is initialized with:

```text
ref: refs/heads/main
```

This establishes `main` as the current branch.

### `hash-object`

This command reads a file as binary data, creates a `blob` header, calculates the SHA-1 of the complete Git object, compresses it, stores it in `.git/objects`, and prints the object ID. It demonstrates the complete lifecycle of a Git blob.

### `cat-file`

This command locates an object from its SHA-1, reads the zlib-compressed file, inflates it, removes the Git object header, and prints the original content.

### `ls-tree`

Tree objects contain entries in this binary format:

```text
<mode> <name>\0<20-byte binary SHA-1>
```

The implementation decompresses a tree, parses each entry, skips the binary SHA-1 reference, and prints the entry names.

### `write-tree`

The command recursively scans the working directory while skipping `.git`. Regular files are converted into blob objects. Directories are recursively converted into tree objects. Tree entries are sorted by filename to ensure deterministic object contents and therefore deterministic SHA-1 values.

The implementation uses modes such as:

- `100644` for ordinary files.
- `40000` for directories.

Each generated tree is serialized, hashed, compressed, and stored as a Git object.

### `commit-tree`

The command creates a commit object containing:

- The root tree SHA-1.
- An optional parent commit SHA-1.
- Author identity and timestamp.
- Committer identity and timestamp.
- The commit message.

The commit is serialized, hashed, compressed, stored, and its SHA-1 is printed. This connects the locally-created tree to a versioned commit history.

## 4. Smart HTTP Clone Implementation

The final CodeCrafters stage required cloning a public GitHub repository. The implementation communicates directly with GitHub using Git's **Smart HTTP transfer protocol** instead of relying on the installed Git client.

### Reference Discovery

The first request is sent to:

```text
<repository-url>/info/refs?service=git-upload-pack
```

The server responds with advertised references using Git's pkt-line format. Each packet starts with a four-character hexadecimal length. `parsePktLines()` reads the packet length, handles flush and delimiter packets, validates packet boundaries, and extracts packet payloads.

`getHeadSha()` searches the advertised references for `HEAD`. If that is unavailable, it falls back to `refs/heads/main` and then `refs/heads/master`. The result is the SHA-1 of the commit that should be cloned.

### Pack Request

The client sends a POST request to:

```text
<repository-url>/git-upload-pack
```

The request contains pkt-lines requesting the discovered commit:

```text
want <commit-sha>
0000
done
```

The request is written to a temporary file and passed to curl using `--data-binary`. Curl is configured with the Git upload-pack request and response content types.

The response may contain protocol messages such as `NAK` before the binary packfile. The implementation therefore searches for the `PACK` signature and begins binary parsing at that location.

## 5. Packfile Parsing

A `PackParser` owns the raw pack data and a current byte position. It provides bounds-checked `readByte()` and `readBytes()` operations so malformed or truncated input produces a controlled error.

`parsePackHeader()` validates:

1. The four-byte `PACK` signature.
2. The packfile version, supporting versions 2 and 3.
3. The big-endian object count.

Each pack object has a variable-length header. The header contains the object type and size. `readPackSize()` decodes the size using Git's continuation-bit encoding, where the high bit indicates that another size byte follows.

The compressed object body is decompressed by `inflatePackObject()` using zlib's streaming interface. Normal Git object types are mapped as follows:

- Type 1: commit
- Type 2: tree
- Type 3: blob
- Type 4: tag

## 6. Delta Object Resolution

Git packfiles commonly avoid storing complete copies of similar objects. Instead, they store delta instructions relative to another object. The implementation supports both standard delta forms:

- **OFS-delta:** references a previous object using a backward byte offset inside the packfile.
- **REF-delta:** references a previous object using its 20-byte SHA-1.

The decoder indexes already-resolved objects by pack offset and SHA-1. Delta metadata is read before the compressed delta instruction stream is inflated. This ordering is required by the Git pack format.

`applyDelta()` reads:

1. The expected base-object size.
2. The expected resulting-object size.
3. A sequence of delta instructions.

A copy instruction selects a byte range from the base object. An insert instruction provides literal bytes. The function checks base size, copy bounds, invalid zero instructions, and final result size. Once applied, the delta becomes a normal complete commit, tree, or blob object.

## 7. Converting Pack Objects to Loose Objects

After all pack objects are resolved, each object is reconstructed into the normal Git representation:

```text
<type> <content-size>\0<content>
```

The implementation calculates the object SHA-1 and writes the compressed object under the target repository's `.git/objects` directory. Converting pack contents into loose objects makes the cloned repository compatible with the already-implemented `cat-file`, tree, and object-reading logic. It also makes the repository directly inspectable by the CodeCrafters tester.

## 8. Checkout of the Cloned Repository

After writing the objects, `cloneRepository()` reads the fetched commit object and extracts its `tree` line. `checkoutTree()` then recursively walks the tree structure.

For each tree entry:

- A directory mode creates a directory and triggers recursive traversal.
- A blob is read from `.git/objects` and written to the corresponding working-tree path.
- Executable modes such as `100755` receive an executable permission where supported.

The clone process creates:

```text
<target>/.git/HEAD
<target>/.git/refs/heads/main
<target>/.git/objects/...
<target>/<checked-out files>
```

The branch reference points to the advertised tip commit, allowing the tester to inspect commit metadata and the checked-out working tree.

## 9. Error Handling and Data Validation

The implementation uses exceptions for malformed protocol data, invalid pack signatures, unsupported pack versions, truncated compressed objects, missing delta bases, invalid delta instructions, object-size mismatches, missing Git objects, and failed curl commands. Temporary upload-pack request files are removed after the HTTP request completes, including failure paths.

The program reports clone failures through the `clone` command's exception handler and returns a failure status rather than silently producing an incomplete repository.

## 10. Local Checkpoints and Decision History

The design decisions are recorded in [`decision.md`](decision.md). Local Git commits were used to preserve rollback points:

- `9ede237` - checkpoint before clone-stage implementation.
- `56841fa` - Smart HTTP clone, pack decoding, object storage, and checkout implementation.
- `27072b4` - documented the implementation checkpoint in `decision.md`.
- `9fbbc34` - later CodeCrafters submission checkpoint.

These commits provide a recoverable history of the implementation. A previous version can be inspected or restored by referring to its commit ID.

## 11. Resume-Ready Technical Points

The strongest resume points from this project are:

- Built a miniature Git implementation in C++ using Git's content-addressable object model.
- Implemented SHA-1 hashing, zlib compression, loose-object storage, blob/tree/commit creation, and object inspection.
- Implemented recursive tree serialization with deterministic ordering and Git-compatible object formats.
- Implemented Git Smart HTTP reference discovery using pkt-line parsing.
- Implemented upload-pack negotiation and binary packfile extraction.
- Implemented Git packfile parsing for versions 2 and 3.
- Implemented variable-length object-size decoding.
- Implemented OFS-delta and REF-delta resolution with copy/insert delta instructions.
- Converted reconstructed pack objects into standard loose objects.
- Implemented recursive checkout from commit to tree to blob contents.
- Added rollback checkpoints and documented architectural decisions.

A concise project description would be:

> Developed a miniature Git client in C++ that implements Git object storage, SHA-1 addressing, tree and commit creation, Smart HTTP repository discovery, upload-pack negotiation, packfile decompression, OFS/REF delta resolution, loose-object reconstruction, and recursive working-tree checkout.

## 12. Validation Status

The source compiles locally with MSYS2 UCRT64 `g++` and zlib. The local runner
passes shell syntax validation, `hash-object` output matches Git, and a live
public GitHub clone has been completed successfully. OpenSSL and `sys/wait.h`
are not required by the Windows build; CMake remains available for the
CodeCrafters configuration, while `your_program.sh` uses the direct local
`g++` workflow.
