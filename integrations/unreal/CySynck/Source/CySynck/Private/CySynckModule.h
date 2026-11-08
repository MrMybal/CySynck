#pragma once
#include "Modules/ModuleManager.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Ticker.h"

class FEvent;
class FRunnableThread;

class FCySynckModule : public IModuleInterface, private FRunnable
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    virtual bool SupportsDynamicReloading() override { return false; }
    bool Open(const FString& Directory, FString& Error);
    bool Request(const FString& Method, const FString& Path, const FString& Body, FString& Result);
    using FCompletion = TFunction<void(bool, const FString&)>;
    void RequestAsync(FString Method, FString Path, FString Body, FCompletion Completion);
    void Close();
private:
    struct FResult { bool Success = false; FString Json; };
    struct FQueuedRequest { FString Method, Path, Body; FCompletion Completion; };
    bool Tick(float DeltaTime);
    void StartNext();
    uint32 Run() override;
    TArray<TUniquePtr<FQueuedRequest>> Queue;
    TUniquePtr<FQueuedRequest> Work;
    FResult CompletedResult;
    FCriticalSection WorkMutex;
    FRunnableThread* WorkerThread = nullptr;
    FEvent* WorkAvailable = nullptr;
    FEvent* WorkComplete = nullptr;
    FThreadSafeBool WorkerStopping = false;
    bool Active = false;
    FCompletion ActiveCompletion;
    FTSTicker::FDelegateHandle Ticker;
    bool Closing = false;
    bool Stopping = false;
    FCriticalSection Mutex;
    void* Library = nullptr;
    void* Handle = nullptr;
    using OpenFn = void* (*)(const char*, char**);
    using RequestFn = int (*)(void*, const char*, const char*, const char*, char**);
    using FreeFn = void (*)(char*);
    using CloseFn = void (*)(void*);
    OpenFn OpenCore = nullptr;
    RequestFn RequestCore = nullptr;
    FreeFn FreeCore = nullptr;
    CloseFn CloseCore = nullptr;
};
