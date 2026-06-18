#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UObject;
class UDataTable;

namespace AssetToJson
{
	bool IsChooserTableAsset(const UObject* Asset);

	TSharedPtr<FJsonObject> ExportDataTableAsset(const UDataTable* DataTable, const FString& ObjectPathString);
	TSharedPtr<FJsonObject> ExportChooserTableAsset(const UObject* ChooserTable, const FString& ObjectPathString);
}
