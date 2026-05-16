# PES-VCS Lab Report — Phase 5 & 6 Analysis

**Name:** R Saurav Srrinivas 
**SRN:** PES1UG24CS352  
**Repository:** https://github.com/SauravS9/PES1UG24CS352-pes-vcs

---

## Phase 5: Branching and Checkout

### Q5.1 — Implementing `pes checkout <branch>`

**What files change in `.pes/`:**

- `HEAD` must be updated to point to the new branch: write `ref: refs/heads/<branch>` into it.
- If the branch doesn't exist yet (e.g., `pes checkout -b <branch>`), create `.pes/refs/heads/<branch>` containing the current commit hash.

**What must happen to the working directory:**

1. Read the target branch's commit hash from `.pes/refs/heads/<branch>`.
2. Parse the commit object to get its tree hash.
3. Recursively walk the target tree and compare every entry against the current HEAD's tree.
4. For each file:
   - **Present in target but not current** → write the blob content to disk (create the file).
   - **Present in current but not target** → delete the file from disk.
   - **Present in both but with different hashes** → overwrite with the target blob's content.
5. Rewrite the index to exactly match the new tree (mode, hash, mtime, size, path for every entry).
6. Write the new branch name into `HEAD`.

**What makes this operation complex:**

- **Dirty working directory:** Uncommitted local changes to tracked files could be silently overwritten. The operation must detect and refuse in this case (see Q5.2).
- **Untracked file collisions:** A file in the working directory may have the same name as a file being checked out from the target branch. Overwriting it would destroy untracked work — must detect and refuse.
- **Nested subdirectories:** Creating or removing subtrees requires recursive `mkdir -p` for new directories and removal of directories that become empty after checkout.
- **Atomicity:** A crash halfway through checkout leaves the repository in an inconsistent state (some files from the old tree, some from the new). Real Git uses a lock file and stages the pointer update last to minimize this window.
- **Index rewrite:** The index must be fully rewritten to match the checked-out tree; otherwise `pes status` will incorrectly report all files as modified.

---

### Q5.2 — Detecting "Dirty Working Directory" Conflicts

The index stores per entry: `mode`, `hash`, `mtime`, `size`, `path`.

**Algorithm:**

For every file that **differs between the current branch's tree and the target branch's tree** (i.e., files checkout would overwrite or delete):

1. Look up the file's entry in the **index**.
2. Compare the index hash to the **current HEAD's tree hash** for that file.
   - If they differ → the file is **staged** (has changes queued for commit). Refuse checkout with an error.
3. Stat the file on disk. Compare `mtime` and `size` to the index entry.
   - If either differs → the file may have been modified. Recompute its SHA-256 and compare to the index hash.
   - If hashes differ → file has **unstaged local changes**. Refuse checkout with an error.
4. If all conflict-zone files pass both checks (working directory == index == current HEAD for those paths), it is safe to proceed.

This uses only the index and the object store — no full directory scan is needed. The `mtime` + `size` shortcut avoids re-hashing files that clearly have not changed, matching the same optimization used in `pes status`.

---

### Q5.3 — Detached HEAD

**What happens when you commit in detached HEAD state:**

- `HEAD` contains a raw commit hash directly (e.g., `a1b2c3...`) instead of `ref: refs/heads/main`.
- When `head_update` runs, it writes the new commit hash directly into `HEAD`.
- The new commit object is created and stored correctly in the object store — it is structurally valid.
- **However, no branch ref points to it.** It is reachable only via `HEAD`, which is temporary.

**The problem:**  
As soon as you run `pes checkout <branch>`, `HEAD` is overwritten with `ref: refs/heads/<branch>`. The commits made in detached state are now **unreachable** — no branch, tag, or any other ref points to them. They become candidates for garbage collection.

**How to recover:**

- **Before switching away:** Run `pes branch <new-branch>` while still in detached HEAD state. This creates `.pes/refs/heads/<new-branch>` containing the current commit hash, permanently anchoring the commits to a branch.
- **After switching (if the hash is known):** If the commit hash was noted before switching, run `pes branch <new-branch> <hash>` to create a new branch pointing at it.
- In real Git, the **reflog** (`git reflog`) records every position HEAD has ever pointed to and provides a 30-day grace period for recovery. PES-VCS does not implement a reflog, so recovery depends entirely on remembering the commit hash.

---

## Phase 6: Garbage Collection and Space Reclamation

### Q6.1 — Algorithm to Find and Delete Unreachable Objects

**Goal:** Delete any object in `.pes/objects/` that is not reachable from any branch tip.

**Algorithm (Mark and Sweep):**

**Mark phase:**

1. Collect all branch tips: read every file in `.pes/refs/heads/` to get a set of commit hashes.
2. Initialize a **hash set** (keyed by the 64-character hex hash string) to track reachable objects.
3. For each branch tip commit, perform a BFS/DFS:
   - Mark the commit hash as reachable.
   - Parse the commit object → extract its `tree` hash and `parent` hash(es).
   - Recursively process the tree: parse each entry, mark the child blob/tree hash, recurse into subtrees.
   - Follow the `parent` pointer and repeat until reaching the initial commit (no parent).
4. Any object hash encountered during traversal is added to the reachable set. If a hash is already in the set, skip it — this is the key deduplication step that prevents redundant work on shared history.

**Sweep phase:**

5. Walk the entire `.pes/objects/` directory (iterate 2-char shard directories, then object files within each).
6. For each object file, reconstruct its hash from the path (`shard-dir + filename = full 64-char hex hash`).
7. If the hash is **not** in the reachable set → delete the file.
8. After deletion, remove any shard directories that are now empty.

**Data structure:** A hash set (e.g., a hash table with open addressing or chaining). Average-case O(1) insert and lookup, O(n) total for n objects.

**Estimate for 100,000 commits, 50 branches:**

- Each commit has 1 commit object + 1 root tree object + approximately 20 blob/subtree objects (assuming a moderately sized project with a few directory levels).
- Total objects ≈ 100,000 × 22 ≈ **2.2 million objects**.
- The mark phase visits each reachable object once (shared objects are skipped after the first encounter), so it processes approximately 2.2 million objects.
- The sweep phase walks all 2.2 million file paths in the object store.
- **Total: approximately 4–5 million operations** — feasible in a few seconds on modern hardware.

---

### Q6.2 — Race Condition Between GC and Concurrent Commit

**The race condition:**

| Time | GC process | Commit process |
|------|-----------|---------------|
| T1 | Starts mark phase; reads all branch tip hashes | — |
| T2 | Completes mark phase; reachable set is finalized | — |
| T3 | — | Writes a **new blob** to the object store (hash = `X`) |
| T4 | — | Writes a **new tree** referencing blob `X` |
| T5 | Starts sweep phase | — |
| T6 | Encounters blob `X` — **not in the reachable set** (mark ended at T2) → **deletes it** | — |
| T7 | — | Writes commit object referencing the tree (which references the now-deleted blob `X`) |
| T8 | — | Updates HEAD → commit is published |

The repository now has a committed snapshot that references a deleted blob. `pes log` works fine, but reading that file from history silently fails or errors — the object is gone.

**How Git's real GC avoids this:**

1. **Grace period (primary defence):** Git's GC never deletes objects younger than 2 weeks by default (configurable via `gc.pruneExpire`). The new blob written at T3 is brand-new, so it survives the sweep regardless of reachability. This alone eliminates the race in practice.

2. **Bottom-up write ordering in commit:** Git always writes objects in dependency order — blobs first, then trees that reference them, then the commit. The ref update (which makes the commit reachable) is the very last step. GC's mark phase starts from refs, so any object written before its ref update is "invisible" to GC's reachability scan. The grace period covers the window between "object written" and "ref updated."

3. **Lock file:** `git gc` acquires a lock (`.git/gc.pid`) before starting. This prevents two GC processes from running simultaneously and causing double-deletion or inconsistent sweeps.

4. **Reflog as additional roots:** Branch reflogs also serve as GC roots during the mark phase. This preserves recent commits that may no longer be pointed to by any branch, giving users time to recover from accidental branch deletion.

---

*End of Phase 5 & 6 Analysis*
