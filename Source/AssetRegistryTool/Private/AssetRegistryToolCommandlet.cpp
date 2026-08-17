#include "AssetRegistryToolCommandlet.h"
#include "AssetRegistryToolHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY(LogAssetRegistryTool);

int32 UAssetRegistryToolCommandlet::Main(const FString& Params)
{
    UE_LOG(LogAssetRegistryTool, Display, TEXT("AssetRegistryTool started with params: %s"), *Params);

    TArray<FString> Tokens, Switches;
    TMap<FString, FString> ParamsMap;
    UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

    if (Tokens.Num() == 0)
    {
        UE_LOG(LogAssetRegistryTool, Error, TEXT("No command specified. Use List or Merge."));
        return 1;
    }

    const FString& Command = Tokens[0];

    if (Command.Equals(TEXT("Help"), ESearchCase::IgnoreCase))
    {
        // œ‘ æ∞Ô÷˙–≈œ¢
        UE_LOG(LogAssetRegistryTool, Display, TEXT("AssetRegistryTool Help:"));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("Usage: AssetRegistryTool <Command> [Options]"));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("Commands:"));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("  List - Lists assets from the specified AssetRegistry."));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("    -AssetRegistry=<Path>   : Path to the AssetRegistry file."));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("    -OutFile=<Path>         : Path to export the asset list (optional)."));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("  Merge - Merges one AssetRegistry into another."));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("    -HostAssetRegistry=<Path> : Path to the host AssetRegistry."));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("    -AssetRegistry=<Path>     : Path to the source AssetRegistry to merge."));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("    -OutputAssetRegistry=<Path> : Path to save the merged AssetRegistry."));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("    -OverwriteExistingAssets  : Whether to overwrite existing assets during the merge."));
        UE_LOG(LogAssetRegistryTool, Display, TEXT("    -FilterPaths :   by provided Package Paths"));

       
        return 0;
    }
    else if (Command.Equals(TEXT("convert"), ESearchCase::IgnoreCase))
    {
        return FAssetRegistryToolHelper::ConvertJSONToBin(ParamsMap);
    }
    else if (Command.Equals(TEXT("List"), ESearchCase::IgnoreCase))
    {
        return FAssetRegistryToolHelper::RunList(ParamsMap, Switches);
    }
    else if (Command.Equals(TEXT("Merge"), ESearchCase::IgnoreCase))
    {
        return FAssetRegistryToolHelper::RunMerge(ParamsMap, Switches);
    }

    UE_LOG(LogAssetRegistryTool, Error, TEXT("Unknown command: %s"), *Command);
    return 1;
}
