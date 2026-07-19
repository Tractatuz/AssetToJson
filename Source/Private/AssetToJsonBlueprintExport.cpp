#include "AssetToJsonBlueprintExport.h"

#include "AssetToJsonAnimationExport.h"
#include "AssetToJsonWidgetExport.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimationAsset.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_AssetPlayerBase.h"
#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/TimelineTemplate.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_Timeline.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TaskEvidenceBuilder.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/ActorComponent.h"
#include "Components/Widget.h"

namespace
{
	FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	FString ClassPath(const UObject* Object)
	{
		return Object && Object->GetClass() ? Object->GetClass()->GetPathName() : FString();
	}

	FString NormalizeObjectPath(const FString& AssetPath)
	{
		const FString TrimmedPath = AssetPath.TrimStartAndEnd();

		if (!TrimmedPath.Contains(TEXT(".")) && FPackageName::IsValidLongPackageName(TrimmedPath))
		{
			return FString::Printf(TEXT("%s.%s"), *TrimmedPath, *FPackageName::GetLongPackageAssetName(TrimmedPath));
		}

		return TrimmedPath;
	}

	TSharedPtr<FJsonObject> MakeErrorJson(const FString& Message)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("ue.asset_to_json.blueprint_visual_script.v1"));
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetStringField(TEXT("error"), Message);
		return Root;
	}

	TSharedPtr<FJsonObject> MakeExporterJson(const FString& Type, const FString& WriteSupport)
	{
		TSharedPtr<FJsonObject> JsonExporter = MakeShared<FJsonObject>();
		JsonExporter->SetStringField(TEXT("type"), Type);
		JsonExporter->SetStringField(TEXT("write_support"), WriteSupport);
		JsonExporter->SetBoolField(TEXT("supports_round_trip"), false);
		return JsonExporter;
	}

	bool SaveJsonStringToFile(const FString& JsonString, const FString& OutputFilePath)
	{
		if (OutputFilePath.IsEmpty())
		{
			return true;
		}

		const FString Directory = FPaths::GetPath(OutputFilePath);
		if (!Directory.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*Directory, true);
		}

		return FFileHelper::SaveStringToFile(JsonString, *OutputFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString SerializeJson(const TSharedRef<FJsonObject>& Root, bool bPrettyPrint)
	{
		FString JsonString;

		if (bPrettyPrint)
		{
			TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
			FJsonSerializer::Serialize(Root, Writer);
		}
		else
		{
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonString);
			FJsonSerializer::Serialize(Root, Writer);
		}

		return JsonString;
	}

	void WriteAssetToJsonEvidence(const FString& AssetPath, const FString& OutputFilePath, bool bOk, const FString& Error, int32 GraphCount = 0, int32 VariableCount = 0, int32 ComponentCount = 0)
	{
		FTaskEvidenceBuilder Evidence(TEXT("AssetToJson"), TEXT("ReadBlueprintVisualScriptAsJson"));
		Evidence
			.SetStatus(bOk ? TEXT("succeeded") : TEXT("failed"))
			.SetSummary(bOk ? TEXT("Blueprint visual script exported to JSON.") : TEXT("Blueprint visual script export failed."), Error)
			.AddFact(TEXT("asset_to_json.ok"), bOk)
			.AddFact(TEXT("asset.path"), AssetPath)
			.AddFact(TEXT("asset_to_json.graph_count"), GraphCount)
			.AddFact(TEXT("asset_to_json.variable_count"), VariableCount)
			.AddFact(TEXT("asset_to_json.component_count"), ComponentCount);

		if (!OutputFilePath.IsEmpty())
		{
			Evidence.AddArtifact(OutputFilePath, TEXT("asset_export"), TEXT("application/json"), TEXT("Exported Blueprint visual script JSON."));
		}

		FString EvidencePath;
		FString EvidenceError;
		Evidence.WriteToDefaultLocation(EvidencePath, EvidenceError);
	}

	FString ContainerTypeToString(EPinContainerType ContainerType)
	{
		switch (ContainerType)
		{
		case EPinContainerType::Array:
			return TEXT("array");
		case EPinContainerType::Set:
			return TEXT("set");
		case EPinContainerType::Map:
			return TEXT("map");
		case EPinContainerType::None:
		default:
			return TEXT("none");
		}
	}

	FString PinDirectionToString(EEdGraphPinDirection Direction)
	{
		switch (Direction)
		{
		case EGPD_Input:
			return TEXT("input");
		case EGPD_Output:
			return TEXT("output");
		default:
			return TEXT("unknown");
		}
	}

	FString PinIdToString(const UEdGraphPin* Pin)
	{
		return Pin ? Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens) : FString();
	}

	FString GuidToString(const FGuid& Guid)
	{
		return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
	}

	FString NodeIdToString(const UEdGraphNode* Node)
	{
		return Node ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
	}

	TSharedPtr<FJsonObject> ExportPinType(const FEdGraphPinType& PinType)
	{
		TSharedPtr<FJsonObject> JsonPinType = MakeShared<FJsonObject>();
		JsonPinType->SetStringField(TEXT("category"), PinType.PinCategory.ToString());
		if (!PinType.PinSubCategory.IsNone())
		{
			JsonPinType->SetStringField(TEXT("subcategory"), PinType.PinSubCategory.ToString());
		}

		if (PinType.PinSubCategoryObject.IsValid())
		{
			JsonPinType->SetStringField(TEXT("subcategory_object"), ObjectPath(PinType.PinSubCategoryObject.Get()));
		}

		if (PinType.ContainerType != EPinContainerType::None)
		{
			JsonPinType->SetStringField(TEXT("container"), ContainerTypeToString(PinType.ContainerType));
		}

		if (PinType.bIsReference)
		{
			JsonPinType->SetBoolField(TEXT("is_reference"), true);
		}

		if (PinType.bIsConst)
		{
			JsonPinType->SetBoolField(TEXT("is_const"), true);
		}

		if (PinType.bIsWeakPointer)
		{
			JsonPinType->SetBoolField(TEXT("is_weak_pointer"), true);
		}

		if (PinType.bIsUObjectWrapper)
		{
			JsonPinType->SetBoolField(TEXT("is_uobject_wrapper"), true);
		}

		return JsonPinType;
	}

	TSharedPtr<FJsonObject> ExportMemberReference(const FMemberReference& Reference)
	{
		TSharedPtr<FJsonObject> JsonReference = MakeShared<FJsonObject>();
		JsonReference->SetStringField(TEXT("member_name"), Reference.GetMemberName().ToString());
		JsonReference->SetStringField(TEXT("member_guid"), GuidToString(Reference.GetMemberGuid()));
		JsonReference->SetStringField(TEXT("member_scope"), Reference.GetMemberScopeName());
		JsonReference->SetStringField(TEXT("member_parent_class"), ObjectPath(Reference.GetMemberParentClass()));
		JsonReference->SetStringField(TEXT("member_parent_package"), ObjectPath(Reference.GetMemberParentPackage()));
		JsonReference->SetBoolField(TEXT("is_self_context"), Reference.IsSelfContext());
		JsonReference->SetBoolField(TEXT("is_local_scope"), Reference.IsLocalScope());
		JsonReference->SetBoolField(TEXT("is_deprecated"), Reference.IsDeprecated());
		return JsonReference;
	}

	TSharedPtr<FJsonObject> ExportFunction(const UFunction* Function)
	{
		TSharedPtr<FJsonObject> JsonFunction = MakeShared<FJsonObject>();
		if (!Function)
		{
			return JsonFunction;
		}

		JsonFunction->SetStringField(TEXT("name"), Function->GetName());
		JsonFunction->SetStringField(TEXT("path"), Function->GetPathName());
		JsonFunction->SetStringField(TEXT("owner_class"), ObjectPath(Function->GetOwnerClass()));
		JsonFunction->SetNumberField(TEXT("function_flags"), static_cast<double>(Function->FunctionFlags));
		JsonFunction->SetBoolField(TEXT("is_native"), Function->HasAnyFunctionFlags(FUNC_Native));
		JsonFunction->SetBoolField(TEXT("is_static"), Function->HasAnyFunctionFlags(FUNC_Static));
		JsonFunction->SetBoolField(TEXT("is_const"), Function->HasAnyFunctionFlags(FUNC_Const));
		JsonFunction->SetBoolField(TEXT("is_blueprint_callable"), Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
		JsonFunction->SetBoolField(TEXT("is_blueprint_event"), Function->HasAnyFunctionFlags(FUNC_BlueprintEvent));
		JsonFunction->SetBoolField(TEXT("is_latent"), Function->HasMetaData(TEXT("Latent")));
		JsonFunction->SetBoolField(TEXT("is_net"), Function->HasAnyFunctionFlags(FUNC_Net));
		JsonFunction->SetBoolField(TEXT("is_net_server"), Function->HasAnyFunctionFlags(FUNC_NetServer));
		JsonFunction->SetBoolField(TEXT("is_net_client"), Function->HasAnyFunctionFlags(FUNC_NetClient));
		JsonFunction->SetBoolField(TEXT("is_net_multicast"), Function->HasAnyFunctionFlags(FUNC_NetMulticast));
		return JsonFunction;
	}

	TSharedPtr<FJsonObject> ExportProperty(const FProperty* Property)
	{
		TSharedPtr<FJsonObject> JsonProperty = MakeShared<FJsonObject>();
		if (!Property)
		{
			return JsonProperty;
		}

		JsonProperty->SetStringField(TEXT("name"), Property->GetName());
		JsonProperty->SetStringField(TEXT("display_name"), Property->GetDisplayNameText().ToString());
		JsonProperty->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
		JsonProperty->SetStringField(TEXT("owner_class"), ObjectPath(Property->GetOwnerClass()));
		JsonProperty->SetStringField(TEXT("full_name"), Property->GetFullName());
		JsonProperty->SetNumberField(TEXT("property_flags"), static_cast<double>(Property->GetPropertyFlags()));
		JsonProperty->SetBoolField(TEXT("is_blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
		JsonProperty->SetBoolField(TEXT("is_blueprint_read_only"), Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
		JsonProperty->SetBoolField(TEXT("is_editable"), Property->HasAnyPropertyFlags(CPF_Edit));
		JsonProperty->SetBoolField(TEXT("is_net"), Property->HasAnyPropertyFlags(CPF_Net));
		return JsonProperty;
	}

	bool ShouldExportDetailsProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const bool bIsDetailsVisible = Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
		const bool bIsTransientOnly = Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_TextExportTransient);
		return bIsDetailsVisible && !bIsTransientOnly;
	}

	bool ExportPropertyValueText(const UObject* Object, const FProperty* Property, FString& OutValue)
	{
		if (!Object || !Property || !Object->GetClass()->IsChildOf(Property->GetOwnerClass()))
		{
			return false;
		}

		Property->ExportText_InContainer(0, OutValue, Object, nullptr, const_cast<UObject*>(Object), PPF_None);
		return true;
	}

	bool HasSamePropertyValue(const UObject* Object, const UObject* ComparisonObject, const FProperty* Property, const FString& ExportedValue)
	{
		if (!Object || !ComparisonObject || !Property || !ComparisonObject->GetClass()->IsChildOf(Property->GetOwnerClass()))
		{
			return false;
		}

		FString ComparisonValue;
		if (!ExportPropertyValueText(ComparisonObject, Property, ComparisonValue))
		{
			return false;
		}

		return ExportedValue == ComparisonValue;
	}

	TSharedPtr<FJsonObject> ExportDetailsProperty(const UObject* Object, const FProperty* Property, const FString& ExportedValue)
	{
		TSharedPtr<FJsonObject> JsonProperty = MakeShared<FJsonObject>();
		if (!Object || !Property)
		{
			return JsonProperty;
		}

		JsonProperty->SetStringField(TEXT("name"), Property->GetName());
		JsonProperty->SetStringField(TEXT("category"), Property->GetMetaData(TEXT("Category")));
		JsonProperty->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
		JsonProperty->SetStringField(TEXT("owner_class"), ObjectPath(Property->GetOwnerClass()));
		JsonProperty->SetStringField(TEXT("value"), ExportedValue);
		return JsonProperty;
	}

	TSharedPtr<FJsonObject> ExportObjectDefaults(const UObject* Object, const UObject* ComparisonObject = nullptr)
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		if (!Object)
		{
			return JsonObject;
		}

		JsonObject->SetStringField(TEXT("object"), ObjectPath(Object));
		JsonObject->SetStringField(TEXT("class"), ObjectPath(Object->GetClass()));
		if (ComparisonObject)
		{
			JsonObject->SetStringField(TEXT("comparison_object"), ObjectPath(ComparisonObject));
			JsonObject->SetBoolField(TEXT("only_changed_from_comparison"), true);
		}

		TArray<TSharedPtr<FJsonValue>> Properties;
		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!ShouldExportDetailsProperty(Property))
			{
				continue;
			}

			FString ExportedValue;
			if (!ExportPropertyValueText(Object, Property, ExportedValue))
			{
				continue;
			}

			if (HasSamePropertyValue(Object, ComparisonObject, Property, ExportedValue))
			{
				continue;
			}

			Properties.Add(MakeShared<FJsonValueObject>(ExportDetailsProperty(Object, Property, ExportedValue)));
		}

		JsonObject->SetArrayField(TEXT("properties"), Properties);
		return JsonObject;
	}

	const UActorComponent* FindComparisonComponent(const AActor* ComparisonActor, const UActorComponent* Component)
	{
		if (!ComparisonActor || !Component)
		{
			return nullptr;
		}

		TArray<UActorComponent*> ComparisonComponents;
		ComparisonActor->GetComponents(ComparisonComponents);
		for (const UActorComponent* ComparisonComponent : ComparisonComponents)
		{
			if (ComparisonComponent && ComparisonComponent->GetFName() == Component->GetFName())
			{
				return ComparisonComponent;
			}
		}

		return nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> ExportActorComponentDefaults(const AActor* Actor, const AActor* ComparisonActor)
	{
		TArray<TSharedPtr<FJsonValue>> JsonComponents;
		if (!Actor)
		{
			return JsonComponents;
		}

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (const UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			const UActorComponent* ComparisonComponent = FindComparisonComponent(ComparisonActor, Component);
			TSharedPtr<FJsonObject> JsonComponent = MakeShared<FJsonObject>();
			JsonComponent->SetStringField(TEXT("name"), Component->GetName());
			JsonComponent->SetStringField(TEXT("class"), ObjectPath(Component->GetClass()));
			JsonComponent->SetStringField(TEXT("path"), ObjectPath(Component));
			JsonComponent->SetObjectField(TEXT("defaults"), ExportObjectDefaults(Component, ComparisonComponent));
			JsonComponents.Add(MakeShared<FJsonValueObject>(JsonComponent));
		}

		return JsonComponents;
	}

	TSharedPtr<FJsonObject> ExportPinRef(const UEdGraphPin* Pin)
	{
		TSharedPtr<FJsonObject> JsonPinRef = MakeShared<FJsonObject>();
		if (!Pin)
		{
			return JsonPinRef;
		}

		JsonPinRef->SetStringField(TEXT("id"), PinIdToString(Pin));
		JsonPinRef->SetStringField(TEXT("name"), Pin->PinName.ToString());
		const FString DisplayName = Pin->GetDisplayName().ToString();
		if (!DisplayName.IsEmpty() && DisplayName != Pin->PinName.ToString())
		{
			JsonPinRef->SetStringField(TEXT("display_name"), DisplayName);
		}

		if (!Pin->DefaultValue.IsEmpty())
		{
			JsonPinRef->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		}

		if (Pin->DefaultObject)
		{
			JsonPinRef->SetStringField(TEXT("default_object"), ObjectPath(Pin->DefaultObject));
		}

		return JsonPinRef;
	}

	void AddTimelinePinRef(const TSharedPtr<FJsonObject>& JsonTimeline, const FString& FieldName, const UEdGraphPin* Pin)
	{
		if (Pin)
		{
			JsonTimeline->SetObjectField(FieldName, ExportPinRef(Pin));
		}
	}

	TSharedPtr<FJsonObject> ExportNodeSemantic(const UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> JsonSemantic = MakeShared<FJsonObject>();
		if (!Node)
		{
			return JsonSemantic;
		}

		JsonSemantic->SetStringField(TEXT("node_class"), ClassPath(Node));

		const UK2Node* K2Node = Cast<UK2Node>(Node);
		if (K2Node)
		{
			JsonSemantic->SetBoolField(TEXT("is_k2_node"), true);
			JsonSemantic->SetBoolField(TEXT("is_pure"), K2Node->IsNodePure());
		}
		else
		{
			JsonSemantic->SetBoolField(TEXT("is_k2_node"), false);
		}

		if (const UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(Node))
		{
			JsonSemantic->SetObjectField(TEXT("anim_graph_node"), AssetToJson::ExportAnimGraphNodeDetails(AnimGraphNode));
		}

		if (const UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
		{
			JsonSemantic->SetStringField(TEXT("kind"), TEXT("custom_event"));
			JsonSemantic->SetStringField(TEXT("function_name"), CustomEventNode->GetFunctionName().ToString());
			JsonSemantic->SetStringField(TEXT("custom_function_name"), CustomEventNode->CustomFunctionName.ToString());
			JsonSemantic->SetBoolField(TEXT("call_in_editor"), CustomEventNode->bCallInEditor);
			JsonSemantic->SetBoolField(TEXT("is_deprecated"), CustomEventNode->bIsDeprecated);
			JsonSemantic->SetStringField(TEXT("deprecation_message"), CustomEventNode->DeprecationMessage);
			JsonSemantic->SetNumberField(TEXT("function_flags"), static_cast<double>(CustomEventNode->FunctionFlags));
			JsonSemantic->SetNumberField(TEXT("net_flags"), static_cast<double>(CustomEventNode->GetNetFlags()));
			JsonSemantic->SetBoolField(TEXT("is_override"), CustomEventNode->IsOverride());
			JsonSemantic->SetObjectField(TEXT("event_reference"), ExportMemberReference(CustomEventNode->EventReference));
		}
		else if (const UK2Node_ComponentBoundEvent* ComponentEventNode = Cast<UK2Node_ComponentBoundEvent>(Node))
		{
			JsonSemantic->SetStringField(TEXT("kind"), TEXT("component_bound_event"));
			JsonSemantic->SetStringField(TEXT("function_name"), ComponentEventNode->GetFunctionName().ToString());
			JsonSemantic->SetStringField(TEXT("component_property_name"), ComponentEventNode->GetComponentPropertyName().ToString());
			JsonSemantic->SetStringField(TEXT("delegate_property_name"), ComponentEventNode->DelegatePropertyName.ToString());
			JsonSemantic->SetStringField(TEXT("delegate_display_name"), ComponentEventNode->GetTargetDelegateDisplayName().ToString());
			JsonSemantic->SetStringField(TEXT("delegate_owner_class"), ObjectPath(ComponentEventNode->DelegateOwnerClass));
			JsonSemantic->SetObjectField(TEXT("event_reference"), ExportMemberReference(ComponentEventNode->EventReference));
			JsonSemantic->SetObjectField(TEXT("signature_function"), ExportFunction(ComponentEventNode->FindEventSignatureFunction()));
		}
		else if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
		{
			JsonSemantic->SetStringField(TEXT("kind"), TEXT("event"));
			JsonSemantic->SetStringField(TEXT("function_name"), EventNode->GetFunctionName().ToString());
			JsonSemantic->SetStringField(TEXT("custom_function_name"), EventNode->CustomFunctionName.ToString());
			JsonSemantic->SetBoolField(TEXT("is_override_function"), EventNode->bOverrideFunction);
			JsonSemantic->SetBoolField(TEXT("is_internal_event"), EventNode->bInternalEvent);
			JsonSemantic->SetBoolField(TEXT("is_interface_event"), EventNode->IsInterfaceEventNode());
			JsonSemantic->SetNumberField(TEXT("function_flags"), static_cast<double>(EventNode->FunctionFlags));
			JsonSemantic->SetObjectField(TEXT("event_reference"), ExportMemberReference(EventNode->EventReference));
			JsonSemantic->SetObjectField(TEXT("signature_function"), ExportFunction(EventNode->FindEventSignatureFunction()));
		}
		else if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
		{
			const UFunction* TargetFunction = CallFunctionNode->GetTargetFunction();
			JsonSemantic->SetStringField(TEXT("kind"), TEXT("call_function"));
			JsonSemantic->SetStringField(TEXT("function_name"), CallFunctionNode->GetFunctionName().ToString());
			JsonSemantic->SetBoolField(TEXT("is_latent"), CallFunctionNode->IsLatentFunction());
			JsonSemantic->SetObjectField(TEXT("function_reference"), ExportMemberReference(CallFunctionNode->FunctionReference));
			JsonSemantic->SetObjectField(TEXT("target_function"), ExportFunction(TargetFunction));
		}
		else if (const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(Node))
		{
			JsonSemantic->SetStringField(TEXT("kind"), TEXT("variable_get"));
			JsonSemantic->SetStringField(TEXT("variable_name"), VariableGetNode->GetVarNameString());
			JsonSemantic->SetStringField(TEXT("variable_source_class"), ObjectPath(VariableGetNode->GetVariableSourceClass()));
			JsonSemantic->SetObjectField(TEXT("variable_reference"), ExportMemberReference(VariableGetNode->VariableReference));
			JsonSemantic->SetObjectField(TEXT("property"), ExportProperty(VariableGetNode->GetPropertyForVariable()));
		}
		else if (const UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(Node))
		{
			JsonSemantic->SetStringField(TEXT("kind"), TEXT("variable_set"));
			JsonSemantic->SetStringField(TEXT("variable_name"), VariableSetNode->GetVarNameString());
			JsonSemantic->SetStringField(TEXT("variable_source_class"), ObjectPath(VariableSetNode->GetVariableSourceClass()));
			JsonSemantic->SetStringField(TEXT("rep_notify_name"), VariableSetNode->GetRepNotifyName().ToString());
			JsonSemantic->SetBoolField(TEXT("has_local_rep_notify"), VariableSetNode->HasLocalRepNotify());
			JsonSemantic->SetBoolField(TEXT("is_net_property"), VariableSetNode->IsNetProperty());
			JsonSemantic->SetBoolField(TEXT("should_flush_dormancy_on_set"), VariableSetNode->ShouldFlushDormancyOnSet());
			JsonSemantic->SetBoolField(TEXT("is_field_notify_property"), VariableSetNode->IsFieldNotifyProperty());
			JsonSemantic->SetBoolField(TEXT("has_field_notification_broadcast"), VariableSetNode->HasFieldNotificationBroadcast());
			JsonSemantic->SetObjectField(TEXT("variable_reference"), ExportMemberReference(VariableSetNode->VariableReference));
			JsonSemantic->SetObjectField(TEXT("property"), ExportProperty(VariableSetNode->GetPropertyForVariable()));
		}
		else if (const UK2Node_DynamicCast* DynamicCastNode = Cast<UK2Node_DynamicCast>(Node))
		{
			JsonSemantic->SetStringField(TEXT("kind"), TEXT("dynamic_cast"));
			JsonSemantic->SetStringField(TEXT("target_type"), ObjectPath(DynamicCastNode->TargetType));
			JsonSemantic->SetObjectField(TEXT("source_pin"), ExportPinRef(DynamicCastNode->GetCastSourcePin()));
			JsonSemantic->SetObjectField(TEXT("result_pin"), ExportPinRef(DynamicCastNode->GetCastResultPin()));
		}
		else if (const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
		{
			const UEdGraph* MacroGraph = MacroNode->GetMacroGraph();
			JsonSemantic->SetStringField(TEXT("kind"), TEXT("macro_instance"));
			JsonSemantic->SetStringField(TEXT("macro_graph"), ObjectPath(MacroGraph));
			JsonSemantic->SetStringField(TEXT("macro_graph_name"), MacroGraph ? MacroGraph->GetName() : FString());
			JsonSemantic->SetStringField(TEXT("source_blueprint"), ObjectPath(MacroNode->GetSourceBlueprint()));
			JsonSemantic->SetObjectField(TEXT("resolved_wildcard_type"), ExportPinType(MacroNode->ResolvedWildcardType));
		}
		else if (const UK2Node_Timeline* TimelineNode = Cast<UK2Node_Timeline>(Node))
		{
			TSharedPtr<FJsonObject> JsonPins = MakeShared<FJsonObject>();
			AddTimelinePinRef(JsonPins, TEXT("play"), TimelineNode->GetPlayPin());
			AddTimelinePinRef(JsonPins, TEXT("play_from_start"), TimelineNode->GetPlayFromStartPin());
			AddTimelinePinRef(JsonPins, TEXT("stop"), TimelineNode->GetStopPin());
			AddTimelinePinRef(JsonPins, TEXT("update"), TimelineNode->GetUpdatePin());
			AddTimelinePinRef(JsonPins, TEXT("reverse"), TimelineNode->GetReversePin());
			AddTimelinePinRef(JsonPins, TEXT("reverse_from_end"), TimelineNode->GetReverseFromEndPin());
			AddTimelinePinRef(JsonPins, TEXT("finished"), TimelineNode->GetFinishedPin());
			AddTimelinePinRef(JsonPins, TEXT("new_time"), TimelineNode->GetNewTimePin());
			AddTimelinePinRef(JsonPins, TEXT("set_new_time"), TimelineNode->GetSetNewTimePin());
			AddTimelinePinRef(JsonPins, TEXT("direction"), TimelineNode->GetDirectionPin());

			JsonSemantic->SetStringField(TEXT("kind"), TEXT("timeline"));
			JsonSemantic->SetStringField(TEXT("timeline_name"), TimelineNode->TimelineName.ToString());
			JsonSemantic->SetStringField(TEXT("timeline_template_name"), UTimelineTemplate::TimelineVariableNameToTemplateName(TimelineNode->TimelineName));
			JsonSemantic->SetStringField(TEXT("timeline_guid"), GuidToString(TimelineNode->TimelineGuid));
			JsonSemantic->SetBoolField(TEXT("auto_play"), TimelineNode->bAutoPlay);
			JsonSemantic->SetBoolField(TEXT("loop"), TimelineNode->bLoop);
			JsonSemantic->SetBoolField(TEXT("replicated"), TimelineNode->bReplicated);
			JsonSemantic->SetBoolField(TEXT("ignore_time_dilation"), TimelineNode->bIgnoreTimeDilation);
			JsonSemantic->SetObjectField(TEXT("timeline_pins"), JsonPins);
		}
		else if (const UK2Node_SpawnActorFromClass* SpawnActorNode = Cast<UK2Node_SpawnActorFromClass>(Node))
		{
			TArray<TSharedPtr<FJsonValue>> SpawnVariablePins;
			for (UEdGraphPin* Pin : SpawnActorNode->Pins)
			{
				if (SpawnActorNode->IsSpawnVarPin(Pin))
				{
					SpawnVariablePins.Add(MakeShared<FJsonValueObject>(ExportPinRef(Pin)));
				}
			}

			JsonSemantic->SetStringField(TEXT("kind"), TEXT("spawn_actor_from_class"));
			JsonSemantic->SetStringField(TEXT("class_to_spawn"), ObjectPath(SpawnActorNode->GetClassToSpawn()));
			JsonSemantic->SetObjectField(TEXT("class_pin"), ExportPinRef(SpawnActorNode->GetClassPin()));
			JsonSemantic->SetObjectField(TEXT("result_pin"), ExportPinRef(SpawnActorNode->GetResultPin()));
			JsonSemantic->SetArrayField(TEXT("spawn_variable_pins"), SpawnVariablePins);
		}
		else if (const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
		{
			JsonSemantic->SetStringField(TEXT("kind"), TEXT("variable"));
			JsonSemantic->SetStringField(TEXT("variable_name"), VariableNode->GetVarNameString());
			JsonSemantic->SetStringField(TEXT("variable_source_class"), ObjectPath(VariableNode->GetVariableSourceClass()));
			JsonSemantic->SetObjectField(TEXT("variable_reference"), ExportMemberReference(VariableNode->VariableReference));
			JsonSemantic->SetObjectField(TEXT("property"), ExportProperty(VariableNode->GetPropertyForVariable()));
		}
		else
		{
			JsonSemantic->SetStringField(TEXT("kind"), K2Node ? TEXT("k2_node") : TEXT("ed_graph_node"));
		}

		return JsonSemantic;
	}

	TSharedPtr<FJsonObject> ExportPin(const UEdGraphPin* Pin)
	{
		TSharedPtr<FJsonObject> JsonPin = MakeShared<FJsonObject>();
		if (!Pin)
		{
			return JsonPin;
		}

		JsonPin->SetStringField(TEXT("id"), PinIdToString(Pin));
		JsonPin->SetStringField(TEXT("name"), Pin->PinName.ToString());
		const FString DisplayName = Pin->GetDisplayName().ToString();
		if (!DisplayName.IsEmpty() && DisplayName != Pin->PinName.ToString())
		{
			JsonPin->SetStringField(TEXT("display_name"), DisplayName);
		}

		JsonPin->SetStringField(TEXT("direction"), PinDirectionToString(Pin->Direction));
		JsonPin->SetObjectField(TEXT("type"), ExportPinType(Pin->PinType));
		if (!Pin->DefaultValue.IsEmpty())
		{
			JsonPin->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		}

		if (!Pin->DefaultTextValue.IsEmpty())
		{
			JsonPin->SetStringField(TEXT("default_text_value"), Pin->DefaultTextValue.ToString());
		}

		if (Pin->DefaultObject)
		{
			JsonPin->SetStringField(TEXT("default_object"), ObjectPath(Pin->DefaultObject));
		}

		if (Pin->bHidden)
		{
			JsonPin->SetBoolField(TEXT("hidden"), true);
		}

		if (Pin->bNotConnectable)
		{
			JsonPin->SetBoolField(TEXT("not_connectable"), true);
		}

		return JsonPin;
	}

	void AddNodeProperties(const UEdGraphNode* Node, const TSharedPtr<FJsonObject>& JsonNode)
	{
		static const TSet<FName> SkippedProperties = {
			TEXT("Pins"),
			TEXT("NodeGuid"),
			TEXT("NodePosX"),
			TEXT("NodePosY"),
			TEXT("NodeWidth"),
			TEXT("NodeHeight"),
			TEXT("NodeComment"),
			TEXT("AdvancedPinDisplay"),
			TEXT("bCanResizeNode"),
			TEXT("bCommentBubblePinned"),
			TEXT("bCommentBubbleVisible"),
			TEXT("bCommentBubbleMakeVisible"),
			TEXT("bCommentBubbleVisible_InDetailsPanel"),
			TEXT("ErrorMsg"),
			TEXT("ErrorType"),
			TEXT("NodeUpgradeMessage")
		};

		TSharedPtr<FJsonObject> JsonProperties = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || SkippedProperties.Contains(Property->GetFName()))
			{
				continue;
			}

			FString ExportedValue;
			Property->ExportText_InContainer(0, ExportedValue, Node, Node, const_cast<UEdGraphNode*>(Node), PPF_None);
			JsonProperties->SetStringField(Property->GetName(), ExportedValue);
		}

		JsonNode->SetObjectField(TEXT("properties"), JsonProperties);
	}

	TSharedPtr<FJsonObject> ExportNode(const UEdGraphNode* Node, bool bIncludeNodeProperties)
	{
		TSharedPtr<FJsonObject> JsonNode = MakeShared<FJsonObject>();
		if (!Node)
		{
			return JsonNode;
		}

		JsonNode->SetStringField(TEXT("id"), NodeIdToString(Node));
		JsonNode->SetStringField(TEXT("name"), Node->GetName());
		JsonNode->SetStringField(TEXT("class"), ClassPath(Node));
		JsonNode->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		JsonNode->SetStringField(TEXT("comment"), Node->NodeComment);
		JsonNode->SetObjectField(TEXT("semantic"), ExportNodeSemantic(Node));

		TArray<TSharedPtr<FJsonValue>> Pins;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			Pins.Add(MakeShared<FJsonValueObject>(ExportPin(Pin)));
		}
		JsonNode->SetArrayField(TEXT("pins"), Pins);

		if (bIncludeNodeProperties)
		{
			AddNodeProperties(Node, JsonNode);
		}

		return JsonNode;
	}

	TSharedPtr<FJsonObject> ExportLink(const UEdGraphNode* FromNode, const UEdGraphPin* FromPin, const UEdGraphPin* ToPin)
	{
		TSharedPtr<FJsonObject> JsonLink = MakeShared<FJsonObject>();
		if (!FromNode || !FromPin || !ToPin || !ToPin->GetOwningNode())
		{
			return JsonLink;
		}

		TSharedPtr<FJsonObject> JsonFrom = MakeShared<FJsonObject>();
		JsonFrom->SetStringField(TEXT("node_id"), NodeIdToString(FromNode));
		JsonFrom->SetStringField(TEXT("pin_id"), PinIdToString(FromPin));
		JsonFrom->SetStringField(TEXT("pin_name"), FromPin->PinName.ToString());

		TSharedPtr<FJsonObject> JsonTo = MakeShared<FJsonObject>();
		JsonTo->SetStringField(TEXT("node_id"), NodeIdToString(ToPin->GetOwningNode()));
		JsonTo->SetStringField(TEXT("pin_id"), PinIdToString(ToPin));
		JsonTo->SetStringField(TEXT("pin_name"), ToPin->PinName.ToString());

		JsonLink->SetObjectField(TEXT("from"), JsonFrom);
		JsonLink->SetObjectField(TEXT("to"), JsonTo);
		return JsonLink;
	}

	TSharedPtr<FJsonObject> ExportGraph(const UEdGraph* Graph, const FString& GraphType, bool bIncludeNodeProperties)
	{
		TSharedPtr<FJsonObject> JsonGraph = MakeShared<FJsonObject>();
		if (!Graph)
		{
			return JsonGraph;
		}

		JsonGraph->SetStringField(TEXT("name"), Graph->GetName());
		JsonGraph->SetStringField(TEXT("path"), ObjectPath(Graph));
		JsonGraph->SetStringField(TEXT("outer"), ObjectPath(Graph->GetOuter()));
		JsonGraph->SetStringField(TEXT("type"), GraphType);
		JsonGraph->SetStringField(TEXT("schema"), ClassPath(Graph->GetSchema()));

		TArray<TSharedPtr<FJsonValue>> Nodes;
		TArray<TSharedPtr<FJsonValue>> Links;
		TSet<FString> SeenLinks;

		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			Nodes.Add(MakeShared<FJsonValueObject>(ExportNode(Node, bIncludeNodeProperties)));

			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin)
				{
					continue;
				}

				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode())
					{
						continue;
					}

					const bool bUseThisDirection = Pin->Direction == EGPD_Output || LinkedPin->Direction != EGPD_Output;
					if (!bUseThisDirection)
					{
						continue;
					}

					const FString LinkKey = FString::Printf(TEXT("%s:%s>%s:%s"), *NodeIdToString(Node), *PinIdToString(Pin), *NodeIdToString(LinkedPin->GetOwningNode()), *PinIdToString(LinkedPin));
					if (SeenLinks.Contains(LinkKey))
					{
						continue;
					}

					SeenLinks.Add(LinkKey);
					Links.Add(MakeShared<FJsonValueObject>(ExportLink(Node, Pin, LinkedPin)));
				}
			}
		}

		JsonGraph->SetArrayField(TEXT("nodes"), Nodes);
		JsonGraph->SetArrayField(TEXT("links"), Links);

		return JsonGraph;
	}

	void AddReferencedAsset(TMap<FString, TSharedPtr<FJsonObject>>& ReferencedAssets, const UObject* Object, const FString& Source, const FString& Detail)
	{
		if (!Object)
		{
			return;
		}

		const UObject* ReferencedObject = Object;
		if (const UClass* Class = Cast<UClass>(ReferencedObject))
		{
			if (const UBlueprint* ClassBlueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
			{
				ReferencedObject = ClassBlueprint;
			}
		}

		if (!ReferencedObject->IsA<UAnimationAsset>() && !ReferencedObject->IsA<UBlueprint>())
		{
			return;
		}

		const FString Path = ObjectPath(ReferencedObject);
		if (Path.IsEmpty())
		{
			return;
		}

		TSharedPtr<FJsonObject>* ExistingAsset = ReferencedAssets.Find(Path);
		if (!ExistingAsset)
		{
			TSharedPtr<FJsonObject> JsonAsset = MakeShared<FJsonObject>();
			JsonAsset->SetStringField(TEXT("path"), Path);
			JsonAsset->SetStringField(TEXT("class"), ClassPath(ReferencedObject));
			JsonAsset->SetArrayField(TEXT("sources"), TArray<TSharedPtr<FJsonValue>>());
			ReferencedAssets.Add(Path, JsonAsset);
			ExistingAsset = ReferencedAssets.Find(Path);
		}

		if (ExistingAsset)
		{
			TArray<TSharedPtr<FJsonValue>> Sources;
			const TArray<TSharedPtr<FJsonValue>>* ExistingSources = nullptr;
			if ((*ExistingAsset)->TryGetArrayField(TEXT("sources"), ExistingSources) && ExistingSources)
			{
				Sources = *ExistingSources;
			}

			TSharedPtr<FJsonObject> JsonSource = MakeShared<FJsonObject>();
			JsonSource->SetStringField(TEXT("source"), Source);
			JsonSource->SetStringField(TEXT("detail"), Detail);
			Sources.Add(MakeShared<FJsonValueObject>(JsonSource));
			(*ExistingAsset)->SetArrayField(TEXT("sources"), Sources);
		}
	}

	void AddReferencedAssetPath(TMap<FString, TSharedPtr<FJsonObject>>& ReferencedAssets, const FString& Path, const FString& Source, const FString& Detail)
	{
		if (Path.IsEmpty() || ReferencedAssets.Contains(Path))
		{
			return;
		}

		TSharedPtr<FJsonObject> JsonAsset = MakeShared<FJsonObject>();
		JsonAsset->SetStringField(TEXT("path"), Path);
		JsonAsset->SetStringField(TEXT("class"), FString());
		TArray<TSharedPtr<FJsonValue>> Sources;
		TSharedPtr<FJsonObject> JsonSource = MakeShared<FJsonObject>();
		JsonSource->SetStringField(TEXT("source"), Source);
		JsonSource->SetStringField(TEXT("detail"), Detail);
		Sources.Add(MakeShared<FJsonValueObject>(JsonSource));
		JsonAsset->SetArrayField(TEXT("sources"), Sources);
		ReferencedAssets.Add(Path, JsonAsset);
	}

	void AddPropertyReferencedAssets(TMap<FString, TSharedPtr<FJsonObject>>& ReferencedAssets, const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const FString Detail = FString::Printf(TEXT("%s.%s"), *Node->GetName(), *Property->GetName());
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				AddReferencedAsset(ReferencedAssets, ObjectProperty->GetObjectPropertyValue_InContainer(Node), TEXT("node_property"), Detail);
			}
			else if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
			{
				const FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue_InContainer(Node);
				AddReferencedAssetPath(ReferencedAssets, SoftObject.ToSoftObjectPath().ToString(), TEXT("node_soft_property"), Detail);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> ExportReferencedAssets(const UAnimBlueprint* AnimBlueprint)
	{
		TArray<TSharedPtr<FJsonValue>> Assets;
		if (!AnimBlueprint)
		{
			return Assets;
		}

		TMap<FString, TSharedPtr<FJsonObject>> ReferencedAssets;
		TArray<UEdGraph*> AllGraphs;
		AnimBlueprint->GetAllGraphs(AllGraphs);
		for (const UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}

				const FString Source = FString::Printf(TEXT("graph:%s node:%s"), *ObjectPath(Graph), *Node->GetName());
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin && Pin->DefaultObject)
					{
						AddReferencedAsset(ReferencedAssets, Pin->DefaultObject, Source, FString::Printf(TEXT("pin:%s default_object"), *Pin->PinName.ToString()));
					}
				}

				AddPropertyReferencedAssets(ReferencedAssets, Node);

				if (const UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(Node))
				{
					AddReferencedAsset(ReferencedAssets, AnimGraphNode->GetAnimationAsset(), Source, TEXT("anim_graph_node.animation_asset"));
				}

				if (const UAnimGraphNode_AssetPlayerBase* AssetPlayerNode = Cast<UAnimGraphNode_AssetPlayerBase>(Node))
				{
					TArray<UAnimationAsset*> ReferredAssets;
					const_cast<UAnimGraphNode_AssetPlayerBase*>(AssetPlayerNode)->GetAllAnimationSequencesReferred(ReferredAssets);
					for (const UAnimationAsset* ReferredAsset : ReferredAssets)
					{
						AddReferencedAsset(ReferencedAssets, ReferredAsset, Source, TEXT("asset_player.referred_animation_asset"));
					}
				}
			}
		}

		ReferencedAssets.ValueSort([](const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right)
		{
			return Left->GetStringField(TEXT("path")) < Right->GetStringField(TEXT("path"));
		});

		for (const TPair<FString, TSharedPtr<FJsonObject>>& ReferencedAsset : ReferencedAssets)
		{
			Assets.Add(MakeShared<FJsonValueObject>(ReferencedAsset.Value));
		}

		return Assets;
	}

	void AddGraphs(const TArray<UEdGraph*>& Graphs, const FString& GraphType, bool bIncludeNodeProperties, TArray<TSharedPtr<FJsonValue>>& JsonGraphs, TSet<const UEdGraph*>* AddedGraphs = nullptr)
	{
		for (const UEdGraph* Graph : Graphs)
		{
			if (Graph && (!AddedGraphs || !AddedGraphs->Contains(Graph)))
			{
				JsonGraphs.Add(MakeShared<FJsonValueObject>(ExportGraph(Graph, GraphType, bIncludeNodeProperties)));
				if (AddedGraphs)
				{
					AddedGraphs->Add(Graph);
				}
			}
		}
	}

	FString GetAdditionalGraphType(const UEdGraph* Graph)
	{
		if (!Graph)
		{
			return TEXT("additional");
		}

		const FString GraphName = Graph->GetName();
		const FString SchemaClass = ClassPath(Graph->GetSchema());
		if (Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph || GraphName.Contains(TEXT("AnimGraph")) || SchemaClass.Contains(TEXT("AnimGraph")))
		{
			return TEXT("anim_graph");
		}

		return TEXT("additional");
	}

	TSharedPtr<FJsonObject> ExportVariable(const FBPVariableDescription& Variable)
	{
		TSharedPtr<FJsonObject> JsonVariable = MakeShared<FJsonObject>();
		JsonVariable->SetStringField(TEXT("name"), Variable.VarName.ToString());
		JsonVariable->SetStringField(TEXT("guid"), Variable.VarGuid.ToString(EGuidFormats::DigitsWithHyphens));
		JsonVariable->SetStringField(TEXT("friendly_name"), Variable.FriendlyName);
		JsonVariable->SetStringField(TEXT("category"), Variable.Category.ToString());
		JsonVariable->SetStringField(TEXT("default_value"), Variable.DefaultValue);
		JsonVariable->SetStringField(TEXT("rep_notify_func"), Variable.RepNotifyFunc.ToString());
		JsonVariable->SetNumberField(TEXT("property_flags"), static_cast<double>(Variable.PropertyFlags));
		JsonVariable->SetObjectField(TEXT("type"), ExportPinType(Variable.VarType));
		return JsonVariable;
	}

	TSharedPtr<FJsonObject> ExportSCSNode(const USCS_Node* Node)
	{
		TSharedPtr<FJsonObject> JsonNode = MakeShared<FJsonObject>();
		if (!Node)
		{
			return JsonNode;
		}

		JsonNode->SetStringField(TEXT("variable_name"), Node->GetVariableName().ToString());
		JsonNode->SetStringField(TEXT("component_class"), ObjectPath(Node->ComponentClass));
		JsonNode->SetStringField(TEXT("component_template"), ObjectPath(Node->ComponentTemplate));
		JsonNode->SetObjectField(TEXT("defaults"), ExportObjectDefaults(Node->ComponentTemplate));

		TArray<TSharedPtr<FJsonValue>> Children;
		for (const USCS_Node* ChildNode : Node->GetChildNodes())
		{
			Children.Add(MakeShared<FJsonValueObject>(ExportSCSNode(ChildNode)));
		}
		JsonNode->SetArrayField(TEXT("children"), Children);

		return JsonNode;
	}

	TSharedPtr<FJsonObject> ExportWidget(const UWidget* Widget)
	{
		TSharedPtr<FJsonObject> JsonWidget = MakeShared<FJsonObject>();
		if (!Widget)
		{
			return JsonWidget;
		}

		JsonWidget->SetStringField(TEXT("name"), Widget->GetName());
		JsonWidget->SetStringField(TEXT("class"), ClassPath(Widget));
		JsonWidget->SetObjectField(TEXT("defaults"), ExportObjectDefaults(Widget));

		if (Widget->Slot)
		{
			JsonWidget->SetStringField(TEXT("slot_class"), ClassPath(Widget->Slot));
			JsonWidget->SetObjectField(TEXT("slot_defaults"), ExportObjectDefaults(Widget->Slot));
		}

		TArray<TSharedPtr<FJsonValue>> Children;
		if (const UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
		{
			const int32 ChildCount = PanelWidget->GetChildrenCount();
			for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
			{
				Children.Add(MakeShared<FJsonValueObject>(ExportWidget(PanelWidget->GetChildAt(ChildIndex))));
			}
		}
		JsonWidget->SetArrayField(TEXT("children"), Children);

		return JsonWidget;
	}

}

FString AssetToJson::ReadBlueprintVisualScriptAsJson(
	const FString& AssetPath,
	const FString& OutputFilePath,
	bool bPrettyPrint,
	bool bReturnJson,
	bool bIncludeNodeProperties)
{
	const FString ObjectPathString = NormalizeObjectPath(AssetPath);
	if (ObjectPathString.IsEmpty())
	{
		WriteAssetToJsonEvidence(AssetPath, OutputFilePath, false, TEXT("AssetPath is empty"));
		return SerializeJson(MakeErrorJson(TEXT("AssetPath is empty")).ToSharedRef(), bPrettyPrint);
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPathString);
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (!Blueprint)
	{
		WriteAssetToJsonEvidence(ObjectPathString, OutputFilePath, false, FString::Printf(TEXT("Asset is not a Blueprint or could not be loaded: %s"), *ObjectPathString));
		return SerializeJson(MakeErrorJson(FString::Printf(TEXT("Asset is not a Blueprint or could not be loaded: %s"), *ObjectPathString)).ToSharedRef(), bPrettyPrint);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("ue.asset_to_json.blueprint_visual_script.v1"));
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetObjectField(TEXT("exporter"), MakeExporterJson(TEXT("blueprint_visual_script"), TEXT("separate_patch_schema")));
	Root->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());

	TSharedPtr<FJsonObject> JsonAsset = MakeShared<FJsonObject>();
	JsonAsset->SetStringField(TEXT("path"), FPackageName::ObjectPathToPackageName(ObjectPathString));
	JsonAsset->SetStringField(TEXT("object_path"), ObjectPathString);
	JsonAsset->SetStringField(TEXT("name"), Blueprint->GetName());
	JsonAsset->SetStringField(TEXT("class"), ClassPath(Blueprint));
	JsonAsset->SetStringField(TEXT("parent_class"), ObjectPath(Blueprint->ParentClass));
	JsonAsset->SetStringField(TEXT("generated_class"), ObjectPath(Blueprint->GeneratedClass));
	Root->SetObjectField(TEXT("asset"), JsonAsset);

	TSharedPtr<FJsonObject> JsonBlueprint = MakeShared<FJsonObject>();
	JsonBlueprint->SetStringField(TEXT("type"), StaticEnum<EBlueprintType>()->GetNameStringByValue(Blueprint->BlueprintType));
	JsonBlueprint->SetStringField(TEXT("status"), StaticEnum<EBlueprintStatus>()->GetNameStringByValue(Blueprint->Status));
	Root->SetObjectField(TEXT("blueprint"), JsonBlueprint);

	const UObject* ClassDefaultObject = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
	const UObject* ParentClassDefaultObject = Blueprint->ParentClass ? Blueprint->ParentClass->GetDefaultObject() : nullptr;
	Root->SetObjectField(TEXT("class_defaults"), ExportObjectDefaults(ClassDefaultObject, ParentClassDefaultObject));
	Root->SetArrayField(TEXT("component_defaults"), ExportActorComponentDefaults(Cast<AActor>(ClassDefaultObject), Cast<AActor>(ParentClassDefaultObject)));

	TArray<TSharedPtr<FJsonValue>> Graphs;
	TSet<const UEdGraph*> AddedGraphs;
	AddGraphs(Blueprint->UbergraphPages, TEXT("ubergraph"), bIncludeNodeProperties, Graphs, &AddedGraphs);
	AddGraphs(Blueprint->FunctionGraphs, TEXT("function"), bIncludeNodeProperties, Graphs, &AddedGraphs);
	AddGraphs(Blueprint->MacroGraphs, TEXT("macro"), bIncludeNodeProperties, Graphs, &AddedGraphs);
	AddGraphs(Blueprint->DelegateSignatureGraphs, TEXT("delegate_signature"), bIncludeNodeProperties, Graphs, &AddedGraphs);
	if (const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint))
	{
		TArray<UEdGraph*> AllGraphs;
		AnimBlueprint->GetAllGraphs(AllGraphs);
		for (const UEdGraph* Graph : AllGraphs)
		{
			if (Graph && !AddedGraphs.Contains(Graph))
			{
				Graphs.Add(MakeShared<FJsonValueObject>(ExportGraph(Graph, GetAdditionalGraphType(Graph), bIncludeNodeProperties)));
				AddedGraphs.Add(Graph);
			}
		}

		TSharedPtr<FJsonObject> JsonAnimBlueprint = MakeShared<FJsonObject>();
		JsonAnimBlueprint->SetStringField(TEXT("target_skeleton"), ObjectPath(AnimBlueprint->TargetSkeleton));
		JsonAnimBlueprint->SetArrayField(TEXT("state_machines"), AssetToJson::ExportAnimationStateMachines(AnimBlueprint));
		JsonAnimBlueprint->SetArrayField(TEXT("referenced_assets"), ExportReferencedAssets(AnimBlueprint));
		Root->SetObjectField(TEXT("anim_blueprint"), JsonAnimBlueprint);
	}
	Root->SetArrayField(TEXT("graphs"), Graphs);

	TArray<TSharedPtr<FJsonValue>> Variables;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		Variables.Add(MakeShared<FJsonValueObject>(ExportVariable(Variable)));
	}
	Root->SetArrayField(TEXT("variables"), Variables);

	TArray<TSharedPtr<FJsonValue>> Components;
	if (Blueprint->SimpleConstructionScript)
	{
		for (const USCS_Node* RootNode : Blueprint->SimpleConstructionScript->GetRootNodes())
		{
			Components.Add(MakeShared<FJsonValueObject>(ExportSCSNode(RootNode)));
		}
	}
	Root->SetArrayField(TEXT("components"), Components);

	if (const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint))
	{
		TSharedPtr<FJsonObject> JsonWidgetBlueprint = MakeShared<FJsonObject>();

		if (WidgetBlueprint->WidgetTree)
		{
			JsonWidgetBlueprint->SetStringField(TEXT("widget_tree"), ObjectPath(WidgetBlueprint->WidgetTree));
			JsonWidgetBlueprint->SetObjectField(TEXT("root_widget"), ExportWidget(WidgetBlueprint->WidgetTree->RootWidget));
		}

		TArray<TSharedPtr<FJsonValue>> Animations;
		for (const UWidgetAnimation* Animation : WidgetBlueprint->Animations)
		{
			Animations.Add(MakeShared<FJsonValueObject>(AssetToJson::ExportEnhancedWidgetAnimation(Animation)));
		}
		JsonWidgetBlueprint->SetArrayField(TEXT("animations"), Animations);

		TArray<TSharedPtr<FJsonValue>> Bindings;
		for (const FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
		{
			Bindings.Add(MakeShared<FJsonValueObject>(AssetToJson::ExportWidgetBinding(Binding)));
		}
		JsonWidgetBlueprint->SetArrayField(TEXT("bindings"), Bindings);

		Root->SetObjectField(TEXT("widget_blueprint"), JsonWidgetBlueprint);
	}

	const FString JsonString = SerializeJson(Root.ToSharedRef(), bPrettyPrint);
	if (!SaveJsonStringToFile(JsonString, OutputFilePath))
	{
		WriteAssetToJsonEvidence(ObjectPathString, OutputFilePath, false, FString::Printf(TEXT("Failed to write JSON file: %s"), *OutputFilePath), Graphs.Num(), Variables.Num(), Components.Num());
		return SerializeJson(MakeErrorJson(FString::Printf(TEXT("Failed to write JSON file: %s"), *OutputFilePath)).ToSharedRef(), bPrettyPrint);
	}

	WriteAssetToJsonEvidence(ObjectPathString, OutputFilePath, true, FString(), Graphs.Num(), Variables.Num(), Components.Num());

	return bReturnJson ? JsonString : FString();
}
