#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Json.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistryToolCommandlet.h"

class FAssetRegistryToolHelper
{
public:
    // 加载指定路径的AssetRegistry
    static bool LoadRegistry(const FString& Path, FAssetRegistryState& OutState);

    // 列出所有资产
    static int32 RunList(const TMap<FString, FString>& Params, const TArray<FString>& Switches);

    static int32 ConvertJSONToBin(const TMap<FString, FString>& Params);

    // 合并AssetRegistry
    static int32 RunMerge(const TMap<FString, FString>& Params, const TArray<FString>& Switches);
};
