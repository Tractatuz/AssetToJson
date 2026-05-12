#include "AssetToJsonCommandlet.h"

#include "AssetToJsonLibrary.h"

#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace AssetToJsonCommandletPrivate
{
	FString NormalizeObjectPath(const FString& AssetPath)
	{
		const FString TrimmedPath = AssetPath.TrimStartAndEnd();
		if (!TrimmedPath.Contains(TEXT(".")) && FPackageName::IsValidLongPackageName(TrimmedPath))
		{
			return FString::Printf(TEXT("%s.%s"), *TrimmedPath, *FPackageName::GetLongPackageAssetName(TrimmedPath));
		}

		return TrimmedPath;
	}

	FString MakeDefaultOutputPath(const FString& AssetPath)
	{
		FString SafeName = AssetPath;
		for (TCHAR& Character : SafeName)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('.') && Character != TEXT('-'))
			{
				Character = TEXT('_');
			}
		}

		SafeName.TrimStartAndEndInline();
		if (SafeName.IsEmpty())
		{
			SafeName = TEXT("Asset");
		}

		return FPaths::ProjectSavedDir() / TEXT("AssetToJson") / FString::Printf(TEXT("%s.json"), *SafeName);
	}

	FString ResolveOutputPath(const FString& OutputPath)
	{
		if (OutputPath.IsEmpty() || FPaths::IsRelative(OutputPath) == false)
		{
			return OutputPath;
		}

		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), OutputPath);
	}

	bool TryReadOkField(const FString& JsonString, bool& bOutOk)
	{
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}

		return Root->TryGetBoolField(TEXT("ok"), bOutOk);
	}
}

UAssetToJsonCommandlet::UAssetToJsonCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UAssetToJsonCommandlet::Main(const FString& Params)
{
	FString AssetPath;
	FParse::Value(*Params, TEXT("AssetPath="), AssetPath);
	if (AssetPath.IsEmpty())
	{
		FParse::Value(*Params, TEXT("Asset="), AssetPath);
	}

	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogInit, Error, TEXT("Missing required -AssetPath=<PackagePathOrObjectPath> argument."));
		return 1;
	}

	FString OutputFilePath;
	FParse::Value(*Params, TEXT("Output="), OutputFilePath);
	if (OutputFilePath.IsEmpty())
	{
		FParse::Value(*Params, TEXT("OutputJson="), OutputFilePath);
	}

	const bool bNoOutputFile = FParse::Param(*Params, TEXT("NoOutputFile"));
	if (bNoOutputFile)
	{
		OutputFilePath.Empty();
	}
	else if (OutputFilePath.IsEmpty())
	{
		OutputFilePath = AssetToJsonCommandletPrivate::MakeDefaultOutputPath(AssetPath);
	}
	else
	{
		OutputFilePath = AssetToJsonCommandletPrivate::ResolveOutputPath(OutputFilePath);
	}

	const bool bPrettyPrint = !FParse::Param(*Params, TEXT("NoPretty"));
	const bool bIncludeNodeProperties = FParse::Param(*Params, TEXT("IncludeNodeProperties"));
	const bool bPrintJson = bNoOutputFile || FParse::Param(*Params, TEXT("ReturnJson"));
	const FString ObjectPathString = AssetToJsonCommandletPrivate::NormalizeObjectPath(AssetPath);

	const FString JsonString = UAssetToJsonLibrary::ReadAssetAsJson(ObjectPathString, OutputFilePath, bPrettyPrint, true, bIncludeNodeProperties);

	bool bOk = false;
	if (!AssetToJsonCommandletPrivate::TryReadOkField(JsonString, bOk))
	{
		UE_LOG(LogInit, Error, TEXT("AssetToJson returned invalid JSON for %s."), *ObjectPathString);
		return 1;
	}

	if (bPrintJson)
	{
		UE_LOG(LogInit, Display, TEXT("%s"), *JsonString);
	}

	if (!OutputFilePath.IsEmpty())
	{
		UE_LOG(LogInit, Display, TEXT("OUTPUT_JSON %s"), *OutputFilePath);
	}

	UE_LOG(LogInit, Display, TEXT("ASSET_TO_JSON_OK %s"), bOk ? TEXT("true") : TEXT("false"));
	return bOk ? 0 : 1;
}
