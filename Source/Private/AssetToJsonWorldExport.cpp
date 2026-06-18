#include "AssetToJsonWorldExport.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/EngineTypes.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/MemberReference.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/Volume.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace AssetToJsonWorldPrivate
{
	struct FActorReferenceEdge
	{
		FString FromActorPath;
		FString FromActorGuid;
		FString PropertyPath;
		FString ToActorPath;
		FString ToActorGuid;
		FString ToActorClass;
		FString ToActorExternalPackage;
		FString Kind;
	};

	struct FGameplayClassification
	{
		TArray<FString> Categories;
		TArray<TSharedPtr<FJsonValue>> Reasons;
	};

	FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	FString ClassPath(const UClass* Class)
	{
		return Class ? Class->GetPathName() : FString();
	}

	FString ClassPath(const UObject* Object)
	{
		return Object && Object->GetClass() ? ClassPath(Object->GetClass()) : FString();
	}

	FString PackagePath(const UObject* Object)
	{
		const UPackage* Package = Object ? Object->GetPackage() : nullptr;
		return Package ? Package->GetName() : FString();
	}

	FString ExternalPackagePath(const UObject* Object)
	{
		const UPackage* Package = Object && Object->IsPackageExternal() ? Object->GetExternalPackage() : nullptr;
		return Package ? Package->GetName() : FString();
	}

	FString GuidToString(const FGuid& Guid)
	{
		return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
	}

	FString NodeIdToString(const UEdGraphNode* Node)
	{
		return Node ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
	}

	FString PinIdToString(const UEdGraphPin* Pin)
	{
		return Pin ? Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens) : FString();
	}

	FString EnumValueToString(const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetNameStringByValue(Value) : FString::Printf(TEXT("%lld"), Value);
	}

	FString WorldTypeToString(EWorldType::Type WorldType)
	{
		return LexToString(WorldType);
	}

	FString ComponentCreationMethodToString(EComponentCreationMethod CreationMethod)
	{
		return EnumValueToString(StaticEnum<EComponentCreationMethod>(), static_cast<int64>(CreationMethod));
	}

	TSharedPtr<FJsonObject> MakeErrorJson(const FString& Message)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("ue.asset_to_json.world.v1"));
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetStringField(TEXT("error"), Message);
		return Root;
	}

	TSharedPtr<FJsonObject> MakeExporterJson()
	{
		TSharedPtr<FJsonObject> JsonExporter = MakeShared<FJsonObject>();
		JsonExporter->SetStringField(TEXT("type"), TEXT("world_map"));
		JsonExporter->SetStringField(TEXT("write_support"), TEXT("none"));
		JsonExporter->SetBoolField(TEXT("supports_round_trip"), false);
		return JsonExporter;
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

	TSharedPtr<FJsonObject> ExportVector(const FVector& Vector)
	{
		TSharedPtr<FJsonObject> JsonVector = MakeShared<FJsonObject>();
		JsonVector->SetNumberField(TEXT("x"), Vector.X);
		JsonVector->SetNumberField(TEXT("y"), Vector.Y);
		JsonVector->SetNumberField(TEXT("z"), Vector.Z);
		return JsonVector;
	}

	TSharedPtr<FJsonObject> ExportRotator(const FRotator& Rotator)
	{
		TSharedPtr<FJsonObject> JsonRotator = MakeShared<FJsonObject>();
		JsonRotator->SetNumberField(TEXT("pitch"), Rotator.Pitch);
		JsonRotator->SetNumberField(TEXT("yaw"), Rotator.Yaw);
		JsonRotator->SetNumberField(TEXT("roll"), Rotator.Roll);
		return JsonRotator;
	}

	TSharedPtr<FJsonObject> ExportTransform(const FTransform& Transform)
	{
		TSharedPtr<FJsonObject> JsonTransform = MakeShared<FJsonObject>();
		JsonTransform->SetObjectField(TEXT("location"), ExportVector(Transform.GetLocation()));
		JsonTransform->SetObjectField(TEXT("rotation"), ExportRotator(Transform.Rotator()));
		JsonTransform->SetObjectField(TEXT("scale"), ExportVector(Transform.GetScale3D()));
		return JsonTransform;
	}

	TSharedPtr<FJsonObject> ExportBounds(const AActor* Actor)
	{
		TSharedPtr<FJsonObject> JsonBounds = MakeShared<FJsonObject>();
		if (!Actor)
		{
			return JsonBounds;
		}

		FVector Origin = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		Actor->GetActorBounds(false, Origin, Extent);
		JsonBounds->SetObjectField(TEXT("origin"), ExportVector(Origin));
		JsonBounds->SetObjectField(TEXT("box_extent"), ExportVector(Extent));
		return JsonBounds;
	}

	TArray<TSharedPtr<FJsonValue>> ExportNameArray(const TArray<FName>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> JsonNames;
		for (const FName& Name : Names)
		{
			if (!Name.IsNone())
			{
				JsonNames.Add(MakeShared<FJsonValueString>(Name.ToString()));
			}
		}

		return JsonNames;
	}

	bool ShouldExportDetailsProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const bool bIsDetailsVisible = Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_Interp);
		const bool bIsTransientOnly = Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_TextExportTransient);
		const bool bIsDeprecated = Property->HasAnyPropertyFlags(CPF_Deprecated);
		return bIsDetailsVisible && !bIsTransientOnly && !bIsDeprecated;
	}

	bool ShouldCollectReferenceProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		return !Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_TextExportTransient | CPF_Deprecated);
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

	TSharedPtr<FJsonObject> ExportDetailsProperty(const UObject* Object, const FProperty* Property, const FString& ExportedValue, bool bIsOverride)
	{
		TSharedPtr<FJsonObject> JsonProperty = MakeShared<FJsonObject>();
		if (!Object || !Property)
		{
			return JsonProperty;
		}

		JsonProperty->SetStringField(TEXT("name"), Property->GetName());
		JsonProperty->SetStringField(TEXT("display_name"), Property->GetDisplayNameText().ToString());
		JsonProperty->SetStringField(TEXT("category"), Property->GetMetaData(TEXT("Category")));
		JsonProperty->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
		JsonProperty->SetStringField(TEXT("owner_class"), ClassPath(Property->GetOwnerClass()));
		JsonProperty->SetNumberField(TEXT("property_flags"), static_cast<double>(Property->GetPropertyFlags()));
		JsonProperty->SetBoolField(TEXT("is_override"), bIsOverride);
		JsonProperty->SetStringField(TEXT("value"), ExportedValue);
		return JsonProperty;
	}

	TArray<TSharedPtr<FJsonValue>> ExportDetailsProperties(const UObject* Object, const UObject* ComparisonObject, bool bOnlyChanged)
	{
		TArray<TSharedPtr<FJsonValue>> Properties;
		if (!Object)
		{
			return Properties;
		}

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

			const bool bIsOverride = ComparisonObject && !HasSamePropertyValue(Object, ComparisonObject, Property, ExportedValue);
			if (bOnlyChanged && !bIsOverride)
			{
				continue;
			}

			Properties.Add(MakeShared<FJsonValueObject>(ExportDetailsProperty(Object, Property, ExportedValue, bIsOverride)));
		}

		return Properties;
	}

	TSharedPtr<FJsonObject> ExportObjectProperties(const UObject* Object, const UObject* ComparisonObject, bool bOnlyChanged)
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		if (!Object)
		{
			return JsonObject;
		}

		JsonObject->SetStringField(TEXT("object"), ObjectPath(Object));
		JsonObject->SetStringField(TEXT("class"), ClassPath(Object));
		if (ComparisonObject)
		{
			JsonObject->SetStringField(TEXT("comparison_object"), ObjectPath(ComparisonObject));
		}

		JsonObject->SetBoolField(TEXT("only_changed_from_comparison"), bOnlyChanged);
		JsonObject->SetArrayField(TEXT("properties"), ExportDetailsProperties(Object, ComparisonObject, bOnlyChanged));
		return JsonObject;
	}

	const UClass* FindNativeClass(const UClass* Class)
	{
		const UClass* Current = Class;
		while (Current && !Current->HasAnyClassFlags(CLASS_Native))
		{
			Current = Current->GetSuperClass();
		}

		return Current;
	}

	TSharedPtr<FJsonObject> ExportClassInfo(const UClass* Class)
	{
		TSharedPtr<FJsonObject> JsonClass = MakeShared<FJsonObject>();
		if (!Class)
		{
			return JsonClass;
		}

		JsonClass->SetStringField(TEXT("class"), ClassPath(Class));
		JsonClass->SetStringField(TEXT("class_name"), Class->GetName());
		JsonClass->SetStringField(TEXT("super_class"), ClassPath(Class->GetSuperClass()));
		JsonClass->SetStringField(TEXT("native_class"), ClassPath(FindNativeClass(Class)));
		JsonClass->SetBoolField(TEXT("is_blueprint_class"), Cast<UBlueprintGeneratedClass>(Class) != nullptr);

#if WITH_EDITORONLY_DATA
		const UObject* GeneratedBy = Class->ClassGeneratedBy;
		JsonClass->SetStringField(TEXT("generated_by"), ObjectPath(GeneratedBy));
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(GeneratedBy))
		{
			JsonClass->SetStringField(TEXT("blueprint_asset"), ObjectPath(Blueprint));
			JsonClass->SetStringField(TEXT("blueprint_class"), ClassPath(Class));
			JsonClass->SetStringField(TEXT("blueprint_parent_class"), ClassPath(Blueprint->ParentClass));
		}
		else
		{
			JsonClass->SetStringField(TEXT("blueprint_asset"), FString());
			JsonClass->SetStringField(TEXT("blueprint_class"), FString());
		}
#else
		JsonClass->SetStringField(TEXT("generated_by"), FString());
		JsonClass->SetStringField(TEXT("blueprint_asset"), FString());
		JsonClass->SetStringField(TEXT("blueprint_class"), FString());
#endif

		TArray<TSharedPtr<FJsonValue>> ParentClasses;
		for (const UClass* ParentClass = Class; ParentClass; ParentClass = ParentClass->GetSuperClass())
		{
			ParentClasses.Add(MakeShared<FJsonValueString>(ClassPath(ParentClass)));
		}
		JsonClass->SetArrayField(TEXT("class_hierarchy"), ParentClasses);

		return JsonClass;
	}

	TSharedPtr<FJsonObject> ExportActorReference(const AActor* Actor)
	{
		TSharedPtr<FJsonObject> JsonActor = MakeShared<FJsonObject>();
		if (!Actor)
		{
			return JsonActor;
		}

		JsonActor->SetStringField(TEXT("path"), ObjectPath(Actor));
		JsonActor->SetStringField(TEXT("name"), Actor->GetName());
#if WITH_EDITOR
		JsonActor->SetStringField(TEXT("label"), Actor->GetActorLabel());
		JsonActor->SetStringField(TEXT("guid"), GuidToString(Actor->GetActorGuid()));
#else
		JsonActor->SetStringField(TEXT("label"), Actor->GetName());
		JsonActor->SetStringField(TEXT("guid"), FString());
#endif
		JsonActor->SetStringField(TEXT("class"), ClassPath(Actor));
		JsonActor->SetStringField(TEXT("blueprint_asset"), ExportClassInfo(Actor->GetClass())->GetStringField(TEXT("blueprint_asset")));
		JsonActor->SetStringField(TEXT("level"), ObjectPath(Actor->GetLevel()));
		JsonActor->SetStringField(TEXT("external_package"), ExternalPackagePath(Actor));
		return JsonActor;
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

	TSharedPtr<FJsonObject> ExportPinRef(const UEdGraphPin* Pin)
	{
		TSharedPtr<FJsonObject> JsonPin = MakeShared<FJsonObject>();
		if (!Pin)
		{
			return JsonPin;
		}

		JsonPin->SetStringField(TEXT("id"), PinIdToString(Pin));
		JsonPin->SetStringField(TEXT("name"), Pin->PinName.ToString());
		if (!Pin->DefaultValue.IsEmpty())
		{
			JsonPin->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		}
		if (Pin->DefaultObject)
		{
			JsonPin->SetStringField(TEXT("default_object"), ObjectPath(Pin->DefaultObject));
		}

		return JsonPin;
	}

	TSharedPtr<FJsonObject> ExportLevelBlueprintEventNode(const UEdGraph* Graph, const UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> JsonEvent = MakeShared<FJsonObject>();
		if (!Graph || !Node)
		{
			return JsonEvent;
		}

		JsonEvent->SetStringField(TEXT("node_id"), NodeIdToString(Node));
		JsonEvent->SetStringField(TEXT("node_name"), Node->GetName());
		JsonEvent->SetStringField(TEXT("node_class"), ClassPath(Node));
		JsonEvent->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		JsonEvent->SetStringField(TEXT("graph"), Graph->GetName());
		JsonEvent->SetStringField(TEXT("comment"), Node->NodeComment);

		if (const UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
		{
			JsonEvent->SetStringField(TEXT("kind"), TEXT("custom_event"));
			JsonEvent->SetStringField(TEXT("function_name"), CustomEventNode->GetFunctionName().ToString());
			JsonEvent->SetStringField(TEXT("custom_function_name"), CustomEventNode->CustomFunctionName.ToString());
			JsonEvent->SetBoolField(TEXT("call_in_editor"), CustomEventNode->bCallInEditor);
			JsonEvent->SetBoolField(TEXT("is_override"), CustomEventNode->IsOverride());
			JsonEvent->SetNumberField(TEXT("function_flags"), static_cast<double>(CustomEventNode->FunctionFlags));
			JsonEvent->SetNumberField(TEXT("net_flags"), static_cast<double>(CustomEventNode->GetNetFlags()));
			JsonEvent->SetObjectField(TEXT("event_reference"), ExportMemberReference(CustomEventNode->EventReference));
		}
		else if (const UK2Node_ComponentBoundEvent* ComponentEventNode = Cast<UK2Node_ComponentBoundEvent>(Node))
		{
			JsonEvent->SetStringField(TEXT("kind"), TEXT("component_bound_event"));
			JsonEvent->SetStringField(TEXT("function_name"), ComponentEventNode->GetFunctionName().ToString());
			JsonEvent->SetStringField(TEXT("component_property_name"), ComponentEventNode->GetComponentPropertyName().ToString());
			JsonEvent->SetStringField(TEXT("delegate_property_name"), ComponentEventNode->DelegatePropertyName.ToString());
			JsonEvent->SetStringField(TEXT("delegate_display_name"), ComponentEventNode->GetTargetDelegateDisplayName().ToString());
			JsonEvent->SetStringField(TEXT("delegate_owner_class"), ObjectPath(ComponentEventNode->DelegateOwnerClass));
			JsonEvent->SetObjectField(TEXT("event_reference"), ExportMemberReference(ComponentEventNode->EventReference));
		}
		else if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
		{
			JsonEvent->SetStringField(TEXT("kind"), TEXT("event"));
			JsonEvent->SetStringField(TEXT("function_name"), EventNode->GetFunctionName().ToString());
			JsonEvent->SetStringField(TEXT("custom_function_name"), EventNode->CustomFunctionName.ToString());
			JsonEvent->SetBoolField(TEXT("is_override_function"), EventNode->bOverrideFunction);
			JsonEvent->SetBoolField(TEXT("is_internal_event"), EventNode->bInternalEvent);
			JsonEvent->SetBoolField(TEXT("is_interface_event"), EventNode->IsInterfaceEventNode());
			JsonEvent->SetNumberField(TEXT("function_flags"), static_cast<double>(EventNode->FunctionFlags));
			JsonEvent->SetObjectField(TEXT("event_reference"), ExportMemberReference(EventNode->EventReference));
		}

		TArray<TSharedPtr<FJsonValue>> OutputPins;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				OutputPins.Add(MakeShared<FJsonValueObject>(ExportPinRef(Pin)));
			}
		}
		JsonEvent->SetArrayField(TEXT("output_pins"), OutputPins);

		return JsonEvent;
	}

	bool IsLevelBlueprintEventNode(const UEdGraphNode* Node)
	{
		return Node && (Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_CustomEvent>() || Node->IsA<UK2Node_ComponentBoundEvent>());
	}

	void AddBlueprintGraphs(const TArray<UEdGraph*>& SourceGraphs, const FString& GraphType, TArray<TSharedPtr<FJsonValue>>& OutGraphs, TArray<TSharedPtr<FJsonValue>>& OutEvents)
	{
		for (const UEdGraph* Graph : SourceGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			TSharedPtr<FJsonObject> JsonGraph = MakeShared<FJsonObject>();
			JsonGraph->SetStringField(TEXT("name"), Graph->GetName());
			JsonGraph->SetStringField(TEXT("type"), GraphType);
			JsonGraph->SetStringField(TEXT("schema"), ClassPath(Graph->GetSchema()));
			JsonGraph->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

			TArray<TSharedPtr<FJsonValue>> GraphEvents;
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (IsLevelBlueprintEventNode(Node))
				{
					TSharedPtr<FJsonObject> JsonEvent = ExportLevelBlueprintEventNode(Graph, Node);
					GraphEvents.Add(MakeShared<FJsonValueObject>(JsonEvent));
					OutEvents.Add(MakeShared<FJsonValueObject>(JsonEvent));
				}
			}

			JsonGraph->SetArrayField(TEXT("events"), GraphEvents);
			OutGraphs.Add(MakeShared<FJsonValueObject>(JsonGraph));
		}
	}

	TSharedPtr<FJsonObject> ExportLevelBlueprint(const ULevel* Level)
	{
		TSharedPtr<FJsonObject> JsonLevelBlueprint = MakeShared<FJsonObject>();
		JsonLevelBlueprint->SetBoolField(TEXT("exists"), false);
		if (!Level)
		{
			return JsonLevelBlueprint;
		}

		const ALevelScriptActor* LevelScriptActor = Level->GetLevelScriptActor();
		JsonLevelBlueprint->SetStringField(TEXT("level_script_actor"), ObjectPath(LevelScriptActor));
		JsonLevelBlueprint->SetStringField(TEXT("level_script_actor_class"), ClassPath(LevelScriptActor));

#if WITH_EDITOR
		const ULevelScriptBlueprint* LevelScriptBlueprint = const_cast<ULevel*>(Level)->GetLevelScriptBlueprint(true);
		JsonLevelBlueprint->SetBoolField(TEXT("exists"), LevelScriptBlueprint != nullptr);
		if (!LevelScriptBlueprint)
		{
			return JsonLevelBlueprint;
		}

		JsonLevelBlueprint->SetStringField(TEXT("blueprint"), ObjectPath(LevelScriptBlueprint));
		JsonLevelBlueprint->SetStringField(TEXT("friendly_name"), LevelScriptBlueprint->GetFriendlyName());
		JsonLevelBlueprint->SetStringField(TEXT("parent_class"), ClassPath(LevelScriptBlueprint->ParentClass));
		JsonLevelBlueprint->SetStringField(TEXT("generated_class"), ClassPath(LevelScriptBlueprint->GeneratedClass));
		JsonLevelBlueprint->SetStringField(TEXT("status"), EnumValueToString(StaticEnum<EBlueprintStatus>(), static_cast<int64>(LevelScriptBlueprint->Status)));

		TArray<TSharedPtr<FJsonValue>> Graphs;
		TArray<TSharedPtr<FJsonValue>> Events;
		AddBlueprintGraphs(LevelScriptBlueprint->UbergraphPages, TEXT("ubergraph"), Graphs, Events);
		AddBlueprintGraphs(LevelScriptBlueprint->FunctionGraphs, TEXT("function"), Graphs, Events);
		AddBlueprintGraphs(LevelScriptBlueprint->MacroGraphs, TEXT("macro"), Graphs, Events);
		AddBlueprintGraphs(LevelScriptBlueprint->DelegateSignatureGraphs, TEXT("delegate_signature"), Graphs, Events);
		JsonLevelBlueprint->SetArrayField(TEXT("graphs"), Graphs);
		JsonLevelBlueprint->SetArrayField(TEXT("events"), Events);
		JsonLevelBlueprint->SetNumberField(TEXT("event_count"), Events.Num());
#endif

		return JsonLevelBlueprint;
	}

	const AActor* FindReferencedActor(const UObject* Object)
	{
		if (!Object)
		{
			return nullptr;
		}

		if (const AActor* Actor = Cast<AActor>(Object))
		{
			return Actor;
		}

		if (const UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			return Component->GetOwner();
		}

		return Object->GetTypedOuter<AActor>();
	}

	void AddActorReferenceEdge(const AActor* FromActor, const AActor* ToActor, const FString& PropertyPath, const FString& Kind, TArray<FActorReferenceEdge>& OutEdges, TSet<FString>& SeenEdges)
	{
		if (!FromActor || !ToActor || FromActor == ToActor)
		{
			return;
		}

		const FString FromPath = ObjectPath(FromActor);
		const FString ToPath = ObjectPath(ToActor);
		const FString EdgeKey = FString::Printf(TEXT("%s|%s|%s"), *FromPath, *PropertyPath, *ToPath);
		if (SeenEdges.Contains(EdgeKey))
		{
			return;
		}

		SeenEdges.Add(EdgeKey);

		FActorReferenceEdge Edge;
		Edge.FromActorPath = FromPath;
#if WITH_EDITOR
		Edge.FromActorGuid = GuidToString(FromActor->GetActorGuid());
		Edge.ToActorGuid = GuidToString(ToActor->GetActorGuid());
#endif
		Edge.PropertyPath = PropertyPath;
		Edge.ToActorPath = ToPath;
		Edge.ToActorClass = ClassPath(ToActor);
		Edge.ToActorExternalPackage = ExternalPackagePath(ToActor);
		Edge.Kind = Kind;
		OutEdges.Add(Edge);
	}

	void CollectActorReferencesFromValue(const AActor* SourceActor, const FProperty* Property, const void* ValuePtr, const FString& PropertyPath, TArray<FActorReferenceEdge>& OutEdges, TSet<FString>& SeenEdges);

	void CollectActorReferencesFromStruct(const AActor* SourceActor, const UStruct* Struct, const void* StructPtr, const FString& PropertyPath, TArray<FActorReferenceEdge>& OutEdges, TSet<FString>& SeenEdges)
	{
		if (!SourceActor || !Struct || !StructPtr)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* InnerProperty = *It;
			if (!ShouldCollectReferenceProperty(InnerProperty))
			{
				continue;
			}

			const FString InnerPath = PropertyPath.IsEmpty() ? InnerProperty->GetName() : FString::Printf(TEXT("%s.%s"), *PropertyPath, *InnerProperty->GetName());
			const void* InnerValuePtr = InnerProperty->ContainerPtrToValuePtr<void>(StructPtr);
			CollectActorReferencesFromValue(SourceActor, InnerProperty, InnerValuePtr, InnerPath, OutEdges, SeenEdges);
		}
	}

	void CollectActorReferencesFromValue(const AActor* SourceActor, const FProperty* Property, const void* ValuePtr, const FString& PropertyPath, TArray<FActorReferenceEdge>& OutEdges, TSet<FString>& SeenEdges)
	{
		if (!SourceActor || !Property || !ValuePtr)
		{
			return;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* ReferencedObject = ObjectProperty->GetObjectPropertyValue(ValuePtr);
			AddActorReferenceEdge(SourceActor, FindReferencedActor(ReferencedObject), PropertyPath, TEXT("hard_object_property"), OutEdges, SeenEdges);
			return;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
			{
				CollectActorReferencesFromValue(SourceActor, ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), FString::Printf(TEXT("%s[%d]"), *PropertyPath, Index), OutEdges, SeenEdges);
			}
			return;
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper SetHelper(SetProperty, ValuePtr);
			for (int32 Index = 0; Index < SetHelper.GetMaxIndex(); ++Index)
			{
				if (SetHelper.IsValidIndex(Index))
				{
					CollectActorReferencesFromValue(SourceActor, SetProperty->ElementProp, SetHelper.GetElementPtr(Index), FString::Printf(TEXT("%s[%d]"), *PropertyPath, Index), OutEdges, SeenEdges);
				}
			}
			return;
		}

		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper MapHelper(MapProperty, ValuePtr);
			for (int32 Index = 0; Index < MapHelper.GetMaxIndex(); ++Index)
			{
				if (MapHelper.IsValidIndex(Index))
				{
					CollectActorReferencesFromValue(SourceActor, MapProperty->KeyProp, MapHelper.GetKeyPtr(Index), FString::Printf(TEXT("%s[%d].key"), *PropertyPath, Index), OutEdges, SeenEdges);
					CollectActorReferencesFromValue(SourceActor, MapProperty->ValueProp, MapHelper.GetValuePtr(Index), FString::Printf(TEXT("%s[%d].value"), *PropertyPath, Index), OutEdges, SeenEdges);
				}
			}
			return;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			CollectActorReferencesFromStruct(SourceActor, StructProperty->Struct, ValuePtr, PropertyPath, OutEdges, SeenEdges);
		}
	}

	void CollectActorReferencesFromObject(const AActor* SourceActor, const UObject* Object, const FString& PropertyPrefix, TArray<FActorReferenceEdge>& OutEdges, TSet<FString>& SeenEdges)
	{
		if (!SourceActor || !Object)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!ShouldCollectReferenceProperty(Property))
			{
				continue;
			}

			const FString PropertyPath = PropertyPrefix.IsEmpty() ? Property->GetName() : FString::Printf(TEXT("%s.%s"), *PropertyPrefix, *Property->GetName());
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
			CollectActorReferencesFromValue(SourceActor, Property, ValuePtr, PropertyPath, OutEdges, SeenEdges);
		}
	}

	TSharedPtr<FJsonObject> ExportActorReferenceEdge(const FActorReferenceEdge& Edge)
	{
		TSharedPtr<FJsonObject> JsonEdge = MakeShared<FJsonObject>();
		JsonEdge->SetStringField(TEXT("from_actor"), Edge.FromActorPath);
		JsonEdge->SetStringField(TEXT("from_actor_guid"), Edge.FromActorGuid);
		JsonEdge->SetStringField(TEXT("property_path"), Edge.PropertyPath);
		JsonEdge->SetStringField(TEXT("to_actor"), Edge.ToActorPath);
		JsonEdge->SetStringField(TEXT("to_actor_guid"), Edge.ToActorGuid);
		JsonEdge->SetStringField(TEXT("to_actor_class"), Edge.ToActorClass);
		JsonEdge->SetStringField(TEXT("to_actor_external_package"), Edge.ToActorExternalPackage);
		JsonEdge->SetStringField(TEXT("kind"), Edge.Kind);
		return JsonEdge;
	}

	TArray<TSharedPtr<FJsonValue>> ExportActorReferenceEdges(const TArray<FActorReferenceEdge>& Edges)
	{
		TArray<TSharedPtr<FJsonValue>> JsonEdges;
		for (const FActorReferenceEdge& Edge : Edges)
		{
			JsonEdges.Add(MakeShared<FJsonValueObject>(ExportActorReferenceEdge(Edge)));
		}

		return JsonEdges;
	}

	void AddClassificationReason(FGameplayClassification& Classification, const FString& Category, const FString& Reason)
	{
		if (!Classification.Categories.Contains(Category))
		{
			Classification.Categories.Add(Category);
		}

		TSharedPtr<FJsonObject> JsonReason = MakeShared<FJsonObject>();
		JsonReason->SetStringField(TEXT("category"), Category);
		JsonReason->SetStringField(TEXT("reason"), Reason);
		Classification.Reasons.Add(MakeShared<FJsonValueObject>(JsonReason));
	}

	bool ContainsGameplayToken(const FString& HaystackLower, std::initializer_list<const TCHAR*> Tokens)
	{
		for (const TCHAR* Token : Tokens)
		{
			if (HaystackLower.Contains(Token))
			{
				return true;
			}
		}

		return false;
	}

	FGameplayClassification ClassifyGameplayActor(const AActor* Actor)
	{
		FGameplayClassification Classification;
		if (!Actor)
		{
			return Classification;
		}

		const UClass* ActorClass = Actor->GetClass();
		const TSharedPtr<FJsonObject> ClassInfo = ExportClassInfo(ActorClass);
		FString SearchText = Actor->GetName() + TEXT(" ") + ClassPath(ActorClass) + TEXT(" ") + ClassInfo->GetStringField(TEXT("blueprint_asset"));
#if WITH_EDITOR
		SearchText += TEXT(" ") + Actor->GetActorLabel();
#endif
		SearchText.ToLowerInline();

		if (ContainsGameplayToken(SearchText, { TEXT("enemy"), TEXT("enemies"), TEXT("monster"), TEXT("zombie"), TEXT("bot"), TEXT("npc"), TEXT("hostile") }))
		{
			AddClassificationReason(Classification, TEXT("enemy"), TEXT("name_or_class_keyword"));
		}

		if (ContainsGameplayToken(SearchText, { TEXT("spawner"), TEXT("spawn_point"), TEXT("spawnpoint"), TEXT("spawnpad"), TEXT("respawner") }))
		{
			AddClassificationReason(Classification, TEXT("spawner"), TEXT("name_or_class_keyword"));
		}

		if (ContainsGameplayToken(SearchText, { TEXT("checkpoint"), TEXT("check_point"), TEXT("savepoint"), TEXT("respawn") }))
		{
			AddClassificationReason(Classification, TEXT("checkpoint"), TEXT("name_or_class_keyword"));
		}

		if (ContainsGameplayToken(SearchText, { TEXT("pickup"), TEXT("collectible"), TEXT("coin"), TEXT("reward") }))
		{
			AddClassificationReason(Classification, TEXT("pickup"), TEXT("name_or_class_keyword"));
		}

		if (ContainsGameplayToken(SearchText, { TEXT("objective"), TEXT("goal"), TEXT("mission") }))
		{
			AddClassificationReason(Classification, TEXT("objective"), TEXT("name_or_class_keyword"));
		}

		if (Actor->IsA<APlayerStart>())
		{
			AddClassificationReason(Classification, TEXT("player_start"), TEXT("is_a_player_start"));
			AddClassificationReason(Classification, TEXT("spawner"), TEXT("player_start_spawn_point"));
		}

		if (Actor->IsA<AVolume>() || ContainsGameplayToken(SearchText, { TEXT("volume") }))
		{
			AddClassificationReason(Classification, TEXT("volume"), Actor->IsA<AVolume>() ? TEXT("is_a_volume") : TEXT("name_or_class_keyword"));
		}

		if (ContainsGameplayToken(SearchText, { TEXT("trigger") }))
		{
			AddClassificationReason(Classification, TEXT("trigger"), TEXT("name_or_class_keyword"));
		}

		if (Actor->IsA<ACharacter>())
		{
			AddClassificationReason(Classification, TEXT("character"), TEXT("is_a_character"));
		}
		else if (Actor->IsA<APawn>())
		{
			AddClassificationReason(Classification, TEXT("pawn"), TEXT("is_a_pawn"));
		}

		return Classification;
	}

	TSharedPtr<FJsonObject> ExportGameplayClassification(const FGameplayClassification& Classification)
	{
		TSharedPtr<FJsonObject> JsonClassification = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Categories;
		for (const FString& Category : Classification.Categories)
		{
			Categories.Add(MakeShared<FJsonValueString>(Category));
		}

		JsonClassification->SetArrayField(TEXT("categories"), Categories);
		JsonClassification->SetArrayField(TEXT("reasons"), Classification.Reasons);
		return JsonClassification;
	}

	TSharedPtr<FJsonObject> ExportComponent(const UActorComponent* Component)
	{
		TSharedPtr<FJsonObject> JsonComponent = MakeShared<FJsonObject>();
		if (!Component)
		{
			return JsonComponent;
		}

		const UObject* Archetype = Component->GetArchetype();
		if (Archetype == Component)
		{
			Archetype = nullptr;
		}

		JsonComponent->SetStringField(TEXT("name"), Component->GetName());
		JsonComponent->SetStringField(TEXT("path"), ObjectPath(Component));
		JsonComponent->SetObjectField(TEXT("class_info"), ExportClassInfo(Component->GetClass()));
		JsonComponent->SetStringField(TEXT("creation_method"), ComponentCreationMethodToString(Component->CreationMethod));
		JsonComponent->SetBoolField(TEXT("is_registered"), Component->IsRegistered());
		JsonComponent->SetBoolField(TEXT("is_editor_only"), Component->IsEditorOnly());
		JsonComponent->SetArrayField(TEXT("tags"), ExportNameArray(Component->ComponentTags));
		JsonComponent->SetObjectField(TEXT("property_overrides"), ExportObjectProperties(Component, Archetype, true));
		if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			JsonComponent->SetObjectField(TEXT("relative_transform"), ExportTransform(SceneComponent->GetRelativeTransform()));
			JsonComponent->SetStringField(TEXT("attach_parent"), ObjectPath(SceneComponent->GetAttachParent()));
			JsonComponent->SetStringField(TEXT("attach_socket"), SceneComponent->GetAttachSocketName().ToString());
		}

		return JsonComponent;
	}

	TArray<TSharedPtr<FJsonValue>> ExportActorComponents(const AActor* Actor)
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
			JsonComponents.Add(MakeShared<FJsonValueObject>(ExportComponent(Component)));
		}

		return JsonComponents;
	}

	TSharedPtr<FJsonObject> ExportActor(const AActor* Actor, TArray<FActorReferenceEdge>& OutReferenceEdges, TSet<FString>& SeenReferenceEdges, FGameplayClassification& OutClassification)
	{
		TSharedPtr<FJsonObject> JsonActor = MakeShared<FJsonObject>();
		if (!Actor)
		{
			return JsonActor;
		}

		const UObject* Archetype = Actor->GetArchetype();
		if (Archetype == Actor)
		{
			Archetype = Actor->GetClass() ? Actor->GetClass()->GetDefaultObject() : nullptr;
		}

		CollectActorReferencesFromObject(Actor, Actor, TEXT("actor"), OutReferenceEdges, SeenReferenceEdges);

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (const UActorComponent* Component : Components)
		{
			if (Component)
			{
				CollectActorReferencesFromObject(Actor, Component, FString::Printf(TEXT("component:%s"), *Component->GetName()), OutReferenceEdges, SeenReferenceEdges);
			}
		}

		const int32 ReferenceStartIndex = 0;
		OutClassification = ClassifyGameplayActor(Actor);

		JsonActor->SetStringField(TEXT("name"), Actor->GetName());
		JsonActor->SetStringField(TEXT("path"), ObjectPath(Actor));
		JsonActor->SetStringField(TEXT("package"), PackagePath(Actor));
		JsonActor->SetStringField(TEXT("external_package"), ExternalPackagePath(Actor));
		JsonActor->SetBoolField(TEXT("is_external_package"), Actor->IsPackageExternal());
		JsonActor->SetStringField(TEXT("level"), ObjectPath(Actor->GetLevel()));
		JsonActor->SetObjectField(TEXT("class_info"), ExportClassInfo(Actor->GetClass()));
#if WITH_EDITOR
		JsonActor->SetStringField(TEXT("label"), Actor->GetActorLabel());
		JsonActor->SetStringField(TEXT("folder_path"), Actor->GetFolderPath().ToString());
		JsonActor->SetStringField(TEXT("guid"), GuidToString(Actor->GetActorGuid()));
		JsonActor->SetStringField(TEXT("instance_guid"), GuidToString(Actor->GetActorInstanceGuid()));
		JsonActor->SetBoolField(TEXT("is_editor_only"), Actor->IsEditorOnly());
#else
		JsonActor->SetStringField(TEXT("label"), Actor->GetName());
		JsonActor->SetStringField(TEXT("folder_path"), FString());
		JsonActor->SetStringField(TEXT("guid"), FString());
		JsonActor->SetStringField(TEXT("instance_guid"), FString());
		JsonActor->SetBoolField(TEXT("is_editor_only"), false);
#endif
		JsonActor->SetObjectField(TEXT("transform"), ExportTransform(Actor->GetActorTransform()));
		JsonActor->SetObjectField(TEXT("bounds"), ExportBounds(Actor));
		JsonActor->SetArrayField(TEXT("tags"), ExportNameArray(Actor->Tags));
		JsonActor->SetArrayField(TEXT("layers"), ExportNameArray(Actor->Layers));

		TSharedPtr<FJsonObject> JsonFlags = MakeShared<FJsonObject>();
		JsonFlags->SetBoolField(TEXT("hidden"), Actor->IsHidden());
		JsonFlags->SetBoolField(TEXT("hidden_in_game"), Actor->IsHidden());
		JsonFlags->SetBoolField(TEXT("replicates"), Actor->GetIsReplicated());
		JsonFlags->SetBoolField(TEXT("always_relevant"), Actor->bAlwaysRelevant);
		JsonFlags->SetBoolField(TEXT("net_load_on_client"), Actor->bNetLoadOnClient);
		JsonFlags->SetBoolField(TEXT("can_be_damaged"), Actor->CanBeDamaged());
		JsonFlags->SetBoolField(TEXT("actor_tick_enabled"), Actor->PrimaryActorTick.IsTickFunctionEnabled());
		JsonActor->SetObjectField(TEXT("flags"), JsonFlags);

		JsonActor->SetObjectField(TEXT("instance_property_overrides"), ExportObjectProperties(Actor, Archetype, true));
		JsonActor->SetArrayField(TEXT("components"), ExportActorComponents(Actor));
		JsonActor->SetObjectField(TEXT("gameplay_classification"), ExportGameplayClassification(OutClassification));

		TArray<TSharedPtr<FJsonValue>> JsonReferences;
		for (int32 Index = ReferenceStartIndex; Index < OutReferenceEdges.Num(); ++Index)
		{
			if (OutReferenceEdges[Index].FromActorPath == ObjectPath(Actor))
			{
				JsonReferences.Add(MakeShared<FJsonValueObject>(ExportActorReferenceEdge(OutReferenceEdges[Index])));
			}
		}
		JsonActor->SetArrayField(TEXT("actor_references"), JsonReferences);

		return JsonActor;
	}

	void AddCompositionActor(TSharedPtr<FJsonObject> Composition, const FString& Category, const AActor* Actor, const FGameplayClassification& Classification)
	{
		if (!Composition.IsValid() || !Actor)
		{
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Actors;
		const TArray<TSharedPtr<FJsonValue>>* ExistingActors = nullptr;
		if (Composition->TryGetArrayField(FStringView(*Category), ExistingActors) && ExistingActors)
		{
			Actors = *ExistingActors;
		}
		TSharedPtr<FJsonObject> JsonActor = ExportActorReference(Actor);
		TArray<TSharedPtr<FJsonValue>> Categories;
		for (const FString& ActorCategory : Classification.Categories)
		{
			Categories.Add(MakeShared<FJsonValueString>(ActorCategory));
		}
		JsonActor->SetArrayField(TEXT("categories"), Categories);
		Actors.Add(MakeShared<FJsonValueObject>(JsonActor));
		Composition->SetArrayField(Category, Actors);
	}

	TSharedPtr<FJsonObject> MakeGameplayCompositionRoot()
	{
		TSharedPtr<FJsonObject> Composition = MakeShared<FJsonObject>();
		for (const TCHAR* Category : { TEXT("enemies"), TEXT("spawners"), TEXT("checkpoints"), TEXT("volumes"), TEXT("triggers"), TEXT("pickups"), TEXT("objectives"), TEXT("player_starts"), TEXT("characters"), TEXT("pawns") })
		{
			Composition->SetArrayField(Category, TArray<TSharedPtr<FJsonValue>>());
		}
		return Composition;
	}

	void AddActorToGameplayComposition(TSharedPtr<FJsonObject> Composition, const AActor* Actor, const FGameplayClassification& Classification)
	{
		for (const FString& Category : Classification.Categories)
		{
			if (Category == TEXT("enemy"))
			{
				AddCompositionActor(Composition, TEXT("enemies"), Actor, Classification);
			}
			else if (Category == TEXT("spawner"))
			{
				AddCompositionActor(Composition, TEXT("spawners"), Actor, Classification);
			}
			else if (Category == TEXT("checkpoint"))
			{
				AddCompositionActor(Composition, TEXT("checkpoints"), Actor, Classification);
			}
			else if (Category == TEXT("volume"))
			{
				AddCompositionActor(Composition, TEXT("volumes"), Actor, Classification);
			}
			else if (Category == TEXT("trigger"))
			{
				AddCompositionActor(Composition, TEXT("triggers"), Actor, Classification);
			}
			else if (Category == TEXT("pickup"))
			{
				AddCompositionActor(Composition, TEXT("pickups"), Actor, Classification);
			}
			else if (Category == TEXT("objective"))
			{
				AddCompositionActor(Composition, TEXT("objectives"), Actor, Classification);
			}
			else if (Category == TEXT("player_start"))
			{
				AddCompositionActor(Composition, TEXT("player_starts"), Actor, Classification);
			}
			else if (Category == TEXT("character"))
			{
				AddCompositionActor(Composition, TEXT("characters"), Actor, Classification);
			}
			else if (Category == TEXT("pawn"))
			{
				AddCompositionActor(Composition, TEXT("pawns"), Actor, Classification);
			}
		}
	}

	void AddGameplayCompositionSummary(TSharedPtr<FJsonObject> Composition)
	{
		if (!Composition.IsValid())
		{
			return;
		}

		TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
		for (const TCHAR* Category : { TEXT("enemies"), TEXT("spawners"), TEXT("checkpoints"), TEXT("volumes"), TEXT("triggers"), TEXT("pickups"), TEXT("objectives"), TEXT("player_starts"), TEXT("characters"), TEXT("pawns") })
		{
			TArray<TSharedPtr<FJsonValue>> Actors;
			const TArray<TSharedPtr<FJsonValue>>* ExistingActors = nullptr;
			if (Composition->TryGetArrayField(FStringView(Category), ExistingActors) && ExistingActors)
			{
				Actors = *ExistingActors;
			}
			Summary->SetNumberField(FString::Printf(TEXT("%s_count"), Category), Actors.Num());
		}

		Composition->SetObjectField(TEXT("summary"), Summary);
	}

	TSharedPtr<FJsonObject> ExportGameMode(const AWorldSettings* WorldSettings)
	{
		TSharedPtr<FJsonObject> JsonGameMode = MakeShared<FJsonObject>();
		FString GlobalDefaultGameMode;
		if (GConfig)
		{
			GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), GlobalDefaultGameMode, GGameIni);
		}

		const UClass* OverrideClass = WorldSettings ? WorldSettings->DefaultGameMode.Get() : nullptr;
		JsonGameMode->SetBoolField(TEXT("has_world_override"), OverrideClass != nullptr);
		JsonGameMode->SetStringField(TEXT("world_override_class"), ClassPath(OverrideClass));
		JsonGameMode->SetStringField(TEXT("project_global_default_game_mode"), GlobalDefaultGameMode);
		JsonGameMode->SetStringField(TEXT("effective_class"), OverrideClass ? ClassPath(OverrideClass) : GlobalDefaultGameMode);
		return JsonGameMode;
	}

	TSharedPtr<FJsonObject> ExportWorldSettings(const AWorldSettings* WorldSettings)
	{
		TSharedPtr<FJsonObject> JsonWorldSettings = MakeShared<FJsonObject>();
		if (!WorldSettings)
		{
			return JsonWorldSettings;
		}

		const UObject* ClassDefaultObject = WorldSettings->GetClass() ? WorldSettings->GetClass()->GetDefaultObject() : nullptr;
		JsonWorldSettings->SetStringField(TEXT("path"), ObjectPath(WorldSettings));
		JsonWorldSettings->SetObjectField(TEXT("class_info"), ExportClassInfo(WorldSettings->GetClass()));
		JsonWorldSettings->SetObjectField(TEXT("all_properties"), ExportObjectProperties(WorldSettings, nullptr, false));
		JsonWorldSettings->SetObjectField(TEXT("overrides_from_class_default"), ExportObjectProperties(WorldSettings, ClassDefaultObject, true));
		JsonWorldSettings->SetStringField(TEXT("game_mode_override"), ClassPath(WorldSettings->DefaultGameMode.Get()));
		JsonWorldSettings->SetNumberField(TEXT("kill_z"), WorldSettings->KillZ);
		JsonWorldSettings->SetNumberField(TEXT("world_to_meters"), WorldSettings->WorldToMeters);
		JsonWorldSettings->SetBoolField(TEXT("enable_world_bounds_checks"), WorldSettings->bEnableWorldBoundsChecks != 0);
		JsonWorldSettings->SetBoolField(TEXT("force_no_precomputed_lighting"), WorldSettings->bForceNoPrecomputedLighting != 0);
		JsonWorldSettings->SetBoolField(TEXT("enable_world_composition"), WorldSettings->bEnableWorldComposition != 0);
		return JsonWorldSettings;
	}

	TSharedPtr<FJsonObject> ExportLevel(const ULevel* Level, const UWorld* World)
	{
		TSharedPtr<FJsonObject> JsonLevel = MakeShared<FJsonObject>();
		if (!Level)
		{
			return JsonLevel;
		}

		JsonLevel->SetStringField(TEXT("path"), ObjectPath(Level));
		JsonLevel->SetStringField(TEXT("name"), Level->GetName());
		JsonLevel->SetStringField(TEXT("package"), PackagePath(Level));
		JsonLevel->SetBoolField(TEXT("is_persistent_level"), World && World->PersistentLevel == Level);
		JsonLevel->SetNumberField(TEXT("loaded_actor_count"), Level->Actors.Num());
		JsonLevel->SetStringField(TEXT("world_settings"), ObjectPath(Level->GetWorldSettings(false)));
		JsonLevel->SetStringField(TEXT("level_script_actor"), ObjectPath(Level->GetLevelScriptActor()));
		JsonLevel->SetStringField(TEXT("level_script_actor_class"), ClassPath(Level->GetLevelScriptActor()));

#if WITH_EDITOR
		JsonLevel->SetBoolField(TEXT("is_using_external_actors"), Level->IsUsingExternalActors());
		JsonLevel->SetBoolField(TEXT("is_using_external_objects"), Level->IsUsingExternalObjects());
		JsonLevel->SetBoolField(TEXT("is_partitioned"), ULevel::GetIsLevelPartitionedFromPackage(*PackagePath(Level)));
		JsonLevel->SetBoolField(TEXT("is_using_actor_descs"), ULevel::GetIsLevelUsingActorsDescsFromPackage(*PackagePath(Level)));
		JsonLevel->SetNumberField(TEXT("on_disk_external_actor_package_count"), Level->GetOnDiskExternalActorPackages().Num());
#endif

		return JsonLevel;
	}

	TSharedPtr<FJsonObject> ExportExternalActors(const UWorld* World, const TArray<const AActor*>& LoadedActors, const TArray<FActorReferenceEdge>& ReferenceEdges)
	{
		TSharedPtr<FJsonObject> JsonExternalActors = MakeShared<FJsonObject>();
		const ULevel* PersistentLevel = World ? World->PersistentLevel : nullptr;
		const FString LevelPackageName = PackagePath(PersistentLevel);

#if WITH_EDITOR
		const bool bUsesExternalActors = PersistentLevel && PersistentLevel->IsUsingExternalActors();
		JsonExternalActors->SetBoolField(TEXT("uses_external_actors"), bUsesExternalActors);
		JsonExternalActors->SetBoolField(TEXT("uses_actor_descs"), !LevelPackageName.IsEmpty() && ULevel::GetIsLevelUsingActorsDescsFromPackage(*LevelPackageName));
		JsonExternalActors->SetBoolField(TEXT("is_partitioned"), !LevelPackageName.IsEmpty() && ULevel::GetIsLevelPartitionedFromPackage(*LevelPackageName));

		TArray<FGuid> ExternalActorGuids;
		if (!LevelPackageName.IsEmpty())
		{
			ULevel::GetWorldExternalActorsReferencesFromPackage(*LevelPackageName, ExternalActorGuids);
		}

		TArray<TSharedPtr<FJsonValue>> JsonGuids;
		for (const FGuid& Guid : ExternalActorGuids)
		{
			JsonGuids.Add(MakeShared<FJsonValueString>(GuidToString(Guid)));
		}
		JsonExternalActors->SetArrayField(TEXT("world_external_actor_references"), JsonGuids);

		TArray<TSharedPtr<FJsonValue>> JsonDiskPackages;
		if (PersistentLevel)
		{
			for (const FString& PackageName : PersistentLevel->GetOnDiskExternalActorPackages())
			{
				JsonDiskPackages.Add(MakeShared<FJsonValueString>(PackageName));
			}
		}
		JsonExternalActors->SetArrayField(TEXT("on_disk_packages"), JsonDiskPackages);

		TArray<TSharedPtr<FJsonValue>> JsonLoadedExternalPackages;
		if (PersistentLevel)
		{
			for (const UPackage* Package : PersistentLevel->GetLoadedExternalObjectPackages())
			{
				JsonLoadedExternalPackages.Add(MakeShared<FJsonValueString>(Package ? Package->GetName() : FString()));
			}
		}
		JsonExternalActors->SetArrayField(TEXT("loaded_external_object_packages"), JsonLoadedExternalPackages);
#else
		JsonExternalActors->SetBoolField(TEXT("uses_external_actors"), false);
		JsonExternalActors->SetBoolField(TEXT("uses_actor_descs"), false);
		JsonExternalActors->SetBoolField(TEXT("is_partitioned"), false);
		JsonExternalActors->SetArrayField(TEXT("world_external_actor_references"), TArray<TSharedPtr<FJsonValue>>());
		JsonExternalActors->SetArrayField(TEXT("on_disk_packages"), TArray<TSharedPtr<FJsonValue>>());
		JsonExternalActors->SetArrayField(TEXT("loaded_external_object_packages"), TArray<TSharedPtr<FJsonValue>>());
#endif

		TArray<TSharedPtr<FJsonValue>> JsonLoadedActors;
		for (const AActor* Actor : LoadedActors)
		{
			if (Actor && Actor->IsPackageExternal())
			{
				JsonLoadedActors.Add(MakeShared<FJsonValueObject>(ExportActorReference(Actor)));
			}
		}
		JsonExternalActors->SetArrayField(TEXT("loaded_external_actors"), JsonLoadedActors);
		JsonExternalActors->SetArrayField(TEXT("reference_edges"), ExportActorReferenceEdges(ReferenceEdges));
		return JsonExternalActors;
	}
}

FString AssetToJson::ReadWorldAsJson(
	UWorld* World,
	const FString& ObjectPathString,
	const FString& OutputFilePath,
	bool bPrettyPrint,
	bool bReturnJson,
	bool bIncludeNodeProperties)
{
	namespace WorldExport = AssetToJsonWorldPrivate;

	if (!World)
	{
		return WorldExport::SerializeJson(WorldExport::MakeErrorJson(FString::Printf(TEXT("World could not be loaded: %s"), *ObjectPathString)).ToSharedRef(), bPrettyPrint);
	}

	TArray<TSharedPtr<FJsonValue>> Warnings;
	Warnings.Add(MakeShared<FJsonValueString>(TEXT("World actor exports reflect actors loaded in editor memory. World Partition or External Actor maps may also expose unloaded actor GUID/package metadata under external_actors.")));
	if (bIncludeNodeProperties)
	{
		Warnings.Add(MakeShared<FJsonValueString>(TEXT("bIncludeNodeProperties is currently ignored by the world exporter; level blueprint events are exported as event summaries.")));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("ue.asset_to_json.world.v1"));
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetObjectField(TEXT("exporter"), WorldExport::MakeExporterJson());
	Root->SetArrayField(TEXT("warnings"), Warnings);

	TSharedPtr<FJsonObject> JsonAsset = MakeShared<FJsonObject>();
	JsonAsset->SetStringField(TEXT("path"), FPackageName::ObjectPathToPackageName(ObjectPathString));
	JsonAsset->SetStringField(TEXT("object_path"), ObjectPathString);
	JsonAsset->SetStringField(TEXT("name"), World->GetName());
	JsonAsset->SetStringField(TEXT("class"), WorldExport::ClassPath(World));
	Root->SetObjectField(TEXT("asset"), JsonAsset);

	TSharedPtr<FJsonObject> JsonWorld = MakeShared<FJsonObject>();
	JsonWorld->SetStringField(TEXT("name"), World->GetName());
	JsonWorld->SetStringField(TEXT("path"), WorldExport::ObjectPath(World));
	JsonWorld->SetStringField(TEXT("package"), WorldExport::PackagePath(World));
	JsonWorld->SetStringField(TEXT("world_type"), WorldExport::WorldTypeToString(World->WorldType));
	JsonWorld->SetStringField(TEXT("persistent_level"), WorldExport::ObjectPath(World->PersistentLevel));
	JsonWorld->SetNumberField(TEXT("streaming_level_count"), World->GetStreamingLevels().Num());
	Root->SetObjectField(TEXT("world"), JsonWorld);

	const AWorldSettings* WorldSettings = World->PersistentLevel ? World->PersistentLevel->GetWorldSettings(false) : nullptr;
	Root->SetObjectField(TEXT("game_mode"), WorldExport::ExportGameMode(WorldSettings));
	Root->SetObjectField(TEXT("world_settings"), WorldExport::ExportWorldSettings(WorldSettings));
	Root->SetObjectField(TEXT("level_blueprint"), WorldExport::ExportLevelBlueprint(World->PersistentLevel));

	TArray<TSharedPtr<FJsonValue>> JsonLevels;
	for (const ULevel* Level : World->GetLevels())
	{
		JsonLevels.Add(MakeShared<FJsonValueObject>(WorldExport::ExportLevel(Level, World)));
	}
	Root->SetArrayField(TEXT("levels"), JsonLevels);

	TArray<TSharedPtr<FJsonValue>> JsonActors;
	TArray<const AActor*> LoadedActors;
	TArray<WorldExport::FActorReferenceEdge> ReferenceEdges;
	TSet<FString> SeenReferenceEdges;
	TSharedPtr<FJsonObject> GameplayComposition = WorldExport::MakeGameplayCompositionRoot();

	for (const ULevel* Level : World->GetLevels())
	{
		if (!Level)
		{
			continue;
		}

		for (const AActor* Actor : Level->Actors)
		{
			if (!Actor)
			{
				continue;
			}

			LoadedActors.Add(Actor);
			WorldExport::FGameplayClassification Classification;
			JsonActors.Add(MakeShared<FJsonValueObject>(WorldExport::ExportActor(Actor, ReferenceEdges, SeenReferenceEdges, Classification)));
			WorldExport::AddActorToGameplayComposition(GameplayComposition, Actor, Classification);
		}
	}

	Root->SetArrayField(TEXT("actors"), JsonActors);
	WorldExport::AddGameplayCompositionSummary(GameplayComposition);
	Root->SetObjectField(TEXT("gameplay_composition"), GameplayComposition);
	Root->SetObjectField(TEXT("external_actors"), WorldExport::ExportExternalActors(World, LoadedActors, ReferenceEdges));

	TSharedPtr<FJsonObject> Coverage = MakeShared<FJsonObject>();
	Coverage->SetNumberField(TEXT("loaded_level_count"), JsonLevels.Num());
	Coverage->SetNumberField(TEXT("loaded_actor_count"), JsonActors.Num());
	Coverage->SetNumberField(TEXT("actor_reference_edge_count"), ReferenceEdges.Num());
#if WITH_EDITOR
	const ULevel* PersistentLevel = World->PersistentLevel;
	Coverage->SetBoolField(TEXT("uses_external_actors"), PersistentLevel && PersistentLevel->IsUsingExternalActors());
	Coverage->SetBoolField(TEXT("uses_actor_descs"), PersistentLevel && ULevel::GetIsLevelUsingActorsDescsFromPackage(*WorldExport::PackagePath(PersistentLevel)));
	Coverage->SetBoolField(TEXT("is_partitioned"), PersistentLevel && ULevel::GetIsLevelPartitionedFromPackage(*WorldExport::PackagePath(PersistentLevel)));
#else
	Coverage->SetBoolField(TEXT("uses_external_actors"), false);
	Coverage->SetBoolField(TEXT("uses_actor_descs"), false);
	Coverage->SetBoolField(TEXT("is_partitioned"), false);
#endif
	Root->SetObjectField(TEXT("coverage"), Coverage);

	const FString JsonString = WorldExport::SerializeJson(Root.ToSharedRef(), bPrettyPrint);
	if (!WorldExport::SaveJsonStringToFile(JsonString, OutputFilePath))
	{
		return WorldExport::SerializeJson(WorldExport::MakeErrorJson(FString::Printf(TEXT("Failed to write JSON file: %s"), *OutputFilePath)).ToSharedRef(), bPrettyPrint);
	}

	return bReturnJson ? JsonString : FString();
}
