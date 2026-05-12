#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UObject;

namespace AssetToJson
{
	bool IsEnhancedInputAsset(const UObject* Asset);

	TSharedPtr<FJsonObject> ExportEnhancedInputAsset(
		const UObject* Asset,
		const FString& ObjectPathString,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings);
}
