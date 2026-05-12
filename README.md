# AssetToJson
Unreal Engine plugin that converts uasset contents into JSON, so AI agents can read it.

## Requirements

- Requires [TaskEvidence](https://github.com/Tractatuz/TaskEvidence). Install it alongside this plugin under your project's `Plugins/` directory.

## Schema Policy

AssetToJson uses read-focused JSON schemas for AI inspection and understanding. These schemas are separate from JsonToAsset patch schemas; only essential identifiers such as asset paths, object paths, class paths, and stable names/guids are shared.

Use `ReadAssetAsJson` as the generic entry point. It dispatches supported asset classes to specialized exporters and returns metadata with warnings for asset classes that do not have a specialized exporter yet.
