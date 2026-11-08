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
        // Loose resources are needed by the OS loader and the local file:// reader.
        // Public headers/build intermediates are compile-time inputs only.
        string LibraryDirectory = Path.GetDirectoryName(Library);
        foreach (string File in Directory.GetFiles(LibraryDirectory))
            if (File != Library && (File.EndsWith(".dll") || File.EndsWith(".dylib") || File.Contains(".so")))
                RuntimeDependencies.Add(File, StagedFileType.NonUFS);
        foreach (string Name in new[] { "LICENSE", "THIRD_PARTY.md", "version.json" })
            RuntimeDependencies.Add(Path.Combine(NativeRoot, Name), StagedFileType.NonUFS);
        foreach (string Name in new[] { "licenses", "Documentation" })
        {
            string ResourceDirectory = Path.Combine(NativeRoot, Name);
            if (Directory.Exists(ResourceDirectory))
                foreach (string File in Directory.GetFiles(ResourceDirectory, "*", SearchOption.AllDirectories))
                    RuntimeDependencies.Add(File, StagedFileType.NonUFS);
        }
    }
}
