# Multi-repository development workflow

Status: recommended implementation workflow

## Recommendation

The next substantial work should run through a **local coding agent with simultaneous access to**:

- `electron-like`;
- `native-typescript`;
- the ScriptC source used by Native TypeScript;
- a full Chromium checkout at the pinned revision;
- `depot_tools`, GN, Ninja/autoninja, Clang and Python;
- enough disk, memory and CPU to build and execute renderer tests.

The GitHub connector remains useful for architecture, reviews, small target-neutral patches and repository maintenance. It is not the efficient primary environment for a WebIDL backend because the generator consumes a GN-produced `web_idl_database.pickle`, the generated C++ must compile inside Chromium, and the ScriptC lowering must be tested against the same schema in another repository.

A local agent can close that loop in one work session instead of editing one repository while reasoning about unbuilt state in the others.

## Workspace shape

Use sibling checkouts under one workspace root:

```text
nts-workspace/
├── electron-like/
├── native-typescript/
├── scriptc/
└── chromium/
    └── src/
```

The Chromium checkout is pinned by `electron-like/chromium/revision.json`.

Recommended environment variables:

```sh
export NTS_WORKSPACE=/absolute/path/to/nts-workspace
export NTS_ELECTRON_LIKE=$NTS_WORKSPACE/electron-like
export NTS_NATIVE_TYPESCRIPT=$NTS_WORKSPACE/native-typescript
export NTS_SCRIPTC=$NTS_WORKSPACE/scriptc
export NTS_CHROMIUM_SRC=$NTS_WORKSPACE/chromium/src
export PATH=/absolute/path/to/depot_tools:$PATH
```

The paths should be explicit in generated provenance. Do not infer a random ScriptC checkout from `PATH` when producing release or conformance evidence.

## Repository ownership

### `electron-like`

Owns Chromium-specific product and binding work:

- pinned Chromium revision;
- Chromium patch series;
- `nts_bind_gen`, initially delivered as an overlay or patch into Chromium;
- Native Web schema generation;
- direct Blink capsule generation;
- Blink-side realm/object/callback/subscription/promise runtime;
- content/browser renderer host integration;
- script-free acceptance pages;
- Chromium build/run helpers;
- behavioral and browser conformance tests;
- records of exact Chromium seams and evidence.

The disposable Chromium checkout is not the source of record for project modifications. Changes discovered in `chromium/src` are exported back to `electron-like` as generator source, overlay files or pinned patches.

### `native-typescript`

Owns target-independent compiler/product architecture:

- the Native Web target/provider declaration;
- artifact graph and cross-repository build orchestration;
- schema provenance and compatibility checking;
- application configuration and selected Web capability surface;
- integration tests that span ScriptC and the Chromium target;
- target documentation visible to Native TypeScript users.

It should not own Blink C++ method names or Chromium Python object formats.

### ScriptC

Owns language and compiler/runtime mechanisms:

- TypeScript symbol resolution against `lib.dom.d.ts`;
- recognition of Native Web schema symbols and overloads;
- reachability and operation-selection emission;
- Native Web IR operations;
- C/LLVM lowering to typed C ABI symbols;
- closure/callback-token lifetime;
- ScriptC promise and microtask primitives;
- exception construction in the TypeScript language model;
- native handle cells and escape/ownership analysis.

ScriptC should consume the versioned Native Web schema. It should not parse Blink IDL or import Chromium's `web_idl_database.pickle` directly.

### `chromium/src`

Serves as the exact build and execution environment:

- produces `web_idl_database.pickle`;
- compiles generated Blink capsules;
- links the Native TypeScript runtime/application into the renderer target;
- runs unit, browser and Web Platform tests;
- exposes signature and dependency errors that cannot be proven in the standalone repository.

The checkout may be dirty while an agent experiments, but the final state must be reproducible from the recorded `electron-like` pin, patch series and overlays.

## Local agent capabilities

The agent should be allowed to:

- read and edit all four checkouts;
- invoke Python, CMake, GN, Ninja/autoninja and test binaries;
- inspect generated WebIDL database objects interactively;
- create temporary generated output under build directories;
- run Clang formatting and Chromium presubmit-style checks;
- preserve concise dated investigation records;
- commit changes to the project repositories according to the maintainer's chosen branch policy.

For `electron-like`, the current maintainer policy is direct work on `main`: no pull requests and no feature branches unless that policy is explicitly changed.

The agent should not commit generated Chromium build output, entire Chromium source trees, object files or toolchain binaries to the project repositories.

## Bootstrap

The exact Chromium revision is read from:

```text
$NTS_ELECTRON_LIKE/chromium/revision.json
```

A typical checkout preparation is:

```sh
cd "$NTS_CHROMIUM_SRC"
git fetch origin <pinned-revision>
git checkout --detach <pinned-revision>
gclient sync

cd "$NTS_ELECTRON_LIKE"
python3 scripts/verify_chromium_patches.py
python3 scripts/apply_chromium.py "$NTS_CHROMIUM_SRC"
```

A helper may automate these commands, but it must still verify the exact commit and refuse an unknown dirty base.

## Core development loop

The generator/compiler loop should become:

```text
1. Build Chromium web_idl_database
2. Run nts_bind_gen schema generation
3. Run schema/golden tests
4. Compile the TypeScript fixture with ScriptC
5. Emit app Web-operation selection + native object
6. Generate/select Blink capsules for that manifest
7. Build the Chromium renderer/product target
8. Run the script-free page and browser tests
9. Record exact commands, revisions and results
```

Representative commands, with target names finalized by implementation:

```sh
cd "$NTS_CHROMIUM_SRC"
gn gen out/nts

autoninja -C out/nts \
  third_party/blink/renderer/bindings:web_idl_database

python3 \
  third_party/blink/renderer/bindings/scripts/generate_nts_bindings.py \
  --web-idl-database \
  out/nts/gen/third_party/blink/renderer/bindings/web_idl_database.pickle \
  --schema-out out/nts/gen/native_typescript/native_web_schema.bin \
  --coverage-out out/nts/gen/native_typescript/native_web_coverage.json

cd "$NTS_NATIVE_TYPESCRIPT"
<compiler-command> \
  --target chromium \
  --native-web-schema \
  "$NTS_CHROMIUM_SRC/out/nts/gen/native_typescript/native_web_schema.bin" \
  --emit-web-selection \
  "$NTS_CHROMIUM_SRC/out/nts/gen/native_typescript/app.webops.json"

cd "$NTS_CHROMIUM_SRC"
autoninja -C out/nts content_shell
out/nts/content_shell \
  --native-typescript-counter \
  "file://$NTS_ELECTRON_LIKE/examples/counter/index.html"
```

The exact command-line interface will evolve. The invariant is that every artifact is explicit and digest-checked.

## Generator implementation location

During development, `nts_bind_gen` may live in `electron-like` and be copied into the pinned Chromium tree by the overlay script.

Suggested source layout:

```text
electron-like/
├── chromium/
│   ├── bindings/
│   │   ├── generate_nts_bindings.py
│   │   └── nts_bind_gen/
│   ├── runtime/
│   ├── host/
│   └── patches/
├── schemas/
│   └── fixtures/
└── tests/
    ├── schema/
    ├── generator/
    └── browser/
```

The overlay installs the backend into Chromium beside `web_idl` and `bind_gen`, where it can import Chromium's normalized WebIDL package and participate in GN's dependency graph.

Do not copy Chromium's `web_idl` Python package into `electron-like`. Use the package from the pinned Chromium checkout. Copying it would create a silent second compiler.

## First generator slice

The first automated schema/capsule slice should cover exactly the already proven counter surface:

```text
Window.document or realm document root
Document.body
Document.createElement(DOMString)
Node.textContent setter
Node.appendChild(Node)
EventTarget.addEventListener(string, EventListener)
EventTarget.removeEventListener(string, EventListener)
EventListener callback(Event)
```

The handwritten bridge remains the executable oracle while generated output is introduced. For each operation:

1. generate a schema entry;
2. generate the ABI declaration;
3. generate the C++ capsule;
4. compare behavior with the handwritten capsule;
5. switch the GN target to generated output;
6. delete the handwritten operation only after the generated path passes the same tests.

The runtime registries and realm are not regenerated.

## Cross-repository contract fixtures

Keep small committed fixtures that do not require a full Chromium checkout to inspect:

```text
schema fixture input description
expected native schema JSON
expected ScriptC operation selection
expected generated C call
expected generated C++ capsule
expected refusal report
```

These fixtures let `electron-like`, `native-typescript` and ScriptC test the same operation identity and ABI contract in lightweight CI.

The authoritative integration test still runs against Chromium's generated database.

## Testing matrix

### Fast tests on every project change

- portable C ABI compilation;
- handle/subscription/callback runtime tests;
- schema serialization determinism;
- operation identity stability within a fixture;
- generator golden tests;
- ScriptC lowering tests;
- V8/JavaScript bridge-token scan;
- patch application against the pin.

### Chromium compile tests

- `nts_bind_gen` runs inside GN;
- generated capsules compile with Blink configs;
- generated source lists are complete;
- implementation signature drift fails clearly;
- no forbidden dependency from Blink core to product host is introduced.

### Browser behavior tests

- script-free native counter renders;
- real click updates native state and DOM text;
- invalid `createElement` reports `InvalidCharacterError`;
- wrapper/native handle identity is stable;
- navigation invalidates handles and subscriptions;
- shutdown leaves no admitted callback or Oilpan root;
- event ordering is deterministic;
- later, promise and microtask ordering matches the declared contract.

### Compatibility tests

- chosen TypeScript `lib.dom.d.ts` maps to the pinned Native Web schema;
- missing/mismatched source members fail with precise diagnostics;
- runtime and schema digests match at link/startup;
- selected runtime features are enabled or explicitly refused.

## Evidence discipline

Every claim that a Chromium path works should record:

```text
Chromium commit
GN args
compiler/toolchain version
Native TypeScript commit
ScriptC commit
electron-like commit
schema digest
selection-manifest digest
build target
exact test command
exit status
relevant output/screenshots or machine-readable results
```

A source patch that applies is patch evidence. A target that compiles is compile evidence. A rendered/clickable counter is behavioral evidence. These should not be conflated.

## Keeping Chromium changes maintainable

Prefer horizontal patches that unlock classes of WebIDL members:

- pluggable exception sink;
- binding-neutral realm/context;
- binding-neutral promise resolver;
- native callback gateway;
- generic Oilpan object registry.

Avoid patches that add one Native TypeScript overload per Web API member. If a generated member needs a one-off custom path, declare it in the reviewed override catalog and record why the horizontal model cannot express it.

When updating the Chromium pin:

1. regenerate the revision-wide schema;
2. compare schema and coverage changes;
3. reapply horizontal patches;
4. compile the proven operation set;
5. run behavioral tests;
6. review new refusals and changed call plans;
7. only then move `revision.json`.

## Recommended immediate ownership split

A productive local agent can work in parallel across the repositories without creating architectural coupling:

```text
electron-like
  - add nts_bind_gen skeleton using web_idl.Database
  - emit schema for Document/Node/EventTarget slice
  - emit generated C++ capsules
  - add GN and Chromium tests

ScriptC
  - import Native Web schema
  - map resolved lib.dom symbols to operation IDs
  - emit app.webops selection
  - lower the counter calls to the typed C ABI

native-typescript
  - declare chromium target/provider
  - orchestrate schema -> ScriptC -> capsule -> Chromium build
  - freeze artifact/provenance contract
```

This is the point where a local agent offers a material advantage. The next uncertainty is no longer primarily conceptual; it is whether the same normalized operation can be carried correctly through three repositories and a Chromium build.
