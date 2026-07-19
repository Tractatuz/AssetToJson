#include "AssetToJsonLibrary.h"

#include "AssetToJsonBlueprintExport.h"
#include "AssetToJsonEnhancedInputExport.h"
#include "AssetToJsonAnimationAssetExport.h"
#include "AssetToJsonTableExport.h"
#include "AssetToJsonWorldExport.h"

#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Animation/AnimationAsset.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace AssetToJsonLibraryPrivate
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
		if (TrimmedPath.EndsWith(TEXT(".umap"), ESearchCase::IgnoreCase))
		{
			FString NormalizedFilename = TrimmedPath;
			FPaths::NormalizeFilename(NormalizedFilename);

			FString LongPackageName;
			if (!FPackageName::TryConvertFilenameToLongPackageName(NormalizedFilename, LongPackageName))
			{
				const FString FullFilename = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), NormalizedFilename);
				FPackageName::TryConvertFilenameToLongPackageName(FullFilename, LongPackageName);
			}

			if (FPackageName::IsValidLongPackageName(LongPackageName))
			{
				return FString::Printf(TEXT("%s.%s"), *LongPackageName, *FPackageName::GetLongPackageAssetName(LongPackageName));
			}
		}

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

	TArray<TSharedPtr<FJsonValue>> MakeWarningsArray(std::initializer_list<const TCHAR*> Warnings)
	{
		TArray<TSharedPtr<FJsonValue>> JsonWarnings;
		for (const TCHAR* Warning : Warnings)
		{
			JsonWarnings.Add(MakeShared<FJsonValueString>(Warning));
		}

		return JsonWarnings;
	}

	TSharedPtr<FJsonObject> MakeExporterJson(const FString& Type, const FString& WriteSupport)
	{
		TSharedPtr<FJsonObject> JsonExporter = MakeShared<FJsonObject>();
		JsonExporter->SetStringField(TEXT("type"), Type);
		JsonExporter->SetStringField(TEXT("write_support"), WriteSupport);
		JsonExporter->SetBoolField(TEXT("supports_round_trip"), false);
		return JsonExporter;
	}

	TSharedPtr<FJsonObject> MakeAssetJson(const UObject* Asset, const FString& ObjectPathString)
	{
		TSharedPtr<FJsonObject> JsonAsset = MakeShared<FJsonObject>();
		JsonAsset->SetStringField(TEXT("path"), FPackageName::ObjectPathToPackageName(ObjectPathString));
		JsonAsset->SetStringField(TEXT("object_path"), ObjectPathString);
		JsonAsset->SetStringField(TEXT("name"), Asset ? Asset->GetName() : FPackageName::ObjectPathToObjectName(ObjectPathString));
		JsonAsset->SetStringField(TEXT("class"), ClassPath(Asset));
		return JsonAsset;
	}

	TSharedPtr<FJsonObject> MakeUnsupportedAssetJson(const UObject* Asset, const FString& ObjectPathString)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("ue.asset_to_json.asset.v1"));
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Root->SetObjectField(TEXT("asset"), MakeAssetJson(Asset, ObjectPathString));
		Root->SetObjectField(TEXT("exporter"), MakeExporterJson(TEXT("unsupported"), TEXT("none")));
		Root->SetArrayField(TEXT("warnings"), MakeWarningsArray({ TEXT("No specialized AssetToJson exporter is implemented for this asset class yet.") }));
		return Root;
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
}

FString UAssetToJsonLibrary::ReadAssetAsJson(
	const FString& AssetPath,
	const FString& OutputFilePath,
	bool bPrettyPrint,
	bool bReturnJson,
	bool bIncludeNodeProperties)
{
	const FString ObjectPathString = AssetToJsonLibraryPrivate::NormalizeObjectPath(AssetPath);
	if (ObjectPathString.IsEmpty())
	{
		return AssetToJsonLibraryPrivate::SerializeJson(AssetToJsonLibraryPrivate::MakeErrorJson(TEXT("AssetPath is empty")).ToSharedRef(), bPrettyPrint);
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPathString);
	if (!Asset)
	{
		return AssetToJsonLibraryPrivate::SerializeJson(AssetToJsonLibraryPrivate::MakeErrorJson(FString::Printf(TEXT("Asset could not be loaded: %s"), *ObjectPathString)).ToSharedRef(), bPrettyPrint);
	}

	if (Asset->IsA<UBlueprint>())
	{
		return ReadBlueprintVisualScriptAsJson(ObjectPathString, OutputFilePath, bPrettyPrint, bReturnJson, bIncludeNodeProperties);
	}

	if (UWorld* World = Cast<UWorld>(Asset))
	{
		return AssetToJson::ReadWorldAsJson(World, ObjectPathString, OutputFilePath, bPrettyPrint, bReturnJson, bIncludeNodeProperties);
	}

	if (AssetToJson::IsEnhancedInputAsset(Asset))
	{
		TArray<TSharedPtr<FJsonValue>> Warnings;
		const FString JsonString = AssetToJsonLibraryPrivate::SerializeJson(AssetToJson::ExportEnhancedInputAsset(Asset, ObjectPathString, Warnings).ToSharedRef(), bPrettyPrint);
		if (!AssetToJsonLibraryPrivate::SaveJsonStringToFile(JsonString, OutputFilePath))
		{
			return AssetToJsonLibraryPrivate::SerializeJson(AssetToJsonLibraryPrivate::MakeErrorJson(FString::Printf(TEXT("Failed to write JSON file: %s"), *OutputFilePath)).ToSharedRef(), bPrettyPrint);
		}

		return bReturnJson ? JsonString : FString();
	}

	if (AssetToJson::IsAnimationAsset(Asset))
	{
		const FString JsonString = AssetToJsonLibraryPrivate::SerializeJson(AssetToJson::ExportAnimationAsset(Cast<UAnimationAsset>(Asset), ObjectPathString).ToSharedRef(), bPrettyPrint);
		if (!AssetToJsonLibraryPrivate::SaveJsonStringToFile(JsonString, OutputFilePath))
		{
			return AssetToJsonLibraryPrivate::SerializeJson(AssetToJsonLibraryPrivate::MakeErrorJson(FString::Printf(TEXT("Failed to write JSON file: %s"), *OutputFilePath)).ToSharedRef(), bPrettyPrint);
		}

		return bReturnJson ? JsonString : FString();
	}

	if (const UDataTable* DataTable = Cast<UDataTable>(Asset))
	{
		const FString JsonString = AssetToJsonLibraryPrivate::SerializeJson(AssetToJson::ExportDataTableAsset(DataTable, ObjectPathString).ToSharedRef(), bPrettyPrint);
		if (!AssetToJsonLibraryPrivate::SaveJsonStringToFile(JsonString, OutputFilePath))
		{
			return AssetToJsonLibraryPrivate::SerializeJson(AssetToJsonLibraryPrivate::MakeErrorJson(FString::Printf(TEXT("Failed to write JSON file: %s"), *OutputFilePath)).ToSharedRef(), bPrettyPrint);
		}

		return bReturnJson ? JsonString : FString();
	}

	if (AssetToJson::IsChooserTableAsset(Asset))
	{
		const FString JsonString = AssetToJsonLibraryPrivate::SerializeJson(AssetToJson::ExportChooserTableAsset(Asset, ObjectPathString).ToSharedRef(), bPrettyPrint);
		if (!AssetToJsonLibraryPrivate::SaveJsonStringToFile(JsonString, OutputFilePath))
		{
			return AssetToJsonLibraryPrivate::SerializeJson(AssetToJsonLibraryPrivate::MakeErrorJson(FString::Printf(TEXT("Failed to write JSON file: %s"), *OutputFilePath)).ToSharedRef(), bPrettyPrint);
		}

		return bReturnJson ? JsonString : FString();
	}

	const FString JsonString = AssetToJsonLibraryPrivate::SerializeJson(AssetToJsonLibraryPrivate::MakeUnsupportedAssetJson(Asset, ObjectPathString).ToSharedRef(), bPrettyPrint);
	if (!AssetToJsonLibraryPrivate::SaveJsonStringToFile(JsonString, OutputFilePath))
	{
		return AssetToJsonLibraryPrivate::SerializeJson(AssetToJsonLibraryPrivate::MakeErrorJson(FString::Printf(TEXT("Failed to write JSON file: %s"), *OutputFilePath)).ToSharedRef(), bPrettyPrint);
	}

	return bReturnJson ? JsonString : FString();
}

FString UAssetToJsonLibrary::ReadBlueprintVisualScriptAsJson(
	const FString& AssetPath,
	const FString& OutputFilePath,
	bool bPrettyPrint,
	bool bReturnJson,
	bool bIncludeNodeProperties)
{
	return AssetToJson::ReadBlueprintVisualScriptAsJson(AssetPath, OutputFilePath, bPrettyPrint, bReturnJson, bIncludeNodeProperties);
}
