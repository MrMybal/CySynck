#include "CySynckModule.h"
#include "CySynckBlueprintLibrary.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "HAL/Event.h"
#include "HAL/RunnableThread.h"

IMPLEMENT_MODULE(FCySynckModule, CySynck)

void FCySynckModule::StartupModule()
{
    if (WorkAvailable) return;
    Stopping = false;
    WorkerStopping = false;
    WorkAvailable = FPlatformProcess::GetSynchEventFromPool(false);
    WorkComplete = FPlatformProcess::GetSynchEventFromPool(true);
    WorkerThread = FRunnableThread::Create(this, TEXT("CySynckRequests"));
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FCySynckModule::Tick));
}

bool FCySynckModule::Open(const FString& Directory, FString& Error)
{
    check(IsInGameThread());
    if (Closing || Stopping) { Error = TEXT("CySynck is closing."); return false; }
    if (Directory.IsEmpty() || FPaths::IsRelative(Directory)) { Error = TEXT("Choose an explicit absolute CySynck state directory."); return false; }
    FScopeLock Lock(&Mutex);
    if (Handle) { Error = TEXT("Close the current CySynck core first."); return false; }
    if (!Library)
    {
        auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CySynck"));
        if (!Plugin) { Error = TEXT("CySynck plugin directory unavailable."); return false; }
#if PLATFORM_WINDOWS
        const TCHAR* Relative = TEXT("ThirdParty/CySynck/bin/cysynck_core.dll");
#elif PLATFORM_MAC
        const TCHAR* Relative = TEXT("ThirdParty/CySynck/lib/libcysynck_core.dylib");
#else
        const TCHAR* Relative = TEXT("ThirdParty/CySynck/lib/libcysynck_core.so");
#endif
        Library = FPlatformProcess::GetDllHandle(*FPaths::Combine(Plugin->GetBaseDir(), Relative));
        if (!Library) { Error = TEXT("Build and install the native CySynck library into ThirdParty/CySynck."); return false; }
        OpenCore = reinterpret_cast<OpenFn>(FPlatformProcess::GetDllExport(Library, TEXT("cysynck_open")));
        RequestCore = reinterpret_cast<RequestFn>(FPlatformProcess::GetDllExport(Library, TEXT("cysynck_request")));
        FreeCore = reinterpret_cast<FreeFn>(FPlatformProcess::GetDllExport(Library, TEXT("cysynck_free")));
        CloseCore = reinterpret_cast<CloseFn>(FPlatformProcess::GetDllExport(Library, TEXT("cysynck_close")));
        using AbiFn = unsigned int (*)();
        auto Abi = reinterpret_cast<AbiFn>(FPlatformProcess::GetDllExport(Library, TEXT("cysynck_abi_version")));
        if (!OpenCore || !RequestCore || !FreeCore || !CloseCore || !Abi || Abi() != 1)
        {
            FPlatformProcess::FreeDllHandle(Library); Library = nullptr;
            Error = TEXT("CySynck native ABI mismatch."); return false;
        }
    }
    char* NativeError = nullptr;
    Handle = OpenCore(TCHAR_TO_UTF8(*Directory), &NativeError);
    Error = NativeError ? UTF8_TO_TCHAR(NativeError) : TEXT("");
    if (NativeError) FreeCore(NativeError);
    return Handle != nullptr;
}

bool FCySynckModule::Request(const FString& Method, const FString& Path, const FString& Body, FString& Result)
{
    FScopeLock Lock(&Mutex);
    if (!Handle) { Result = TEXT("{\"error\":\"Open CySynck first\"}"); return false; }
    char* NativeResult = nullptr;
    int Code = RequestCore(Handle, TCHAR_TO_UTF8(*Method), TCHAR_TO_UTF8(*Path), TCHAR_TO_UTF8(*Body), &NativeResult);
    Result = NativeResult ? UTF8_TO_TCHAR(NativeResult) : TEXT("");
    if (NativeResult) FreeCore(NativeResult);
    return Code == 0;
}
void FCySynckModule::RequestAsync(FString Method, FString Path, FString Body, FCompletion Completion)
{
    check(IsInGameThread());
    if (!Completion) return;
    if (Closing || Stopping)
    {
        Completion(false, TEXT("{\"error\":\"CySynck is closing\"}"));
        return;
    }
    if (!WorkerThread)
    {
        Completion(false, TEXT("{\"error\":\"CySynck worker thread is unavailable\"}"));
        return;
    }
    if (Queue.Num() + (Active ? 1 : 0) >= 64)
    {
        Completion(false, TEXT("{\"error\":\"CySynck request queue is full\"}"));
        return;
    }
    auto Request = MakeUnique<FQueuedRequest>();
    Request->Method = MoveTemp(Method);
    Request->Path = MoveTemp(Path);
    Request->Body = MoveTemp(Body);
    Request->Completion = MoveTemp(Completion);
    Queue.Add(MoveTemp(Request));
    StartNext();
}

void FCySynckModule::StartNext()
{
    if (Active || Queue.IsEmpty() || Closing || Stopping || !WorkerThread) return;
    auto Next = MoveTemp(Queue[0]);
    Queue.RemoveAt(0);
    ActiveCompletion = MoveTemp(Next->Completion);
    {
        FScopeLock Lock(&WorkMutex);
        Work = MoveTemp(Next);
    }
    Active = true;
    WorkComplete->Reset();
    WorkAvailable->Trigger();
}

uint32 FCySynckModule::Run()
{
    for (;;)
    {
        WorkAvailable->Wait();
        if (WorkerStopping) return 0;
        TUniquePtr<FQueuedRequest> Next;
        {
            FScopeLock Lock(&WorkMutex);
            Next = MoveTemp(Work);
        }
        if (!Next) continue;
        FResult Result;
        Result.Success = Request(Next->Method, Next->Path, Next->Body, Result.Json);
        {
            FScopeLock Lock(&WorkMutex);
            CompletedResult = MoveTemp(Result);
        }
        WorkComplete->Trigger();
    }
}

bool FCySynckModule::Tick(float)
{
    check(IsInGameThread());
    if (Active && WorkComplete->Wait(0))
    {
        FResult Result;
        {
            FScopeLock Lock(&WorkMutex);
            Result = MoveTemp(CompletedResult);
        }
        Active = false;
        auto Completion = MoveTemp(ActiveCompletion);
        if (Completion) Completion(Result.Success, Result.Json);
    }
    StartNext();
    return !Stopping;
}

void FCySynckModule::Close()
{
    check(IsInGameThread());
    if (Closing) return;
    Closing = true;
    // Requests that have not started have no side effects. An active native
    // request completes before its handle/library can be closed or unloaded.
    auto Cancelled = MoveTemp(Queue);
    Queue.Reset();
    for (auto& Request : Cancelled)
        if (Request->Completion)
            Request->Completion(false, TEXT("{\"error\":\"CySynck closed before the request started\"}"));
    FResult Result;
    auto Completion = MoveTemp(ActiveCompletion);
    if (Active)
    {
        WorkComplete->Wait();
        {
            FScopeLock Lock(&WorkMutex);
            Result = MoveTemp(CompletedResult);
        }
        Active = false;
    }
    {
        FScopeLock Lock(&Mutex);
        if (Handle) CloseCore(Handle);
        Handle = nullptr;
    }
    if (Completion) Completion(Result.Success, Result.Json);
    Closing = false;
}

void FCySynckModule::ShutdownModule()
{
    if (Stopping) return;
    Stopping = true;
    FTSTicker::RemoveTicker(Ticker);
    Close();
    // A ready result alone does not prove a worker has left module code. Join
    // the owned thread before releasing native functions or unloading this DLL.
    WorkerStopping = true;
    WorkAvailable->Trigger();
    if (WorkerThread)
    {
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
    }
    FPlatformProcess::ReturnSynchEventToPool(WorkAvailable);
    FPlatformProcess::ReturnSynchEventToPool(WorkComplete);
    WorkAvailable = WorkComplete = nullptr;
    if (Library) FPlatformProcess::FreeDllHandle(Library);
    Library = nullptr;
}
bool UCySynckBlueprintLibrary::Open(const FString& Directory, FString& Error) { return FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck")).Open(Directory, Error); }
bool UCySynckBlueprintLibrary::Request(const FString& Method, const FString& Path, const FString& Body, FString& Result) { return FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck")).Request(Method, Path, Body, Result); }
void UCySynckBlueprintLibrary::Close() { FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck")).Close(); }
