#pragma once

#include "CoreMinimal.h"

class UAnimBlueprint;
class UAnimGraphNode_Base;
class FJsonObject;
class FJsonValue;

namespace AssetToJson
{
	TSharedPtr<FJsonObject> ExportAnimGraphNodeDetails(const UAnimGraphNode_Base* AnimGraphNode);
	TArray<TSharedPtr<FJsonValue>> ExportAnimationStateMachines(const UAnimBlueprint* AnimBlueprint);
}
