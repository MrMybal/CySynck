#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CySynckModule.h"
#include "CySynckAsyncRequest.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace
{
FString TestState()
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CySynckTests"), FGuid::NewGuid().ToString(), TEXT("profil été")));
}

struct FAsyncState
{
    FCySynckModule* Module = nullptr;
    TArray<int32> Order;
    bool OnGameThread = true;
    bool ExpectedStatus = true;
    TWeakObjectPtr<UCySynckAsyncRequest> Proxy;
    double Started = FPlatformTime::Seconds();
};

class FWaitForRequests : public IAutomationLatentCommand
{
public:
    FWaitForRequests(FAutomationTestBase* InTest, TSharedRef<FAsyncState> InState) : Test(InTest), State(InState) {}
    virtual bool Update() override
    {
        const bool ProxyCompleted = !State->Proxy.IsValid() || !State->Proxy->IsRooted();
        if (State->Order.Num() != 4 || !ProxyCompleted)
        {
            if (FPlatformTime::Seconds() - State->Started < 15) return false;
            Test->AddError(TEXT("Timed out waiting for asynchronous CySynck requests"));
            State->Module->Close();
            return true;
        }
        Test->TestTrue(TEXT("Completions run on the game thread"), State->OnGameThread);
        Test->TestTrue(TEXT("Success and failure JSON propagate"), State->ExpectedStatus);
        for (int32 Index = 0; Index < 4; ++Index)
            Test->TestEqual(TEXT("Requests retain FIFO completion order"), State->Order[Index], Index);
        State->Module->Close();
        return true;
    }
private:
    FAutomationTestBase* Test;
    TSharedRef<FAsyncState> State;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCySynckAbiTest, "CySynck.Native.AbiAndUnicode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCySynckAbiTest::RunTest(const FString&)
{
    auto& Module = FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck"));
    Module.Close();
    FString Error, Result;
    TestFalse(TEXT("An empty profile is refused"), Module.Open(TEXT(""), Error));
    TestFalse(TEXT("A relative profile is refused"), Module.Open(TEXT("relative-profile"), Error));
    if (!TestTrue(TEXT("Open the versioned C ABI with a Unicode profile"), Module.Open(TestState(), Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Overview request succeeds"), Module.Request(TEXT("GET"), TEXT("/api/overview"), TEXT("{}"), Result));
    TestTrue(TEXT("Overview contains its version"), Result.Contains(TEXT("\"version\"")));
    TestFalse(TEXT("Unknown routes report failure"), Module.Request(TEXT("GET"), TEXT("/api/unknown"), TEXT("{}"), Result));
    TestTrue(TEXT("Error JSON is returned"), Result.Contains(TEXT("\"error\"")));
    Module.Close();
    TestFalse(TEXT("Closed handles refuse requests"), Module.Request(TEXT("GET"), TEXT("/api/overview"), TEXT("{}"), Result));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCySynckAsyncTest, "CySynck.Native.AsyncFifoAndLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCySynckAsyncTest::RunTest(const FString&)
{
    auto& Module = FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck"));
    Module.Close();
    FString Error;
    if (!TestTrue(TEXT("Open an isolated async profile"), Module.Open(TestState(), Error)))
    {
        AddError(Error);
        return false;
    }
    auto State = MakeShared<FAsyncState>();
    State->Module = &Module;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Module.RequestAsync(TEXT("GET"), Index == 3 ? TEXT("/api/unknown") : TEXT("/api/overview"), TEXT("{}"),
            [State, Index](bool Success, const FString& Result) {
                State->Order.Add(Index);
                State->OnGameThread &= IsInGameThread();
                State->ExpectedStatus &= Success == (Index != 3) && !Result.IsEmpty();
            });
    }
    State->Proxy = UCySynckAsyncRequest::RequestAsync(nullptr, TEXT("GET"), TEXT("/api/overview"), TEXT("{}"));
    State->Proxy->Activate();
    TestTrue(TEXT("Blueprint proxy stays alive while pending"), State->Proxy->IsRooted());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForRequests(this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCySynckCloseTest, "CySynck.Native.CloseDrainsActiveAndCancelsQueued", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCySynckCloseTest::RunTest(const FString&)
{
    auto& Module = FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck"));
    Module.Close();
    FString Error;
    if (!TestTrue(TEXT("Open an isolated close profile"), Module.Open(TestState(), Error)))
    {
        AddError(Error);
        return false;
    }
    bool ActiveCompleted = false, QueuedCancelled = false;
    Module.RequestAsync(TEXT("GET"), TEXT("/api/overview"), TEXT("{}"), [&ActiveCompleted](bool Success, const FString&) { ActiveCompleted = Success; });
    Module.RequestAsync(TEXT("GET"), TEXT("/api/overview"), TEXT("{}"), [&QueuedCancelled](bool Success, const FString& Result) {
        QueuedCancelled = !Success && Result.Contains(TEXT("before the request started"));
    });
    Module.Close();
    TestTrue(TEXT("Active work completes before Close returns"), ActiveCompleted);
    TestTrue(TEXT("Queued work is explicitly cancelled"), QueuedCancelled);
    TestTrue(TEXT("The module can reopen after Close"), Module.Open(TestState(), Error));
    Module.Close();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCySynckQueueTest, "CySynck.Native.QueueBackpressure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCySynckQueueTest::RunTest(const FString&)
{
    auto& Module = FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck"));
    Module.Close();
    FString Error;
    if (!TestTrue(TEXT("Open an isolated queue profile"), Module.Open(TestState(), Error)))
    {
        AddError(Error);
        return false;
    }
    int32 Completed = 0, Full = 0, Successes = 0;
    for (int32 Index = 0; Index < 65; ++Index)
        Module.RequestAsync(TEXT("GET"), TEXT("/api/overview"), TEXT("{}"), [&](bool Success, const FString& Result) {
            ++Completed;
            Successes += Success ? 1 : 0;
            Full += Result.Contains(TEXT("queue is full")) ? 1 : 0;
        });
    TestEqual(TEXT("Excess requests fail without waiting for a tick"), Full, 1);
    Module.Close();
    TestEqual(TEXT("Every queued or rejected request completes exactly once"), Completed, 65);
    TestEqual(TEXT("Only the already active request executes during Close"), Successes, 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCySynckShutdownTest, "CySynck.Native.ShutdownJoinsWorker", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCySynckShutdownTest::RunTest(const FString&)
{
    auto& Module = FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck"));
    Module.Close();
    FString Error;
    if (!TestTrue(TEXT("Open an isolated shutdown profile"), Module.Open(TestState(), Error)))
    {
        AddError(Error);
        return false;
    }
    int32 Completed = 0;
    Module.RequestAsync(TEXT("GET"), TEXT("/api/overview"), TEXT("{}"), [&](bool, const FString&) { ++Completed; });
    Module.RequestAsync(TEXT("GET"), TEXT("/api/overview"), TEXT("{}"), [&](bool, const FString&) { ++Completed; });
    Module.ShutdownModule();
    TestEqual(TEXT("Shutdown completes/cancels all owned work before returning"), Completed, 2);
    Module.ShutdownModule();
    Module.StartupModule();
    TestTrue(TEXT("Native library and worker reload after shutdown"), Module.Open(TestState(), Error));
    Module.Close();
    return true;
}
#endif
