<!-- 이 파일은 CL9 저장소 docs/goal/ 의 같은 이름 문서를 2026-10-18 에 복사한 것이다. 이후 고칠 때는 이 저장소(docs/)를 먼저 고치고
     CL9 사본을 맞춘다. 본문의 C:\Work\CL9, C:\Work\P4 경로는 처음 개발한 PC 의 것이다. -->

# goal: radar-umg-diff

## One-line Goal
Build **RadarUMGDiff**, a UE editor plugin (5.8 first, the company's 5.1 next, one GitHub
branch per engine version) plus a standalone pure-Python analyzer that
shows what changed between two git revisions of a Widget Blueprint: both revisions rendered
side by side, changed widgets outlined, removed widgets marked with a red box where they used
to be, a details grid that lists only changed properties (old → new), and search/filter over
everything — including animations, slots, bindings, variables and the event graph.

## Background / Motivation
Text/binary diff tools cannot read `.uasset`. The engine's built-in Blueprint Diff window
(`UWidgetBlueprint::FindDiffs` → `SBlueprintDiff`) lists per-widget differences but renders
nothing, shows no values ("{0} changed value in {1}") and has no "it used to be here" marker,
which is what made the in-house tool the user saw illegible. The company source is not
available; this is a from-scratch rebuild in the CL9 repo, kept independent of CL9 game code so
the plugin folder can be detached later.

Track: **HEAVY** (new system; design settled by a 4-perspective panel + cross-examination on
2026-10-17; see Design Decisions).

## In-scope
- **Plugin** `Plugins/RadarUMGDiff/` with:
  - `RadarUMGDiff.uplugin` (Editor-only module `RadarUMGDiffEditor`, type `Editor`, no runtime module).
  - C++ editor module: Content Browser action, revision picker, git access, two-revision
    loading, native diff producer, side-by-side viewer with overlays, hierarchy, 3-column
    details, animation tab, graph summary tab, `-run=RadarUMGDiff.Dump` commandlet.
  - `Content/Python/radar_umgdiff/` pure-stdlib Python package (3.11-compatible) + CLI
    `Content/Python/umgdiff_cli.py`: `.uasset` parser, full-state dump, diff producer,
    conformance test runner, schema-dump consumer.
  - `Content/Python/radar_umgdiff/data/struct_kinds_<engine>.json`: per engine version, the
    struct-kind table the legacy (< 5.4) tag reader needs — dumped from that engine's editor by
    `Content/Python/editor/dump_struct_kinds.py`, never hand-written (see Design Decisions).
  - `Content/Python/editor/dump_schema.py`: editor-side script (run inside the editor's
    Python) that exports UCLASS/USTRUCT property lists, flags, enum values and CDO defaults to
    `Saved/RadarUMGDiff/schema.json`.
  - `Content/Test/` fixture Widget Blueprints (animation with transform/colour/opacity tracks,
    function with in/out pins, custom event with params, variables, TMap/TArray-of-struct
    property, soft texture brush, Korean DisplayLabels, nested UserWidget child, WidgetSwitcher
    with inactive page), saved as 3–4 deliberately different versions.
  - `Config/DefaultRadarUMGDiff.ini` (+ `URadarUMGDiffSettings`): colour table, rename
    threshold, dim-unchanged default, preview DPI mode, temp folder — tunable without rebuild.
- **Diff scope (v1 = everything)**: widget tree (added / removed / changed / moved /
  renamed-probable), widget properties, slot properties, named slots, bindings, CDO
  (blueprint variables' defaults), animations (list level and per-track/per-key values),
  event graph / functions / macros / variables (nodes by NodeGuid, pins by PinId, variables by
  name+VarGuid) classified Logic vs Layout.
- **diff.json contract** shared by both producers (native C++ and Python), plus full-state
  dumps of both revisions.
- **Accuracy mode**: run both producers over every historical blob of every `WBP_*.uasset`
  in the repo (34 blobs today) + fixtures, report disagreements and Python coverage %.

## Out-of-scope
- Non-UMG assets (generic Blueprint, materials, levels).
- Writing/patching `.uasset` files.
- Writing our own client for any VCS protocol. Perforce, Plastic/Unity VCS, SVN, Diversion and the
  engine's Git plugin are all reached through the engine's **connected Source Control provider**;
  git is additionally reached through the git CLI with no provider at all; anything else through
  "compare two files". (Changed 2026-10-18 from "git only" — see the Revision sources decision.)
- Unversioned (cooked) packages and `.usmap` support.
- Translating the UI (ko strings only, per repo rule).

## Design Decisions (panel-settled — do not relitigate during implementation)
- **Two producers, one contract.** `diff.json` is the single contract between engines and the
  viewer. Producer A: native C++ in `RadarUMGDiffEditor` (in-editor default, and the
  `-run=RadarUMGDiff.Dump` commandlet for CI). Producer B: the pure-stdlib Python analyzer
  (CLI, no-editor environments, and the fallback when an old revision fails to load in the
  editor). Both must emit the same schema; the conformance test compares them.
  (Rejected: Python as the only engine inside the editor — blocks the game thread under the
  GIL, forces a PythonScriptPlugin dependency, no accuracy gain. Rejected: native only — no
  standalone path, and FText keys regenerate on load so "key changed" is detectable only in
  Python.) User confirmed 2026-10-17.
- **Engine-version axis — one viewer, two inputs, N producers** (user decided 2026-10-17, after
  the company engine turned out to be 5.1). The viewer is ONE codebase that takes two inputs:
  (a) `diff.json` from any producer, and (b) the two loaded `UWidgetBlueprint`s that the
  editor it runs in produced with its own loader. Summary strip, hierarchy, 3-column details,
  animation tab and graph summary read only (a); the rendered side-by-side previews and the
  overlays read only (b). When (b) is unavailable (CLI-produced diff of an asset the current
  editor cannot load) the viewer runs in **data-only mode**: preview area replaced by a notice,
  everything else unchanged. Producers are keyed by engine version × environment: the native
  producer of each engine branch, and the Python analyzer with its version gates. The user's
  requirement is "what is shown is identical; only what extracts it differs" — that holds
  because (b) is extracted by that engine's loader, not by our code. (Rejected: a viewer driven
  by `diff.json` alone — overlay geometry is recomputed every frame from live painted widgets
  and capture-once geometry was already rejected below; a viewer outside the editor cannot
  render UMG.)
- **Distribution: one GitHub repository for the plugin, one branch per engine version**
  (user decided 2026-10-17). Default branch names `ue5.8` (CL9, built first) and `ue5.1`
  (company engine); small API moves are absorbed with `#if ENGINE_MAJOR_VERSION/MINOR_VERSION`
  inside a branch, large ones by the branch itself. Each branch is built and conformance-tested
  against its own engine; the Python package and the `diff.json` schema are identical across
  branches (the Python code carries every version gate; only `data/struct_kinds_<engine>.json`
  differs). CL9 consumes the `ue5.8` branch at `Plugins/RadarUMGDiff/`. **Settled
  2026-10-18 at implementation start (user): public GitHub repository `RadarUMGDiff`; CL9
  holds it as a git submodule; branches are named `5.8` and `5.1`** (everywhere this
  document says `ue5.8` / `ue5.1`, read `5.8` / `5.1`). Creating the repository and pushing
  are outward-facing and are confirmed with the user when they happen. (Rejected: one branch with the whole version matrix in `#if` — untestable here; a
  separate repo per version — duplicates the Python package and the schema.)
- **Python analyzer version policy: ONE codebase, version gates at two points, fixtures per
  version** (user decided 2026-10-17). Gate 1 = package summary fields by `FileVersionUE5`
  (already below). Gate 2 = the property-tag reader, mirroring
  `Engine/Source/Runtime/CoreUObject/Private/UObject/PropertyTag.cpp` (5.8: lines 420–424
  branch, 205–365 legacy reader): `FileVersionUE5 < PROPERTY_TAG_COMPLETE_TYPE_NAME (1012)` →
  **legacy tag** = Name, Type, Size, ArrayIndex, then per type: StructProperty → StructName +
  StructGuid; BoolProperty → BoolVal byte; ByteProperty/EnumProperty → EnumName;
  Array/Set/Optional → InnerType; Map → InnerType + ValueType; then HasPropertyGuid byte (+
  FGuid). No flags byte, no `HasBinaryOrNativeSerialize`, no `SkippedSerialize`.
  `≥ 1012` → the current layout described below. Consequences for the legacy path:
  (i) whether a struct body is a nested tag stream or native bytes is NOT in the file — it
  comes from a **struct-kind table** keyed by StructName (`tagged` / `native` / `immutable`),
  dumped from that engine version's editor (`UScriptStruct` CppStructOps `HasSerializer` /
  `HasStructuredSerializer`, `STRUCT_Immutable`) into
  `data/struct_kinds_<engine>.json`. This table is *not* the schema dump: it says where bytes
  are, never what default values are, so "verdicts identical with and without `schema.json`"
  still holds, and "schema as parsing prerequisite" stays rejected — the struct-kind table is
  the one piece of out-of-file knowledge the legacy format needs. Unknown StructName → skip
  by Size, raw hex + `unparsed`, counted in coverage %. (ii) `< PROPERTY_TAG_EXTENSION_AND_
  OVERRIDABLE_SERIALIZATION (1011)`: no 1-byte `EClassSerializationControlExtension` prefix.
  (iii) `< SCRIPT_SERIALIZATION_OFFSET (1010)`: the export table has no
  `ScriptSerializationStart/EndOffset`; the start of the tag stream inside each export must be
  derived from that engine version's `UObject::Serialize` order (read from its source — spike).
  (iv) `< LARGE_WORLD_COORDINATES (1004)`: FVector/FVector2D/FBox2D… are floats, not doubles.
  `< FSOFTOBJECTPATH_REMOVE_ASSET_PATH_FNAMES (1007)`: FSoftObjectPath = FName asset path +
  sub path; `< ADD_SOFTOBJECTPATH_LIST (1008)`: no summary list. FText history, sequencer
  channels and editor-only bodies gate on the custom versions in the header
  (FEditorObjectVersion, FFortniteMainBranchObjectVersion, FSequencerObjectVersion,
  FUE5MainStreamObjectVersion, FUE5ReleaseStreamObjectVersion), exactly as the 5.8 reader
  does. Each analyzer release declares its supported `FileVersionUE5` range and
  custom-version set; outside → hard refuse (existing rule). Conformance baseline per
  version = the native commandlet of that engine's branch; until the `ue5.1` branch builds,
  a 5.8 editor loading the 5.1-saved asset via `LoadPackageForDiff` (upgrade on load) is a
  partial baseline — disagreements where the upgrade rewrites values are expected and must be
  listed, not hidden. (Rejected: a separate analyzer codebase per engine version — the name
  map, import/export tables, FText, soft paths and the native-struct readers are shared and
  the engine itself is one loader with version gates; "per version" lives in fixtures, the
  struct-kind data file and the conformance run, not in duplicated code. Rejected: inferring
  struct kind by trial-parsing the body — ambiguous for small structs, silently wrong.)
  Version numbers from `Engine/Source/Runtime/Core/Public/UObject/ObjectVersion.h` (5.8):
  INITIAL 1000, NAMES_REFERENCED 1001, PAYLOAD_TOC 1002, OPTIONAL_RESOURCES 1003, LWC 1004,
  REMOVE_OBJECT_EXPORT_PACKAGE_GUID 1005, TRACK_OBJECT_EXPORT_IS_INHERITED 1006,
  FSOFTOBJECTPATH_REMOVE_ASSET_PATH_FNAMES 1007, ADD_SOFTOBJECTPATH_LIST 1008, DATA_RESOURCES
  1009, SCRIPT_SERIALIZATION_OFFSET 1010, PROPERTY_TAG_EXTENSION 1011,
  PROPERTY_TAG_COMPLETE_TYPE_NAME 1012, ASSETREGISTRY_PACKAGEBUILDDEPENDENCIES 1013,
  METADATA_SERIALIZATION_OFFSET 1014, VERSE_CELLS 1015, PACKAGE_SAVED_HASH 1016,
  OS_SUB_OBJECT_SHADOW_SERIALIZATION 1017. **The number a 5.1 editor actually writes is not
  known from the 5.8 source — read it from the company engine's `ObjectVersion.h` (spike 5).**
- **`diff.json` carries a `producer` block**: `{name, kind: native|python, engine: "5.8",
  fileVersionUE5, customVersions, capabilities: ["default-state", "text-key-only", …]}`.
  The viewer greys out kinds the producer cannot emit (e.g. `default` state needs the schema,
  text-key-only changes come from Python only) instead of showing them as "no change".
  Added 2026-10-17 so that "same return value from every analyzer" is honest about gaps.
- **Editor module dependencies**: Core, CoreUObject, Engine, Slate, SlateCore, UnrealEd,
  Kismet, UMG, UMGEditor, GraphEditor, ToolMenus, ContentBrowser, Projects, PropertyEditor,
  EditorStyle, InputCore, Json, JsonUtilities, SourceControl (provider backend; no specific
  provider plugin). **No PythonScriptPlugin dependency.** git is
  invoked through `FPlatformProcess::ExecProcess` (`git rev-parse --show-toplevel`,
  `git -c core.quotepath=off log --format=... -- <relpath>`, `git show <sha>:<relpath>`).
  Optional CLI actions spawn the bundled `Engine/Binaries/ThirdParty/Python3/Win64/python.exe -I`
  out of process. (Rejected: `IPythonScriptPlugin::ExecPythonCommandEx`.)
- **Python analyzer is schema-free by construction** (for the complete-type-name tag format,
  FileVersionUE5 >= 1012; the legacy path is the version-policy bullet above). Parse `FPackageFileSummary` with the UE5
  version gates (SavedHash ≥1016, SoftObjectPaths list ≥1008, CellExports ≥1015,
  MetaDataOffset ≥1014, ImportTypeHierarchies ≥1018, DataResourceOffset ≥1009,
  PayloadToc ≥1002), name map (FString + 2×uint16 hashes), import map, export map (incl.
  `ScriptSerializationStartOffset/EndOffset`, relative to `SerialOffset`), custom-version
  container (Optimized format). Walk tags via `FPropertyTypeName` (FName + int32 InnerCount
  node list) + int32 Size + uint8 flags (HasArrayIndex 0x01, HasPropertyGuid 0x02,
  HasPropertyExtensions 0x04, HasBinaryOrNativeSerialize 0x08, BoolTrue 0x10,
  SkippedSerialize 0x20). Model each export as `[prefix][tags…None][suffix]`: prefix = 1-byte
  `EClassSerializationControlExtension` on UObject roots (not on struct bodies); suffix =
  4-byte bool + optional 16-byte FGuid on non-CDO UObjects (`PossiblySerializeObjectGuid`),
  `GeneratedClass` FPackageIndex for `UBlueprintCore`, pin array for `UEdGraphNode`,
  `TArray<FUserPinInfo>` for `UK2Node_EditablePinBase`. Hand-written readers only for:
  Immutable layouts (NoExportTypes.h: FVector2D = 2 doubles, FVector = 3 doubles, FColor =
  BGRA u8, FLinearColor = 4 floats, FGuid = 4 u32, FIntPoint, FBox2D, FDateTime, FTimespan,
  FRotator, FQuat, FTransform, …), FText (flags + int8 history type + Base / NamedFormat /
  OrderedFormat / StringTableEntry / Transform / AsNumber…), FSoftObjectPath (int32 index into
  the summary list when SoftObjectPathsCount > 0, else FTopLevelAssetPath + sub path),
  FFrameNumber / FFrameRate / FFrameTime, FMovieSceneFloatChannel / FMovieSceneDoubleChannel
  / FMovieSceneTangentData / FMovieSceneFloatValue, FEdGraphPinType, UEdGraphPin,
  FUserPinInfo, FScriptDelegate / FMulticastScriptDelegate, FFieldPath. Any other native body
  → raw hex + `unparsed` flag; per-asset **coverage % = bytes understood / total export
  bytes**. Hard-refuse unknown FileVersionUE5 or unknown custom-version GUIDs with a clear
  message. (Rejected: schema-dependent parsing; porting UAssetAPI lists — it stops at 5.7 and
  lacks the 1018 header and FPropertyTypeName tags. The "94% without schema" figure is
  replaced by the measured coverage number.)
- **Schema dump stays in scope but is the LAST analyzer milestone and only a
  "current-state completion" layer.** Properties absent from the tag stream are filled with
  the archetype's CDO default and labelled `default`; categories/flags label rows. Diff
  verdicts must be identical with and without the schema (enforced by a test).
  (Rejected: schema as parsing prerequisite; dropping it — fails "complete current state".)
- **Diff model kinds**: `added`, `removed`, `changed` (per property old → new), `moved`
  (old/new parent + index; slot class change shown as one row), `renamed-probable` (pairing
  of removed+added with same class AND (same parent+index OR ≥ rename-threshold identical
  property set), with a confidence; it never collapses graph diffs unless a matching
  `K2Node_Variable` MemberName change is actually observed), per-property states
  `present / absent / default` (absent ↔ present is `changed (maybe default)` unless the
  schema proves equality). FText compared by source string (+ string-table id/key);
  text-key-only changes are a separate low-severity kind emitted by the Python producer only.
  Graph: nodes by NodeGuid, pins by PinId, variables by name+VarGuid, animations by name,
  tracks by binding, sections by index within track, keys by frame number.
  Widget identity = object name within the WidgetTree (same keying as `FindDiffs`).
  (Rejected: rename as plain remove+add — unreadable; rename as hard first-class kind that
  hides graph diffs on heuristic evidence.)
- **Previews**: per side `FWidgetBlueprintEditorUtils::CreateUserWidgetFromBlueprint(
  PreviewScene.GetWorld(), BP, EWidgetDesignFlags::Designing)` (ExecutePreConstruct is an
  opt-in toggle, default off), wrapped in an `SDesignSurface`-derived pane → `SZoomPan` →
  `SDPIScaler` → `Preview->TakeWidget()`; both panes share zoom/offset and are forced to the
  same preview area (new side's DesignSizeMode/DesignTimeSize; warn when the sides differ).
  One `FGCObject` session owns both `UPackage*`, both `UWidgetBlueprint*`, both
  `UUserWidget*`; releases via `DestroyUserWidget` on close and stays alive while a spawned
  Blueprint Diff window is open. (Rejected: `TakeWidget()` on WidgetTree templates — hits
  `check(!RF_ArchetypeObject)` for nested user widgets, caches live SWidgets on templates,
  skips named-slot merging and design flags.)
- **Revision sources are VCS-agnostic — three backends behind one interface** (user decided
  2026-10-18: "it must work regardless of the source-control system"). (A) **Engine Source Control
  provider**, whatever the project has connected (Perforce, Git plugin, Plastic/Unity VCS, SVN…):
  history via `FUpdateStatus::SetUpdateHistory(true)` + `ISourceControlState::GetHistorySize/
  GetHistoryItem`, download via `ISourceControlRevision::Get(OutTempFile)` into `FPaths::DiffDir()`
  (= `Saved/Diff/`), then the same load path as below. This is exactly the engine's own "Diff
  against previous" path (5.1 `SBlueprintEditorToolbar.cpp:578`; 5.8 has the
  `DiffUtils::LoadPackageForDiff(TSharedPtr<ISourceControlRevision>)` overload). The interfaces
  are identical in 5.1 and 5.8. (B) **git CLI** (`git log` / `git show` via `ExecProcess`) when no
  provider is enabled or the file has no provider history — no plugin needed. (C) **Two files**:
  any two `.uasset` paths the user obtained any way (P4V "get revision to file", SVN export, a
  backup copy); always offered. The picker shows the backends that apply, in that order; the
  one-click "compare with previous" uses the first available. `diff.json` records which backend
  produced each side (`old.kind` ∈ `scm|commit|head|working|file`). (Rejected: requiring a specific
  provider; writing P4/SVN clients; git only.)
- **Revision loading**: `git show <sha>:<relpath>` → `<Project>/Saved/Diff/temp-<sha7>-<AssetName>.uasset`
  → `DiffUtils::LoadPackageForDiff(FPackagePath::FromLocalPath(temp), FPackagePath::FromLocalPath(original))`.
  The `/Temp/` mount (→ `Saved/`) is what makes the engine take the `LOAD_ForDiff |
  LOAD_DisableCompileOnLoad` path. Working copy = the live `UWidgetBlueprint`; HEAD is offered
  as a revision when the working tree is dirty; one-click "compare with previous" and
  "compare with working copy" plus two explicit pickers. Nested UserWidgets / textures resolve
  to the project's current versions — surfaced as a badge on those rows.
  Provider-backend files land in the same `Saved/Diff/` folder (`FPaths::DiffDir()`), so the
  `/Temp/` rule holds for them too. (Rejected: system temp directory — falls through to
  `LoadPackage(LOAD_None)`, compiles on load, name collision; requiring any particular provider.)
- **Overlays**: every rect is stored root-local, pre-DPI, relative to the preview UUserWidget's
  cached SWidget geometry and recomputed in the overlay's `OnPaint` every frame from both LIVE
  painted previews (geometry is only valid for painted widgets). Removed widgets are drawn on
  the NEW side inside the nearest surviving ancestor's new rect at the old offset+size: hatched
  dashed red box, corner glyph badge, hover label "<DisplayLabel> 가 여기 있었음". When the old
  widget had no geometry (Collapsed, inactive switcher page, scrolled out) a marker chip is
  pinned to the ancestor's edge and the hierarchy always shows a ghost row at the old index.
  Colours = `SourceControl.Diff.*` style keys with 1px dark halo + 2px stroke, plus a
  "dim unchanged" toggle; colour table and rename threshold live in the plugin config.
  (Rejected: capture-once absolute geometry; hue-only outlines.)
- **Viewer layout**: summary strip (counts per kind × area: designer / slot / anim / graph
  logic / graph layout) whose chips filter hierarchy, overlays and Next/Prev; merged hierarchy
  with removed rows at OLD index (strikethrough + hatch), added rows at NEW index, ghost+arrow
  for moves, descendant-change count badges, "changed only" toggle, search over name /
  DisplayLabel / property name / old+new value text (changed matches coloured); details =
  custom 3-column grid (Property | Old | New) with humanized renderers (colour swatches,
  component-level highlight for FMargin / FAnchors / FVector2D, word-level text diff with
  string-table key, asset short name + tooltip, nested rows for style structs), grouped by
  category, with "open full details" (two linked `FDetailsDiff` views) as secondary; preview
  modes side-by-side (default, locked pan/zoom), overlay/onion-skin with opacity slider, flip
  (Space); Next/Prev hotkeys walk one flattened list across tabs; graph tab = per-graph
  Logic/Layout summary + "Open in Blueprint Diff" (`SBlueprintDiff::CreateDiffWindow`).
  (Rejected: reusing `SDetailsDiff`/`SBlueprintDiff` lists for the designer side; side-by-side
  as the only mode.)
- **Milestones (all v1, in order, each verified before the next; M0–M4 on the `ue5.8`
  branch against UE 5.8, M5 on the `ue5.1` branch)**:
  - **M0** Content Browser "Radar UMG Diff..." → revision picker → `git show` →
    `LoadPackageForDiff` ×2 → `SBlueprintDiff::CreateDiffWindow` (proves loading, lifetime,
    two temp packages of one asset).
  - **M1** native widget/slot/binding/CDO diff + animation list-level + moved/renamed
    detection + native `diff.json` producer + commandlet.
  - **M2** rendered side-by-side + overlays + red box + hierarchy + 3-column details +
    summary strip + search + Next/Prev.
  - **M3** Python analyzer: header / tags / widgets / slots / CDO + full-state dump +
    coverage % + conformance test vs native over all 34 CL9 blobs.
  - **M4** Python graph/pins + FUserPinInfo + MovieScene channels + per-key animation diff +
    schema dump; fixture WBPs; accuracy mode report.
  - **Status 2026-10-18:** M0–M4 done and verified on UE 5.8.3 (dev document §19, accuracy report
    `Plugins/RadarUMGDiff/Test/accuracy_report.md`). M5 done and verified on UE 5.1.1 the same day
    (branch `5.1`, dev document §19 M5: 5.1 build, struct-kind table, legacy tags, 5.1-saved fixtures,
    5.1 commandlet vs Python 15 pairs 0 disagreements, viewer + data-only screenshots).
  - **M5** company engine 5.1: branch `ue5.1` of the plugin repo builds against UE 5.1
    (`#if` for moved APIs — FRevisionInfo lives elsewhere before 5.2, check
    `CreateUserWidgetFromBlueprint`, `SDesignSurface`, `SZoomPan`, `LoadPackageForDiff`,
    `SBlueprintDiff::CreateDiffWindow`, ToolMenus names); `dump_struct_kinds.py` run in a 5.1
    editor → `data/struct_kinds_ue5.1.json`; Python legacy-tag path (gate 2) + the (ii)–(iv)
    consequences; fixtures = the same fixture WBPs re-created and saved by a 5.1 editor (no
    company content needed); conformance vs the `ue5.1` native commandlet; data-only viewer
    mode verified by feeding a CLI `diff.json` to the viewer. Needs access to the 5.1 engine
    source and a 5.1 editor on the machine that runs it.
  (Rejected: Python-first before any UX; landing everything in one step; starting with 5.1 —
  CL9 is the only engine on this machine.)
- **Spikes before M1 code** (cheap falsification of the assumptions everything rests on):
  (1) stdlib prototype parses one CL9 WBP header to TotalHeaderSize and walks one UWidget
  export from ScriptSerializationStartOffset to None, asserting the trailing bool(+guid) ends
  at SerialOffset+SerialSize — **done 2026-10-17 on WBP_HUD (header, 218 names, 41 imports,
  139 exports parse; tag layout confirmed)**; (2) `CreateUserWidgetFromBlueprint` on WBP_HUD
  at 65c99c5 vs 347f315 loaded via `LoadPackageForDiff`, both temp packages alive at once
  (= M0); (3) `GetCachedGeometry` validity for Collapsed / inactive-switcher widgets
  (fixture); (4) HasPropertyExtensions (0x04) never set in CL9 tags, else implement the
  extension branch.
- **Spikes before M5 code** (against the 5.1 engine source, installed locally on 2026-10-17; 5–7
  are done, results in the dev document): (5) `FileVersionUE5` the 5.1 editor writes and which of 1004–1012 it is below
  — read `Engine/Source/Runtime/Core/Public/UObject/ObjectVersion.h` of 5.1, which fixes
  consequences (ii)–(iv) above; (6) `UObject::Serialize` order in 5.1 for a linker archive:
  what, if anything, precedes the tag stream and what follows it (`PossiblySerializeObjectGuid`
  bool+guid), and the `UWidget` / `UWidgetBlueprint` / `UEdGraphNode` suffixes; (7) the
  struct-kind dump: confirm `UScriptStruct::GetCppStructOps()->HasSerializer()` /
  `HasStructuredSerializer()` and `STRUCT_Immutable` are reachable from editor Python (else
  a tiny commandlet); (8) build the `ue5.8` branch's C++ against 5.1 once and list every
  compile error — that list is the `#if` budget of the `ue5.1` branch.

## Acceptance Criteria
- [x] Right-clicking a Widget Blueprint in the Content Browser shows "Radar UMG Diff..."; the
      picker lists the asset's history from the connected Source Control provider when one is
      enabled (revision, changelist, date, user, description), otherwise its git commits (sha7,
      date, author, subject), plus working copy and HEAD; "compare two files" is always offered;
      "compare with previous" opens the viewer in one click.
      — 2026-10-18 M0: verified in the 5.8 editor (context menu → picker; git tab: working
      copy (수정됨) + HEAD + 11 commits; Perforce tab against the local p4d: #1~#11 with CL,
      date, user, description; "파일 두 개" tab always present; "이전과 비교" opened 65c99c5 ↔
      working copy (git) and #11 ↔ working copy (Perforce)). The viewer it opens is the engine
      Blueprint Diff window until M2 replaces it. Screenshots `Saved/RadarUMGDiff/shots/m0_*.png`.
- [x] Provider backend verified **against a real Perforce server on this machine** (user decided
      2026-10-18: "install P4 and check"): a local Helix Core server (`p4d`) holds the CL9 Widget
      Blueprints with their git history replayed as P4 submits; the editor is connected through the
      bundled `PerforceSourceControl` plugin (present in 5.1 and 5.8); the picker lists the P4
      revisions (revision, changelist, user, description) and the diff opens; the result equals the
      git-CLI diff of the same two revisions. The bundled Git provider plugin is checked the same way
      as a second provider. Procedure: dev document §8.4.
      — progress 2026-10-18 (M0): the editor connected to the local p4d with the existing
      ticket (no password typed), the picker listed #1~#11 and "이전과 비교" opened #11 ↔
      working copy; the file the provider fetched (`temp-scm11-WBP_HUD.uasset`) is byte-identical
      to `git show 65c99c5` (SHA256). Still open: the diff.json equality (needs M1) and the
      bundled Git provider run.
      — verified 2026-10-18 (M4): all 10 adjacent WBP_HUD pairs dumped three ways —
      Perforce provider (`scm:1..11`), bundled Git provider (`scm:<sha8>`, picker tab "소스 컨트롤(Git)"
      lists the 11 revisions) and git CLI (`git:<sha>`): widgets, bindings, CDO, variables,
      animations, graphs and summary are identical in all three (0 differing fields). Editor set
      back to `Provider=None` afterwards (dev document §19 M4).
- [x] Both revisions render side by side at the same design size with locked pan/zoom; overlay
      and flip modes work.
      — verified 2026-10-18 (M2): WBP_HUD 347f315 ↔ 65c99c5 and WBP_Fix_Layout v1 ↔ v2; screenshots
      `m2_side_by_side`, `m2_overlay`, `m2_flip_old/new` (dev document §19 M2).
- [x] Changed widgets get an outline; added widgets get an outline on the new side; removed
      widgets get a hatched red box on the new side at their old position inside the surviving
      ancestor, with a hover label, and a ghost row in the hierarchy at the old index; widgets
      with no geometry get an edge marker chip.
      — verified 2026-10-18 (M2): WBP_Fix_Layout — HpBar and Collapsed HiddenNote get hatched
      boxes with the hover label, the inactive-switcher PageBText gets a chip on the Pages edge,
      Icon gets a ghost row and an arrow (spike 3 result in dev document §9.6).
- [x] Selecting a widget/slot shows only its changed properties as Property | Old | New with
      humanized values; "open full details" shows the engine details views.
      — verified 2026-10-18 (M2): colour chips, (기본) labels, `FDetailsDiff` pair (`m2_full_details`).
- [x] Search matches name / DisplayLabel / property / value and colours changed matches;
      "changed only" filter and summary-strip chips filter hierarchy, overlays and Next/Prev.
      — verified 2026-10-18 (M2): `m2_search`, `m2_chip_filter`; F8/F7 walk the filtered list.
- [x] Animation tab lists added/removed/changed animations and, per track/section/key, the
      changed values (verified on fixtures).
      — verified 2026-10-18 (M4): WBP_Fix_Anim v1→v4 (key value, interp mode,
      key frame move + section range, track removed, animation added) — native and Python agree
      key by key (`m4_anim_tab`).
- [x] Graph tab summarizes Logic vs Layout changes per graph (nodes, pins, variables,
      functions) and opens the engine Blueprint Diff window for detail.
      — verified 2026-10-18 (M4): WBP_Fix_Graph v1→v5 (node added,
      pin default, layout-only move, variable rename with VarGuid kept, variable type change);
      per-graph logic/layout counts and node list (`m4_graph_tab`); Blueprint Diff button (M2).
- [x] `umgdiff_cli.py dump <uasset>` writes a full-state JSON with coverage %; `umgdiff_cli.py
      diff <old> <new>` writes `diff.json`; both run on the bundled Python 3.11 and system
      Python 3.14 with no third-party imports.
      — verified 2026-10-18 (M4): 21 unit tests on 3.9.7 / 3.11.8 / 3.14.7;
      `diff` and `dump --schema` run on the bundled 3.11; stdlib only.
- [x] `-run=RadarUMGDiff.Dump` writes the same `diff.json` schema natively; the conformance
      test over all CL9 WBP blobs + fixtures reports zero verdict disagreements (added /
      removed / changed sets) for widgets, slots and CDO, and lists any unparsed native bytes
      with their struct names.
      — verified 2026-10-18 (M4): commandlet `-run=RadarUMGDiffDump` (single pair and
      `-history`) output equals the in-editor dump field for field; conformance: 22 CL9 git pairs
      + 15 fixture pairs, 0 verdict disagreements (also over bindings, variables, animations,
      graphs). Unparsed bytes are listed per struct in `Plugins/RadarUMGDiff/Test/accuracy_report.md`
      — only the UStruct bodies of compiled class/function exports (in-scope coverage 1.0000).
- [x] Diff verdicts are identical with and without `schema.json`; with the schema, the dump
      shows default-valued properties labelled `default`.
      — verified 2026-10-18 (M4): `umgdiff_cli.py schema-check` over 37 pairs:
      0 verdicts changed, 123 "maybe default" rows resolved by the schema; `dump --schema` fills
      unsaved properties as `state: default`. Schema from `RadarUMGDiff.DumpSchema` via
      `editor/dump_schema.py` (editor Python cannot see non-BlueprintVisible properties).
- [x] Colour table, rename threshold and other tunables are read from
      `Config/DefaultRadarUMGDiff.ini`; no rebuild needed to change them.
      — verified 2026-10-18 (M4): changed `ColorRemoved`, restarted the
      editor without building — removed boxes and chips took the new colour (`m4_ini_color_crop`),
      then restored; the Python analyzer reads the same ini on every run.
- [x] Plugin builds with `CL9Editor` and contains no reference to CL9 game modules.
      — verified 2026-10-18 (M4): broker builds OK, warnings 0; "CL9" occurs 0 times in
      the plugin's Source, Build.cs and .uplugin.
- [x] (M5) The `ue5.1` branch builds against UE 5.1 and shows the same viewer (previews,
      overlays, hierarchy, details) on 5.1-saved Widget Blueprints.
      — verified 2026-10-18 (M5, UE 5.1.1): branch `5.1` builds (`RadarHost51Editor`, a blank 5.1 C++
      project); every 5.1 compile/link error is an `#if` in `RadarEngineCompat.h` (dev §19 M5 table).
      WBP_Fix_Layout v1↔v2 saved by the 5.1 editor opens with previews, overlays (hatched removed boxes,
      inactive-page chip, move arrow), hierarchy and details as on 5.8 (`m5_layout_side_by_side`); a 5.8 CLI
      `diff.json` opens in the 5.1 viewer in data-only mode (`m5_data_only_58json`).
- [x] (M5) `umgdiff_cli.py dump` on a 5.1-saved fixture reads the legacy tags via the
      struct-kind table, reports coverage %, and the conformance test against the `ue5.1`
      native commandlet reports zero verdict disagreements for widgets, slots and CDO.
      — verified 2026-10-18 (M5): `data/struct_kinds_ue5.1.json` from `-run=RadarStructKinds` (3,667 structs,
      344 set/map properties); all 21 fixtures saved by the 5.1 editor (FileVersionUE5 1008) parse, coverage
      in scope 1.0000 (total 0.9464); 5.1 commandlet vs Python over 15 fixture pairs: 0 disagreements, 0 deferred.
- [x] `diff.json` from every producer carries the `producer` block; the viewer greys out
      kinds the producer declares it cannot emit, and runs in data-only mode when the current
      editor cannot load one of the two revisions.
      — verified 2026-10-18 (M2): native and Python diff.json both carry `producer`; "diff.json 열기..."
      opens a Python-made file in data-only mode with the unsupported chips greyed (`m2_data_only`).

## Related Files / Modules
| File | Role |
|------|------|
| `Plugins/RadarUMGDiff/RadarUMGDiff.uplugin` | Plugin descriptor (Editor module only) |
| `Plugins/RadarUMGDiff/Source/RadarUMGDiffEditor/` | C++ editor module (menu, git, loading, native diff, viewer, commandlet) |
| `Plugins/RadarUMGDiff/Content/Python/radar_umgdiff/` | Pure-stdlib analyzer package (reader, tags, native structs, dump, diff, conformance) |
| `Plugins/RadarUMGDiff/Content/Python/umgdiff_cli.py` | CLI entry (`dump`, `diff`, `conform`, `history`) |
| `Plugins/RadarUMGDiff/Content/Python/editor/dump_schema.py` | Editor-side schema exporter |
| `Plugins/RadarUMGDiff/Content/Python/editor/dump_struct_kinds.py` | Editor-side struct-kind exporter (per engine version, for the legacy tag reader) |
| `Plugins/RadarUMGDiff/Content/Python/radar_umgdiff/data/struct_kinds_<engine>.json` | Struct-kind table (data file, one per engine version; only `ue5.1` needs it today) |
| `Plugins/RadarUMGDiff/Content/Test/` | Fixture Widget Blueprints |
| `Plugins/RadarUMGDiff/Config/DefaultRadarUMGDiff.ini` | Tunables |
| `Plugins/RadarUMGDiff/README.md` | How to use, diff.json schema, coverage/accuracy notes |
| `CL9.uproject` | Enables the plugin |
| GitHub repo for the plugin (name/URL to be set at implementation start), branches `ue5.8`, `ue5.1` | Distribution; CL9 holds the `ue5.8` branch (submodule vs copy: open) |
| Engine (5.8): `Runtime/CoreUObject/Private/UObject/PropertyTag.cpp` (legacy vs complete-type-name tag readers), `Runtime/Core/Public/UObject/ObjectVersion.h` (FileVersionUE5 numbers) | Format evidence for the version gates |
| Engine: `Editor/UnrealEd/Public/DiffUtils.h`, `Editor/UMGEditor/Public/WidgetBlueprintEditorUtils.h`, `Editor/Kismet/Public/SBlueprintDiff.h`, `Editor/Kismet/Public/DetailsDiff.h` | APIs reused |

## Must-Preserve
- CL9 game code and content are untouched; the plugin must compile without any CL9 module.
- No `.uasset` is modified by the tool; temp files live only under `Saved/Diff/` and
  `Saved/RadarUMGDiff/`.
- Build requests go through the build broker (`Tools/Build/build_request.py`), never a direct
  UBT call from the session; the editor is not killed by this work.
- Korean UI strings only; comments in Korean (repo convention), Copyright header excepted.

## Execution Notes
- Recommended model: Claude Fable 5.1 for the parser (binary format reasoning), the overlay
  geometry/Slate work and the diff model; Sonnet acceptable for the commandlet plumbing,
  config/ini scaffolding, README and the CLI argument surface.
- This document cannot enforce the model — the executing session's `/model` setting decides.
  If the session model is below the recommendation, surface it to the user and confirm before
  proceeding.
- Builds are asynchronous (build broker); keep working on the Python side while the editor
  module compiles.
- 2026-10-17 (second talk, no code yet): the user stopped implementation to settle the
  engine-version question; the result is the three bullets "Engine-version axis",
  "Distribution" and "Python analyzer version policy" plus the `producer` block, M5 and
  spikes 5–8 above. Implementation happens in a separate session starting from this document.
- Asked once at the start of the chain and answered 2026-10-18: repo `RadarUMGDiff`, public;
  submodule; branches `5.8` / `5.1`; `bIsVariable` / `DesignerFlags` — "support both, applied
  as the user asks": producers keep them and mark them `auto`, the viewer has an "자동 값
  숨기기" toggle (default from ini `bHideAutoDefault`), CLI has the same switch (dev §12).
- Implementation guide: `C:\Work\CL9\docs\goal\radar-umg-diff-dev.md` (2026-10-17) — file layout, Build.cs,
  diff.json/state.json schemas, per-engine compat table, byte layouts verified against the 5.8 and
  5.1 sources, milestone task lists, and the prompt for the implementing session. Spikes 5–7 are
  done there: a 5.1 editor writes FileVersionUE5 **1008**, no export prefix byte, struct-kind table
  must come from a commandlet (editor Python exposes no StructFlags), bundled Python is 3.9.7.
- UE 5.1.1 is now installed on this machine (`C:\Program Files\Epic Games\UE_5.1`, 2026-10-17), so
  M5 can be built and tested here; company content is still not needed (fixtures are re-saved by
  the 5.1 editor).
