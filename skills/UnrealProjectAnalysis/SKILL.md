---
name: UnrealProjectAnalysis
description: Analyze Unreal Engine project gameplay logic by reading C++, Config, Blueprint, and Asset data together.
---

## Purpose

Use this skill as the primary workflow when the user asks to analyze all or part of an Unreal Engine project, including gameplay behavior, feature flow, system structure, project logic, or the relationship between C++ classes, config, Blueprint, and assets.

The goal is to explain runtime behavior from evidence across source code, config, Blueprint graphs, component defaults, CDO values, input assets, animation/UI assets, and content references.

## Default Workflow

1. Clarify the user's intent when the requested analysis scope is ambiguous.

Ask what behavior, feature, bug, class, map, Blueprint, or asset path they want analyzed. If the user already provided a clear target, proceed without asking.

2. Classify what must be inspected.

Identify the likely relevant source files, config files, assets, Blueprints, input mappings, animation assets, widgets, maps, data assets, and references needed to answer the user's question.

3. Read source and config evidence.

Search and read relevant C++ headers, C++ implementation files, `.Build.cs`, target files, and `Config/*.ini`. Use source code to identify class ownership, inheritance, Blueprint hooks, reflected properties, input bindings, delegates, components, constructors, lifecycle functions, and runtime control flow.

If a relevant `UPROPERTY` can be edited or overridden by Blueprint defaults, classify the owning Blueprint asset as required evidence and read it before finalizing the analysis.

4. Inspect assets when asset data affects the answer.

Locate related assets from C++ properties, config paths, soft object references, input mapping contexts, animation classes, widgets, Blueprint class references, maps, naming patterns, or asset registry relationships.

When binary asset data must be read, use the `asset-to-json` command, such as for Blueprint graphs, Blueprint variables, component defaults, CDO values, timelines, Enhanced Input assets, widget structure, animation data, or asset metadata. Do not treat `.uasset` binary files as directly readable source.

5. Cross-validate the evidence.

Connect C++ implementation, config values, Blueprint graph execution, component defaults, CDO/default values, and asset references into one runtime explanation. Prefer explaining behavior over listing files or nodes.

6. Report confidence and limits.

State what evidence was used, what was cross-validated, and what could not be inspected. Distinguish confirmed behavior from inference.

## C++ And Config Analysis

- Read the local class hierarchy first, especially base character, pawn, controller, component, ability, animation, UI, and feature-specific classes.
- Inspect `UPROPERTY` and `UFUNCTION` metadata to understand Blueprint-facing extension points and defaults.
- When a relevant `UPROPERTY` is Blueprint-editable, Blueprint-readable with asset-assigned defaults, exposed on spawn, instanced, or otherwise likely to differ from the C++ constructor value, inspect the owning Blueprint before treating the C++ value as final behavior.
- Check constructors, `BeginPlay`, `SetupPlayerInputComponent`, tick functions, overlap callbacks, delegates, timers, RPCs, and component initialization.
- Check relevant `.ini` files for maps, GameMode, input settings, redirects, collision channels, plugin settings, and project defaults.
- Preserve Unreal terminology precisely: package path, object path, generated class, CDO, component, graph, pin, node, mapping context, action, and asset registry data.

## Asset Path Normalization

Accept common Unreal asset path forms and convert them to a long package path or object path as needed:

```text
/Game/Variant_Combat/Blueprints/BP_CombatCharacter
/Game/Variant_Combat/Blueprints/BP_CombatCharacter.BP_CombatCharacter
Content/Variant_Combat/Blueprints/BP_CombatCharacter.uasset
BP_CombatCharacter when the location is obvious from search results
```

If the path is ambiguous, locate it with file search, usually by searching `Content/**/*.uasset` for the asset name.

## asset-to-json Command

Use `asset-to-json` only when asset internals are needed for the analysis.

Command arguments:

- `<asset-path>`: Unreal package path, object path, or recognizable asset file path.
- `--include-node-properties`: include detailed Blueprint node properties when needed.
- `--save`: save JSON to the default output location.
- `--output <path>`: save JSON to a specific output path.

Examples:

```text
/asset-to-json /Game/Variant_Combat/Blueprints/BP_CombatCharacter
/asset-to-json /Game/Variant_Combat/Blueprints/BP_CombatCharacter --include-node-properties
/asset-to-json /Game/Variant_Combat/Blueprints/BP_CombatCharacter --output Saved/AssetToJson/BP_CombatCharacter.json
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
