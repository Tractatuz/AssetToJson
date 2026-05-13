---
name: UnrealProjectAnalysis
description: Analyze Unreal Engine project gameplay logic by reading C++, Config, Blueprint, and Asset data together.
---

## Purpose

Use this skill as the primary workflow when agent need to analyze all or part of an Unreal Engine project.
The goal is to explain runtime behavior from evidence across source code, config, Blueprint graphs, component defaults, CDO values, input assets, animation/UI assets, content references and so on.

## Workflow

1. Ask the user's intent when the requested analysis scope is ambiguous.

If everything is not deterministically certain, be sure to ask questions.

2. Classify what must be inspected.

Identify the likely relevant source files, config files, assets, Blueprints, input mappings, animation assets, widgets, maps, data assets, and references needed to answer the user's question.

3. Read source and config evidence.

Search and read relevant C++ headers, C++ implementation files, `.Build.cs`, target files, and `Config/*.ini`. Use source code to identify class ownership, inheritance, Blueprint hooks, reflected properties, input bindings, delegates, components, constructors, lifecycle functions, and runtime control flow.

If a relevant `UPROPERTY` can be edited or overridden by Blueprint defaults(EditDefaultsOnly | EditAnywhere | BlueprintReadWrite), classify the owning Blueprint asset as required evidence and read it before finalizing the analysis.

4. Inspect assets when asset data affects the answer.

Locate related assets from C++ properties, config paths, soft object references, input mapping contexts, animation classes, widgets, Blueprint class references, maps, naming patterns, or asset registry relationships.

When source analysis starts from a C++ class and Blueprint subclasses may override defaults, components, graphs, animation classes, widgets, events, or exposed properties, first discover derived Blueprint assets with `scripts/find_derived_blueprint_assets.ps1`. Use the discovered assets to decide which specific Blueprints must be inspected with `scripts/read_asset_json.ps1`.

When binary asset data must be read, use `scripts/read_asset_json.ps1`, such as for Blueprint graphs, Blueprint variables, component defaults, CDO values, timelines, Enhanced Input assets, widget structure, animation data, or asset metadata. Do not treat `.uasset` binary files as directly readable source.

5. Cross-validate the evidence.

Connect C++ implementation, config values, Blueprint graph execution, component defaults, CDO/default values, and asset references into one runtime explanation. Prefer explaining behavior over listing files or nodes.

6. Report confidence and limits.

State what evidence was used, what was cross-validated, and what could not be inspected. Distinguish confirmed behavior from inference.

## C++ And Config Analysis

- Read the local class hierarchy first, especially base character, pawn, controller, component, ability, animation, UI, and feature-specific classes.
- Inspect `UPROPERTY` and `UFUNCTION` metadata to understand Blueprint-facing extension points and defaults.
- When a relevant `UPROPERTY` is Blueprint-editable, Blueprint-readable with asset-assigned defaults, exposed on spawn, instanced, or otherwise likely to differ from the C++ constructor value, inspect the owning Blueprint before treating the C++ value as final behavior.
- If the owning Blueprint is not already known, search for Blueprint assets derived from the relevant native or Blueprint parent class before finalizing the analysis.
- Check constructors, `BeginPlay`, `SetupPlayerInputComponent`, tick functions, overlap callbacks, delegates, timers, RPCs, and component initialization.
- Check relevant `.ini` files for maps, GameMode, input settings, redirects, collision channels, plugin settings, and project defaults.
- Preserve Unreal terminology precisely: package path, object path, generated class, CDO, component, graph, pin, node, mapping context, action, and asset registry data.

## Derived Blueprint Discovery

Use this step when you know a C++ or Blueprint parent class but do not know which Blueprint assets inherit from it. The helper uses Unreal Asset Registry derived-class data, so indirect inheritance through native C++ classes and Blueprint generated classes is included unless `-DirectOnly` is specified. For example, searching `/Script/Engine.Actor` can return Blueprints whose immediate parent is a project C++ class that ultimately derives from `AActor`.

Run the helper from the skill root with the Bash tool using PowerShell syntax:

```powershell
& "scripts\find_derived_blueprint_assets.ps1" -ClassPath "/Script/TestTPS.TestTPSCharacter" -NoOutputFile
```

Supported options:

- `-ClassPath <path>`: parent class path, usually `/Script/<Module>.<ClassName>` for native C++ classes or a generated class path for Blueprint classes.
- `-SearchPaths <paths>`: content roots to scan, default is `/Game`.
- `-DirectOnly`: include only Blueprints whose immediate parent class is the target class.
- `-NoOutputFile`: return JSON in command output for immediate context.
- `-OutputJson <path>`: save JSON to a specific path.
- `-TimeoutSeconds <seconds>` and `-EnginePath <path>`: remote execution configuration.

The helper requires an editor instance with Python Remote Execution available. If no remote node is found, report that Blueprint discovery could not be performed; do not guess that no derived Blueprints exist.

Run derived Blueprint discovery commands sequentially. Do not run multiple `find_derived_blueprint_assets.ps1` instances in parallel, and do not parallelize them with other Unreal Remote Execution commands, because the editor command socket can reject or reset concurrent connections.

Interpret the returned JSON fields:

- `assets[].path`: package path to pass to `scripts/read_asset_json.ps1`.
- `assets[].object_path`: full Blueprint asset object path.
- `assets[].parent_class`: immediate parent class.
- `assets[].generated_class`: generated class for runtime inheritance checks.
- `assets[].is_direct_child`: whether the Blueprint directly inherits from the searched class.

If many assets are returned, prioritize ones referenced by maps, config, GameMode defaults, input/animation/widget properties, nearby feature folders, or naming patterns. Then read the selected assets with `scripts/read_asset_json.ps1`.

## Asset Path Normalization

Accept common Unreal asset path forms and convert them to a long package path or object path as needed:

```text
/Game/<FolderPath>/<AssetName>
/Game/<FolderPath>/<AssetName>.<AssetName>
Content/<FolderPath>/<AssetName>.uasset
<AssetName> when the location is obvious from search results
```

If the path is ambiguous, locate it with file search, usually by searching `Content/**/*.uasset` for the asset name.

## Asset JSON Helper

Use `scripts/read_asset_json.ps1` only when asset internals are needed for the analysis.

Run the helper from the skill root with the Bash tool using PowerShell syntax:

```powershell
& "scripts\read_asset_json.ps1" -AssetPath "/Game/<FolderPath>/<AssetName>" -NoOutputFile
```

Supported options:

- `-AssetPath <path>`: Unreal package path, object path, or recognizable asset file path.
- `-IncludeNodeProperties`: include detailed Blueprint node properties when needed.
- `-NoOutputFile`: return JSON in command output for immediate context.
- `-OutputJson <path>`: save JSON to a specific output path.
- `-TimeoutSeconds <seconds>` and `-EnginePath <path>`: remote execution configuration.

Examples:

```powershell
& "scripts\read_asset_json.ps1" -AssetPath "/Game/<FolderPath>/<AssetName>" -NoOutputFile
& "scripts\read_asset_json.ps1" -AssetPath "/Game/<FolderPath>/<AssetName>" -IncludeNodeProperties -NoOutputFile
& "scripts\read_asset_json.ps1" -AssetPath "/Game/<FolderPath>/<AssetName>" -OutputJson "Saved/AssetToJson/<AssetName>.json"
```

If multiple assets must be inspected, export and read them one at a time.

## Blueprint Interpretation

For Blueprint visual scripts, interpret these JSON sections when present:

- `asset`: parent class, generated class, object path
- `blueprint`: type and compile status
- `graphs`: graph name/type, nodes, pins, and links
- `variables`: Blueprint-declared variables and default text values
- `components`: Blueprint SCS component tree

Build execution flows from linked exec pins first, then data pins.

- Identify entry nodes: events, function entries, construction script entries, timeline updates, delegate events.
- Follow output exec pins to call nodes, branch nodes, macro nodes, timeline nodes, and latent nodes.
- Map important data connections: event parameters, variable gets/sets, function inputs, default objects, and default scalar values.
- Ignore comment nodes except when they clarify intent.

If asset data is missing, stale, inaccessible, or only partially exported, say so explicitly and report what was still observable.

## Response Style

- Answer in Korean unless the user asks otherwise.
- Start with the main gameplay conclusion when the task is analysis-oriented.
- Mention the evidence used: C++ files, config files, asset paths, exported JSON, or cached JSON.
- Include exact class names, function names, asset paths, and key defaults when they matter.
- For Blueprint visual scripts, explain runtime behavior rather than only listing nodes.
- State limitations clearly, including inaccessible assets, unavailable exports, stale cached data, or inferred behavior.

## Safety

- Do not modify assets while inspecting them.
- Do not save the project or resave packages unless the user explicitly asks.
- Do not run destructive editor commands.
- Keep generated JSON under `Saved/` unless the user asks for another output path.
