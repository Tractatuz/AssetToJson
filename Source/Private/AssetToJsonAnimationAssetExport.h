#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UObject;
class UAnimationAsset;

namespace AssetToJson
{
	bool IsAnimationAsset(const UObject* Asset);
	TSharedPtr<FJsonObject> ExportAnimationAsset(const UAnimationAsset* AnimationAsset, const FString& ObjectPathString);
}
