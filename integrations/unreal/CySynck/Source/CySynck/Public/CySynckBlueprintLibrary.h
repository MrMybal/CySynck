#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CySynckBlueprintLibrary.generated.h"

UCLASS()
class CYSYNCK_API UCySynckBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Call expensive operations from a task, not an Unreal tick. Returns UTF-8 JSON as FString.
    UFUNCTION(BlueprintCallable, Category="CySynck")
    static bool Open(const FString& StateDirectory, FString& Error);

    UFUNCTION(BlueprintCallable, Category="CySynck")
    static bool Request(const FString& Method, const FString& Path, const FString& BodyJson, FString& ResultJson);

    UFUNCTION(BlueprintCallable, Category="CySynck")
    static void Close();
};
