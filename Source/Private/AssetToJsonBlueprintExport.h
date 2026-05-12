#pragma once

#include "CoreMinimal.h"

namespace AssetToJson
{
	FString ReadBlueprintVisualScriptAsJson(
		const FString& AssetPath,
		const FString& OutputFilePath,
		bool bPrettyPrint,
		bool bReturnJson,
		bool bIncludeNodeProperties);
}
