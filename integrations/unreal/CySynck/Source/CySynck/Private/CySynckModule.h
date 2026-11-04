#pragma once
#include "Modules/ModuleManager.h"
#include "HAL/CriticalSection.h"

class FCySynckModule : public IModuleInterface
{
public:
    virtual void ShutdownModule() override;
    bool Open(const FString& Directory, FString& Error);
    bool Request(const FString& Method, const FString& Path, const FString& Body, FString& Result);
    void Close();
private:
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
