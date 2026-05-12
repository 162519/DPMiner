# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
# Build (produces bin/miner)
make

# Run on 3 nodes (reads hosts.txt for hostnames)
mpiexec -np 3 -hostfile hosts.txt ./bin/miner hash <GraphPrefix> <PatternV> <PatternE> <precache_ratio>
# precache_ratio: 0.0~1.0, proportion of high-degree remote vertices to pre-cache (0 = disabled)

# Debug with gdbserver (one process per node)
bash debug_mpi.sh
```

- Compiler: `mpic++` with `-pthread -g -std=c++17 -ltbb`
- Static links `libstdc++` and `libstdgcc`, hardcodes rpath to GCC 7
- Requires `MPI_THREAD_MULTIPLE` support
- `WORKER_NUM` is hardcoded to 3 in `util/global.h` — must match `-np`

## Architecture

This is a **distributed graph pattern mining** system (PMiner) using MPI + Intel TBB. Each MPI process loads a partition of the data graph, then collaboratively mines all embeddings of a given query pattern.

### Core classes

| File | Class | Role |
|------|-------|------|
| `src/Graph.h` | `Graph` | Data graph: local vertices/edges + lazily cached remote neighbors (`tbb::concurrent_hash_map`). Supports hash-based and BDG-based partitioning. |
| `src/Pattern.h` | `Pattern` | Query pattern: pre-computes symmetry groups, center-order traversal, equivalent groups per level, and execution schedules (`min_schedule`/`int_schedule`/`equivalent_group_schedule_final`). |
| `src/PMiner.h` | `PMiner` | Mining engine. Owns the pipeline, task dispatch, and all pattern-matching logic (`searchPG`, `extendEdgePattern_new`, etc.). |
| `src/Request.h` | `Request` | MPI request handler: listen thread receives remote vertex requests, worker threads assemble and send neighbor data back. |
| `src/Response.h` | `Response` | MPI response handler: listen thread receives neighbor data, worker threads update local `Graph` and `ConcurrentBitMap`. |
| `src/Task.h` | `Task` | Unit of work: `CowSnapshot*` + `TravSet` + `centerIdx`. |
| `src/snapshot.h` | `CowSnapshot` / `RowSlice` | Copy-on-Write snapshot of candidate sets. `RowSlice` wraps a `shared_ptr<unsigned>` for cheap sharing of unchanged rows. |
| `src/OptmConcurrentBitMap.h` | `ConcurrentBitMap` | Tracks per-worker vertex ownership (LOCAL/REMOTE/INTEGRAL states). |
| `src/Cache.h` | `Cache` | Thread-safe bounded cache for frequently-accessed remote vertex IDs. |

### Global state (`util/global.h`)

Shared pointers initialized at startup: `g` (Graph), `bitmap` (ConcurrentBitMap), `cache`, `levelQueues` (multi-level TBB concurrent queues). The `PrefetchBatch` class groups tasks for batch remote-data prefetching. Request/response communication uses `sendReq()` and `getRemoteData()`.

### Pipeline flow (4-stage ring buffer in `searchALLPR`)

1. **Main thread** initializes level-0 tasks from local vertices matching the pattern's minMatchID
2. **Prefetch thread** collects batches from level queues (deepest-first), extracts remote vertex IDs from candidate rows, sends MPI requests, and marks the batch ready once all requested data arrives
3. **Main thread** consumes ready batches, dispatches each task to TBB `task_group`
4. Each `searchPG()` extends the current pattern level: for intermediate levels it creates new tasks pushed to deeper queues; for the final level it counts valid embeddings via set-operation combinators

### Pattern matching internals

`extendEdgePattern_new()` at each center level:
- Shares unchanged rows from the parent snapshot via `RowSlice` (no copy)
- Processes `min_schedule` (edge extension with degree filtering), `int_schedule` (sorted intersection of neighbor list with existing candidate set), and `equivalent_group_schedule_final` (copy results between structurally equivalent pattern vertices)

Final counting (`count_set_from_rows`) uses optimized closed-form set operations for 1-5 remaining rows, falling back to recursive full permutation for larger counts.

### Profiling

`util/profiler.h` provides per-thread activity timing (Active/Idle/Wait) and per-phase cumulative timers. Wrapped in `ScopedTimer`/`ScopedActive`/`ScopedWait` RAII guards. Report printed at end via `Profiler::instance().report()`.

## Input format

- **Graph vertex file** (per rank): `<max_id>\n<vid> <nodeid> [vlabel...]\n...`
- **Graph edge file** (per rank): `<src> <dst> [elabel...]\n...`
- **Pattern vertex file**: `<vid> [vlabel...]\n...`
- **Pattern edge file**: `<src> <dst> [elabel...]\n...`

## Key constants

- `WORKER_NUM = 3` at `util/global.h:213` — must match MPI `-np`
- `PREFETCH_BATCH_SIZE = 512` at `util/global.h:65`
- `PIPELINE_DEPTH = 4` at `src/PMiner.h:43`
- MPI tags: `REQUEST_MSG=100`, `RESPONSE_MSG=101`, `STATUS_SYNC_CHANNEL=102`, `tb_msg=103`
