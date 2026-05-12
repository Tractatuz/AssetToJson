#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AssetToJsonCommandlet.generated.h"

UCLASS()
class UAssetToJsonCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAssetToJsonCommandlet();

	virtual int32 Main(const FString& Params) override;
};
