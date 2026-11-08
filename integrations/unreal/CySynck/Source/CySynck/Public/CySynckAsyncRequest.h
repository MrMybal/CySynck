#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "CySynckAsyncRequest.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCySynckAsyncCompleted, bool, Success, const FString&, ResultJson);

/** Queued C ABI request. Native work runs off the game thread; completion runs on it. */
UCLASS()
class CYSYNCK_API UCySynckAsyncRequest : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, Category="CySynck")
    FCySynckAsyncCompleted Completed;

    UFUNCTION(BlueprintCallable, Category="CySynck", meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", DisplayName="CySynck Request Async"))
    static UCySynckAsyncRequest* RequestAsync(UObject* WorldContextObject, const FString& Method, const FString& Path, const FString& BodyJson);

    virtual void Activate() override;

private:
    FString RequestMethod, RequestPath, RequestBody;
    TWeakObjectPtr<UObject> Context;
    bool HasContext = false;
    bool Activated = false;
    void Finish(bool Success, const FString& ResultJson);
};
