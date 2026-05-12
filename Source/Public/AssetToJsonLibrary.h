#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AssetToJsonLibrary.generated.h"

UCLASS()
class ASSETTOJSON_API UAssetToJsonLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Reads any supported asset as AI inspection JSON. Blueprint assets are routed to the Blueprint visual script exporter. */
	UFUNCTION(BlueprintCallable, Category="Asset To JSON", meta=(AdvancedDisplay="bPrettyPrint,bReturnJson,bIncludeNodeProperties"))
	static FString ReadAssetAsJson(
		const FString& AssetPath,
		const FString& OutputFilePath,
		bool bPrettyPrint = true,
		bool bReturnJson = true,
		bool bIncludeNodeProperties = false);

	/** Reads a Blueprint visual script asset as logic-focused JSON. If OutputFilePath is set, the same JSON is also written to disk. */
	UFUNCTION(BlueprintCallable, Category="Asset To JSON", meta=(AdvancedDisplay="bPrettyPrint,bReturnJson,bIncludeNodeProperties"))
	static FString ReadBlueprintVisualScriptAsJson(
		const FString& AssetPath,
		const FString& OutputFilePath,
		bool bPrettyPrint = true,
		bool bReturnJson = true,
		bool bIncludeNodeProperties = false);
};
