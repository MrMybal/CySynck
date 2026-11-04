using System.IO;
using UnrealBuildTool;

public class CySynck : ModuleRules
{
    public CySynck(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.Add("Projects");
        string NativeRoot = Path.Combine(PluginDirectory, "ThirdParty", "CySynck");
        PublicIncludePaths.Add(Path.Combine(NativeRoot, "include"));
        string Library = Target.Platform == UnrealTargetPlatform.Win64
            ? Path.Combine(NativeRoot, "bin", "cysynck_core.dll")
            : Path.Combine(NativeRoot, "lib", Target.Platform == UnrealTargetPlatform.Mac
                ? "libcysynck_core.dylib" : "libcysynck_core.so");
        RuntimeDependencies.Add(Library);
    }
}
