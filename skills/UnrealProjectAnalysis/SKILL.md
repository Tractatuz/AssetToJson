---
name: UnrealProjectAnalysis
description: Analyze Unreal project gameplay logic by reading C++, Config, Blueprint, and Asset data together, using AssetToJson when binary assets must be inspected.
compatibility: opencode
metadata:
  project: TestTPS
  engine: UE_5.7
---

## Purpose

Use this skill when the user asks to analyze gameplay behavior, trace a feature, explain an Unreal system, diagnose project logic, or connect C++ classes with Blueprint and asset behavior in this project.

The goal is to understand Unreal gameplay logic across source code, config, Blueprint graphs, component defaults, CDO values, input assets, animation/UI assets, and content references. Treat `AssetToJson` as a supporting tool for binary `.uasset` inspection, not as the default starting point.

## Default Workflow

1. Start with readable project files.

Search and read relevant C++ headers, C++ implementation files, `.Build.cs`, target files, and `Config/*.ini` before opening binary assets. Use source code to identify class ownership, inheritance, Blueprint hooks, reflected properties, input bindings, delegates, components, and runtime control flow.

2. Locate related assets only when they affect behavior.

Use `glob` for `Content/**/*.uasset` and naming patterns from C++ properties, config paths, soft object references, input mapping contexts, animation classes, widgets, or Blueprint class references.

3. Use AssetToJson when binary asset internals matter.

Export Blueprint graphs, Blueprint variables, component defaults, CDO values, timelines, Enhanced Input assets, widget structure, animation data, or asset metadata when source/config alone cannot answer the question. Do not treat `.uasset` binary files as directly readable source.

Run AssetToJson exports and any Unreal Python Remote Execution asset must reads sequentially, never in parallel. The editor remote execution bridge can only reliably service one command connection at a time, and concurrent exports can abort or time out.

4. Cross-reference the evidence.

Connect C++ implementation, config values, Blueprint graph execution, component defaults, and asset references into one runtime explanation. Prefer explaining behavior over listing files or nodes.

5. Report confidence and limits.

State when an answer is based only on source/config, when a Blueprint was exported, when cached JSON was used, or when remote asset inspection was unavailable.

## C++ And Config Analysis

- Read the local class hierarchy first, especially base character, pawn, controller, component, ability, animation, UI, and variant-specific classes.
- Inspect `UPROPERTY` and `UFUNCTION` metadata to understand Blueprint-facing extension points and defaults.
- Check constructors and `BeginPlay`, `SetupPlayerInputComponent`, tick functions, overlap callbacks, delegates, timers, RPCs, and component initialization.
- Check `Config/DefaultEngine.ini`, `Config/DefaultGame.ini`, `Config/DefaultInput.ini`, and other relevant `.ini` files for maps, GameMode, input settings, redirects, collision channels, Python remote execution, and plugin settings.
- Preserve Unreal terminology precisely: package path, object path, generated class, CDO, component, graph, pin, node, mapping context, action, and asset registry data.

## AssetToJson Preconditions

- The Unreal Editor should already be running for live asset inspection.
- Python remote execution is enabled by `Config/DefaultEngine.ini` under `/Script/PythonScriptPlugin.PythonScriptPluginSettings` with `bRemoteExecution=True`.
- The `AssetToJson` plugin is available for Blueprint visual script export and related asset exports.
- The `/asset-to-json` command exists at `.opencode/commands/asset-to-json.md` for argument-driven exports.
- The remote execution client is expected at `[EnginePath]\Plugins\Experimental\PythonScriptPlugin\Content\Python\remote_execution.py`.

## Asset Path Normalization

Accept any of these forms and convert them to a long package path or object path as needed:

```text
/Game/Variant_Combat/Blueprints/BP_CombatCharacter
/Game/Variant_Combat/Blueprints/BP_CombatCharacter.BP_CombatCharacter
Content/Variant_Combat/Blueprints/BP_CombatCharacter.uasset
BP_CombatCharacter when the location is obvious from search results
```

If the path is ambiguous, locate it with `glob`, usually by searching `Content/**/*.uasset` for the asset name.

## AssetToJson Workflow

Use the `/asset-to-json` command when the user explicitly supplies an asset path or asks for command-style execution.

If multiple assets must be inspected, export them one at a time and wait for each command to finish before starting the next.

```text
/asset-to-json /Game/Variant_Combat/Blueprints/BP_CombatCharacter
/asset-to-json /Game/Variant_Combat/Blueprints/BP_CombatCharacter --include-node-properties
/asset-to-json /Game/Variant_Combat/Blueprints/BP_CombatCharacter --output Saved/AssetToJson/BP_CombatCharacter.json
```

The command defaults to `-NoOutputFile` unless `--save` or `--output` is provided, so small JSON payloads can be used directly in agent context. For large Blueprints, prefer saving to `Saved/AssetToJson/` and reading only the relevant sections.

When invoking the helper directly is more appropriate, run it from the repo root. It connects to the already-running editor, invokes `unreal.AssetToJsonLibrary.read_blueprint_visual_script_as_json`, and writes JSON under `Saved/AssetToJson/` by default.

```powershell
& ".agents\skills\UnrealProjectAnalysis\scripts\read_asset_json.ps1" -AssetPath "/Game/Variant_Combat/Blueprints/BP_CombatCharacter" -IncludeNodeProperties
```

Use `-NoOutputFile` when the agent only needs the returned JSON in command output and does not need a saved artifact. Prefer a file for large Blueprints because terminal output can be truncated.

```powershell
& ".agents\skills\UnrealProjectAnalysis\scripts\read_asset_json.ps1" -AssetPath "/Game/Variant_Combat/Blueprints/BP_CombatCharacter" -NoOutputFile
```

Use `-OutputJson "Saved\AssetToJson\BP_CombatCharacter.json"` when a stable output path is needed. Use `-EnginePath` only when the engine is not installed at `C:\EpicGames\UE_5.7`.

If the helper script is not suitable, connect to the running Unreal Editor with Python Remote Execution manually.

```powershell
$env:PYTHONIOENCODING='utf-8'; @'
import sys
import time

sys.path.append(r'C:\EpicGames\UE_5.7\Engine\Plugins\Experimental\PythonScriptPlugin\Content\Python')
import remote_execution

r = remote_execution.RemoteExecution()
r.start()

deadline = time.time() + 10
while not r.remote_nodes and time.time() < deadline:
    time.sleep(0.1)

if not r.remote_nodes:
    raise RuntimeError('No Unreal remote execution nodes found')

node = r.remote_nodes[0]
print('node', node)
r.open_command_connection(node['node_id'])

code = r'''
import unreal

asset_path = 'ASSET_PATH'
output_json = r'OUTPUT_JSON'

result = unreal.AssetToJsonLibrary.read_blueprint_visual_script_as_json(
    asset_path,
    output_json,  # Leave empty to avoid writing a file.
    True,
    True,
    True,
)

print(result[:1000])
print('WROTE_JSON', output_json, 'LENGTH', len(result))
'''

res = r.run_command('exec(' + repr(code) + ')', exec_mode=remote_execution.MODE_EXEC_STATEMENT)
print('success', res.get('success'))
print('result', res.get('result'))
print('output', res.get('output'))

r.close_command_connection()
r.stop()
'@ | py -3
```

## Cached JSON Fallback

If Python Remote Execution fails before the bridge can read the asset, for example `No Unreal remote execution nodes found`, do not guess from `.uasset` binaries. Check whether a prior export exists under `Saved/AssetToJson/` for the normalized asset name. Prefer the exact expected output path if one was supplied; otherwise derive the helper's default safe name from the package path, for example `/Game/Variant_Combat/Blueprints/BP_CombatCharacter` becomes `Saved/AssetToJson/_Game_Variant_Combat_Blueprints_BP_CombatCharacter.json`, and also check stable names such as `Saved/AssetToJson/BP_CombatCharacter.json`.

Use an existing JSON only if it parses successfully. State clearly that the answer is based on a saved prior export, including its path and timestamp if available. If no usable saved JSON exists, report that the asset could not be read because remote execution failed and no cached export was available.

```powershell
$AssetName = "BP_CombatCharacter"
$Expected = "Saved\AssetToJson\_Game_Variant_Combat_Blueprints_BP_CombatCharacter.json"
$Stable = "Saved\AssetToJson\$AssetName.json"
$Candidates = @($Expected, $Stable) | Where-Object { Test-Path -LiteralPath $_ }
$Candidates | ForEach-Object { Get-Item -LiteralPath $_ | Format-List FullName,Length,LastWriteTime }
```

The bridge writes UTF-8 JSON. Use Python to load it when a concise summary is needed:

```powershell
$env:PYTHONIOENCODING='utf-8'; @'
import json
from pathlib import Path

path = Path(r'OUTPUT_JSON')
data = json.loads(path.read_text(encoding='utf-8'))

print('asset', data.get('asset'))
for graph in data.get('graphs', []):
    print('GRAPH', graph.get('type'), graph.get('name'), 'nodes', len(graph.get('nodes', [])), 'links', len(graph.get('links', [])))
for variable in data.get('variables', []):
    print('VAR', variable.get('name'), variable.get('category'), variable.get('default_value'), variable.get('type'))
'@ | py -3
```

## Blueprint Interpretation

For Blueprint visual scripts, interpret these JSON sections:

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

For Timeline nodes, the bridge JSON may expose the node and pins but not every curve key. Inspect the loaded objects remotely when exact curve behavior matters.

```python
import unreal

for obj in unreal.ObjectIterator():
    try:
        cls = obj.get_class().get_name()
        path = obj.get_path_name()
    except Exception:
        continue

    if 'ASSET_NAME' in path and ('TimelineTemplate' in cls or 'CurveFloat' in cls or 'TimelineComponent' in cls):
        print('OBJ', cls, path)
        if cls == 'TimelineTemplate':
            print(' length', obj.get_editor_property('timeline_length'))
            print(' length_mode', obj.get_editor_property('length_mode'))
        if cls == 'CurveFloat':
            print(' time_range', obj.get_time_range())
            print(' value_range', obj.get_value_range())
            for t in [0, 0.25, 0.5, 0.75, 1.0]:
                print(' eval', t, obj.get_float_value(t))
```

For Blueprint CDO/default values, load the generated class and inspect the CDO.

```python
import unreal

generated_class = unreal.load_object(None, '/Game/Path/Asset.Asset_C')
cdo = unreal.get_default_object(generated_class)
print(cdo)
print(cdo.get_editor_property('Property Name'))
```

If the asset is not a Blueprint or the bridge returns an asset-level error while remote execution is connected, fall back to Unreal Python reflection. Do not use this fallback when Python Remote Execution itself failed or no remote node was found.

```python
import unreal

asset = unreal.load_object(None, 'ASSET_OBJECT_PATH')
print('asset', asset)
print('class', asset.get_class().get_path_name() if asset else None)
print('path', asset.get_path_name() if asset else None)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
asset_data = registry.get_asset_by_object_path('ASSET_OBJECT_PATH')
print('asset_data', asset_data)
print('tags', asset_data.tags_and_values if asset_data.is_valid() else None)
```

Use class-specific Unreal Python APIs if they exist. If properties are protected or inaccessible from Python, say so explicitly and report what was still observable.

## Response Style

- Answer in Korean unless the user asks otherwise.
- Start with the main gameplay conclusion when the task is analysis-oriented.
- Mention the evidence used: C++ files, config files, asset paths, remote execution output, or cached JSON.
- Include exact class names, function names, asset paths, and key defaults when they matter.
- For Blueprint visual scripts, explain runtime behavior rather than only listing nodes.
- State limitations clearly, for example when an asset could not be read live or when a timeline curve could only be sampled.

## Safety

- Do not modify assets while inspecting them.
- Do not save the project or resave packages unless the user explicitly asks.
- Do not run destructive editor commands.
- Keep generated JSON under `Saved/` unless the user asks for another output path.
