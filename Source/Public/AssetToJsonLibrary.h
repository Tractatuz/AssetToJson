#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AssetToJsonLibrary.generated.h"

UCLASS()
class ASSETTOJSON_API UAssetToJsonLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Reads a Blueprint visual script asset as logic-focused JSON. If OutputFilePath is set, the same JSON is also written to disk. Raw T3D text is not emitted. */
	UFUNCTION(BlueprintCallable, Category="Asset To JSON", meta=(AdvancedDisplay="bPrettyPrint,bReturnJson,bIncludeT3DText,bIncludeNodeProperties"))
	static FString ReadBlueprintVisualScriptAsJson(
		const FString& AssetPath,
		const FString& OutputFilePath,
		bool bPrettyPrint = true,
		bool bReturnJson = true,
		bool bIncludeT3DText = false,
		bool bIncludeNodeProperties = false);
};
