# Decisions

- Keep the existing single-file architecture: the challenge already routes all commands through `src/main.cpp`, so the clone implementation stays local and avoids a broad refactor.
- Preserve the current implementation first: commit `9ede237` is the pre-clone rollback checkpoint.
- Use Git Smart HTTP directly with `info/refs` and `git-upload-pack`: this matches the protocol required by the final CodeCrafters stage and avoids depending on the installed Git CLI.
- Extract the raw `PACK` stream from the upload-pack response: the server may prefix it with pkt-lines such as `NAK`.
- Support normal, OFS-delta, and REF-delta pack objects: public repositories commonly use deltas, and handling only ordinary objects would fail on real repositories.
- Store fetched objects as standard loose objects: existing commands already understand that format, and it makes the cloned `.git` inspectable by the tester.
- Checkout the advertised tip tree recursively: the tester verifies working-tree contents as well as commit metadata.
- Record meaningful versions with local Git commits: each implementation checkpoint gets a commit ID so it can be restored deliberately without rewriting history.
