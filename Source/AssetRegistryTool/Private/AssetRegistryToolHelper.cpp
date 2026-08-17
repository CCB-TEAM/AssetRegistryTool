#include "AssetRegistryToolHelper.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Json.h"
#include "AssetRegistryToolCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Logging/LogMacros.h"
#include "Algo/Sort.h"
#include "Serialization/BufferArchive.h"
#include "AssetRegistryArchive.h"



bool FAssetRegistryToolHelper::LoadRegistry(const FString& Path, FAssetRegistryState& OutState)
{
    // 加载 AssetRegistry 文件
    TArray<uint8> Data;
    if (!FFileHelper::LoadFileToArray(Data, *Path))
    {
        UE_LOG(LogAssetRegistryTool, Error, TEXT("Failed to load AssetRegistry from: %s"), *Path);
        return false;
    }

    // 读取 AssetRegistry 数据
    FMemoryReader Reader(Data, true);
    FAssetRegistryLoadOptions LoadOptions;
    LoadOptions.bLoadDependencies = true;
    LoadOptions.bLoadPackageData = true;

    // 加载资产数据
    return OutState.Load(Reader, LoadOptions);
}

int32 FAssetRegistryToolHelper::ConvertJSONToBin(const TMap<FString, FString>& Params)
{
    if (!Params.Contains(TEXT("JSON")))
    {
        UE_LOG(LogAssetRegistryTool, Error, TEXT("convert requires -JSON=Path"));
        return 1;
    }
    FString JsonPath = Params[TEXT("JSON")];
    FString OutFile = Params.Contains(TEXT("OutFile")) ? Params[TEXT("OutFile")] : TEXT("");

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonPath))
    {
        UE_LOG(LogAssetRegistryTool, Error, TEXT("Failed to load JSON file: %s"), *JsonPath);
        return false;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
    {
        UE_LOG(LogAssetRegistryTool, Error, TEXT("Failed to parse JSON file. Error: %s"), *JsonReader->GetErrorMessage());
        return false;
    }
    // Create asset registry state
    FAssetRegistryState State; //创建一个FAssetRegistryState对象

    //读取assets  
    TArray<TSharedPtr<FJsonValue>> AssetsJson = JsonObject->GetArrayField(TEXT("Assets"));
    //遍历json中所有assets
    for (const auto& AssetJsonValue : AssetsJson)
    {
        TSharedPtr<FJsonObject> AssetJsonObject = AssetJsonValue->AsObject();
        if (!AssetJsonObject.IsValid())
        {
            UE_LOG(LogAssetRegistryTool, Warning, TEXT("Invalid asset data encountered. Skipping."));
            continue;
        }
        FAssetData* AssetData = new FAssetData(); //创建新的FAssetData
        FAssetDataTagMap Tags;

        // 从JSON对象中提取数据并设置到新的FAssetData对象
        AssetData->PackageName = FName(AssetJsonObject->GetStringField(TEXT("PackageName")));
        AssetData->PackagePath = FName(AssetJsonObject->GetStringField(TEXT("PackagePath")));
        AssetData->AssetName = FName(AssetJsonObject->GetStringField(TEXT("AssetName")));
        AssetData->AssetClassPath = FTopLevelAssetPath(AssetJsonObject->GetStringField(TEXT("AssetClass")));
        //处理 TagsAndValues
        TArray<TSharedPtr<FJsonValue>> TagJsonArray = AssetJsonObject->GetArrayField(TEXT("TagsAndValues"));
        if (TagJsonArray.Num() > 0) {
            for (const auto& TagJsonValue : TagJsonArray) {
                TSharedPtr<FJsonObject> TagJsonObject = TagJsonValue->AsObject();
                if (TagJsonObject)
                {
                    FName TagName = FName(TagJsonObject->GetStringField(TEXT("TagName")));  // 假设这里使用FName保存TagName
                    FString TagValue = TagJsonObject->GetStringField(TEXT("TagValue"));
                    Tags.Emplace(TagName,TagValue);
                }
            }
        }
        //process chunk ids
        TArray<TSharedPtr<FJsonValue>> ChunkIDsJson = AssetJsonObject->GetArrayField(TEXT("ChunkIDs"));
        // 将获得的浮点数（在JSON里 numbers 都作为 浮点储存）转化为int32 并加入到AssetData的ChunkIDs中去
        TArray<int32> ChunkList;

        for (const auto& Value : ChunkIDsJson)
        {
            ChunkList.Add(static_cast<int32>(Value->AsNumber()));
        }
        AssetData->ChunkIDs.Append(ChunkList);

        //取得PackageFlags 并赋值
        uint32  PackageFlags2 = static_cast<uint32>(AssetJsonObject->GetNumberField(TEXT("PackageFlags")));
        AssetData->PackageFlags = PackageFlags2;

        //UE_LOG(LogAssetRegistryTool, Display, TEXT("成功读取Asset到状态: PackageName=%s"), AssetData->PackageName.ToString());
        State.AddAssetData(AssetData);  //给state加入Asset
        State.AddTagsToAssetData(AssetData->ToSoftObjectPath(), MoveTemp(Tags));
    }
    // 将 AssetRegistryState 保存到文件
    FBufferArchive Ar;      //创建一个FBufferArchive    
    FAssetRegistrySerializationOptions SaveOptions;
    SaveOptions.bSerializeAssetRegistry = true;
    SaveOptions.bSerializeDependencies = true;
    SaveOptions.bSerializePackageData = true;
    SaveOptions.bSerializeSearchableNameDependencies = true;
    SaveOptions.bSerializeManageDependencies = true;
    Ar.SetFilterEditorOnly(true);
    if (!State.Save(Ar, SaveOptions))
    {
        UE_LOG(LogAssetRegistryTool, Error,
            TEXT("❌ Failed to serialize merged AssetRegistry"));
        return 1;
    }

    if (!FFileHelper::SaveArrayToFile(Ar, *OutFile))
    {
        UE_LOG(LogAssetRegistryTool, Error,
            TEXT("❌ Failed to save cooked AssetRegistry to %s"), *OutFile);
        return 1;
    }

    UE_LOG(LogAssetRegistryTool, Display,
        TEXT("✅ Successfully saved cooked-style AssetRegistry to: %s (size: %d bytes)"),
        *OutFile, Ar.Num());
    return 0;
}

int32 FAssetRegistryToolHelper::RunList(const TMap<FString, FString>& Params, const TArray<FString>& Switches)
{
    if (!Params.Contains(TEXT("AssetRegistry")))
    {
        UE_LOG(LogAssetRegistryTool, Error, TEXT("List requires -AssetRegistry=Path"));
        return 1;
    }
    FString RegistryPath = Params[TEXT("AssetRegistry")];
    FString OutFile = Params.Contains(TEXT("OutFile")) ? Params[TEXT("OutFile")] : TEXT("");
    bool bVerbose = Switches.Contains(TEXT("Verbose"));
    // 加载 AssetRegistry
    FAssetRegistryState State;
    if (!LoadRegistry(RegistryPath, State))
    {
        return 1;
    }
    UE_LOG(LogAssetRegistryTool, Display, TEXT("Listing assets from: %s"), *RegistryPath);

    // JSON Array to store all asset data
    TArray<TSharedPtr<FJsonValue>> AssetArray;

    // 遍历所有资产并输出
    State.EnumerateAllAssets([&](const FAssetData& Asset)
        {
            // 格式化并添加数据
            TSharedPtr<FJsonObject> AssetJson = MakeShareable(new FJsonObject());

            AssetJson->SetStringField(TEXT("ObjectPath"), Asset.GetSoftObjectPath().ToString());
            AssetJson->SetStringField(TEXT("PackageName"), Asset.PackageName.ToString());
            AssetJson->SetStringField(TEXT("PackagePath"), Asset.PackagePath.ToString());
            AssetJson->SetStringField(TEXT("AssetName"), Asset.AssetName.ToString());
            AssetJson->SetStringField(TEXT("AssetClass"), Asset.AssetClassPath.ToString());

            // 处理标签
            TArray<TSharedPtr<FJsonValue>> TagArray;
            // 只处理 TagsAndValues 非空的情况
            if (Asset.TagsAndValues.Num() > 0)
            {
                for (const auto& Tag : Asset.TagsAndValues)
                {
                    TSharedPtr<FJsonObject> TagJson = MakeShareable(new FJsonObject());
                    TagJson->SetStringField(TEXT("TagName"), Tag.Key.ToString());
                    TagJson->SetStringField(TEXT("TagValue"), Tag.Value.AsString());
                    TagArray.Add(MakeShareable(new FJsonValueObject(TagJson)));
                }
                // 如果有标签数据才设置
                AssetJson->SetArrayField(TEXT("TagsAndValues"), TagArray);
            }
            else
            {
                // 如果没有 TagsAndValues，跳过此字段
                UE_LOG(LogAssetRegistryTool, Display, TEXT("Asset with empty TagsAndValues: %s"), *Asset.GetSoftObjectPath().ToString());
            }

            TArray<int32> ChunkIDs;
            const FAssetData::FChunkArrayView& ChunkArrayView = Asset.GetChunkIDs();
            ChunkIDs.Append(ChunkArrayView.GetData(), ChunkArrayView.Num());
            TArray<TSharedPtr<FJsonValue>> ChunkIDsJson;
            for (int32 ChunkID : ChunkIDs)
            {
                ChunkIDsJson.Add(MakeShareable(new FJsonValueNumber(ChunkID)));
            }
            AssetJson->SetArrayField(TEXT("ChunkIDs"), ChunkIDsJson);
            uint32 PackageFlags = Asset.PackageFlags;
            AssetJson->SetNumberField(TEXT("PackageFlags"), PackageFlags);
            // Add this asset's JSON object to the array
            AssetArray.Add(MakeShareable(new FJsonValueObject(AssetJson)));

            if (bVerbose)
            {
                FString AssetString = FString::Printf(
                    TEXT("{\"ObjectPath\": \"%s\", \"PackagePath\": \"%s\", \"PackageName\": \"%s\", \"AssetName\": \"%s\", \"AssetClass\": \"%s\", \"TagsAndValues\": [%s], \"ChunkIDs\": [%s], \"PackageFlags\": %u}"),
                    *Asset.GetSoftObjectPath().ToString(),
                    *Asset.PackageName.ToString(),
                    *Asset.PackagePath.ToString(),
                    *Asset.AssetName.ToString(),
                    *Asset.AssetClassPath.ToString(),
                    *FString::JoinBy(TagArray, TEXT(", "), [](const TSharedPtr<FJsonValue>& Value) { return Value->AsString(); }),
                    *FString::JoinBy(ChunkIDsJson, TEXT(", "), [](const TSharedPtr<FJsonValue>& Value) { return Value->AsString(); }),
                    PackageFlags
                );
                UE_LOG(LogAssetRegistryTool, Display, TEXT("%s"), *AssetString);
            }
        });

    // Create the final JSON object containing the array of assets
    TSharedPtr<FJsonObject> FinalJson = MakeShareable(new FJsonObject());
    FinalJson->SetArrayField(TEXT("Assets"), AssetArray);

    // Serialize the JSON object to a string
    FString OutputString;
    TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&OutputString);
    if (FJsonSerializer::Serialize(FinalJson.ToSharedRef(), JsonWriter))
    {
        // If we have an output file, save the JSON string to that file
        if (!OutFile.IsEmpty())
        {
            if (FFileHelper::SaveStringToFile(OutputString, *OutFile))
            {
                UE_LOG(LogAssetRegistryTool, Display, TEXT("Exported %d assets to %s"), AssetArray.Num(), *OutFile);
            }
            else
            {
                UE_LOG(LogAssetRegistryTool, Error, TEXT("Failed to save JSON to %s"), *OutFile);
            }
        }
        else
        {
            // Otherwise, just log the output (useful for debugging)
            UE_LOG(LogAssetRegistryTool, Display, TEXT("%s"), *OutputString);
        }
    }

    return 0;
}
int32 FAssetRegistryToolHelper::RunMerge(const TMap<FString, FString>& Params, const TArray<FString>& Switches)
{
	if (!Params.Contains(TEXT("HostAssetRegistry")) ||
		!Params.Contains(TEXT("AssetRegistry")) ||
		!Params.Contains(TEXT("OutputAssetRegistry")))
	{
		UE_LOG(LogAssetRegistryTool, Error,
			TEXT("Merge requires -HostAssetRegistry=Path -AssetRegistry=Path -OutputAssetRegistry=Path"));
		return 1;
	}

	const FString HostPath = Params[TEXT("HostAssetRegistry")];
	const FString DonorPath = Params[TEXT("AssetRegistry")];
	const FString OutputPath = Params[TEXT("OutputAssetRegistry")];

	const bool bOverwriteExistingAssets = Switches.Contains(TEXT("OverwriteExistingAssets"));

	// ---- Filters ----
	TArray<FString> FilterClasses;
	if (Params.Contains(TEXT("FilterClass")))
	{
		Params[TEXT("FilterClass")].ParseIntoArray(FilterClasses, TEXT(","), true);
	}

	TArray<FString> FilterPaths;
	if (Params.Contains(TEXT("FilterPaths")))
	{
		Params[TEXT("FilterPaths")].ParseIntoArray(FilterPaths, TEXT(","), true);
	}

	const bool bNoRecursivePaths = Switches.Contains(TEXT("NoRecursivePaths"));

	// ---- Load Registries ----
	FAssetRegistryState HostState, DonorState;
	if (!LoadRegistry(HostPath, HostState) || !LoadRegistry(DonorPath, DonorState))
	{
		return 1;
	}

	UE_LOG(LogAssetRegistryTool, Display,
		TEXT("Host assets: %d, Donor assets: %d"),
		HostState.GetNumAssets(), DonorState.GetNumAssets());

	// ---- Merged Result ----
	FAssetRegistryState MergedState;
	TSet<FSoftObjectPath> ExistingAssets;

	// 1) Always add Host assets
	HostState.EnumerateAllAssets([&](const FAssetData& Asset)
		{
			MergedState.AddAssetData(new FAssetData(Asset));
			ExistingAssets.Add(Asset.GetSoftObjectPath());
		});

	// 2) Add Donor assets (with filters and overwrite logic)
	DonorState.EnumerateAllAssets([&](const FAssetData& Asset)
		{
			// Filter: Class
			if (FilterClasses.Num() > 0 && !FilterClasses.Contains(Asset.AssetClassPath.ToString()))
			{
				return;
			}

			// Filter: Path
			if (FilterPaths.Num() > 0)
			{
				bool bPathMatches = false;
				for (const FString& P : FilterPaths)
				{
					if (bNoRecursivePaths)
					{
						if (Asset.PackagePath.ToString() == P)
						{
							bPathMatches = true;
							break;
						}
					}
					else
					{
						if (Asset.PackagePath.ToString().StartsWith(P))
						{
							bPathMatches = true;
							break;
						}
					}
				}
				if (!bPathMatches)
				{
					return;
				}
			}

			// Duplicate check
			const FSoftObjectPath AssetPath = Asset.GetSoftObjectPath();
			if (ExistingAssets.Contains(AssetPath) && !bOverwriteExistingAssets)
			{
				// skip donor asset
				return;
			}

			// Add new donor asset
			MergedState.AddAssetData(new FAssetData(Asset));
			ExistingAssets.Add(AssetPath);
		});

    // ---- Save merged state (Cooked format, UE5.4.4) ----
       // ---- Save merged state (Cooked format, UE5.4.4) ----
// ---- Save merged state (Cooked format, UE5.4.4) ----
    {
        FBufferArchive Buffer;

        FAssetRegistrySerializationOptions SaveOptions;
        SaveOptions.bSerializeAssetRegistry = true;
        SaveOptions.bSerializeDependencies = true;
        SaveOptions.bSerializePackageData = true;
        SaveOptions.bSerializeSearchableNameDependencies = true;
        SaveOptions.bSerializeManageDependencies = true;

        // Cooked 时需要过滤掉 EditorOnly
        Buffer.SetFilterEditorOnly(true);

        // ✅ 调用官方的 Save，而不是手工 Serialize
        if (!MergedState.Save(Buffer, SaveOptions))
        {
            UE_LOG(LogAssetRegistryTool, Error,
                TEXT("❌ Failed to serialize merged AssetRegistry"));
            return 1;
        }

        if (!FFileHelper::SaveArrayToFile(Buffer, *OutputPath))
        {
            UE_LOG(LogAssetRegistryTool, Error,
                TEXT("❌ Failed to save cooked AssetRegistry to %s"), *OutputPath);
            return 1;
        }

        UE_LOG(LogAssetRegistryTool, Display,
            TEXT("✅ Successfully saved cooked-style AssetRegistry to: %s (size: %d bytes)"),
            *OutputPath, Buffer.Num());
    }



	return 0;
}
