#include "AssetToJsonEnhancedInputExport.h"

#include "InputCoreTypes.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonTypes.h"
#include "UObject/UnrealType.h"

namespace AssetToJsonEnhancedInputPrivate
{
	FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	FString ClassPath(const UObject* Object)
	{
		return Object && Object->GetClass() ? Object->GetClass()->GetPathName() : FString();
	}

	bool IsClassOrChildOfPath(const UClass* Class, const TCHAR* ExpectedClassPath)
	{
		for (const UClass* CurrentClass = Class; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
		{
			if (CurrentClass->GetPathName() == ExpectedClassPath)
			{
				return true;
			}
		}

		return false;
	}

	const FProperty* FindProperty(const UStruct* Struct, FName PropertyName)
	{
		if (!Struct)
		{
			return nullptr;
		}

		for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (It->GetFName() == PropertyName)
			{
				return *It;
			}
		}

		return nullptr;
	}

	FString ExportPropertyText(const FProperty* Property, const void* Container)
	{
		if (!Property || !Container)
		{
			return FString();
		}

		FString ExportedValue;
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
		Property->ExportText_Direct(ExportedValue, ValuePtr, nullptr, nullptr, PPF_None);
		return ExportedValue;
	}

	void SetStringFieldFromProperty(TSharedRef<FJsonObject> JsonObject, const void* Container, const UStruct* Struct, const TCHAR* FieldName, FName PropertyName)
	{
		if (const FProperty* Property = FindProperty(Struct, PropertyName))
		{
			JsonObject->SetStringField(FieldName, ExportPropertyText(Property, Container));
		}
	}

	void SetBoolFieldFromProperty(TSharedRef<FJsonObject> JsonObject, const void* Container, const UStruct* Struct, const TCHAR* FieldName, FName PropertyName)
	{
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(FindProperty(Struct, PropertyName)))
		{
			const void* ValuePtr = BoolProperty->ContainerPtrToValuePtr<void>(Container);
			JsonObject->SetBoolField(FieldName, BoolProperty->GetPropertyValue(ValuePtr));
		}
	}

	void SetNumberFieldFromProperty(TSharedRef<FJsonObject> JsonObject, const void* Container, const UStruct* Struct, const TCHAR* FieldName, FName PropertyName)
	{
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(FindProperty(Struct, PropertyName)))
		{
			const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Container);
			const double Value = NumericProperty->IsFloatingPoint() ? NumericProperty->GetFloatingPointPropertyValue(ValuePtr) : static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
			JsonObject->SetNumberField(FieldName, Value);
		}
	}

	const UObject* GetObjectPropertyValue(const void* Container, const UStruct* Struct, FName PropertyName)
	{
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(FindProperty(Struct, PropertyName)))
		{
			const void* ValuePtr = ObjectProperty->ContainerPtrToValuePtr<void>(Container);
			return ObjectProperty->GetObjectPropertyValue(ValuePtr);
		}

		return nullptr;
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

	TSharedPtr<FJsonObject> ExportObjectDefaults(const UObject* Object)
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		if (!Object)
		{
			return JsonObject;
		}

		JsonObject->SetStringField(TEXT("object"), ObjectPath(Object));
		JsonObject->SetStringField(TEXT("class"), ClassPath(Object));

		TArray<TSharedPtr<FJsonValue>> Properties;
		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!ShouldExportDetailsProperty(Property))
			{
				continue;
			}

			TSharedPtr<FJsonObject> JsonProperty = MakeShared<FJsonObject>();
			JsonProperty->SetStringField(TEXT("name"), Property->GetName());
			JsonProperty->SetStringField(TEXT("category"), Property->GetMetaData(TEXT("Category")));
			JsonProperty->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
			JsonProperty->SetStringField(TEXT("owner_class"), ObjectPath(Property->GetOwnerClass()));
			JsonProperty->SetStringField(TEXT("value"), ExportPropertyText(Property, Object));
			Properties.Add(MakeShared<FJsonValueObject>(JsonProperty));
		}

		JsonObject->SetArrayField(TEXT("properties"), Properties);
		return JsonObject;
	}

	TArray<TSharedPtr<FJsonValue>> ExportInstancedObjects(const FArrayProperty* ArrayProperty, const void* Container)
	{
		TArray<TSharedPtr<FJsonValue>> JsonObjects;
		const FObjectPropertyBase* ObjectProperty = ArrayProperty ? CastField<FObjectPropertyBase>(ArrayProperty->Inner) : nullptr;
		if (!ArrayProperty || !ObjectProperty || !Container)
		{
			return JsonObjects;
		}

		const void* ArrayPtr = ArrayProperty->ContainerPtrToValuePtr<void>(Container);
		FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayPtr);
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			const UObject* Object = ObjectProperty->GetObjectPropertyValue(ArrayHelper.GetRawPtr(Index));
			TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
			JsonObject->SetNumberField(TEXT("index"), Index);
			JsonObject->SetStringField(TEXT("path"), ObjectPath(Object));
			JsonObject->SetStringField(TEXT("class"), ClassPath(Object));
			JsonObject->SetObjectField(TEXT("defaults"), ExportObjectDefaults(Object));
			JsonObjects.Add(MakeShared<FJsonValueObject>(JsonObject));
		}

		return JsonObjects;
	}

	TArray<TSharedPtr<FJsonValue>> ExportInstancedObjectsFromProperty(const void* Container, const UStruct* Struct, FName PropertyName)
	{
		return ExportInstancedObjects(CastField<FArrayProperty>(FindProperty(Struct, PropertyName)), Container);
	}

	TSharedPtr<FJsonObject> ExportAssetSummary(const UObject* Asset, const FString& ObjectPathString)
	{
		TSharedPtr<FJsonObject> JsonAsset = MakeShared<FJsonObject>();
		JsonAsset->SetStringField(TEXT("path"), FPackageName::ObjectPathToPackageName(ObjectPathString));
		JsonAsset->SetStringField(TEXT("object_path"), ObjectPathString);
		JsonAsset->SetStringField(TEXT("name"), Asset ? Asset->GetName() : FPackageName::ObjectPathToObjectName(ObjectPathString));
		JsonAsset->SetStringField(TEXT("class"), ClassPath(Asset));
		return JsonAsset;
	}

	TSharedPtr<FJsonObject> ExportInputAction(const UObject* InputAction)
	{
		TSharedPtr<FJsonObject> JsonAction = MakeShared<FJsonObject>();
		if (!InputAction)
		{
			return JsonAction;
		}

		const UClass* InputActionClass = InputAction->GetClass();
		SetStringFieldFromProperty(JsonAction.ToSharedRef(), InputAction, InputActionClass, TEXT("value_type"), TEXT("ValueType"));
		SetStringFieldFromProperty(JsonAction.ToSharedRef(), InputAction, InputActionClass, TEXT("action_description"), TEXT("ActionDescription"));
		SetStringFieldFromProperty(JsonAction.ToSharedRef(), InputAction, InputActionClass, TEXT("accumulation_behavior"), TEXT("AccumulationBehavior"));
		SetBoolFieldFromProperty(JsonAction.ToSharedRef(), InputAction, InputActionClass, TEXT("trigger_when_paused"), TEXT("bTriggerWhenPaused"));
		SetBoolFieldFromProperty(JsonAction.ToSharedRef(), InputAction, InputActionClass, TEXT("consume_input"), TEXT("bConsumeInput"));
		SetBoolFieldFromProperty(JsonAction.ToSharedRef(), InputAction, InputActionClass, TEXT("consumes_action_and_axis_mappings"), TEXT("bConsumesActionAndAxisMappings"));
		SetBoolFieldFromProperty(JsonAction.ToSharedRef(), InputAction, InputActionClass, TEXT("reserve_all_mappings"), TEXT("bReserveAllMappings"));
		SetNumberFieldFromProperty(JsonAction.ToSharedRef(), InputAction, InputActionClass, TEXT("trigger_events_that_consume_legacy_keys"), TEXT("TriggerEventsThatConsumeLegacyKeys"));
		JsonAction->SetArrayField(TEXT("triggers"), ExportInstancedObjectsFromProperty(InputAction, InputActionClass, TEXT("Triggers")));
		JsonAction->SetArrayField(TEXT("modifiers"), ExportInstancedObjectsFromProperty(InputAction, InputActionClass, TEXT("Modifiers")));
		JsonAction->SetObjectField(TEXT("player_mappable_settings"), ExportObjectDefaults(GetObjectPropertyValue(InputAction, InputActionClass, TEXT("PlayerMappableKeySettings"))));
		JsonAction->SetObjectField(TEXT("defaults"), ExportObjectDefaults(InputAction));
		return JsonAction;
	}

	TSharedPtr<FJsonObject> ExportEnhancedActionKeyMapping(const void* Mapping, const UStruct* MappingStruct, int32 Index, const FString& ProfileId = FString())
	{
		TSharedPtr<FJsonObject> JsonMapping = MakeShared<FJsonObject>();
		JsonMapping->SetNumberField(TEXT("index"), Index);
		if (!ProfileId.IsEmpty())
		{
			JsonMapping->SetStringField(TEXT("profile_id"), ProfileId);
		}

		const UObject* Action = GetObjectPropertyValue(Mapping, MappingStruct, TEXT("Action"));
		JsonMapping->SetStringField(TEXT("action_path"), ObjectPath(Action));
		JsonMapping->SetStringField(TEXT("action_name"), Action ? Action->GetName() : FString());

		if (const FStructProperty* KeyProperty = CastField<FStructProperty>(FindProperty(MappingStruct, TEXT("Key"))))
		{
			const void* KeyPtr = KeyProperty->ContainerPtrToValuePtr<void>(Mapping);
			if (KeyProperty->Struct == TBaseStructure<FKey>::Get())
			{
				const FKey& Key = *static_cast<const FKey*>(KeyPtr);
				JsonMapping->SetStringField(TEXT("key_name"), Key.GetFName().ToString());
				JsonMapping->SetStringField(TEXT("key_display_name"), Key.GetDisplayName().ToString());
			}
			else
			{
				JsonMapping->SetStringField(TEXT("key_name"), ExportPropertyText(KeyProperty, Mapping));
			}
		}

		SetStringFieldFromProperty(JsonMapping.ToSharedRef(), Mapping, MappingStruct, TEXT("mapping_name"), TEXT("MappingName"));
		SetStringFieldFromProperty(JsonMapping.ToSharedRef(), Mapping, MappingStruct, TEXT("display_name"), TEXT("DisplayName"));
		SetStringFieldFromProperty(JsonMapping.ToSharedRef(), Mapping, MappingStruct, TEXT("display_category"), TEXT("DisplayCategory"));
		SetBoolFieldFromProperty(JsonMapping.ToSharedRef(), Mapping, MappingStruct, TEXT("is_player_mappable"), TEXT("bIsPlayerMappable"));
		JsonMapping->SetArrayField(TEXT("triggers"), ExportInstancedObjectsFromProperty(Mapping, MappingStruct, TEXT("Triggers")));
		JsonMapping->SetArrayField(TEXT("modifiers"), ExportInstancedObjectsFromProperty(Mapping, MappingStruct, TEXT("Modifiers")));
		JsonMapping->SetStringField(TEXT("player_mappable_settings"), ExportPropertyText(FindProperty(MappingStruct, TEXT("PlayerMappableOptions")), Mapping));
		return JsonMapping;
	}

	TArray<TSharedPtr<FJsonValue>> ExportMappings(const void* Container, const UStruct* Struct, FName PropertyName, const FString& ProfileId = FString())
	{
		TArray<TSharedPtr<FJsonValue>> JsonMappings;
		const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(FindProperty(Struct, PropertyName));
		const FStructProperty* StructProperty = ArrayProperty ? CastField<FStructProperty>(ArrayProperty->Inner) : nullptr;
		if (!ArrayProperty || !StructProperty || !Container)
		{
			return JsonMappings;
		}

		const void* ArrayPtr = ArrayProperty->ContainerPtrToValuePtr<void>(Container);
		FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayPtr);
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			JsonMappings.Add(MakeShared<FJsonValueObject>(ExportEnhancedActionKeyMapping(ArrayHelper.GetRawPtr(Index), StructProperty->Struct, Index, ProfileId)));
		}

		return JsonMappings;
	}

	TSharedPtr<FJsonObject> ExportInputMappingContext(const UObject* MappingContext, TArray<TSharedPtr<FJsonValue>>& OutWarnings)
	{
		TSharedPtr<FJsonObject> JsonContext = MakeShared<FJsonObject>();
		if (!MappingContext)
		{
			return JsonContext;
		}

		const UClass* MappingContextClass = MappingContext->GetClass();
		SetStringFieldFromProperty(JsonContext.ToSharedRef(), MappingContext, MappingContextClass, TEXT("context_description"), TEXT("ContextDescription"));
		SetStringFieldFromProperty(JsonContext.ToSharedRef(), MappingContext, MappingContextClass, TEXT("registration_tracking_mode"), TEXT("RegistrationTrackingMode"));
		SetBoolFieldFromProperty(JsonContext.ToSharedRef(), MappingContext, MappingContextClass, TEXT("filters_by_input_mode"), TEXT("bShouldFilterMappingByInputMode"));
		JsonContext->SetArrayField(TEXT("mappings"), ExportMappings(MappingContext, MappingContextClass, TEXT("Mappings")));
		JsonContext->SetObjectField(TEXT("defaults"), ExportObjectDefaults(MappingContext));

		JsonContext->SetArrayField(TEXT("mapping_profiles"), TArray<TSharedPtr<FJsonValue>>());

		OutWarnings.Add(MakeShared<FJsonValueString>(TEXT("Input mapping context priority is runtime caller state and is not stored on the UInputMappingContext asset.")));
		OutWarnings.Add(MakeShared<FJsonValueString>(TEXT("EnhancedInput assets are exported through reflection so AssetToJson does not require an EnhancedInput build dependency.")));
		return JsonContext;
	}
}

namespace AssetToJson
{
	bool IsEnhancedInputAsset(const UObject* Asset)
	{
		return Asset && (AssetToJsonEnhancedInputPrivate::IsClassOrChildOfPath(Asset->GetClass(), TEXT("/Script/EnhancedInput.InputAction")) || AssetToJsonEnhancedInputPrivate::IsClassOrChildOfPath(Asset->GetClass(), TEXT("/Script/EnhancedInput.InputMappingContext")));
	}

	TSharedPtr<FJsonObject> ExportEnhancedInputAsset(
		const UObject* Asset,
		const FString& ObjectPathString,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("ue.asset_to_json.enhanced_input.v1"));
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetObjectField(TEXT("asset"), AssetToJsonEnhancedInputPrivate::ExportAssetSummary(Asset, ObjectPathString));

		TSharedPtr<FJsonObject> JsonExporter = MakeShared<FJsonObject>();
		JsonExporter->SetStringField(TEXT("type"), TEXT("enhanced_input"));
		JsonExporter->SetStringField(TEXT("write_support"), TEXT("separate_patch_schema"));
		JsonExporter->SetBoolField(TEXT("supports_round_trip"), false);
		Root->SetObjectField(TEXT("exporter"), JsonExporter);

		if (AssetToJsonEnhancedInputPrivate::IsClassOrChildOfPath(Asset->GetClass(), TEXT("/Script/EnhancedInput.InputAction")))
		{
			Root->SetStringField(TEXT("asset_type"), TEXT("input_action"));
			Root->SetObjectField(TEXT("input_action"), AssetToJsonEnhancedInputPrivate::ExportInputAction(Asset));
		}
		else if (AssetToJsonEnhancedInputPrivate::IsClassOrChildOfPath(Asset->GetClass(), TEXT("/Script/EnhancedInput.InputMappingContext")))
		{
			Root->SetStringField(TEXT("asset_type"), TEXT("input_mapping_context"));
			Root->SetObjectField(TEXT("input_mapping_context"), AssetToJsonEnhancedInputPrivate::ExportInputMappingContext(Asset, OutWarnings));
		}

		Root->SetArrayField(TEXT("warnings"), OutWarnings);
		return Root;
	}
}
