#include "CySynckModule.h"
#include "CySynckBlueprintLibrary.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

IMPLEMENT_MODULE(FCySynckModule, CySynck)

bool FCySynckModule::Open(const FString& Directory, FString& Error)
{
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
void FCySynckModule::Close() { FScopeLock Lock(&Mutex); if (Handle) CloseCore(Handle); Handle = nullptr; }
void FCySynckModule::ShutdownModule() { Close(); if (Library) FPlatformProcess::FreeDllHandle(Library); Library = nullptr; }
bool UCySynckBlueprintLibrary::Open(const FString& Directory, FString& Error) { return FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck")).Open(Directory, Error); }
bool UCySynckBlueprintLibrary::Request(const FString& Method, const FString& Path, const FString& Body, FString& Result) { return FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck")).Request(Method, Path, Body, Result); }
void UCySynckBlueprintLibrary::Close() { FModuleManager::LoadModuleChecked<FCySynckModule>(TEXT("CySynck")).Close(); }
