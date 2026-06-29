# Proposal: Traversal Primitives for the neo4j-memory MCP Server

**Status:** proposal / spec — requires changes to the MCP server, not this repo.
**Server:** `neo4j-memory` MCP at `http://localhost:8000/mcp/` (HTTP transport).
**Context:** see the independent review in `~/.claude/plans/handoff-independent-review-*.md`
and the memory node `Memory System -- Graph Traversal`.

## Why

The model fails to traverse the graph multi-hop. A large part of that is a tool-surface problem:
the only traversal primitive, `find_memories_by_name`, returns exactly **1 hop**, so multi-hop
requires the model to hand-execute a BFS across many sequential calls — a procedure LLMs run
unreliably. Meanwhile `read_graph` dumps the **entire** store as text, which the model reads
instead of walking. The backing store is **Neo4j**, which does depth-N neighborhoods,
shortest-path, and pattern matching natively — but the MCP surface exposes none of it.

**Design decision (wrapper vs. "Neo4j directly").** MCP is the model's only channel to the graph;
it is not an alternative to Neo4j. The fix is to **extend the existing memory wrapper** with a few
*intent-named* tools, each of which runs the right Cypher internally. This keeps MCP intact and is
non-breaking. We deliberately do **not** make raw Cypher the model's primary surface: authoring
correct Cypher is the same procedure-execution weakness that causes the failure, in a different
costume. Raw Cypher remains only as a guarded, read-only escape hatch (C5). Genuine direct-Neo4j
access stays reserved for offline/batch tooling (e.g. the Dream Engine consolidator).

> Principle: **let Neo4j traverse; let the model reason over a right-sized subgraph.** Push the
> graph procedure into the engine that is good at it; hand the model intent, not iteration.

---

## C1 — `get_neighbors` (the core multi-hop fix)

Depth-parameterized neighborhood retrieval. One call returns the relevant n-hop subgraph.

```
get_neighbors(
  names: [string],            # seed entities
  depth: int = 1,             # hops (cap server-side, e.g. <= 4)
  edge_types: [string]? ,     # optional filter, e.g. ["HAS_PART","PREFERS"]
  direction: "out"|"in"|"both" = "both",
  include_observations: bool = false   # false => map-only nodes (names+types+edges)
) -> { entities:[{name,type,observations?}], relations:[{source,target,relationType}] }
```

Cypher sketch (APOC for parameterized depth + type filter):

```cypher
MATCH (s) WHERE s.name IN $names
CALL apoc.path.subgraphAll(s, {
  maxLevel: $depth,
  relationshipFilter: $edgeTypeFilter   // e.g. "HAS_PART>|PREFERS>" or null
}) YIELD nodes, relationships
RETURN nodes, relationships
```

*Why:* converts "model as BFS engine" into "server as BFS engine." `include_observations:false`
returns a structural subgraph the model can plan over without drowning in text; flip it true only
for the final, narrowed set.

## C2 — `find_path` (answers "how is A related to C?")

```
find_path(from: string, to: string, max_depth: int = 5, edge_types: [string]?)
  -> { paths: [ [{source,target,relationType}, ...] ] }   # ordered edge lists
```

```cypher
MATCH (a {name:$from}), (b {name:$to})
MATCH p = shortestPath((a)-[*..%MAX%]-(b))   // %MAX% inlined literal, or apoc.algo.*
RETURN [r IN relationships(p) | {source:startNode(r).name, type:type(r), target:endNode(r).name}]
```

*Why:* the failing class of question is literally shortest-path between two concepts. Returning the
ordered, typed path lets the model explain the connection without guessing intermediate hops.

## C3 — `get_map` (orientation without the text dump)

A new tool that returns **names + types + edges only, no observations**. Do **not** slim the
existing `read_graph` — Dream Engine's `snapshot_loader` depends on its full payload.

```
get_map(type: string?, name_prefix: string?) -> { entities:[{name,type}], relations:[{source,target,relationType}] }
```

*Why:* gives the model a cheap structural overview to pick seeds for `get_neighbors`/`find_path`,
while making "answer from the dump" impossible (there is nothing to read but structure). The
existing `PreToolUse Glob|Grep` hook greps responses for `name|source|relationType`, all still
present in a map, so it keeps working. Rules then point recall at `search_memories` /
`find_memories_by_name` and treat `read_graph` as last-resort only.

## C4 — Write-side guards (enforce structure where it can't drift)

Validate at the `create_relations` / `create_entities` boundary:

- **Reject self-loops** (`source == target`).
- **Reject/normalize unknown relation types** against the `neo4j-schema.md` vocabulary (UPPERCASE_
  UNDERSCORE); return an error listing the allowed set.
- **Warn on `RELATED_TO`** — accept but flag, so it is visibly a typing debt, not a silent default.
- **Optional:** reject edges whose target entity has zero observations and no summary (dangling).

*Why:* the live graph shows 4 self-loops, 7/8 preferences mis-typed as `RELATED_TO`, off-vocabulary
types, and `HAS_PART` used 0 times (decomposition done with wrong-direction `PART_OF`). Prose rules
have not held; a boundary guard does.

## C5 — `cypher_query` (optional, read-only escape hatch)

```
cypher_query(query: string) -> rows   # read-only role, statement timeout, row cap
```

Run as a **separate read-only Neo4j user** (or the official generic Neo4j Cypher MCP server run
alongside this one). Keep C1–C3 as the primary surface; C5 is for rare arbitrary structural queries
a human or power-user composes — never the model's default path.

---

## Rollout

Two equivalent routes, both non-breaking:

1. **Extend `mcp-neo4j-memory`** (fork): add C1–C4 as new tools next to the existing CRUD/read tools.
2. **Run the generic Neo4j Cypher MCP server alongside** for C5, and add C1–C3 as thin
   intent-wrappers over it.

No change to existing tools, rules contract, hooks, or Dream Engine's write path. The only rules
change is *pointing recall at the new tools* (handled in `.claude/rules/memory.md`).

## Acceptance tests (single-call expectations once implemented)

| Probe | Today (1-hop + dump) | With primitives |
|---|---|---|
| "Which building hosts the Edge AI Workshop?" | 3 chained `find_memories_by_name` or a text-scan guess | `get_neighbors(["Edge AI Deployment Workshop"], depth=3, edge_types=["LOCATED_IN","PART_OF"])` → one subgraph |
| "How is Chris connected to Acme Robotics?" | guesswork | `find_path("Chris","Acme Robotics")` → ordered typed path |
| "List all of Chris's preferences" | misses 7/8 (typed `RELATED_TO`) | `get_neighbors(["Chris"], depth=1, edge_types=["PREFERS"])` → all, **after** the write-side re-typing |

Pass = the model answers each from a **single** intent-call, no `read_graph`.
