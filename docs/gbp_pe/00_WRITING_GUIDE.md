# Architecture Document Writing Guide

Purpose: Guide for agents writing RTL architecture documents in `docs/gbp_pe/`.

---

## 1. Document Hierarchy

Documents follow a top-down hierarchy, from abstract to concrete:

| Level | Document | Content | Granularity |
|-------|----------|---------|-------------|
| L0 | `01_ARCHITECTURE.md` | Design goals, core rules, overall data flow, key decisions | Coarsest: "why" and "what" |
| L1 | `02_SPM_AND_METADATA.md` | Data structures, storage layout, format definitions | Medium: "what data looks like" |
| L1 | `03_NOC_PROTOCOL.md` | Packet formats, communication sequences, flow control | Medium: "how components talk" |
| L2 | `04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters, implementation details | Finest: "how it's built" |

When writing a new document, determine which level it belongs to. Do not mix levels within a single section.

---

## 2. Document Structure Template

Every document must follow this skeleton:

```markdown
# [Topic] [Subsystem]   ← H1 title, one per document

## 1. [First Major Section]
Content...

---

## 2. [Second Major Section]
Content...

---

...

## N. Related Documents    ← Always last section
| Document | Content |
|----------|---------|
| `XX_NAME.md` | Brief description |
```

Rules:
- Use `##` for major sections, numbered starting from 1.
- Use `###` for subsections (e.g., `### 2.1 NodeHeader`).
- Separate major sections with `---`.
- End every document with a `Related Documents` table linking to sibling documents.

---

## 3. Content Granularity Rules

### 3.1 What to Include

| Content Type | When to Include | Example |
|--------------|-----------------|---------|
| Design goal / principle | L0 documents only | "Each node belongs to exactly one PE" |
| Data structure definition | When defining formats | `typedef struct packed { ... }` |
| Pseudocode flow | When describing sequences | "1. Receive PULL_REQ. 2. Lookup header..." |
| Parameter table | When listing configurable values | `parameter int NUM_BANKS;` |
| Open item | When decision is deferred | "Open: flit packing strategy" |
| Explicit "not doing" | When scope is constrained | "No node replication in first version" |

### 3.2 What to Exclude

- **Implementation code**: Do not paste full RTL modules. Reference file paths instead.
- **Test details**: Tests belong in `evidence/` or test directories, not architecture docs.
- **Historical narrative**: Do not write "we tried X then switched to Y". State the current decision and rationale only.
- **Redundant explanations**: If a concept is defined in another document, link to it; do not re-explain.

### 3.3 Granularity Heuristics

Ask these questions to calibrate granularity:

1. **Is this a "why" or a "how"?** "Why" goes in L0. "How" goes in L1/L2.
2. **Would a module implementer need this detail?** If yes, include it. If no, omit.
3. **Is this derivable from other stated facts?** If yes, omit or mark as derived.
4. **Is this stable or likely to change?** Stable facts get full treatment; unstable facts get "Open" or "Deferred" tags.

---

## 4. Writing Style

### 4.1 General Rules

- **Be direct.** Start with the fact, not the motivation.
- **Be concise.** One sentence is better than three. One word is better than one sentence.
- **Use code blocks** for data structures, pseudocode, and ASCII diagrams.
- **Use tables** for comparisons, enumerations, and parameter listings.
- **Use lists** for rules, steps, and constraints.
- **Avoid prose paragraphs** longer than 3 sentences. Break into lists or tables.

### 4.2 Terminology

- Use consistent names across documents. If `Pull Server` is the name, do not also call it "pull responder" or "state reader".
- Define terms once in the document that owns them (typically L0 or L1), then reference by name.
- Use `code font` for: signal names, module names, parameter names, type names.

### 4.3 Code and Diagrams

**SystemVerilog structs**: Use fenced code blocks with `systemverilog` language tag.

```systemverilog
typedef struct packed {
    logic [NODE_ID_W-1:0]  neighbor_id;
    logic [X_CORD_W-1:0]   neighbor_x;
    logic [Y_CORD_W-1:0]   neighbor_y;
} adj_entry_t;
```

**Pseudocode / flow**: Use plain fenced code blocks with numbered steps.

**ASCII diagrams**: Use fenced code blocks. Keep diagrams simple; prefer text-based flowcharts over complex art.

**Mermaid**: Use fenced code blocks with `mermaid` language tag for complex diagrams (e.g., `flowchart TD`).

### 4.4 Tables

Use tables for structured comparisons. Keep columns minimal (3-5). If a table exceeds 8 rows, consider splitting.

| Column | Purpose |
|--------|---------|
| Field / Signal / Item | What is being described |
| Meaning / Role | What it does |
| Notes / Status | Constraints, open items |

---

## 5. Section Patterns

### 5.1 Design Goals Section (L0)

```markdown
## 1. Design Goals

One-line summary of the architecture style.

Core rules:
1. [Rule 1]
2. [Rule 2]
...
```

Use a blockquote for the single-sentence takeaway:

```markdown
> **[One-sentence summary of the key insight.]**
```

### 5.2 Explicitly Not Doing Section (L0/L1)

```markdown
## 3. Explicitly Not Doing (First Version)

1. No [feature A].
2. No [feature B].
...
```

This section is critical for scope control. Always include it in L0 and L1 documents.

### 5.3 Data Structure Section (L1)

```markdown
### 2.1 [Structure Name]

[Brief purpose sentence.]

```systemverilog
typedef struct packed { ... } [name]_t;
```

| Field | Meaning |
|-------|---------|
| `field1` | ... |
| `field2` | ... |

[Optional: rationale for inclusion/exclusion decisions.]
```

### 5.4 Flow / Sequence Section (L1/L2)

```markdown
### [Section Title]

```
1. [Step 1]
2. [Step 2]
...
```

[Optional: participant roles, timing constraints.]
```

### 5.5 Module Description Section (L2)

```markdown
### 2.[N] [Module Name]

[One-line role description.]

[Flow or behavior description.]

[Key parameters or constraints.]

[Open items, if any.]
```

### 5.6 Open Items Section (L2)

```markdown
## 4. Open Items

| # | Item | Status | Notes |
|---|------|--------|-------|
| 1 | [Description] | Open/Deferred | [Context] |
```

Use "Open" when the decision is pending. Use "Deferred" when intentionally postponed to a later version.

### 5.7 Related Documents Section (All levels)

```markdown
## N. Related Documents

| Document | Content |
|----------|---------|
| `01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `02_SPM_AND_METADATA.md` | SPM layout, metadata structures, state block organization |
```

Always include all sibling documents in this table, even if not directly referenced in the text.

---

## 6. Naming Conventions

| Item | Convention | Example |
|------|------------|---------|
| Document file | `NN_UPPER_SNAKE_CASE.md` | `01_ARCHITECTURE.md` |
| Section title | `## N. Title Case` | `## 1. Design Goals` |
| Subsection title | `### N.M Title Case` | `### 2.1 NodeHeader` |
| Type name | `snake_case_t` | `node_header_t` |
| Signal name | `snake_case` | `neighbor_x` |
| Module name | `snake_case` | `pull_server` |
| Parameter name | `UPPER_SNAKE_CASE` | `OUTSTANDING_DEPTH` |

---

## 7. Cross-Referencing

- **Within a document**: Use section numbers (e.g., "See Section 4.2").
- **Across documents**: Use relative filenames (e.g., "See `03_NOC_PROTOCOL.md`").
- **To RTL code**: Use file paths relative to repo root (e.g., "`v/gbp_pe/interfaces.sv`").
- **To evidence**: Use full path (e.g., "`evidence/2026-03-27_gbp_pe_mesh_whitebox_dpi_spm.md`").

---

## 8. Maintenance Rules

1. **RTL changes require doc updates.** Before modifying a port, interface, or timing rule, update the corresponding architecture document first.
2. **New decisions require open items.** If a decision is deferred, add an entry to the Open Items table.
3. **No orphan documents.** Every document in `doc/` must be listed in the Related Documents table of at least one sibling.
4. **No stale cross-references.** If a referenced document is renamed or removed, update all referencing documents.
5. **Version scope.** Each document targets a specific version scope (e.g., "first version"). If scope changes, update the document header.

---

## 9. Checklist for New Documents

Before committing a new architecture document:

- [ ] Correct naming: `NN_UPPER_SNAKE_CASE.md`
- [ ] Correct hierarchy level (L0/L1/L2)
- [ ] Follows template structure
- [ ] Has Related Documents table linking to all siblings
- [ ] All sibling documents updated to include the new document
- [ ] No redundant content already covered in another document
- [ ] Open items table present (if any decisions are deferred)
- [ ] "Explicitly Not Doing" section present (if L0 or L1)
- [ ] Code blocks use correct language tags
- [ ] Tables have ≤ 8 rows (or justified exception)

---

## 10. Deriving RTL Documents from C++ Simulators

When a C++ simulator exists as reference, use it as the primary source for architecture documents. The simulator encodes the actual data structures, algorithms, and control flow — but it does not encode hardware-specific concerns (timing, concurrency, resource limits).

### 10.1 Analysis Workflow

Execute these steps in order. Do not skip steps.

**Step 1: Identify the module boundary.**
Find the C++ class or file that corresponds to the RTL module you are documenting. Look for:
- A class with the same name as the target module (e.g., `class PullServer` → `pull_server.sv`)
- A file in a directory named after the subsystem (e.g., `sim/pull_server.cpp`)
- Entry points: `init()`, `tick()`, `step()`, `process()`, `update()`

**Step 2: Extract data structures.**
For each struct/class that represents hardware state:
- Map to a SystemVerilog `typedef struct packed` or register set
- Identify which fields are configuration (parameters), which are runtime state (registers), which are derived (localparams)
- Note bit widths: C++ `int` → need explicit width decision; C++ `uint32_t` → direct mapping

**Step 3: Extract control flow.**
For each `tick()` or `step()` method:
- Identify the sequence of operations (this becomes a state machine or pipeline description)
- Identify conditional branches (these become FSM states or mux selects)
- Identify loops (these become multi-cycle operations or unrolled hardware)
- Note: C++ loops over data structures → RTL iterates over cycles or uses parallel hardware

**Step 4: Extract interfaces.**
For each function call between classes:
- Map to a hardware interface (valid/ready, request/response, memory port)
- Identify the data payload (what signals cross the boundary)
- Identify the handshake protocol (blocking call → valid/ready with backpressure)

**Step 5: Identify abstraction gaps.**
C++ simulators often omit hardware details. For each gap:
- Mark as "Open" in the document if the RTL decision is pending
- Mark as "Deferred" if intentionally postponed
- Common gaps listed in Section 10.3

### 10.2 Concept Mapping Table

| C++ Concept | RTL Equivalent | Action Required |
|-------------|----------------|-----------------|
| `class` with state | Module with registers | Extract state fields → register declarations |
| `class` without state | Combinational logic / utility module | May not need a separate module; inline or merge |
| Member variable (mutable) | Register (`logic` / `reg`) | Explicit width needed |
| Member variable (const) | Parameter or localparam | Decide: synthesis-time or elaboration-time |
| Member function (const) | Combinational logic | Extract as pure function or assign block |
| Member function (non-const) | Sequential logic / FSM | Extract as always_ff with state transitions |
| Method call on another class | Interface signal crossing | Define port list and handshake protocol |
| `std::vector` / dynamic array | Register file, FIFO, or memory | Decide: fixed-size register file vs. parameterized memory |
| `std::queue` / `std::deque` | FIFO | Define depth, almost-full threshold |
| `std::map` / `std::unordered_map` | Lookup table or CAM | Decide: direct-mapped, associative, or sequential search |
| `std::set` / bitmap | Bit vector | Define width = max element count |
| Loop over collection | Multi-cycle iteration or parallel hardware | Decide: sequential (FSM loop) or combinational (unrolled) |
| `if/else` in tick() | FSM state or combinational mux | Depends on whether it's cycle-dependent |
| `switch` in tick() | FSM state decode | Direct mapping |
| `assert` / `check` | Assertion (SVA) or removed | Decide: keep as RTL assertion or remove |
| Logging / print | Removed | Do not include in RTL doc |
| Random / seed | Removed or replaced with deterministic logic | Note if behavior is non-deterministic in sim |

### 10.3 Common Abstraction Gaps

C++ simulators typically omit these hardware concerns. For each, decide whether to document now or defer.

| Gap | Description | Default Action |
|-----|-------------|----------------|
| Cycle timing | C++ `tick()` may not be cycle-accurate | Document the intended cycle behavior as "Open" |
| Concurrency | C++ is sequential; RTL is parallel | Identify which operations happen in the same cycle |
| Resource limits | C++ has unlimited memory/ports | Document port count, FIFO depth, bandwidth as "Open" |
| Reset behavior | C++ constructors ≠ hardware reset | Note reset values if specified; mark "Open" if not |
| Clock domains | C++ has no clock concept | Mark "Open" if multiple clocks are expected |
| Backpressure | C++ blocking calls hide backpressure | Explicitly define valid/ready protocol for each interface |
| Arbitration | C++ may use locks or serialize access | Document arbitration scheme (round-robin, priority, etc.) |
| Pipeline stages | C++ function may be single-cycle | Identify if multi-cycle pipeline is needed |
| Parameterization | C++ templates or runtime config | Decide which are compile-time parameters vs. runtime registers |

### 10.4 Extraction Checklist

When extracting from a C++ simulator file, verify:

- [ ] All state-holding classes identified and mapped to modules or register sets
- [ ] All inter-class method calls mapped to interfaces with handshake protocols
- [ ] All data structures have explicit bit widths (not C++ `int`)
- [ ] All loops identified as multi-cycle or combinational (with justification)
- [ ] All conditionals classified as FSM-state-dependent or combinational
- [ ] All abstraction gaps listed in Open Items table
- [ ] C++ names mapped to consistent RTL names (see Section 6 naming conventions)
- [ ] No C++-specific idioms leaked into RTL descriptions (no iterators, no exceptions, no RAII)

### 10.5 Naming Translation Rules

When translating C++ names to RTL names:

| C++ Pattern | RTL Convention | Example |
|-------------|----------------|---------|
| `ClassName` | `module_name` | `PullServer` → `pull_server` |
| `memberVariable` | `member_variable` | `txnId` → `txn_id` |
| `CONSTANT_VALUE` | `CONSTANT_VALUE` | Direct mapping |
| `method_name()` | Describe as behavior, not as function | `processRequest()` → "Request processing flow" |
| `enum class` | `typedef enum logic` | `enum class PktType` → `pkt_type_e` |

### 10.6 What NOT to Extract

Do not include these C++ artifacts in RTL documents:

- **Memory management**: `new`, `delete`, `shared_ptr`, `unique_ptr`
- **Error handling**: `try/catch`, `std::exception`
- **Logging**: `printf`, `cout`, log levels
- **Test infrastructure**: testbenches, assertions about simulation state
- **Serialization**: JSON/XML parsing, file I/O
- **Thread synchronization**: `mutex`, `lock`, `condition_variable` (unless modeling hardware synchronization)
- **Standard library containers as-is**: Always translate to hardware equivalent; do not assume `std::vector` means "use a vector"
