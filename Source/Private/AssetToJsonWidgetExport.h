#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UWidgetAnimation;
struct FDelegateEditorBinding;

namespace AssetToJson
{
	TSharedPtr<FJsonObject> ExportEnhancedWidgetAnimation(const UWidgetAnimation* Animation);
	TSharedPtr<FJsonObject> ExportWidgetBinding(const FDelegateEditorBinding& Binding);
}
