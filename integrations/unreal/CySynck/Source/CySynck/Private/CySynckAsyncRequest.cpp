#include "CySynckAsyncRequest.h"
#include "CySynckModule.h"

UCySynckAsyncRequest* UCySynckAsyncRequest::RequestAsync(UObject* WorldContextObject, const FString& Method, const FString& Path, const FString& BodyJson)
{
    check(IsInGameThread());
    auto* Action = NewObject<UCySynckAsyncRequest>();
    Action->RequestMethod = Method;
    Action->RequestPath = Path;
    Action->RequestBody = BodyJson;
    Action->Context = WorldContextObject;
    Action->HasContext = WorldContextObject != nullptr;
    if (WorldContextObject) Action->RegisterWithGameInstance(WorldContextObject);
    return Action;
}

void UCySynckAsyncRequest::Activate()
{
    check(IsInGameThread());
    if (Activated) return;
    Activated = true;
    // Also retain editor requests without a GameInstance until their native work
    // and game-thread completion finish. Every completion path releases this root.
    AddToRoot();
    auto* Module = FModuleManager::LoadModulePtr<FCySynckModule>(TEXT("CySynck"));
    if (!Module)
    {
        Finish(false, TEXT("{\"error\":\"CySynck module is unavailable\"}"));
        return;
    }
    TWeakObjectPtr<UCySynckAsyncRequest> Weak(this);
    Module->RequestAsync(RequestMethod, RequestPath, RequestBody, [Weak](bool Success, const FString& Result) {
        if (auto* Action = Weak.Get()) Action->Finish(Success, Result);
    });
}

void UCySynckAsyncRequest::Finish(bool Success, const FString& ResultJson)
{
    check(IsInGameThread());
    if (!HasContext || Context.IsValid()) Completed.Broadcast(Success, ResultJson);
    RemoveFromRoot();
    SetReadyToDestroy();
}
