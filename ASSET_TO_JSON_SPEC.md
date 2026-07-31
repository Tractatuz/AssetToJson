# AssetToJson 변환 명세

이 문서는 `AssetToJson` 플러그인의 현재 소스 기준으로 `ReadAssetAsJson`를 통해 JSON으로 변환되는 Asset 종류와 각 JSON에 포함되는 내용을 정리한다.

## 공통 동작

- 진입점은 `UAssetToJsonLibrary::ReadAssetAsJson`이다.
- `AssetPath`는 `/Game/Foo/Bar` 같은 long package name 또는 `/Game/Foo/Bar.Bar` 같은 object path를 받는다. long package name이면 같은 이름의 object path로 보정한다.
- `OutputFilePath`가 비어 있지 않으면 JSON을 UTF-8 without BOM 파일로 저장한다.
- `bPrettyPrint`로 pretty/condensed JSON 출력을 선택한다.
- `bReturnJson`이 `false`이면 파일 저장 후 빈 문자열을 반환한다.
- `bIncludeNodeProperties`는 Blueprint graph node의 추가 UObject property 덤프를 포함할지 제어한다.
- 모든 전문 exporter는 읽기/분석용 schema를 사용한다. `exporter.supports_round_trip`은 항상 `false`이며, 쓰기 지원은 별도 patch schema가 필요하다는 의미의 `separate_patch_schema`로 표시된다.

## 지원 Asset 요약

| Asset 종류 | 판별 기준 | schema | 주요 JSON 섹션 |
| --- | --- | --- | --- |
| Blueprint 계열 전체 | `UBlueprint` | `ue.asset_to_json.blueprint_visual_script.v1` | `asset`, `blueprint`, `class_defaults`, `component_defaults`, `graphs`, `variables`, `components` |
| Animation Blueprint | `UAnimBlueprint` | `ue.asset_to_json.blueprint_visual_script.v1` | Blueprint 공통 섹션 + `anim_blueprint`, AnimGraph node semantic |
| Widget Blueprint | `UWidgetBlueprint` | `ue.asset_to_json.blueprint_visual_script.v1` | Blueprint 공통 섹션 + `widget_blueprint` |
| Enhanced Input Action | class path가 `/Script/EnhancedInput.InputAction` 또는 하위 클래스 | `ue.asset_to_json.enhanced_input.v1` | `asset`, `asset_type`, `input_action`, `warnings` |
| Enhanced Input Mapping Context | class path가 `/Script/EnhancedInput.InputMappingContext` 또는 하위 클래스 | `ue.asset_to_json.enhanced_input.v1` | `asset`, `asset_type`, `input_mapping_context`, `warnings` |
| DataTable | `UDataTable` | `ue.asset_to_json.data_table.v1` | `asset`, `row_struct`, `row_count`, `columns`, `rows`, `warnings` |
| ChooserTable | class path가 `/Script/Chooser.ChooserTable` 또는 class name이 `ChooserTable`인 class/하위 class | `ue.asset_to_json.chooser_table.v1` | `asset`, `chooser_table.properties`, `warnings` |
| 기타 로드 가능한 UObject Asset | 위 전문 exporter에 해당하지 않는 Asset | `ue.asset_to_json.asset.v1` | `asset`, `exporter`, `warnings`만 포함 |

## Blueprint 계열

대상: `UBlueprint`로 로드되는 모든 Blueprint asset. 일반 Actor Blueprint, Object Blueprint, Blueprint Function Library, Blueprint Macro Library 등 `UBlueprint` 기반 asset은 같은 exporter를 탄다.

루트 필드:

- `schema`: `ue.asset_to_json.blueprint_visual_script.v1`
- `ok`: 변환 성공 여부
- `engine_version`: 현재 Unreal Engine 버전 문자열
- `exporter`: `{ type: "blueprint_visual_script", write_support: "separate_patch_schema", supports_round_trip: false }`
- `warnings`: 현재 Blueprint exporter에서는 빈 배열로 초기화된다.

`asset` 섹션:

- `path`: package path
- `object_path`: object path
- `name`: Blueprint asset 이름
- `class`: Blueprint asset UObject class path
- `parent_class`: Blueprint parent class path
- `generated_class`: generated class path

`blueprint` 섹션:

- `type`: `EBlueprintType` 이름
- `status`: `EBlueprintStatus` 이름

`class_defaults` 섹션:

- `object`: generated class CDO path
- `class`: CDO class path
- `comparison_object`: parent class CDO path
- `only_changed_from_comparison`: `true`
- `properties`: CDO의 edit 또는 Blueprint-visible property 중 transient 계열을 제외하고, parent CDO와 값이 다른 property만 export한다.
- 각 property는 `name`, `category`, `cpp_type`, `owner_class`, `value`를 포함한다.

`component_defaults` 섹션:

- generated class CDO가 `AActor`인 경우 Actor component들을 export한다.
- 각 component는 `name`, `class`, `path`, `defaults`를 포함한다.
- `defaults`는 같은 이름의 parent component와 비교하여 달라진 edit/Blueprint-visible property를 포함한다.

`graphs` 섹션:

- `UbergraphPages`는 `type: "ubergraph"`로 export한다.
- `FunctionGraphs`는 `type: "function"`으로 export한다.
- `MacroGraphs`는 `type: "macro"`로 export한다.
- `DelegateSignatureGraphs`는 `type: "delegate_signature"`로 export한다.
- 각 graph는 `name`, `type`, `schema`, `nodes`, `links`를 포함한다.

Graph node 내용:

- 공통 node 필드: `id`, `name`, `class`, `title`, `comment`, `semantic`, `pins`
- `bIncludeNodeProperties`가 `true`이면 `properties`가 추가된다. 이 값은 `Pins`, `NodeGuid`, 위치/크기, comment, error 등 일부 노이즈 필드를 제외한 node UObject property text dump이다.
- 각 pin은 `id`, `name`, `display_name`, `direction`, `type`, `default_value`, `default_text_value`, `default_object`, `hidden`, `not_connectable`를 포함할 수 있다.
- pin `type`은 category, subcategory, subcategory object, container type, reference/const/weak/UObject-wrapper 여부를 담는다.
- `links`는 graph 내부 pin 연결을 `{ from: { node_id, pin_id, pin_name }, to: { node_id, pin_id, pin_name } }` 형태로 중복 없이 export한다.

Node semantic 분류:

- 모든 node는 `node_class`, `is_k2_node`, K2 node인 경우 `is_pure`를 가진다.
- `custom_event`: custom event 이름, deprecation, call-in-editor, net flags, event reference
- `component_bound_event`: component property, delegate property/display name, delegate owner class, signature function
- `event`: event function name, override/internal/interface 여부, flags, event reference, signature function
- `call_function`: 호출 함수 이름, latent 여부, function reference, target function metadata
- `variable_get`: variable name/source class/reference, property metadata
- `variable_set`: variable name/source class/reference, rep notify, net/field notify 관련 정보, property metadata
- `dynamic_cast`: target type, source/result pin
- `macro_instance`: macro graph, source blueprint, resolved wildcard type
- `timeline`: timeline name/template/guid, auto play, loop, replicated, ignore time dilation, 주요 timeline pin reference
- `spawn_actor_from_class`: spawn class, class/result pin, spawn variable pins
- `variable`: variable name/source class/reference, property metadata
- 그 외 K2 node는 `kind: "k2_node"`, 일반 graph node는 `kind: "ed_graph_node"`

Function/property metadata:

- 함수 metadata는 `name`, `path`, `owner_class`, `function_flags`, native/static/const/BlueprintCallable/BlueprintEvent/latent/net/server/client/multicast 여부를 담는다.
- property metadata는 `name`, `display_name`, `cpp_type`, `owner_class`, `full_name`, `property_flags`, Blueprint visible/read-only, editable, net 여부를 담는다.

`variables` 섹션:

- Blueprint에 선언된 `NewVariables`를 export한다.
- 각 variable은 `name`, `guid`, `friendly_name`, `category`, `default_value`, `rep_notify_func`, `property_flags`, `type`을 포함한다.

`components` 섹션:

- `SimpleConstructionScript`의 root node부터 component tree를 export한다.
- 각 SCS node는 `variable_name`, `component_class`, `component_template`, `defaults`, `children`을 포함한다.
- `defaults`는 component template의 edit/Blueprint-visible property text dump이다.

## Animation Blueprint 추가 섹션

대상: `UAnimBlueprint`. 기본적으로 Blueprint exporter를 사용하며, 일반 Blueprint 공통 섹션이 모두 포함된다.

추가 graph 동작:

- `UAnimBlueprint::GetAllGraphs` 결과 중 아직 추가되지 않은 graph를 더 export한다.
- graph 이름, schema, anim graph name 기준으로 AnimGraph로 보이면 `type: "anim_graph"`, 아니면 `type: "additional"`로 표시한다.

`anim_blueprint` 섹션:

- `target_skeleton`: target skeleton object path
- `state_machines`: AnimGraph에서 발견한 state machine 목록

State machine 내용:

- 각 state machine은 `name`, `path`, `entry_node`, `owner_node`, `states`, `transitions`를 포함한다.
- state는 `id`, `name`, `class`, `bound_graph`, `state_type`, `always_reset_on_entry`를 포함한다.
- transition은 `id`, `name`, `class`, `bound_graph`, `custom_transition_graph`, `previous_state`, `next_state`, `priority_order`, `crossfade_duration`, `blend_mode`, automatic rule 설정, marker sync rule, logic type, bidirectional/disabled/shared rule/shared crossfade 정보를 포함한다.

AnimGraph node semantic 추가:

- AnimGraph node는 `semantic.anim_graph_node`를 포함한다.
- 공통으로 `kind: "anim_graph_node"`, `node_class`, `animation_asset`을 담는다.
- asset player node는 `referred_animation_assets`에 참조 AnimationAsset path/class 목록을 담는다.
- state machine node는 `state_machine_name`과 state machine 상세 JSON을 담는다.

## Widget Blueprint 추가 섹션

대상: `UWidgetBlueprint`. 기본적으로 Blueprint exporter를 사용하며, 일반 Blueprint 공통 섹션이 모두 포함된다.

`widget_blueprint` 섹션:

- `widget_tree`: WidgetTree object path
- `root_widget`: root widget tree
- `animations`: widget animation 목록
- `bindings`: delegate editor binding 목록

Widget tree 내용:

- 각 widget은 `name`, `class`, `defaults`, `slot_class`, `slot_defaults`, `children`을 포함할 수 있다.
- `defaults`와 `slot_defaults`는 edit/Blueprint-visible property text dump이다.
- panel widget은 child widget을 재귀적으로 `children`에 담는다.

Widget animation 내용:

- animation 기본 정보: `name`, `path`, `class`, `display_label`, `movie_scene`, `defaults`
- `movie_scene_data`: MovieScene 상세 정보
- MovieScene은 `path`, `class`, `name`, tick resolution, playback range, `tracks`, `bindings`를 포함한다.
- track은 `name`, `track_name`, `display_name`, `class`, `defaults`, `sections`를 포함한다.
- section은 `name`, `class`, `row_index`, active/locked 여부, range, defaults, `channels`를 포함한다.
- channel은 float/double/bool/integer/byte 타입, index, name/display text, default value, key frame/value 목록을 포함한다.
- MovieScene binding은 guid와 binding track 목록을 포함한다.

Widget binding 내용:

- 각 binding은 `object_name`, `property_name`, `function_name`, `source_property`, `member_guid`, `kind`를 포함한다.

## Enhanced Input Action

대상: `/Script/EnhancedInput.InputAction` 또는 그 하위 클래스. EnhancedInput 모듈에 직접 의존하지 않기 위해 class path와 reflection 기반으로 판별/export한다.

루트 필드:

- `schema`: `ue.asset_to_json.enhanced_input.v1`
- `ok`: 변환 성공 여부
- `asset`: package/object/name/class 요약
- `exporter`: `{ type: "enhanced_input", write_support: "separate_patch_schema", supports_round_trip: false }`
- `asset_type`: `input_action`
- `input_action`: Input Action 상세
- `warnings`: Enhanced Input reflection export 관련 경고가 들어갈 수 있다.

`input_action` 섹션:

- `value_type`
- `action_description`
- `accumulation_behavior`
- `trigger_when_paused`
- `consume_input`
- `consumes_action_and_axis_mappings`
- `reserve_all_mappings`
- `trigger_events_that_consume_legacy_keys`
- `triggers`: instanced trigger object 목록
- `modifiers`: instanced modifier object 목록
- `player_mappable_settings`: PlayerMappableKeySettings object defaults
- `defaults`: InputAction asset 자체의 edit/Blueprint-visible property text dump

Trigger/modifier instanced object 내용:

- 각 object는 `index`, `path`, `class`, `defaults`를 포함한다.
- `defaults`는 object의 edit/Blueprint-visible property text dump이다.

## Enhanced Input Mapping Context

대상: `/Script/EnhancedInput.InputMappingContext` 또는 그 하위 클래스. EnhancedInput 모듈에 직접 의존하지 않기 위해 class path와 reflection 기반으로 판별/export한다.

루트 필드:

- `schema`: `ue.asset_to_json.enhanced_input.v1`
- `ok`: 변환 성공 여부
- `asset`: package/object/name/class 요약
- `exporter`: `{ type: "enhanced_input", write_support: "separate_patch_schema", supports_round_trip: false }`
- `asset_type`: `input_mapping_context`
- `input_mapping_context`: Mapping Context 상세
- `warnings`: runtime priority가 asset에 저장되지 않는다는 경고와 reflection export 경고를 포함한다.

`input_mapping_context` 섹션:

- `context_description`
- `registration_tracking_mode`
- `filters_by_input_mode`
- `mappings`: Enhanced Action Key Mapping 목록
- `defaults`: Mapping Context asset 자체의 edit/Blueprint-visible property text dump
- `mapping_profiles`: 현재는 빈 배열로 export된다.

Mapping 내용:

- `index`
- `profile_id`: profile 기반 export일 때만 사용되는 선택 필드. 현재 기본 Mapping Context export에서는 비어 있다.
- `action_path`
- `action_name`
- `key_name`
- `key_display_name`
- `mapping_name`
- `display_name`
- `display_category`
- `is_player_mappable`
- `triggers`: mapping 단위 instanced trigger object 목록
- `modifiers`: mapping 단위 instanced modifier object 목록
- `player_mappable_settings`: PlayerMappableOptions text dump

## DataTable

대상: `UDataTable`. Engine의 `UDataTable` 타입에 직접 dispatch된다.

루트 필드:

- `schema`: `ue.asset_to_json.data_table.v1`
- `ok`: 변환 성공 여부
- `engine_version`: 현재 Unreal Engine 버전 문자열
- `asset`: package/object/name/class 요약
- `exporter`: `{ type: "data_table", write_support: "none", supports_round_trip: false }`
- `row_struct`: row `UScriptStruct`의 `name`, `path`, `cpp_name`, `super_struct`, `struct_flags`
- `row_count`: DataTable row 수
- `columns`: row struct의 non-transient property 목록. 각 column은 index, name, property metadata, type metadata를 포함한다.
- `rows`: row name과 property 값 목록
- `warnings`: 현재는 빈 배열

Row 값 형식:

- 각 row는 `{ name, values }` 형태다.
- `values`는 property 이름을 key로 하는 object이며, 각 property 값은 `property`, `export_text`, `value`를 포함한다.
- `property`는 name/display/category/cpp type/owner/full name/flags/editable/Blueprint-visible/transient/type 정보를 포함한다.
- `export_text`는 Unreal `ExportText` 기반 문자열이다.
- `value`는 가능한 경우 구조화 값으로 export한다. bool/numeric/enum/name/string/text/object/class/soft object/struct/array/map/set을 재귀적으로 JSON화하며, 알 수 없는 타입은 text 값으로 fallback한다.

## ChooserTable

대상: class path가 `/Script/Chooser.ChooserTable`이거나 class name이 `ChooserTable`인 asset. Chooser 모듈에 hard dependency를 추가하지 않기 위해 class path/name과 reflection만 사용한다.

루트 필드:

- `schema`: `ue.asset_to_json.chooser_table.v1`
- `ok`: 변환 성공 여부
- `engine_version`: 현재 Unreal Engine 버전 문자열
- `asset`: package/object/name/class 요약
- `exporter`: `{ type: "chooser_table", write_support: "none", supports_round_trip: false }`
- `chooser_table`: `{ object, class, properties }`
- `warnings`: reflection 기반 export 및 round-trip 미지원 안내

`chooser_table.properties`는 non-transient이며 editable 또는 Blueprint-visible인 property를 export한다. Chooser 내부 column/row/result/context/output 관련 property 이름은 에디터 표시 flag가 없더라도 inspect 가능하도록 포함한다. 값 형식은 DataTable row property 값과 같은 `property`, `export_text`, `value` 구조를 사용하며, asset 내부 subobject reference는 depth 제한 내에서 nested `properties`를 확장한다.

## Unsupported Asset fallback

대상: `ReadAssetAsJson`가 로드에는 성공했지만 전문 exporter가 없는 모든 UObject asset.

루트 필드:

- `schema`: `ue.asset_to_json.asset.v1`
- `ok`: `true`
- `engine_version`: 현재 Unreal Engine 버전 문자열
- `asset`: package/object/name/class 요약
- `exporter`: `{ type: "unsupported", write_support: "none", supports_round_trip: false }`
- `warnings`: `No specialized AssetToJson exporter is implemented for this asset class yet.`

이 fallback은 asset 내부 데이터를 분석 가능한 형태로 풀어주지 않는다. 지원 여부 확인용 metadata와 경고만 제공한다.

## Error JSON

Asset path가 비어 있거나 asset load 실패, 파일 저장 실패, `ReadBlueprintVisualScriptAsJson`에 Blueprint가 아닌 asset을 직접 전달한 경우 `ok: false` JSON을 반환한다.

오류 JSON은 다음 필드를 가진다.

- `schema`: 현재 구현상 Blueprint visual script schema 문자열인 `ue.asset_to_json.blueprint_visual_script.v1`
- `ok`: `false`
- `error`: 오류 메시지

## 현재 한계

- 전문 exporter가 없는 asset은 `unsupported` fallback만 반환한다. 예를 들어 Material, Texture, StaticMesh, SkeletalMesh, Animation Sequence, DataAsset 등은 현재 소스 기준으로 내부 내용을 상세 export하지 않는다.
- JSON은 읽기/검사용이며 round-trip 복원을 보장하지 않는다.
- property 값은 Unreal의 `ExportText` 계열 문자열로 저장되는 경우가 많아서, 타입별 구조화 데이터가 아닌 text dump로 보이는 필드가 있다.
- Input Mapping Context priority는 runtime에서 caller가 부여하는 값이라 asset JSON에 포함되지 않는다.
