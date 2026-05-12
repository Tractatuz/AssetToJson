param(
	[Parameter(Mandatory = $true)]
	[string]$AssetPath,

	[string]$OutputJson,

	[string]$EnginePath = "C:\EpicGames\UE_5.7",

	[int]$TimeoutSeconds = 10,

	[switch]$NoOutputFile,

	[switch]$NoPretty,

	[switch]$IncludeNodeProperties
)

$ErrorActionPreference = 'Stop'
$env:PYTHONIOENCODING = 'utf-8'

$RepoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..\..')

if ($NoOutputFile) {
	$OutputJson = ''
}
elseif ([string]::IsNullOrWhiteSpace($OutputJson)) {
	$SafeName = ($AssetPath -replace '[^A-Za-z0-9_.-]+', '_').Trim('_')
	if ([string]::IsNullOrWhiteSpace($SafeName)) {
		$SafeName = 'Asset'
	}

	$OutputJson = Join-Path $RepoRoot "Saved\AssetToJson\$SafeName.json"
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

$env:ASSET_TO_JSON_ASSET_PATH = $AssetPath
$env:ASSET_TO_JSON_OUTPUT_JSON = $OutputJson
$env:ASSET_TO_JSON_REMOTE_PATH = $RemoteExecutionPath
$env:ASSET_TO_JSON_TIMEOUT_SECONDS = [string]$TimeoutSeconds
$env:ASSET_TO_JSON_PRETTY = if ($NoPretty) { '0' } else { '1' }
$env:ASSET_TO_JSON_INCLUDE_NODE_PROPERTIES = if ($IncludeNodeProperties) { '1' } else { '0' }

@'
import os
import sys
import time

sys.path.append(os.environ['ASSET_TO_JSON_REMOTE_PATH'])
import remote_execution

asset_path = os.environ['ASSET_TO_JSON_ASSET_PATH']
output_json = os.environ.get('ASSET_TO_JSON_OUTPUT_JSON', '')
timeout_seconds = int(os.environ.get('ASSET_TO_JSON_TIMEOUT_SECONDS', '10'))
pretty = os.environ.get('ASSET_TO_JSON_PRETTY', '1') == '1'
include_node_properties = os.environ.get('ASSET_TO_JSON_INCLUDE_NODE_PROPERTIES', '0') == '1'

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
import unreal

asset_path = {asset_path!r}
output_json = {output_json!r}

result = unreal.AssetToJsonLibrary.read_blueprint_visual_script_as_json(
    asset_path,
    output_json,
    {pretty!r},
    True,
    {include_node_properties!r},
)

print(result)
print('WROTE_JSON', output_json, 'LENGTH', len(result))
'''.format(
        asset_path=asset_path,
        output_json=output_json,
        pretty=pretty,
        include_node_properties=include_node_properties,
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
