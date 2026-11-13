using UnrealBuildTool;

public class AssetRegistryTool : ModuleRules
{
    public AssetRegistryTool(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bLegacyPublicIncludePaths = false;
        CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Warning;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "AssetRegistry",
            "Json",
            "JsonUtilities"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Projects",
            "InputCore",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "ContentBrowser",
            "DesktopPlatform",
        });

        // PackageReader.h is intentionally an AssetRegistry internal header in UE 5.6.
        PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Source/Runtime/AssetRegistry/Internal"));

    }
}

