---
description: Read an Unreal asset through AssetToJson
---

Read an Unreal asset as JSON through the AssetToJson plugin and Python Remote Execution.

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

Run the command from the workspace root with the Bash tool using PowerShell syntax. Build the PowerShell command safely as separate quoted arguments, for example:

```powershell
& ".agents\skills\UnrealProjectAnalysis\scripts\read_asset_json.ps1" -AssetPath "<AssetPath>" -NoOutputFile
```

After running, report:

- Whether a remote Unreal node was found.
- Whether `ok` is true in the returned JSON if visible.
- The output JSON path when a file was written.
- Any error returned by AssetToJson or Python Remote Execution.

Do not modify assets, save packages, or run destructive editor commands.
