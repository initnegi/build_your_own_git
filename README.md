[![progress-banner](https://backend.codecrafters.io/progress/git/92101e8e-30cd-4521-82f1-d25f6a67c165)](https://app.codecrafters.io/users/initnegi?r=2qF)

This project is an independent C++ implementation of selected Git behavior.
The idea, learning resources, and feature progression were taken from the
[CodeCrafters Build Your Own Git challenge](https://codecrafters.io/challenges/git),
but the implementation and local Windows portability work were completed in
this repository.

# Implemented stages

1. Repository initialization: create `.git`, object, refs, and `HEAD` files.
2. Object inspection: read and display compressed Git objects with `cat-file`.
3. Blob hashing: create Git-compatible blob IDs and store compressed objects.
4. Tree listing: parse tree objects and print their file and directory names.
5. Tree writing: recursively build deterministic trees and blobs from a folder.
6. Commit creation: create commits with tree, parent, author, and message data.
7. Public clone: discover refs, request a pack, decode objects and deltas.
8. Checkout: write loose objects and recursively restore the commit's files.

# Testing locally

The `your_program.sh` script is expected to operate on the `.git` folder inside
the current working directory. If you're running this inside the root of this
repository, you might end up accidentally damaging your repository's `.git`
folder.

We suggest executing `your_program.sh` in a different folder when testing
locally. For example:

```sh
mkdir -p /tmp/testing && cd /tmp/testing
/path/to/your/repo/your_program.sh init
```

To make this easier to type out, you could add a
[shell alias](https://shapeshed.com/unix-alias/):

```sh
alias mygit=/path/to/your/repo/your_program.sh

mkdir -p /tmp/testing && cd /tmp/testing
mygit init
```

# Local Windows build

On Windows, use Git Bash with an MSYS2 UCRT64 `g++` compiler and zlib installed.
`your_program.sh` builds the C++ source directly and configures the UCRT64
environment automatically. Open Git Bash and run:

```bash
cd /c/Users/Chaitanya/codecrafters-git-cpp
rm -rf /tmp/mini-git-test
mkdir -p /tmp/mini-git-test
cd /tmp/mini-git-test

/c/Users/Chaitanya/codecrafters-git-cpp/your_program.sh clone \
   https://github.com/octocat/Hello-World.git hello-world
```

The local runner also supports `init`, `cat-file`, `hash-object`, `ls-tree`,
`write-tree`, and `commit-tree`. OpenSSL and `sys/wait.h` are not required by
the local Windows build. Private troubleshooting notes are kept in the ignored
`.local-notes/` directory.
