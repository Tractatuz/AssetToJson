---
description: Read an Unreal asset through AssetToJson
---

Read an Unreal asset as JSON through the AssetToJson plugin. Prefer Python Remote Execution when an editor is already running; fall back to the AssetToJson commandlet only when remote execution is unavailable.

Arguments: `$ARGUMENTS`

Expected usage:

```text
/asset-to-json /Game/Variant_Combat/Blueprints/BP_CombatCharacter
/asset-to-json /Game/Variant_Combat/Blueprints/BP_CombatCharacter --output Saved/AssetToJson/BP_CombatCharacter.json
/asset-to-json /Game/Variant_Combat/Blueprints/BP_CombatCharacter --include-node-properties
/asset-to-json /Game/Variant_Combat/Blueprints/BP_CombatCharacter --engine-path C:\EpicGames\UE_5.7
```

Parse the first non-option argument as `AssetPath`. Supported options:

- `--output <path>`: pass `-OutputJson <path>` and save JSON to that path.
- `--save`: allow the helper's default output path under `Saved/AssetToJson/`.
- `--include-node-properties`: pass `-IncludeNodeProperties`.
- `--engine-path <path>`: pass `-EnginePath <path>`.
- `--timeout <seconds>`: pass `-TimeoutSeconds <seconds>`.

If neither `--output` nor `--save` is provided, pass `-NoOutputFile` so the JSON is returned in command output for immediate agent context. If output is truncated or the asset is large, rerun with `--save` or `--output` and then inspect the generated file.

Run the Python Remote Execution helper first from the workspace root with the Bash tool using PowerShell syntax. Build the PowerShell command safely as separate quoted arguments, for example:

```powershell
& ".agents\skills\UnrealProjectAnalysis\scripts\read_asset_json.ps1" -AssetPath "<AssetPath>" -NoOutputFile
```

If the helper cannot reach a running editor, for example `No Unreal remote execution nodes found`, or Python Remote Execution itself fails before AssetToJson returns JSON, run the commandlet fallback. Do not fallback when AssetToJson returned valid JSON with `ok=false`; report that asset/export error instead.

Commandlet fallback example:

```powershell
& "<EnginePath>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Projects\Unreal\TestTPS\TestTPS.uproject" -run=AssetToJson -AssetPath="<AssetPath>" -NoOutputFile -unattended -nop4
```

For fallback argument mapping:

- `--output <path>`: pass `-Output=<path>`.
- `--save`: omit `-NoOutputFile` and omit `-Output`; the commandlet writes under `Saved/AssetToJson/`.
- `--include-node-properties`: pass `-IncludeNodeProperties`.
- `--engine-path <path>`: use that path as `<EnginePath>`; otherwise use `C:\EpicGames\UE_5.7`.
- `--timeout <seconds>`: applies only to Python Remote Execution, not the commandlet.

If neither `--output` nor `--save` is provided during fallback, pass `-NoOutputFile` so the JSON is returned in command output, matching the remote helper behavior.

After running, report:

- Whether a remote Unreal node was found.
- Whether the commandlet fallback was used.
- Whether `ok` is true in the returned JSON if visible.
- The output JSON path when a file was written.
- Any error returned by AssetToJson, Python Remote Execution, or the commandlet.

Do not modify assets, save packages, or run destructive editor commands.
