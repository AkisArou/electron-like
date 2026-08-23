# Native TypeScript integration workflow

Status: recommended implementation workflow

## Source of truth

The final Chromium/WebIDL work is integrated into:

```text
https://github.com/AkisArou/native-typescript
```

`native-typescript` is the project workspace, product repository and source of truth for the completed feature.

This `electron-like` repository is a feasibility spike. It currently preserves:

- the direct-Blink architectural proof;
- the handwritten plain-C counter;
- the first Blink runtime/handle/event scaffolding;
- pinned Chromium patches and build experiments;
- dated investigation records.

It is not a permanent sibling implementation repository. Once equivalent code, tests and records have been migrated into `native-typescript`, new product work should happen there and the spike should become read-only reference material or be archived with a pointer to the integrated implementation.

## Actual project shape

`native-typescript` is already a pnpm monorepo and already contains the Native TypeScript ScriptC fork as a git submodule:

```text
native-typescript/
├── packages/
│   ├── core/
│   ├── target-api/
│   ├── scabi/
│   ├── scriptc/                 # parent-repo integration/orchestration
│   ├── bindgen-c/
│   ├── bindgen-gir/
│   ├── bindgen-jvm/
│   ├── target-gtk/
│   └── target-jvm/
├── third_party/
│   └── scriptc/                 # AkisArou/scriptc git submodule
├── fixtures/
├── tests/
├── docs/
└── .native-typescript/          # ignored local build/dependency state
```

The submodule is configured as:

```text
path:   third_party/scriptc
remote: https://github.com/AkisArou/scriptc.git
branch: native-typescript
```

Therefore there is no separate permanent `scriptc/` sibling checkout in the recommended workspace. Compiler/runtime changes are made in `native-typescript/third_party/scriptc`, committed and pushed in the submodule repository, then recorded by updating the parent repository's gitlink.

## Local machine shape

The coding agent should open `native-typescript` as its project root.

A full Chromium checkout is a heavyweight external build dependency, not another project source-of-truth repository. It may live in an ignored Native TypeScript work directory or in an external cache/location:

```text
/path/to/native-typescript/                    # project root
├── third_party/scriptc/                       # checked-out submodule
└── .native-typescript/chromium/src/           # optional managed checkout

# or
/path/to/native-typescript/                    # project root
$HOME/.cache/native-typescript/chromium/src/   # external Chromium checkout
```

The second arrangement is often preferable because editors, indexers and coding agents do not accidentally treat the entire Chromium tree as first-party project source.

During migration only, an `electron-like` checkout may be available as a read-only or temporary source:

```text
/path/to/electron-like/                        # temporary migration input
```

It is not required after migration parity is reached.

Recommended environment variables:

```sh
export NTS_REPO=/absolute/path/to/native-typescript
export NTS_SCRIPTC="$NTS_REPO/third_party/scriptc"
export NTS_CHROMIUM_SRC=/absolute/path/to/chromium/src

# Optional only while porting the spike:
export NTS_ELECTRON_LIKE=/absolute/path/to/electron-like

export PATH=/absolute/path/to/depot_tools:$PATH
```

All build evidence and generated provenance must identify the exact parent commit, ScriptC submodule commit and Chromium commit. A compiler or Chromium checkout found incidentally through `PATH` is not sufficient provenance.

## Recommended in-repository ownership

The exact package names can be finalized when implementation begins, but they should follow the repository's existing bindgen/target split.

### `packages/bindgen-webidl`

Recommended new package for target-independent Native Web schema work and the Chromium WebIDL backend orchestration.

It owns:

- the deterministic Native Web schema format;
- schema serializers/readers and versioning;
- operation/type/callback/dictionary/union identities;
- compatibility reporting against the selected TypeScript standard libraries;
- the `nts_bind_gen` source overlaid into a pinned Chromium checkout;
- generation of typed C declarations and direct Blink C++ capsules;
- coverage/refusal reports;
- generator fixtures and golden tests.

The Python backend may live under a Chromium-specific subtree of the package, for example:

```text
packages/bindgen-webidl/
├── src/                              # TS orchestration/schema tooling
├── chromium/
│   └── nts_bind_gen/                 # Python backend using Chromium web_idl
├── schemas/
└── test-fixtures/
```

It imports Chromium's `web_idl` package from the exact checkout. It does not copy or fork Chromium's WebIDL compiler into the Native TypeScript repository.

### `packages/target-chromium`

Recommended new target package following the role already established by `target-gtk` and `target-jvm`.

It owns:

- the Chromium target/provider declaration;
- the pinned Chromium revision and source provenance;
- artifact-graph actions for checkout validation, overlay, GN generation, Ninja builds and product assembly;
- the Blink-side Native TypeScript runtime;
- realm, interface-object registry, callback, subscription, exception, promise and scheduler integration;
- horizontal Chromium patches;
- renderer/browser product-host integration;
- selected generated capsule integration;
- application resources and packaging;
- Chromium compile tests and browser behavioral tests.

A likely package shape is:

```text
packages/target-chromium/
├── src/
│   ├── provider.ts
│   ├── application-project.ts
│   ├── application-build.ts
│   └── target-runtime-objects.ts
├── chromium/
│   ├── revision.json
│   ├── patches/
│   ├── runtime/
│   ├── host/
│   └── overlay/
├── application/
├── runtime/
└── fixtures/
```

This mirrors the existing target-package architecture rather than creating a standalone Chromium product repository.

### `packages/scriptc`

This existing package remains the parent-repository integration layer around the ScriptC checkout. It should orchestrate:

- exact submodule checkout validation;
- ScriptC builds;
- Native Web schema delivery to the compiler;
- application operation-selection output;
- compiled C/LLVM artifact production;
- cross-repository/submodule conformance fixtures.

It should not duplicate compiler implementation that belongs in the submodule.

### `third_party/scriptc`

The existing submodule owns reusable compiler and runtime mechanics:

- resolution of TypeScript DOM symbols against the Native Web schema;
- Native Web operation IR;
- reachability and application Web-operation selection;
- C/LLVM lowering to typed Web ABI symbols;
- callback-token and closure lifetime;
- ScriptC promise/microtask primitives;
- ScriptC exception construction;
- native-handle cells and target-independent ownership/escape behavior.

Blink C++ names, Chromium Python formats, GN paths and product-host details do not belong here.

### Existing shared packages

Use existing ownership boundaries rather than forcing all Chromium work into one package:

- `packages/core`: artifact/provenance contracts that are genuinely target-independent;
- `packages/target-api`: target/provider contracts needed by Chromium and other targets;
- `packages/scabi`: typed ABI concepts that are reusable beyond Blink;
- `packages/cli`: user-facing target/build commands after the provider exists.

A missing general primitive should be added to its owning shared package. Chromium-specific mechanics remain in `bindgen-webidl` or `target-chromium`.

## Role of the Chromium checkout

The Chromium checkout is disposable build input.

It:

- produces `web_idl_database.pickle`;
- provides Chromium's `web_idl` Python package;
- receives Native TypeScript overlays and patches from `native-typescript`;
- compiles generated capsules and runtime code;
- links the renderer/product target;
- runs unit, browser and Web Platform tests.

It is not where durable project changes live.

Any useful change made experimentally inside `chromium/src` must be exported back into `native-typescript`, as one of:

- `packages/bindgen-webidl/chromium/nts_bind_gen/...`;
- `packages/target-chromium/chromium/runtime/...`;
- `packages/target-chromium/chromium/host/...`;
- `packages/target-chromium/chromium/patches/...`;
- a deterministic generator or build action.

A clean checkout at the pinned revision plus the parent repository must be sufficient to reproduce the modified Chromium tree.

## Role of `electron-like`

The spike is migrated, not co-developed indefinitely.

### Material to migrate

- normative architecture and generator decision records;
- the pinned direct-Blink seam evidence;
- the plain-C counter and script-free page;
- portable handle/subscription/callback tests;
- Chromium patch verification logic;
- Blink realm/object/event runtime specimens;
- Chromium build/run helpers;
- behavioral acceptance criteria.

### Material that should not survive as product architecture

- counter-specific product host names;
- handwritten API-member wrappers after generated replacements exist;
- a separate release/build system;
- separate ownership of the Chromium target;
- duplicated ScriptC or Native TypeScript contracts.

### Migration completion condition

The spike can stop receiving implementation work when `native-typescript` can, from its own checkout:

1. validate/fetch the pinned Chromium revision;
2. build Chromium's WebIDL database;
3. compile the plain TypeScript/native-C counter path;
4. overlay/build the direct Blink runtime and capsules;
5. launch the script-free page;
6. deliver real click events to compiled state;
7. pass exception, teardown and handle/subscription tests;
8. reproduce all of this without reading build inputs from `electron-like`.

At that point `electron-like` is historical evidence only.

## Local agent access

The productive agent configuration is:

```text
read/write: native-typescript repository
read/write: native-typescript/third_party/scriptc submodule
read/write: disposable Chromium checkout and output directories
read:       electron-like during migration only
execute:    pnpm, Node, Python, CMake, Clang, GN, Ninja/autoninja, tests
```

The agent must understand that `third_party/scriptc` is a nested git repository. A ScriptC implementation change requires two commits:

1. commit and push inside `third_party/scriptc` on the Native TypeScript fork branch;
2. update and commit the submodule pointer in `native-typescript` together with its parent-side consumers/tests.

Generated Chromium output, object files, full source trees and toolchains are never committed to `native-typescript`.

## Repository initialization

From the Native TypeScript root:

```sh
corepack enable pnpm
git submodule update --init --recursive
pnpm install --frozen-lockfile
pnpm scriptc:install
```

Follow the repository's existing `docs/development.md` requirements, including the real-filesystem `TMPDIR` rule and the normal workspace validation gates.

Configure the Chromium checkout path explicitly:

```sh
export NTS_REPO="$(pwd)"
export NTS_SCRIPTC="$NTS_REPO/third_party/scriptc"
export NTS_CHROMIUM_SRC=/absolute/path/to/chromium/src
```

The Chromium target package should eventually provide commands/actions that:

- read its committed `chromium/revision.json`;
- verify `NTS_CHROMIUM_SRC` is the exact commit;
- refuse an unknown dirty base;
- apply the committed patch series;
- install the runtime/generator/host overlay;
- produce a content-addressed artifact graph.

## Integrated build loop

The final loop starts and ends in `native-typescript`:

```text
native-typescript target provider
        |
        +-- validate pinned Chromium checkout
        |
        +-- build Chromium web_idl_database
        |
        +-- run packages/bindgen-webidl / nts_bind_gen
        |       |
        |       +-- Native Web schema
        |       +-- coverage/refusal report
        |
        +-- invoke packages/scriptc + third_party/scriptc
        |       |
        |       +-- compile TypeScript
        |       +-- app Web-operation selection
        |       +-- C/LLVM objects
        |
        +-- generate/select Blink capsules
        |
        +-- overlay runtime/host/patches into Chromium
        |
        +-- GN/Ninja build
        |
        +-- run Chromium behavioral tests/product
        |
        v
reproducible Native TypeScript Chromium artifact
```

The generator/compiler iteration is:

1. Build or reuse the pinned Chromium `web_idl_database` artifact.
2. Generate a revision-wide Native Web schema.
3. Run schema determinism and golden tests.
4. Compile the application through the checked-in ScriptC submodule.
5. Emit an application operation-selection manifest.
6. Generate or select the required Blink capsules.
7. Build the Chromium target.
8. Run script-free browser behavior tests.
9. Record exact parent, submodule, Chromium, schema and selection digests.

The target provider should express these as artifact-graph actions so caching, provenance, sandbox policy and invalidation follow the same architecture as the rest of Native TypeScript.

## Proposed developer commands

Command names below are the intended ergonomic surface, not a claim that they already exist:

```sh
# Validate/install the heavy Chromium dependency.
pnpm native-typescript doctor --target chromium

# Generate the pinned Native Web schema and coverage report.
pnpm chromium:schema

# Compile and build the script-free counter through the integrated target.
pnpm chromium:counter

# Run focused schema/generator/runtime/browser tests.
pnpm chromium:test

# Full repository gate.
pnpm test
```

The implementation may expose these through the main CLI rather than package scripts. There should be one parent-repository entry point; users should not manually coordinate four repositories.

## First integrated generator slice

The first automated slice should replace the already proven handwritten surface:

```text
Document root acquisition
Document.body
Document.createElement(DOMString)
Node.textContent setter
Node.appendChild(Node)
EventTarget.addEventListener(...)
EventTarget.removeEventListener(...)
EventListener callback(Event)
```

For each operation:

1. generate a schema entry from Chromium's normalized database;
2. map the resolved TypeScript symbol in ScriptC;
3. emit the typed C ABI declaration/call;
4. emit the direct Blink C++ capsule;
5. run the current counter behavior as the oracle;
6. switch the Chromium target to generated output;
7. remove only the superseded handwritten operation wrapper.

Realm, object registry, callback gateway and subscription runtime are migrated as runtime infrastructure, not regenerated.

## Cross-component contract fixtures

All lightweight contract fixtures belong in `native-typescript`, because it is the repository that integrates their producers and consumers.

Recommended fixtures include:

```text
fixtures/chromium/webidl/              # small normalized/schema fixtures
fixtures/chromium/counter/             # TS app + script-free page
fixtures/chromium/abi/                 # expected C declarations/calls
fixtures/chromium/capsules/            # expected generated C++
tests/chromium-schema.test.ts
tests/chromium-scriptc.test.ts
tests/chromium-target.test.ts
```

They should pin:

- Native Web operation identity;
- schema serialization;
- ScriptC symbol-to-operation mapping;
- operation reachability selection;
- C/LLVM call lowering;
- generated capsule source;
- exact refusal diagnostics;
- artifact/provenance digests.

The authoritative integration lane still runs against the full pinned Chromium database.

## Testing matrix

### Fast parent-repository tests

- schema serialization/determinism;
- operation/type identity fixtures;
- generator golden output;
- ScriptC lowering and operation selection;
- portable handle/callback/subscription runtime tests;
- Chromium patch application against downloaded pinned inputs;
- V8/JavaScript bridge-token scan;
- target-provider artifact graph and provenance tests.

### ScriptC submodule tests

- DOM symbol resolution against schema fixtures;
- Native Web IR construction;
- both C and LLVM lowering where applicable;
- callback/handle/promise/exception lifetime;
- sanitizer/reference-audit lanes for new runtime primitives.

### Full Chromium compile tests

- generation from real `web_idl_database.pickle`;
- generated source list completeness;
- capsule compilation with Blink configs;
- implementation signature drift diagnostics;
- runtime/host link integration;
- sandbox/process placement.

### Browser behavioral tests

- script-free native counter renders;
- real click changes native state and DOM text;
- invalid element name produces `InvalidCharacterError`;
- object identity is stable;
- navigation invalidates handles and subscriptions;
- shutdown leaves no callback lease or Oilpan root;
- event/task ordering is pinned;
- later, promise and microtask ordering matches the declared contract.

## Evidence discipline

Every Chromium result recorded in `native-typescript` should include:

```text
native-typescript commit
third_party/scriptc commit
Chromium commit
GN args
compiler/toolchain versions
Native Web schema digest
authoritative TypeScript library digest
application operation-selection digest
generator/runtime ABI versions
build target
exact test command
exit status and relevant machine-readable output
```

A patch that applies is patch evidence. A capsule that compiles is compile evidence. A rendered and clickable counter is behavioral evidence. Documentation must not collapse those levels.

## Chromium updates

The Chromium pin is owned by `packages/target-chromium`, not by the spike or the disposable checkout.

Updating it requires one parent-repository change that:

1. updates the committed revision;
2. regenerates the revision-wide Native Web schema;
3. reviews schema and coverage differences;
4. rebases horizontal patches;
5. recompiles the proven selected surface;
6. runs ScriptC and browser behavior tests;
7. records the new evidence;
8. invalidates incompatible caches/artifacts atomically.

The ScriptC submodule pointer changes only when compiler/runtime work is actually required.

## Immediate next ownership

The next implementation should be performed in `native-typescript`, with `electron-like` open only as migration reference:

```text
packages/bindgen-webidl
  - add nts_bind_gen skeleton using Chromium web_idl.Database
  - emit schema for the counter surface
  - emit generated C ABI and Blink capsules

third_party/scriptc + packages/scriptc
  - read the Native Web schema
  - map resolved lib.dom symbols to operation identities
  - emit application operation selection
  - lower selected calls to the typed ABI

packages/target-chromium
  - own the Chromium pin, patch/overlay and artifact graph
  - host the migrated realm/object/callback/subscription runtime
  - compile/link the plain-C/ScriptC application in the renderer
  - run the script-free counter and conformance tests
```

That is the integrated architecture: one Native TypeScript repository, one nested ScriptC submodule, and one disposable pinned Chromium build dependency.
