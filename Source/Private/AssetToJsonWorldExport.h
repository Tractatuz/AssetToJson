#pragma once

#include "CoreMinimal.h"

class UWorld;

namespace AssetToJson
{
	FString ReadWorldAsJson(
		UWorld* World,
		const FString& ObjectPathString,
		const FString& OutputFilePath,
		bool bPrettyPrint,
		bool bReturnJson,
		bool bIncludeNodeProperties);
}
