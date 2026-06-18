#include "AssetToJsonTableExport.h"

#include "Engine/DataTable.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonTypes.h"
#include "UObject/UnrealType.h"

namespace AssetToJsonTablePrivate
{
	constexpr int32 MaxExportDepth = 6;
	constexpr int32 MaxContainerElements = 256;

	struct FExportContext
	{
		const UObject* RootAsset = nullptr;
		bool bExpandNestedObjects = false;
		TSet<const UObject*> VisitedObjects;
	};

	FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	FString ClassPath(const UObject* Object)
	{
		return Object && Object->GetClass() ? Object->GetClass()->GetPathName() : FString();
	}

	bool IsClassOrChildOfPathOrName(const UClass* Class, const TCHAR* ExpectedClassPath, const TCHAR* ExpectedClassName)
	{
		for (const UClass* CurrentClass = Class; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
		{
			if (CurrentClass->GetPathName() == ExpectedClassPath || CurrentClass->GetName() == ExpectedClassName)
			{
				return true;
			}
		}

		return false;
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

	TSharedPtr<FJsonObject> MakeExporterJson(const FString& Type, const FString& WriteSupport)
	{
		TSharedPtr<FJsonObject> JsonExporter = MakeShared<FJsonObject>();
		JsonExporter->SetStringField(TEXT("type"), Type);
		JsonExporter->SetStringField(TEXT("write_support"), WriteSupport);
		JsonExporter->SetBoolField(TEXT("supports_round_trip"), false);
		return JsonExporter;
	}

	FString ExportPropertyText(const FProperty* Property, const void* ValuePtr)
	{
		if (!Property || !ValuePtr)
		{
			return FString();
		}

		FString ExportedValue;
		Property->ExportText_Direct(ExportedValue, ValuePtr, nullptr, nullptr, PPF_None);
		return ExportedValue;
	}

	bool IsTransientProperty(const FProperty* Property)
	{
		return !Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_TextExportTransient);
	}

	bool ShouldExportDataTableProperty(const FProperty* Property)
	{
		return Property && !IsTransientProperty(Property);
	}

	bool IsKnownChooserPropertyName(const FString& PropertyName)
	{
		return PropertyName.Contains(TEXT("Column")) || PropertyName.Contains(TEXT("Row")) || PropertyName.Contains(TEXT("Result")) || PropertyName.Contains(TEXT("Context")) || PropertyName.Contains(TEXT("Output"));
	}

	bool ShouldExportChooserProperty(const FProperty* Property)
	{
		if (!Property || IsTransientProperty(Property))
		{
			return false;
		}

		return Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || IsKnownChooserPropertyName(Property->GetName());
	}

	TSharedPtr<FJsonObject> ExportPropertyType(const FProperty* Property)
	{
		TSharedPtr<FJsonObject> JsonType = MakeShared<FJsonObject>();
		if (!Property)
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("unknown"));
			return JsonType;
		}

		JsonType->SetStringField(TEXT("cpp_type"), Property->GetCPPType());

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("enum"));
			JsonType->SetStringField(TEXT("enum"), EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetPathName() : FString());
		}
		else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty && ByteProperty->Enum)
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("enum"));
			JsonType->SetStringField(TEXT("enum"), ByteProperty->Enum->GetPathName());
		}
		else if (CastField<FBoolProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("bool"));
		}
		else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), NumericProperty->IsFloatingPoint() ? TEXT("float") : TEXT("integer"));
		}
		else if (CastField<FNameProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("name"));
		}
		else if (CastField<FStrProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("string"));
		}
		else if (CastField<FTextProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("text"));
		}
		else if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("class"));
			JsonType->SetStringField(TEXT("meta_class"), ObjectPath(ClassProperty->MetaClass));
		}
		else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("object"));
			JsonType->SetStringField(TEXT("property_class"), ObjectPath(ObjectProperty->PropertyClass));
		}
		else if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("soft_class"));
			JsonType->SetStringField(TEXT("meta_class"), ObjectPath(SoftClassProperty->MetaClass));
		}
		else if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("soft_object"));
			JsonType->SetStringField(TEXT("property_class"), ObjectPath(SoftObjectProperty->PropertyClass));
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("struct"));
			JsonType->SetStringField(TEXT("struct"), StructProperty->Struct ? StructProperty->Struct->GetPathName() : FString());
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("array"));
			JsonType->SetObjectField(TEXT("inner_type"), ExportPropertyType(ArrayProperty->Inner));
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("map"));
			JsonType->SetObjectField(TEXT("key_type"), ExportPropertyType(MapProperty->KeyProp));
			JsonType->SetObjectField(TEXT("value_type"), ExportPropertyType(MapProperty->ValueProp));
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("set"));
			JsonType->SetObjectField(TEXT("element_type"), ExportPropertyType(SetProperty->ElementProp));
		}
		else
		{
			JsonType->SetStringField(TEXT("kind"), TEXT("text_export_only"));
		}

		return JsonType;
	}

	TSharedPtr<FJsonObject> ExportPropertyMetadata(const FProperty* Property)
	{
		TSharedPtr<FJsonObject> JsonProperty = MakeShared<FJsonObject>();
		if (!Property)
		{
			return JsonProperty;
		}

		JsonProperty->SetStringField(TEXT("name"), Property->GetName());
		JsonProperty->SetStringField(TEXT("display_name"), Property->GetDisplayNameText().ToString());
		JsonProperty->SetStringField(TEXT("category"), Property->GetMetaData(TEXT("Category")));
		JsonProperty->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
		JsonProperty->SetStringField(TEXT("owner_class"), ObjectPath(Property->GetOwnerClass()));
		JsonProperty->SetStringField(TEXT("full_name"), Property->GetFullName());
		JsonProperty->SetStringField(TEXT("property_flags"), FString::Printf(TEXT("%llu"), static_cast<uint64>(Property->PropertyFlags)));
		JsonProperty->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit));
		JsonProperty->SetBoolField(TEXT("blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
		JsonProperty->SetBoolField(TEXT("transient"), IsTransientProperty(Property));
		JsonProperty->SetObjectField(TEXT("type"), ExportPropertyType(Property));
		return JsonProperty;
	}

	TArray<TSharedPtr<FJsonValue>> ExportObjectProperties(const UObject* Object, FExportContext& Context, int32 Depth);
	TSharedPtr<FJsonValue> ExportStructuredPropertyValue(const FProperty* Property, const void* ValuePtr, FExportContext& Context, int32 Depth);

	TSharedPtr<FJsonValue> ExportObjectReferenceValue(const UObject* Object, FExportContext& Context, int32 Depth)
	{
		if (!Object)
		{
			return MakeShared<FJsonValueNull>();
		}

		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		JsonObject->SetStringField(TEXT("object_path"), ObjectPath(Object));
		JsonObject->SetStringField(TEXT("name"), Object->GetName());
		JsonObject->SetStringField(TEXT("class"), ClassPath(Object));

		const bool bCanExpand = Context.bExpandNestedObjects && Depth < MaxExportDepth && Object != Context.RootAsset && !Object->IsAsset();
		if (bCanExpand && !Context.VisitedObjects.Contains(Object))
		{
			Context.VisitedObjects.Add(Object);
			JsonObject->SetArrayField(TEXT("properties"), ExportObjectProperties(Object, Context, Depth + 1));
		}

		return MakeShared<FJsonValueObject>(JsonObject);
	}

	TSharedPtr<FJsonValue> ExportStructuredPropertyValue(const FProperty* Property, const void* ValuePtr, FExportContext& Context, int32 Depth)
	{
		if (!Property || !ValuePtr)
		{
			return MakeShared<FJsonValueNull>();
		}

		if (Depth >= MaxExportDepth)
		{
			return MakeShared<FJsonValueString>(ExportPropertyText(Property, ValuePtr));
		}

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const int64 RawValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			TSharedPtr<FJsonObject> JsonEnum = MakeShared<FJsonObject>();
			JsonEnum->SetStringField(TEXT("name"), EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetNameStringByValue(RawValue) : FString());
			JsonEnum->SetNumberField(TEXT("value"), static_cast<double>(RawValue));
			return MakeShared<FJsonValueObject>(JsonEnum);
		}

		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty && ByteProperty->Enum)
		{
			const uint8 RawValue = ByteProperty->GetPropertyValue(ValuePtr);
			TSharedPtr<FJsonObject> JsonEnum = MakeShared<FJsonObject>();
			JsonEnum->SetStringField(TEXT("name"), ByteProperty->Enum->GetNameStringByValue(RawValue));
			JsonEnum->SetNumberField(TEXT("value"), RawValue);
			return MakeShared<FJsonValueObject>(JsonEnum);
		}

		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(ValuePtr));
		}

		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			const double NumberValue = NumericProperty->IsFloatingPoint() ? NumericProperty->GetFloatingPointPropertyValue(ValuePtr) : static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
			return MakeShared<FJsonValueNumber>(NumberValue);
		}

		if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			return MakeShared<FJsonValueString>(NameProperty->GetPropertyValue(ValuePtr).ToString());
		}

		if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			return MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(ValuePtr));
		}

		if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(ValuePtr).ToString());
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			return ExportObjectReferenceValue(ObjectProperty->GetObjectPropertyValue(ValuePtr), Context, Depth);
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			TSharedPtr<FJsonObject> JsonSoftObject = MakeShared<FJsonObject>();
			JsonSoftObject->SetStringField(TEXT("object_path"), SoftObjectProperty->GetPropertyValue(ValuePtr).ToSoftObjectPath().ToString());
			return MakeShared<FJsonValueObject>(JsonSoftObject);
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			TSharedPtr<FJsonObject> JsonStruct = MakeShared<FJsonObject>();
			JsonStruct->SetStringField(TEXT("struct"), StructProperty->Struct ? StructProperty->Struct->GetPathName() : FString());

			TArray<TSharedPtr<FJsonValue>> Fields;
			if (StructProperty->Struct)
			{
				for (TFieldIterator<FProperty> It(StructProperty->Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
				{
					const FProperty* InnerProperty = *It;
					if (!ShouldExportDataTableProperty(InnerProperty))
					{
						continue;
					}

					const void* InnerValuePtr = InnerProperty->ContainerPtrToValuePtr<void>(ValuePtr);
					TSharedPtr<FJsonObject> JsonField = MakeShared<FJsonObject>();
					JsonField->SetStringField(TEXT("name"), InnerProperty->GetName());
					JsonField->SetObjectField(TEXT("property"), ExportPropertyMetadata(InnerProperty));
					JsonField->SetStringField(TEXT("export_text"), ExportPropertyText(InnerProperty, InnerValuePtr));
					JsonField->SetField(TEXT("value"), ExportStructuredPropertyValue(InnerProperty, InnerValuePtr, Context, Depth + 1));
					Fields.Add(MakeShared<FJsonValueObject>(JsonField));
				}
			}

			JsonStruct->SetArrayField(TEXT("fields"), Fields);
			return MakeShared<FJsonValueObject>(JsonStruct);
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			TArray<TSharedPtr<FJsonValue>> Elements;
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			const int32 ElementCount = FMath::Min(ArrayHelper.Num(), MaxContainerElements);
			for (int32 Index = 0; Index < ElementCount; ++Index)
			{
				Elements.Add(ExportStructuredPropertyValue(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), Context, Depth + 1));
			}

			if (ArrayHelper.Num() > MaxContainerElements)
			{
				TSharedPtr<FJsonObject> JsonTruncated = MakeShared<FJsonObject>();
				JsonTruncated->SetBoolField(TEXT("truncated"), true);
				JsonTruncated->SetNumberField(TEXT("total_count"), ArrayHelper.Num());
				Elements.Add(MakeShared<FJsonValueObject>(JsonTruncated));
			}

			return MakeShared<FJsonValueArray>(Elements);
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			TArray<TSharedPtr<FJsonValue>> Elements;
			FScriptSetHelper SetHelper(SetProperty, ValuePtr);
			int32 ExportedCount = 0;
			for (int32 Index = 0; Index < SetHelper.GetMaxIndex() && ExportedCount < MaxContainerElements; ++Index)
			{
				if (SetHelper.IsValidIndex(Index))
				{
					Elements.Add(ExportStructuredPropertyValue(SetProperty->ElementProp, SetHelper.GetElementPtr(Index), Context, Depth + 1));
					++ExportedCount;
				}
			}

			return MakeShared<FJsonValueArray>(Elements);
		}

		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			TArray<TSharedPtr<FJsonValue>> Entries;
			FScriptMapHelper MapHelper(MapProperty, ValuePtr);
			int32 ExportedCount = 0;
			for (int32 Index = 0; Index < MapHelper.GetMaxIndex() && ExportedCount < MaxContainerElements; ++Index)
			{
				if (!MapHelper.IsValidIndex(Index))
				{
					continue;
				}

				TSharedPtr<FJsonObject> JsonEntry = MakeShared<FJsonObject>();
				JsonEntry->SetField(TEXT("key"), ExportStructuredPropertyValue(MapProperty->KeyProp, MapHelper.GetKeyPtr(Index), Context, Depth + 1));
				JsonEntry->SetField(TEXT("value"), ExportStructuredPropertyValue(MapProperty->ValueProp, MapHelper.GetValuePtr(Index), Context, Depth + 1));
				Entries.Add(MakeShared<FJsonValueObject>(JsonEntry));
				++ExportedCount;
			}

			return MakeShared<FJsonValueArray>(Entries);
		}

		return MakeShared<FJsonValueString>(ExportPropertyText(Property, ValuePtr));
	}

	TSharedPtr<FJsonObject> ExportPropertyValueObject(const FProperty* Property, const void* ValuePtr, FExportContext& Context, int32 Depth)
	{
		TSharedPtr<FJsonObject> JsonValue = MakeShared<FJsonObject>();
		JsonValue->SetObjectField(TEXT("property"), ExportPropertyMetadata(Property));
		JsonValue->SetStringField(TEXT("export_text"), ExportPropertyText(Property, ValuePtr));
		JsonValue->SetField(TEXT("value"), ExportStructuredPropertyValue(Property, ValuePtr, Context, Depth));
		return JsonValue;
	}

	TArray<TSharedPtr<FJsonValue>> ExportObjectProperties(const UObject* Object, FExportContext& Context, int32 Depth)
	{
		TArray<TSharedPtr<FJsonValue>> Properties;
		if (!Object || Depth >= MaxExportDepth)
		{
			return Properties;
		}

		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!ShouldExportChooserProperty(Property))
			{
				continue;
			}

			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
			TSharedPtr<FJsonObject> JsonProperty = ExportPropertyValueObject(Property, ValuePtr, Context, Depth + 1);
			JsonProperty->SetStringField(TEXT("name"), Property->GetName());
			Properties.Add(MakeShared<FJsonValueObject>(JsonProperty));
		}

		return Properties;
	}

	TSharedPtr<FJsonObject> ExportRowStruct(const UScriptStruct* RowStruct)
	{
		TSharedPtr<FJsonObject> JsonStruct = MakeShared<FJsonObject>();
		if (!RowStruct)
		{
			return JsonStruct;
		}

		JsonStruct->SetStringField(TEXT("name"), RowStruct->GetName());
		JsonStruct->SetStringField(TEXT("path"), RowStruct->GetPathName());
		JsonStruct->SetStringField(TEXT("cpp_name"), RowStruct->GetStructCPPName());
		JsonStruct->SetStringField(TEXT("super_struct"), RowStruct->GetSuperStruct() ? RowStruct->GetSuperStruct()->GetPathName() : FString());
		JsonStruct->SetStringField(TEXT("struct_flags"), FString::Printf(TEXT("%u"), static_cast<uint32>(RowStruct->StructFlags)));
		return JsonStruct;
	}

	TArray<TSharedPtr<FJsonValue>> ExportColumns(const UScriptStruct* RowStruct)
	{
		TArray<TSharedPtr<FJsonValue>> Columns;
		if (!RowStruct)
		{
			return Columns;
		}

		int32 ColumnIndex = 0;
		for (TFieldIterator<FProperty> It(RowStruct, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!ShouldExportDataTableProperty(Property))
			{
				continue;
			}

			TSharedPtr<FJsonObject> JsonColumn = MakeShared<FJsonObject>();
			JsonColumn->SetNumberField(TEXT("index"), ColumnIndex++);
			JsonColumn->SetStringField(TEXT("name"), Property->GetName());
			JsonColumn->SetObjectField(TEXT("property"), ExportPropertyMetadata(Property));
			JsonColumn->SetObjectField(TEXT("type"), ExportPropertyType(Property));
			Columns.Add(MakeShared<FJsonValueObject>(JsonColumn));
		}

		return Columns;
	}

	TArray<TSharedPtr<FJsonValue>> ExportRows(const UDataTable* DataTable, const UScriptStruct* RowStruct)
	{
		TArray<TSharedPtr<FJsonValue>> Rows;
		if (!DataTable || !RowStruct)
		{
			return Rows;
		}

		TArray<FName> RowNames = DataTable->GetRowNames();
		RowNames.Sort([](const FName& Left, const FName& Right)
		{
			return Left.ToString() < Right.ToString();
		});

		const TMap<FName, uint8*>& RowMap = DataTable->GetRowMap();
		FExportContext Context;
		Context.RootAsset = DataTable;

		for (const FName& RowName : RowNames)
		{
			uint8* const* RowDataPtr = RowMap.Find(RowName);
			if (!RowDataPtr || !*RowDataPtr)
			{
				continue;
			}

			TSharedPtr<FJsonObject> JsonRow = MakeShared<FJsonObject>();
			JsonRow->SetStringField(TEXT("name"), RowName.ToString());
			TSharedPtr<FJsonObject> JsonValues = MakeShared<FJsonObject>();

			for (TFieldIterator<FProperty> It(RowStruct, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				const FProperty* Property = *It;
				if (!ShouldExportDataTableProperty(Property))
				{
					continue;
				}

				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(*RowDataPtr);
				JsonValues->SetObjectField(Property->GetName(), ExportPropertyValueObject(Property, ValuePtr, Context, 0));
			}

			JsonRow->SetObjectField(TEXT("values"), JsonValues);
			Rows.Add(MakeShared<FJsonValueObject>(JsonRow));
		}

		return Rows;
	}
}

namespace AssetToJson
{
	bool IsChooserTableAsset(const UObject* Asset)
	{
		return Asset && AssetToJsonTablePrivate::IsClassOrChildOfPathOrName(Asset->GetClass(), TEXT("/Script/Chooser.ChooserTable"), TEXT("ChooserTable"));
	}

	TSharedPtr<FJsonObject> ExportDataTableAsset(const UDataTable* DataTable, const FString& ObjectPathString)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("ue.asset_to_json.data_table.v1"));
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Root->SetObjectField(TEXT("asset"), AssetToJsonTablePrivate::ExportAssetSummary(DataTable, ObjectPathString));
		Root->SetObjectField(TEXT("exporter"), AssetToJsonTablePrivate::MakeExporterJson(TEXT("data_table"), TEXT("none")));

		const UScriptStruct* RowStruct = DataTable ? DataTable->GetRowStruct() : nullptr;
		Root->SetObjectField(TEXT("row_struct"), AssetToJsonTablePrivate::ExportRowStruct(RowStruct));
		Root->SetNumberField(TEXT("row_count"), DataTable ? DataTable->GetRowMap().Num() : 0);
		Root->SetArrayField(TEXT("columns"), AssetToJsonTablePrivate::ExportColumns(RowStruct));
		Root->SetArrayField(TEXT("rows"), AssetToJsonTablePrivate::ExportRows(DataTable, RowStruct));
		Root->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
		return Root;
	}

	TSharedPtr<FJsonObject> ExportChooserTableAsset(const UObject* ChooserTable, const FString& ObjectPathString)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("ue.asset_to_json.chooser_table.v1"));
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Root->SetObjectField(TEXT("asset"), AssetToJsonTablePrivate::ExportAssetSummary(ChooserTable, ObjectPathString));
		Root->SetObjectField(TEXT("exporter"), AssetToJsonTablePrivate::MakeExporterJson(TEXT("chooser_table"), TEXT("none")));

		AssetToJsonTablePrivate::FExportContext Context;
		Context.RootAsset = ChooserTable;
		Context.bExpandNestedObjects = true;
		if (ChooserTable)
		{
			Context.VisitedObjects.Add(ChooserTable);
		}

		TSharedPtr<FJsonObject> JsonChooserTable = MakeShared<FJsonObject>();
		JsonChooserTable->SetStringField(TEXT("object"), AssetToJsonTablePrivate::ObjectPath(ChooserTable));
		JsonChooserTable->SetStringField(TEXT("class"), AssetToJsonTablePrivate::ClassPath(ChooserTable));
		JsonChooserTable->SetArrayField(TEXT("properties"), AssetToJsonTablePrivate::ExportObjectProperties(ChooserTable, Context, 0));
		Root->SetObjectField(TEXT("chooser_table"), JsonChooserTable);

		TArray<TSharedPtr<FJsonValue>> Warnings;
		Warnings.Add(MakeShared<FJsonValueString>(TEXT("ChooserTable assets are exported through reflection so AssetToJson does not require a Chooser build dependency.")));
		Warnings.Add(MakeShared<FJsonValueString>(TEXT("ChooserTable JSON is for inspection only and does not support round-trip import.")));
		Root->SetArrayField(TEXT("warnings"), Warnings);
		return Root;
	}
}
