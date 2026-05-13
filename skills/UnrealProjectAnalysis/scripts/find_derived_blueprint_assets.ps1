param(
	[Parameter(Mandatory = $true)]
	[string]$ClassPath,

	[string]$OutputJson,

	[string[]]$SearchPaths = @('/Game'),

	[string]$EnginePath = "C:\EpicGames\UE_5.7",

	[int]$TimeoutSeconds = 10,

	[switch]$NoOutputFile,

	[switch]$NoPretty,

	[switch]$DirectOnly
)

$ErrorActionPreference = 'Stop'
$env:PYTHONIOENCODING = 'utf-8'

$RepoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..\..')

if ($NoOutputFile) {
	$OutputJson = ''
}
elseif ([string]::IsNullOrWhiteSpace($OutputJson)) {
	$SafeName = ($ClassPath -replace '[^A-Za-z0-9_.-]+', '_').Trim('_')
	if ([string]::IsNullOrWhiteSpace($SafeName)) {
		$SafeName = 'Class'
	}

	$OutputJson = Join-Path $RepoRoot "Saved\AssetToJson\DerivedBlueprints_$SafeName.json"
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputJson)) {
	$OutputJson = Join-Path $RepoRoot $OutputJson
}

if (-not [string]::IsNullOrWhiteSpace($OutputJson)) {
	$OutputDirectory = Split-Path -Parent $OutputJson
	if (-not (Test-Path -LiteralPath $OutputDirectory)) {
		New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
	}
}

$RemoteExecutionPath = Join-Path $EnginePath 'Engine\Plugins\Experimental\PythonScriptPlugin\Content\Python'

if (-not (Test-Path -LiteralPath $RemoteExecutionPath)) {
	throw "Python remote_execution.py path was not found: $RemoteExecutionPath"
}

$env:UE_ANALYSIS_CLASS_PATH = $ClassPath
$env:UE_ANALYSIS_OUTPUT_JSON = $OutputJson
$env:UE_ANALYSIS_REMOTE_PATH = $RemoteExecutionPath
$env:UE_ANALYSIS_TIMEOUT_SECONDS = [string]$TimeoutSeconds
$env:UE_ANALYSIS_PRETTY = if ($NoPretty) { '0' } else { '1' }
$env:UE_ANALYSIS_DIRECT_ONLY = if ($DirectOnly) { '1' } else { '0' }
$env:UE_ANALYSIS_SEARCH_PATHS = ($SearchPaths -join ';')

@'
import os
import sys
import time

sys.path.append(os.environ['UE_ANALYSIS_REMOTE_PATH'])
import remote_execution

class_path = os.environ['UE_ANALYSIS_CLASS_PATH']
output_json = os.environ.get('UE_ANALYSIS_OUTPUT_JSON', '')
timeout_seconds = int(os.environ.get('UE_ANALYSIS_TIMEOUT_SECONDS', '10'))
pretty = os.environ.get('UE_ANALYSIS_PRETTY', '1') == '1'
direct_only = os.environ.get('UE_ANALYSIS_DIRECT_ONLY', '0') == '1'
search_paths = [p for p in os.environ.get('UE_ANALYSIS_SEARCH_PATHS', '/Game').split(';') if p]

r = remote_execution.RemoteExecution()
r.start()

try:
    deadline = time.time() + timeout_seconds
    while not r.remote_nodes and time.time() < deadline:
        time.sleep(0.1)

    if not r.remote_nodes:
        raise RuntimeError('No Unreal remote execution nodes found')

    node = r.remote_nodes[0]
    print('REMOTE_NODE', node)
    r.open_command_connection(node['node_id'])

    code = r'''
import json
import os
import unreal

class_path = {class_path!r}
output_json = {output_json!r}
pretty = {pretty!r}
direct_only = {direct_only!r}
search_paths = {search_paths!r}

def object_path(obj):
    return obj.get_path_name() if obj else ''

def make_error(message):
    return {{
        'schema': 'ue.analysis.derived_blueprints.v1',
        'ok': False,
        'error': message,
        'target_class': class_path,
        'search_paths': search_paths,
        'assets': [],
    }}

target_class = unreal.load_class(None, class_path)
if not target_class:
    result = make_error('Class could not be loaded: ' + class_path)
else:
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    matched_assets = []
    scanned_asset_count = 0
    loaded_blueprint_count = 0
    warnings = []
    blueprint_records = []
    target_class_path = object_path(target_class)
    target_class_asset_path = unreal.SystemLibrary.get_class_top_level_asset_path(target_class)
    derived_class_paths = set()

    def normalize_class_tag(value):
        if not value:
            return ''

        text = str(value)
        if "'" in text:
            text = text.split("'", 1)[1].rsplit("'", 1)[0]

        return text

    def top_level_asset_path_to_string(value):
        return str(value.package_name) + '.' + str(value.asset_name)

    for derived_asset_data in unreal.AssetRegistryHelpers.get_derived_class_asset_data([target_class_asset_path]):
        derived_class_path = normalize_class_tag(derived_asset_data.get_tag_value('GeneratedClass'))
        if derived_class_path:
            derived_class_paths.add(derived_class_path)

    for search_path in search_paths:
        try:
            asset_data_list = asset_registry.get_assets_by_path(search_path, recursive=True)
        except TypeError:
            asset_data_list = asset_registry.get_assets_by_path(search_path, True)

        for asset_data in asset_data_list:
            scanned_asset_count += 1

            generated_class_path = normalize_class_tag(asset_data.get_tag_value('GeneratedClass'))
            if not generated_class_path:
                continue

            parent_class_path = normalize_class_tag(asset_data.get_tag_value('ParentClass'))
            if not parent_class_path:
                parent_class_path = normalize_class_tag(asset_data.get_tag_value('NativeParentClass'))

            if not parent_class_path:
                warnings.append('Blueprint asset has no parent class tag: {{0}}'.format(asset_data.package_name))
                continue

            loaded_blueprint_count += 1
            object_path_string = str(asset_data.package_name) + '.' + str(asset_data.asset_name)
            blueprint_records.append({{
                'path': str(asset_data.package_name),
                'object_path': object_path_string,
                'name': str(asset_data.asset_name),
                'class': top_level_asset_path_to_string(asset_data.asset_class_path),
                'parent_class': parent_class_path,
                'generated_class': generated_class_path,
            }})

    for record in blueprint_records:
        is_direct_child = record['parent_class'] == target_class_path
        if direct_only:
            b_matches = is_direct_child
        else:
            b_matches = is_direct_child or record['generated_class'] in derived_class_paths

        if not b_matches:
            continue

        matched_assets.append({{
            'path': record['path'],
            'object_path': record['object_path'],
            'name': record['name'],
            'class': record['class'],
            'parent_class': record['parent_class'],
            'generated_class': record['generated_class'],
            'is_direct_child': is_direct_child,
        }})

    matched_assets.sort(key=lambda item: item.get('path', ''))
    result = {{
        'schema': 'ue.analysis.derived_blueprints.v1',
        'ok': True,
        'target_class': target_class_path,
        'search_paths': search_paths,
        'direct_only': direct_only,
        'scanned_asset_count': scanned_asset_count,
        'loaded_blueprint_count': loaded_blueprint_count,
        'match_count': len(matched_assets),
        'assets': matched_assets,
        'warnings': warnings,
    }}

json_text = json.dumps(result, indent=2 if pretty else None, ensure_ascii=False)

if output_json:
    os.makedirs(os.path.dirname(output_json), exist_ok=True)
    with open(output_json, 'w', encoding='utf-8') as output_file:
        output_file.write(json_text)

print(json_text)
print('WROTE_JSON', output_json, 'LENGTH', len(json_text))
'''.format(
        class_path=class_path,
        output_json=output_json.replace('\\', '/'),
        pretty=pretty,
        direct_only=direct_only,
        search_paths=search_paths,
    )

    res = r.run_command('exec(' + repr(code) + ')', exec_mode=remote_execution.MODE_EXEC_STATEMENT)
    print('SUCCESS', res.get('success'))
    if res.get('result'):
        print('RESULT', res.get('result'))
    if res.get('output'):
        print(res.get('output'))
finally:
    try:
        r.close_command_connection()
    finally:
        r.stop()
'@ | py -3

if (-not [string]::IsNullOrWhiteSpace($OutputJson) -and (Test-Path -LiteralPath $OutputJson)) {
	"OUTPUT_JSON $OutputJson"
}
