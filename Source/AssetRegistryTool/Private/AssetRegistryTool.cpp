#include "AssetRegistryTool.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/PackageReader.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include <atomic>
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Input/STextComboBox.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "Misc/PackagePath.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Input/DragAndDrop.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AssetRegistryTool"

namespace AssetRegistryToolPrivate
{
static const FName ToolTabName(TEXT("AssetRegistryTool"));

static bool LoadRegistryFile(const FString& Path, FAssetRegistryState& OutState, FString& OutError)
{
    if (Path.IsEmpty())
    {
        OutError = TEXT("AssetRegistry path is empty.");
        return false;
    }

    TArray<uint8> Data;
    if (!FFileHelper::LoadFileToArray(Data, *Path))
    {
        OutError = FString::Printf(TEXT("Failed to read file: %s"), *Path);
        return false;
    }

    FMemoryReader Reader(Data, true);
    FAssetRegistryLoadOptions LoadOptions;
    LoadOptions.bLoadDependencies = true;
    LoadOptions.bLoadPackageData = true;

    if (!OutState.Load(Reader, LoadOptions))
    {
        OutError = FString::Printf(TEXT("Failed to parse AssetRegistry: %s"), *Path);
        return false;
    }

    return true;
}

static bool SaveRegistryFile(const FString& Path, FAssetRegistryState& State, FString& OutError)
{
    if (Path.IsEmpty())
    {
        OutError = TEXT("Output path is empty.");
        return false;
    }

    FBufferArchive Buffer;
    Buffer.SetFilterEditorOnly(true);

    FAssetRegistrySerializationOptions SaveOptions;
    SaveOptions.bSerializeAssetRegistry = true;
    SaveOptions.bSerializeDependencies = true;
    SaveOptions.bSerializePackageData = true;
    SaveOptions.bSerializeSearchableNameDependencies = true;
    SaveOptions.bSerializeManageDependencies = true;

    if (!State.Save(Buffer, SaveOptions))
    {
        OutError = TEXT("Failed to serialize AssetRegistry state.");
        return false;
    }

    const FString OutputDir = FPaths::GetPath(Path);
    if (!OutputDir.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*OutputDir, true);
    }

    if (!FFileHelper::SaveArrayToFile(Buffer, *Path))
    {
        OutError = FString::Printf(TEXT("Failed to write file: %s"), *Path);
        return false;
    }

    return true;
}

static bool IsUsableProjectAsset(const FAssetData& Asset)
{
    if (!Asset.IsValid())
    {
        return false;
    }

    const FString PackagePath = Asset.PackagePath.ToString();
    if (!PackagePath.StartsWith(TEXT("/Game")))
    {
        return false;
    }

    if (Asset.AssetClassPath.GetAssetName() == FName(TEXT("ObjectRedirector")))
    {
        return false;
    }

    return true;
}

static TArray<FAssetData> GetSelectedContentBrowserAssets()
{
    TArray<FAssetData> SelectedAssets;

    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

    SelectedAssets.RemoveAll([](const FAssetData& Asset)
    {
        return !IsUsableProjectAsset(Asset);
    });

    return SelectedAssets;
}

static TArray<FAssetData> GetAllProjectAssets()
{
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
    AssetRegistry.SearchAllAssets(true);

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssetsByPath(FName(TEXT("/Game")), Assets, true, false);

    Assets.RemoveAll([](const FAssetData& Asset)
    {
        return !IsUsableProjectAsset(Asset);
    });

    return Assets;
}

static FString DefaultOutputFor(const FString& BaseRegistryPath)
{
    const FString Dir = BaseRegistryPath.IsEmpty() ? FPaths::ProjectSavedDir() : FPaths::GetPath(BaseRegistryPath);
    return FPaths::Combine(Dir, TEXT("AssetRegistry.generated.bin"));
}

static bool BuildLongPackageName(const FString& Filename, FString& OutPackageName)
{
    FString Absolute = FPaths::ConvertRelativePathToFull(Filename);
    FString ContentToken = TEXT("/Content/");
    int32 ContentIndex = Absolute.Find(ContentToken, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
    if (ContentIndex == INDEX_NONE)
    {
        ContentToken = TEXT("\\Content\\");
        ContentIndex = Absolute.Find(ContentToken, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
    }
    if (ContentIndex == INDEX_NONE)
    {
        return false;
    }

    FString Relative = Absolute.Mid(ContentIndex + ContentToken.Len());
    const FString RelativeDir = FPaths::GetPath(Relative);
    const FString RelativeBase = FPaths::GetBaseFilename(FPaths::GetCleanFilename(Relative));
    Relative = RelativeDir.IsEmpty() ? RelativeBase : FPaths::Combine(RelativeDir, RelativeBase);
    Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
    Relative.ReplaceInline(TEXT("//"), TEXT("/"));
    OutPackageName = FString::Printf(TEXT("/Game/%s"), *Relative);
    return FPackageName::IsValidLongPackageName(OutPackageName);
}

static FString ResolveCookedObjectPath(FPackageIndex Index, const FString& PackageName,
    const TArray<FObjectImport>& Imports, const TArray<FObjectExport>& Exports, int32 Depth = 0)
{
    if (Index.IsNull() || Depth > 64) return FString();
    FName Name;
    FPackageIndex Outer;
    if (Index.IsImport())
    {
        if (!Imports.IsValidIndex(Index.ToImport())) return FString();
        const FObjectImport& Object = Imports[Index.ToImport()];
        Name = Object.ObjectName;
        Outer = Object.OuterIndex;
        if (Outer.IsNull()) return Name.ToString();
    }
    else
    {
        if (!Exports.IsValidIndex(Index.ToExport())) return FString();
        const FObjectExport& Object = Exports[Index.ToExport()];
        Name = Object.ObjectName;
        Outer = Object.OuterIndex;
        if (Outer.IsNull()) return PackageName + TEXT(".") + Name.ToString();
    }
    const FString OuterPath = ResolveCookedObjectPath(Outer, PackageName, Imports, Exports, Depth + 1);
    if (OuterPath.IsEmpty()) return FString();
    return OuterPath + (OuterPath.Contains(TEXT(".")) ? TEXT(":") : TEXT(".")) + Name.ToString();
}

static void AddBlueprintMetadata(FAssetData& Asset, const FString& ParentClassPath, const FString& NativeParentClassPath)
{
    FAssetDataTagMap Tags;
    for (const TPair<FName, FAssetTagValueRef>& Existing : Asset.TagsAndValues)
    {
        Tags.Add(Existing.Key, Existing.Value.AsString());
    }

    const FName ClassName = Asset.AssetClassPath.GetAssetName();
    const FString AssetName = Asset.AssetName.ToString();
    const bool bGenerated = ClassName != FName(TEXT("Blueprint"));
    const FString BaseName = AssetName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive)
        ? AssetName.LeftChop(2) : AssetName;
    const FString GeneratedPath = Asset.PackageName.ToString() + TEXT(".") + BaseName + TEXT("_C");
    if (!ParentClassPath.IsEmpty())
    {
        const TCHAR* ParentType = ParentClassPath.StartsWith(TEXT("/Script/"))
            ? TEXT("/Script/CoreUObject.Class") : TEXT("/Script/Engine.BlueprintGeneratedClass");
        Tags.Add(FName(TEXT("ParentClass")), FString::Printf(TEXT("%s'%s'"), ParentType, *ParentClassPath));
    }
    if (!NativeParentClassPath.IsEmpty())
    {
        Tags.Add(FName(TEXT("NativeParentClass")), FString::Printf(TEXT("/Script/CoreUObject.Class'%s'"), *NativeParentClassPath));
    }
    if (bGenerated)
    {
        Tags.Add(FName(TEXT("ModuleName")), BaseName);
    }
    else
    {
        Tags.Add(FName(TEXT("BlueprintPath")), BaseName);
        Tags.Add(FName(TEXT("GeneratedClass")), FString::Printf(TEXT("/Script/Engine.BlueprintGeneratedClass'%s'"), *GeneratedPath));
    }
    if (NativeParentClassPath.EndsWith(TEXT("BaseCardObject")))
    {
        // These are the stable tags emitted by KARDS' Blueprint/PrimaryAsset
        // pipeline.  Searchable card values are added from the loaded CDO
        // below; these structural values are safe to reconstruct even when
        // the cooked package omits the editor-only Blueprint object.
        Tags.Add(FName(TEXT("PrimaryAssetName")), BaseName);
        Tags.Add(FName(TEXT("PrimaryAssetType")), TEXT("BaseCardObject"));
        Tags.Add(FName(TEXT("NativeClass")), TEXT("/Script/CoreUObject.Class'/Script/kards.BaseCardObject'"));
        Tags.Add(FName(TEXT("BlueprintType")), TEXT("BPTYPE_Normal"));
        Tags.Add(FName(TEXT("IsDataOnly")), TEXT("False"));
        Tags.Add(FName(TEXT("BlueprintComponents")), TEXT("0"));
        Tags.Add(FName(TEXT("NativeComponents")), TEXT("0"));
        Tags.Add(FName(TEXT("NumReplicatedProperties")), TEXT("0"));
        Tags.Add(FName(TEXT("ImportedNamespaces")), TEXT("()"));
    }
    Asset.SetTagsAndAssetBundles(MoveTemp(Tags));
}

static FString FindCardReaderExecutable()
{
    FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("KARDS_UASSET_CARD_READER"));
    if (!Path.IsEmpty() && IFileManager::Get().FileExists(*Path)) return Path;
    Path = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("AssetRegistryTool-main/Tools/UAssetCardReader/bin/Release/net10.0/UAssetCardReader.exe"));
    return Path;
}

static bool ReadUAssetApiCardTags(const FString& Filename, TMap<FName, FAssetDataTagMap>& OutTags, FString& OutDiagnostic,
    const FString& UsmapPath, const FString& EngineVersion, const TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe>& Cancel)
{
    const FString ReaderPath = FindCardReaderExecutable();
    if (!IFileManager::Get().FileExists(*ReaderPath))
    {
        OutDiagnostic = FString::Printf(TEXT("UAssetAPI helper not found: %s"), *ReaderPath);
        return false;
    }
    void* ReadPipe = nullptr;
    void* WritePipe = nullptr;
    if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe)) return false;
    FString Args = FString::Printf(TEXT("\"%s\""), *Filename);
    if (!UsmapPath.IsEmpty()) Args += FString::Printf(TEXT(" --usmap=\"%s\""), *UsmapPath);
    if (!EngineVersion.IsEmpty()) Args += FString::Printf(TEXT(" --engine-version=%s"), *EngineVersion);
    FProcHandle Proc = FPlatformProcess::CreateProc(*ReaderPath, *Args, false, true, true, nullptr, 0, nullptr, WritePipe, nullptr, WritePipe);
    if (!Proc.IsValid())
    {
        FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
        OutDiagnostic = TEXT("failed to start UAssetAPI helper");
        return false;
    }
    FString Output;
    const double Started = FPlatformTime::Seconds();
    bool bAborted = false;
    while (FPlatformProcess::IsProcRunning(Proc))
    {
        Output += FPlatformProcess::ReadPipe(ReadPipe);
        if ((Cancel && Cancel->load()) || FPlatformTime::Seconds() - Started > 60.0)
        {
            bAborted = true;
            FPlatformProcess::TerminateProc(Proc, true);
            FPlatformProcess::WaitForProc(Proc);
            break;
        }
        FPlatformProcess::Sleep(0.01f);
    }
    Output += FPlatformProcess::ReadPipe(ReadPipe);
    int32 ReturnCode = 0;
    FPlatformProcess::GetProcReturnCode(Proc, &ReturnCode);
    FPlatformProcess::CloseProc(Proc);
    FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
    if (bAborted)
    {
        OutDiagnostic = TEXT("UAssetAPI cancelled or exceeded 60 second timeout");
        return false;
    }
    if (ReturnCode != 0)
    {
        OutDiagnostic = FString::Printf(TEXT("UAssetAPI helper failed (%d): %s"), ReturnCode, *Output.Left(300));
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Output);
    if (!FJsonSerializer::Deserialize(JsonReader, Root) || !Root.IsValid())
    {
        OutDiagnostic = TEXT("UAssetAPI helper returned invalid JSON");
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Cards = nullptr;
    if (!Root->TryGetArrayField(TEXT("cards"), Cards)) return false;
    const bool bRequiresCdo = Root->GetBoolField(TEXT("requiresCdo"));
    int32 Added = 0;
    for (const TSharedPtr<FJsonValue>& CardValue : *Cards)
    {
        const TSharedPtr<FJsonObject> Card = CardValue->AsObject();
        if (!Card.IsValid()) continue;
        FString Name;
        if (!Card->TryGetStringField(TEXT("name"), Name)) continue;
        Name.RemoveFromStart(TEXT("Default__"));
        if (Name.EndsWith(TEXT("_C"))) Name.LeftChopInline(2);
        const TSharedPtr<FJsonObject>* Properties = nullptr;
        if (!Card->TryGetObjectField(TEXT("properties"), Properties) || !Properties->IsValid()) continue;
        FAssetDataTagMap& Tags = OutTags.FindOrAdd(FName(*Name));
        for (const auto& Pair : (*Properties)->Values)
        {
            FString Value;
            if (Pair.Value->TryGetString(Value))
            {
                Tags.Add(FName(*Pair.Key), Value);
                ++Added;
            }
        }
    }
    OutDiagnostic = FString::Printf(TEXT("UAssetAPI properties=%d%s"), Added, bRequiresCdo ? TEXT("") : TEXT("; no CDO required"));
    return true;
}

static bool ReadCookedAsset(const FString& Filename, TArray<FAssetData>& OutAssets, FString& OutMessage,
    const FString& UsmapPath = FString(), const FString& EngineVersion = TEXT("VER_UE5_6"),
    const TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe>& Cancel = nullptr)
{
    OutAssets.Reset();
    FString PackageName;
    if (!BuildLongPackageName(Filename, PackageName))
    {
        OutMessage = TEXT("Cooked asset must be below a Content directory so its /Game package path can be inferred.");
        return false;
    }

    TUniquePtr<FPackageReader> Reader = MakeUnique<FPackageReader>();
    FPackageReader::EOpenPackageResult Result;
    // KARDS cooked packages are commonly saved with SAVE_Unversioned_Native.
    // FPackageReader otherwise rejects them before it can reconstruct the
    // cooked AssetRegistry section.  Scope the editor opt-in to this read;
    // this is metadata-only parsing and does not load or mutate a UObject.
    {
        TGuardValue<int32> AllowUnversionedContent(GAllowUnversionedContentInEditor, 1);
        if (!Reader->OpenPackageFile(PackageName, Filename, &Result))
        {
            OutMessage = FString::Printf(TEXT("Could not open cooked package (%s): %s"), *Filename, LexToString(Result));
            return false;
        }
    }

    TArray<FAssetData*> Parsed;
    bool bCookedWithoutAssetData = false;
    const bool bReadAssetRegistryData = Reader->ReadAssetRegistryData(Parsed, bCookedWithoutAssetData);
    if (!bReadAssetRegistryData)
    {
        for (FAssetData* Asset : Parsed) delete Asset;
        Parsed.Reset();
        OutMessage = FString::Printf(TEXT("No embedded AssetRegistry data in %s; using package-path fallback where possible."), *Filename);
    }

    for (FAssetData* Asset : Parsed)
    {
        if (Asset)
        {
            OutAssets.Add(*Asset);
            delete Asset;
        }
    }

    TArray<FObjectImport> Imports;
    TArray<FObjectExport> Exports;
    if (!Reader->GetImports(Imports) || !Reader->GetExports(Exports))
    {
        OutAssets.Reset();
        OutMessage = TEXT("Could not parse cooked import/export tables; no guessed metadata emitted.");
        return false;
    }
    Reader.Reset();
    TMap<FName, FString> ParentPaths;
    TMap<FName, FString> NativeParentPaths;
    for (const FObjectExport& Export : Exports)
    {
        const FString ClassPath = ResolveCookedObjectPath(Export.ClassIndex, PackageName, Imports, Exports);
        if (!ClassPath.EndsWith(TEXT(".BlueprintGeneratedClass")) &&
            !ClassPath.EndsWith(TEXT(".WidgetBlueprintGeneratedClass"))) continue;
        const FString Parent = ResolveCookedObjectPath(Export.SuperIndex, PackageName, Imports, Exports);
        if (Parent.IsEmpty())
        {
            OutAssets.Reset();
            OutMessage = TEXT("Generated-class SuperIndex could not be resolved.");
            return false;
        }
        ParentPaths.Add(Export.ObjectName, Parent);
        FString NativeParent = Parent.StartsWith(TEXT("/Script/")) ? Parent : FString();
        FPackageIndex Super = Export.SuperIndex;
        TSet<int32> Visited;
        while (Super.IsExport() && Exports.IsValidIndex(Super.ToExport()) && !Visited.Contains(Super.ToExport()))
        {
            Visited.Add(Super.ToExport());
            Super = Exports[Super.ToExport()].SuperIndex;
            const FString SuperPath = ResolveCookedObjectPath(Super, PackageName, Imports, Exports);
            if (SuperPath.StartsWith(TEXT("/Script/"))) NativeParent = SuperPath;
        }
        NativeParentPaths.Add(Export.ObjectName, NativeParent);
    }
    for (FAssetData& Asset : OutAssets)
    {
        const FName ClassName = Asset.AssetClassPath.GetAssetName();
        if (ClassName != FName(TEXT("Blueprint")) && ClassName != FName(TEXT("BlueprintGeneratedClass")) && ClassName != FName(TEXT("WidgetBlueprintGeneratedClass")))
        {
            continue;
        }
        const FName GeneratedName = ClassName == FName(TEXT("Blueprint"))
            ? FName(*(Asset.AssetName.ToString() + TEXT("_C"))) : Asset.AssetName;
        AddBlueprintMetadata(Asset, ParentPaths.FindRef(GeneratedName), NativeParentPaths.FindRef(GeneratedName));
        if (ClassName == FName(TEXT("Blueprint")) || ClassName == FName(TEXT("BlueprintGeneratedClass")))
        {
            // Cooked package flags contain storage/editor bits.  The shipped
            // KARDS registry uses the cooked Blueprint asset flag instead.
            if (Asset.PackageFlags & (PKG_Cooked | PKG_FilterEditorOnly))
            {
                Asset.PackageFlags = PKG_RequiresLocalizationGather;
            }
        }
    }

    TMap<FName, FAssetDataTagMap> UAssetApiTagsByBlueprintName;
    FString UAssetApiDiagnostic;
    const bool bUAssetApiTags = ParentPaths.IsEmpty() ? false : ReadUAssetApiCardTags(Filename, UAssetApiTagsByBlueprintName, UAssetApiDiagnostic, UsmapPath, EngineVersion, Cancel);
    if (!ParentPaths.IsEmpty() && !bUAssetApiTags)
    {
        OutAssets.Reset();
        OutMessage = UAssetApiDiagnostic;
        return false;
    }
    for (FAssetData& Asset : OutAssets)
    {
        if (Asset.AssetClassPath.GetAssetName() != FName(TEXT("Blueprint"))) continue;
        if (const FAssetDataTagMap* CardTags = UAssetApiTagsByBlueprintName.Find(Asset.AssetName))
        {
            FAssetDataTagMap Tags;
            for (const TPair<FName, FAssetTagValueRef>& Pair : Asset.TagsAndValues) Tags.Add(Pair.Key, Pair.Value.AsString());
            for (const TPair<FName, FString>& Pair : *CardTags) Tags.Add(Pair.Key, Pair.Value);
            Asset.SetTagsAndAssetBundles(MoveTemp(Tags));
        }
    }

    // When the cooked package can be loaded in the editor, ask the actual
    // object/CDO for its AssetRegistry tags.  This is the authoritative path
    // for AssetRegistrySearchable properties (cardSet, faction, title, Image,
    // etc.); the package reader alone only exposes the structural records.
    TMap<FName, FAssetDataTagMap> CdoTagsByBlueprintName = UAssetApiTagsByBlueprintName;
    int32 CdoTagObjectCount = 0;
    int32 CdoTagCount = 0;
    if (false)
    {
        const FPackagePath LocalPackagePath = FPackagePath::FromLocalPath(Filename);
        TGuardValue<int32> AllowUnversionedForPackageLoad(GAllowUnversionedContentInEditor, 1);
        UPackage* LoadedPackage = LoadPackage(nullptr, LocalPackagePath, LOAD_NoWarn | LOAD_Quiet);
        if (LoadedPackage)
        {
            for (FAssetData& Asset : OutAssets)
            {
                UObject* Object = StaticFindObject(nullptr, LoadedPackage, *Asset.AssetName.ToString());
                if (!Object)
                {
                    Object = StaticFindObject(nullptr, nullptr, *Asset.GetObjectPathString());
                }
                if (!Object && Asset.AssetClassPath.GetAssetName() == FName(TEXT("Blueprint")))
                {
                    Object = StaticFindObject(nullptr, LoadedPackage, *Asset.AssetName.ToString());
                }
                if (!Object)
                {
                    continue;
                }
                FAssetData RuntimeTags;
                Object->GetAssetRegistryTags(RuntimeTags);
                if (RuntimeTags.TagsAndValues.Num() > 0)
                {
                    FAssetDataTagMap Tags;
                    for (const TPair<FName, FAssetTagValueRef>& Pair : Asset.TagsAndValues)
                    {
                        Tags.Add(Pair.Key, Pair.Value.AsString());
                    }
                    for (const TPair<FName, FAssetTagValueRef>& Pair : RuntimeTags.TagsAndValues)
                    {
                        Tags.Add(Pair.Key, Pair.Value.AsString());
                    }
                    Asset.SetTagsAndAssetBundles(MoveTemp(Tags));
                }
                if (Asset.AssetClassPath.GetAssetName() == FName(TEXT("BlueprintGeneratedClass")))
                {
                    if (UClass* GeneratedClass = Cast<UClass>(Object))
                    {
                        if (UObject* CDO = GeneratedClass->GetDefaultObject())
                        {
                            FAssetData CdoTags;
                            CDO->GetAssetRegistryTags(CdoTags);
                            FAssetDataTagMap CdoTagMap;
                            for (const TPair<FName, FAssetTagValueRef>& Pair : CdoTags.TagsAndValues)
                            {
                                CdoTagMap.Add(Pair.Key, Pair.Value.AsString());
                            }
                            FString BlueprintName = Asset.AssetName.ToString();
                            if (BlueprintName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
                            {
                                BlueprintName.LeftChopInline(2);
                                CdoTagsByBlueprintName.Add(FName(*BlueprintName), MoveTemp(CdoTagMap));
                                ++CdoTagObjectCount;
                                CdoTagCount += CdoTags.TagsAndValues.Num();
                            }
                            FAssetDataTagMap Tags;
                            for (const TPair<FName, FAssetTagValueRef>& Pair : Asset.TagsAndValues)
                            {
                                Tags.Add(Pair.Key, Pair.Value.AsString());
                            }
                            for (const TPair<FName, FAssetTagValueRef>& Pair : CdoTags.TagsAndValues)
                            {
                                Tags.Add(Pair.Key, Pair.Value.AsString());
                            }
                            Asset.SetTagsAndAssetBundles(MoveTemp(Tags));
                        }
                    }
                }
            }
            // Cooked packages often retain the Blueprint record but put the
            // searchable CDO values on the generated class.  The shipped
            // KARDS registries keep those values on the Blueprint record, so
            // copy the CDO map to its paired Blueprint entry.
            for (FAssetData& Asset : OutAssets)
            {
                if (Asset.AssetClassPath.GetAssetName() != FName(TEXT("Blueprint")))
                {
                    continue;
                }
                if (const FAssetDataTagMap* CdoMap = CdoTagsByBlueprintName.Find(Asset.AssetName))
                {
                    FAssetDataTagMap Tags;
                    for (const TPair<FName, FAssetTagValueRef>& Pair : Asset.TagsAndValues)
                    {
                        Tags.Add(Pair.Key, Pair.Value.AsString());
                    }
                    for (const TPair<FName, FString>& Pair : *CdoMap)
                    {
                        Tags.Add(Pair.Key, Pair.Value);
                    }
                    Asset.SetTagsAndAssetBundles(MoveTemp(Tags));
                }
            }
        }
    }

    // Cooked Blueprint packages commonly retain only the generated class in
    // their embedded asset-registry data.  The game still expects the paired
    // Blueprint object to be discoverable, so synthesize that editor-only
    // registry record from the generated class name.  This does not create or
    // modify a UObject; it only adds metadata to the generated registry.
    TArray<FAssetData> SyntheticBlueprints;
    TSet<FSoftObjectPath> ExistingPaths;
    for (const FAssetData& Asset : OutAssets)
    {
        ExistingPaths.Add(Asset.GetSoftObjectPath());
    }
    for (const FAssetData& Asset : OutAssets)
    {
        const FName ClassName = Asset.AssetClassPath.GetAssetName();
        if (ClassName != FName(TEXT("BlueprintGeneratedClass")) &&
            ClassName != FName(TEXT("WidgetBlueprintGeneratedClass")))
        {
            continue;
        }

        FString GeneratedName = Asset.AssetName.ToString();
        if (!GeneratedName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
        {
            continue;
        }
        GeneratedName.LeftChopInline(2);
        const FSoftObjectPath BlueprintPath(
            FString::Printf(TEXT("%s.%s"), *Asset.PackageName.ToString(), *GeneratedName));
        if (ExistingPaths.Contains(BlueprintPath))
        {
            continue;
        }

        FAssetData BlueprintAsset;
        BlueprintAsset.PackageName = Asset.PackageName;
        BlueprintAsset.PackagePath = Asset.PackagePath;
        BlueprintAsset.AssetName = FName(*GeneratedName);
        BlueprintAsset.AssetClassPath = FTopLevelAssetPath(FName(TEXT("/Script/Engine")), FName(TEXT("Blueprint")));
        AddBlueprintMetadata(BlueprintAsset, ParentPaths.FindRef(Asset.AssetName), NativeParentPaths.FindRef(Asset.AssetName));
        if (const FAssetDataTagMap* CdoMap = CdoTagsByBlueprintName.Find(BlueprintAsset.AssetName))
        {
            FAssetDataTagMap Tags;
            for (const TPair<FName, FAssetTagValueRef>& Pair : BlueprintAsset.TagsAndValues)
            {
                Tags.Add(Pair.Key, Pair.Value.AsString());
            }
            for (const TPair<FName, FString>& Pair : *CdoMap)
            {
                Tags.Add(Pair.Key, Pair.Value);
            }
            BlueprintAsset.SetTagsAndAssetBundles(MoveTemp(Tags));
        }
        BlueprintAsset.PackageFlags = (Asset.AssetClassPath.GetAssetName() == FName(TEXT("BlueprintGeneratedClass")))
            ? PKG_RequiresLocalizationGather : Asset.PackageFlags;
        BlueprintAsset.SetChunkIDs(Asset.GetChunkIDs());
        SyntheticBlueprints.Add(MoveTemp(BlueprintAsset));
        ExistingPaths.Add(BlueprintPath);
    }
    OutAssets.Append(MoveTemp(SyntheticBlueprints));

    // Only reconstruct records for generated classes proven by the export table.
    if (OutAssets.IsEmpty() && !ParentPaths.IsEmpty())
    {
        for (const auto& Parent : ParentPaths)
        {
            const FString GeneratedName = Parent.Key.ToString();
            if (!GeneratedName.EndsWith(TEXT("_C"))) continue;
            const FString AssetName = GeneratedName.LeftChop(2);
            const FName PackageFName(*PackageName);
            const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);
            FAssetData BlueprintAsset;
            BlueprintAsset.PackageName = PackageFName;
            BlueprintAsset.PackagePath = FName(*PackagePath);
            BlueprintAsset.AssetName = FName(*AssetName);
            BlueprintAsset.AssetClassPath = FTopLevelAssetPath(FName(TEXT("/Script/Engine")), FName(TEXT("Blueprint")));
            BlueprintAsset.PackageFlags = PKG_RequiresLocalizationGather;
            AddBlueprintMetadata(BlueprintAsset, Parent.Value, NativeParentPaths.FindRef(Parent.Key));
            if (const FAssetDataTagMap* CdoMap = CdoTagsByBlueprintName.Find(BlueprintAsset.AssetName))
            {
                FAssetDataTagMap Tags;
                for (const TPair<FName, FAssetTagValueRef>& Pair : BlueprintAsset.TagsAndValues)
                {
                    Tags.Add(Pair.Key, Pair.Value.AsString());
                }
                for (const TPair<FName, FString>& Pair : *CdoMap)
                {
                    Tags.Add(Pair.Key, Pair.Value);
                }
                BlueprintAsset.SetTagsAndAssetBundles(MoveTemp(Tags));
            }
            OutAssets.Add(BlueprintAsset);

            FAssetData GeneratedAsset = BlueprintAsset;
            GeneratedAsset.AssetName = Parent.Key;
            GeneratedAsset.AssetClassPath = FTopLevelAssetPath(FName(TEXT("/Script/Engine")), FName(TEXT("BlueprintGeneratedClass")));
            AddBlueprintMetadata(GeneratedAsset, ParentPaths.FindRef(GeneratedAsset.AssetName), NativeParentPaths.FindRef(GeneratedAsset.AssetName));
            OutAssets.Add(GeneratedAsset);
        }
    }

    const FString Uexp = FPaths::ChangeExtension(Filename, TEXT("uexp"));
    const FString Ubulk = FPaths::ChangeExtension(Filename, TEXT("ubulk"));
    const bool bHasUexp = IFileManager::Get().FileExists(*Uexp);
    const bool bHasUbulk = IFileManager::Get().FileExists(*Ubulk);
    OutMessage = FString::Printf(TEXT("%s: %d asset(s), UAssetAPI %s, CDO tags %d/%d%s%s"), *FPaths::GetCleanFilename(Filename), OutAssets.Num(), *UAssetApiDiagnostic, CdoTagObjectCount, CdoTagCount,
        bCookedWithoutAssetData ? TEXT(" (synthesized from cooked exports)") : TEXT(""),
        bHasUexp ? (bHasUbulk ? TEXT("") : TEXT("; matching .ubulk not found")) : TEXT("; matching .uexp not found"));
    return !OutAssets.IsEmpty();
}

static bool BuildCookedFilenameForPackage(const FString& SourceFilename, const FName& PackageName, FString& OutFilename)
{
    const FString PackageString = PackageName.ToString();
    if (!PackageString.StartsWith(TEXT("/Game/")))
    {
        return false;
    }

    FString AbsoluteSource = FPaths::ConvertRelativePathToFull(SourceFilename);
    int32 ContentIndex = AbsoluteSource.Find(TEXT("/Content/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
    int32 ContentTokenLength = FCString::Strlen(TEXT("/Content/"));
    if (ContentIndex == INDEX_NONE)
    {
        ContentIndex = AbsoluteSource.Find(TEXT("\\Content\\"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
        ContentTokenLength = FCString::Strlen(TEXT("\\Content\\"));
    }
    if (ContentIndex == INDEX_NONE)
    {
        return false;
    }

    FString Relative = PackageString.Mid(FCString::Strlen(TEXT("/Game/")));
    Relative.ReplaceInline(TEXT("/"), TEXT("\\"));
    const FString ContentRoot = AbsoluteSource.Left(ContentIndex + ContentTokenLength);
    OutFilename = FPaths::Combine(ContentRoot, Relative + TEXT(".uasset"));
    return IFileManager::Get().FileExists(*OutFilename);
}

static void CollectCookedFiles(const TArray<FString>& InFiles, TArray<FString>& OutFiles)
{
    for (const FString& File : InFiles)
    {
        const FString Extension = FPaths::GetExtension(File);
        if (Extension.Equals(TEXT("uasset"), ESearchCase::IgnoreCase))
        {
            OutFiles.AddUnique(FPaths::ConvertRelativePathToFull(File));
        }
        else if (Extension.Equals(TEXT("uexp"), ESearchCase::IgnoreCase))
        {
            const FString UAsset = FPaths::ChangeExtension(File, TEXT("uasset"));
            if (IFileManager::Get().FileExists(*UAsset))
            {
                OutFiles.AddUnique(FPaths::ConvertRelativePathToFull(UAsset));
            }
        }
    }
}

class SAssetRegistryToolWidget final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAssetRegistryToolWidget) {}
    SLATE_END_ARGS()

    ~SAssetRegistryToolWidget()
    {
        if (CancelImport) CancelImport->store(true);
        if (ImportFuture.IsValid()) ImportFuture.Wait();
    }

    void Construct(const FArguments& InArgs)
    {
        GConfig->GetString(TEXT("AssetRegistryTool"), TEXT("UsmapPath"), UsmapPath, GEditorPerProjectIni);
        GConfig->GetString(TEXT("AssetRegistryTool"), TEXT("EngineVersion"), EngineVersion, GEditorPerProjectIni);
        for (int32 Minor = 18; Minor <= 27; ++Minor) EngineOptions.Add(MakeShared<FString>(FString::Printf(TEXT("VER_UE4_%d"), Minor)));
        for (int32 Minor = 0; Minor <= 8; ++Minor) EngineOptions.Add(MakeShared<FString>(FString::Printf(TEXT("VER_UE5_%d"), Minor)));
        if (!EngineOptions.ContainsByPredicate([this](const TSharedPtr<FString>& Item) { return *Item == EngineVersion; })) EngineVersion = TEXT("VER_UE5_6");
        StatusText = TEXT("Choose a base AssetRegistry.bin, then generate from selected Content Browser assets or all /Game assets.");

        ChildSlot
        [
            SNew(SBorder)
            .Padding(12.0f)
            [
                SNew(SScrollBox)

                + SScrollBox::Slot()
                .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("Title", "Asset Registry Tool"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                [
                    SNew(STextBlock)
                    .AutoWrapText(true)
                    .Text(LOCTEXT("Description", "Rebuild a new AssetRegistry.bin by loading a base registry and adding selected project assets from the current editor project."))
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [
                    BuildPathRow(
                        LOCTEXT("BaseRegistry", "Base AssetRegistry.bin"),
                        LOCTEXT("BrowseBase", "Browse..."),
                        FOnClicked::CreateSP(this, &SAssetRegistryToolWidget::BrowseBaseRegistry),
                        [this]() { return BaseRegistryPath; },
                        [this](const FString& NewPath) { BaseRegistryPath = NewPath; })
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [
                    BuildPathRow(
                        LOCTEXT("OutputRegistry", "Output AssetRegistry.bin"),
                        LOCTEXT("BrowseOutput", "Save As..."),
                        FOnClicked::CreateSP(this, &SAssetRegistryToolWidget::BrowseOutputRegistry),
                        [this]() { return OutputRegistryPath; },
                        [this](const FString& NewPath) { OutputRegistryPath = NewPath; })
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [
                        BuildPathRow(LOCTEXT("UsmapPath", "UAssetAPI .usmap"), LOCTEXT("BrowseUsmap", "Browse..."),
                            FOnClicked::CreateSP(this, &SAssetRegistryToolWidget::BrowseUsmap),
                            [this]() { return UsmapPath; },
                            [this](const FString& Value) { if (!bImporting) { UsmapPath = Value; SaveParserSettings(); } })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                    [ SNew(STextBlock).Text(LOCTEXT("SourceVersion", "Cooked source engine version")) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [
                        SNew(STextComboBox).OptionsSource(&EngineOptions)
                        .InitiallySelectedItem(EngineOptions.ContainsByPredicate([this](const TSharedPtr<FString>& Item) { return *Item == EngineVersion; })
                            ? *EngineOptions.FindByPredicate([this](const TSharedPtr<FString>& Item) { return *Item == EngineVersion; }) : EngineOptions.Last())
                        .IsEnabled_Lambda([this]() { return !bImporting; })
                        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type) { if (Item) { EngineVersion = *Item; SaveParserSettings(); } })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [
                        SNew(SButton).Text(LOCTEXT("CancelImport", "Cancel Import"))
                        .IsEnabled_Lambda([this]() { return bImporting; })
                        .OnClicked_Lambda([this]() { if (CancelImport) CancelImport->store(true); return FReply::Handled(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
                    [
                        SNew(STextBlock).Text(LOCTEXT("CookedAssets", "Cooked assets"))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f)
                        [
                            SNew(SEditableTextBox)
                            .Text_Lambda([this]() { return FText::FromString(CookedAssetsPath); })
                            .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { CookedAssetsPath = Text.ToString(); })
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f)
                        [
                            SNew(SButton)
                            .Text(LOCTEXT("BrowseCooked", "Add .uasset..."))
                        .IsEnabled_Lambda([this]() { return !bImporting; })
                            .OnClicked(this, &SAssetRegistryToolWidget::BrowseCookedAsset)
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() { return FText::FromString(CookedAssetsStatus); })
                        .AutoWrapText(true)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("BrowseCookedFolder", "Add cooked folder..."))
                        .IsEnabled_Lambda([this]() { return !bImporting; })
                        .OnClicked(this, &SAssetRegistryToolWidget::BrowseCookedFolder)
                    ]
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() { return bOverwriteExistingAssets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bOverwriteExistingAssets = (NewState == ECheckBoxState::Checked); })
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("OverwriteExisting", "Overwrite assets already present in the base registry"))
                    ]
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() { return bCookedOverridesProjectAssets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bCookedOverridesProjectAssets = (NewState == ECheckBoxState::Checked); })
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("CookedOverridesProject", "Cooked assets override project assets with the same path"))
                    ]
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                [
                    SNew(SUniformGridPanel)
                    .SlotPadding(FMargin(4.0f, 0.0f))

                    + SUniformGridPanel::Slot(0, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("GenerateSelected", "Generate From Selected Assets"))
                        .IsEnabled_Lambda([this]() { return !bImporting; })
                        .OnClicked(this, &SAssetRegistryToolWidget::GenerateFromSelectedAssets)
                    ]

                    + SUniformGridPanel::Slot(1, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("GenerateAll", "Generate From All /Game Assets"))
                        .IsEnabled_Lambda([this]() { return !bImporting; })
                        .OnClicked(this, &SAssetRegistryToolWidget::GenerateFromAllAssets)
                    ]
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() { return FText::FromString(StatusText); })
                    .AutoWrapText(true)
                ]
            ]
        ];
    }

    virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
    {
        const TSharedPtr<FExternalDragOperation> ExternalDrag = DragDropEvent.GetOperationAs<FExternalDragOperation>();
        if (ExternalDrag.IsValid() && ExternalDrag->HasFiles())
        {
            for (const FString& File : ExternalDrag->GetFiles())
            {
                const FString Extension = FPaths::GetExtension(File);
                if (Extension.Equals(TEXT("uasset"), ESearchCase::IgnoreCase) || Extension.Equals(TEXT("uexp"), ESearchCase::IgnoreCase))
                {
                    return FReply::Handled();
                }
            }
        }
        return FReply::Unhandled();
    }

    virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
    {
        const TSharedPtr<FExternalDragOperation> ExternalDrag = DragDropEvent.GetOperationAs<FExternalDragOperation>();
        if (!ExternalDrag.IsValid() || !ExternalDrag->HasFiles())
        {
            return FReply::Unhandled();
        }
        TArray<FString> Files;
        CollectCookedFiles(ExternalDrag->GetFiles(), Files);
        ImportCookedFiles(Files);
        return Files.IsEmpty() ? FReply::Unhandled() : FReply::Handled();
    }

private:
    void SaveParserSettings()
    {
        GConfig->SetString(TEXT("AssetRegistryTool"), TEXT("UsmapPath"), *UsmapPath, GEditorPerProjectIni);
        GConfig->SetString(TEXT("AssetRegistryTool"), TEXT("EngineVersion"), *EngineVersion, GEditorPerProjectIni);
        GConfig->Flush(false, GEditorPerProjectIni);
    }

    FReply BrowseUsmap()
    {
        if (bImporting) return FReply::Handled();
        TArray<FString> Files;
        if (IDesktopPlatform* Desktop = FDesktopPlatformModule::Get())
        {
            if (Desktop->OpenFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr), TEXT("Choose .usmap"),
                FPaths::GetPath(UsmapPath), TEXT(""), TEXT("Mappings (*.usmap)|*.usmap"), EFileDialogFlags::None, Files) && !Files.IsEmpty())
            {
                UsmapPath = Files[0]; SaveParserSettings();
            }
        }
        return FReply::Handled();
    }

    TSharedRef<SWidget> BuildPathRow(
        const FText& Label,
        const FText& ButtonText,
        FOnClicked BrowseAction,
        TFunction<FString()> GetPath,
        TFunction<void(const FString&)> SetPath)
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                SNew(STextBlock)
                .Text(Label)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([GetPath]() { return FText::FromString(GetPath()); })
                    .OnTextCommitted_Lambda([SetPath](const FText& NewText, ETextCommit::Type) { SetPath(NewText.ToString()); })
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(ButtonText)
                    .OnClicked(BrowseAction)
                ]
            ];
    }

    FReply BrowseBaseRegistry()
    {
        TArray<FString> Files;
        if (OpenFileDialog(LOCTEXT("ChooseBaseRegistry", "Choose AssetRegistry.bin"), Files))
        {
            BaseRegistryPath = Files[0];
            if (OutputRegistryPath.IsEmpty())
            {
                OutputRegistryPath = DefaultOutputFor(BaseRegistryPath);
            }
            StatusText = FString::Printf(TEXT("Base registry: %s"), *BaseRegistryPath);
        }
        return FReply::Handled();
    }

    FReply BrowseOutputRegistry()
    {
        TArray<FString> Files;
        if (SaveFileDialog(LOCTEXT("ChooseOutputRegistry", "Save new AssetRegistry.bin"), Files))
        {
            OutputRegistryPath = Files[0];
            StatusText = FString::Printf(TEXT("Output registry: %s"), *OutputRegistryPath);
        }
        return FReply::Handled();
    }

    FReply BrowseCookedAsset()
    {
        TArray<FString> Files;
        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
        if (!DesktopPlatform)
        {
            return FReply::Handled();
        }
        const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
        if (DesktopPlatform->OpenFileDialog(ParentWindow, LOCTEXT("ChooseCookedAsset", "Choose cooked .uasset" ).ToString(),
            FPaths::ProjectDir(), TEXT("CookedAsset.uasset"), TEXT("Cooked assets (*.uasset)|*.uasset|All files (*.*)|*.*"), EFileDialogFlags::Multiple, Files))
        {
            ImportCookedFiles(Files);
        }
        return FReply::Handled();
    }

    FReply BrowseCookedFolder()
    {
        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
        if (!DesktopPlatform)
        {
            return FReply::Handled();
        }
        const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
        FString Folder;
        if (DesktopPlatform->OpenDirectoryDialog(ParentWindow, LOCTEXT("ChooseCookedFolder", "Choose cooked asset folder").ToString(), FPaths::ProjectDir(), Folder))
        {
            TArray<FString> Files;
            IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.uasset"), true, false);
            ImportCookedFiles(Files);
        }
        return FReply::Handled();
    }

    void ImportCookedFiles(const TArray<FString>& InFiles)
    {
        if (bImporting) return;
        TArray<FString> Files;
        CollectCookedFiles(InFiles, Files);
        if (!bApplyingImport)
        {
            if (Files.IsEmpty()) return;
            if (!UsmapPath.IsEmpty() && !IFileManager::Get().FileExists(*UsmapPath))
            {
                CookedAssetsStatus = TEXT("The configured .usmap does not exist."); return;
            }
            bImporting = true;
            CancelImport = MakeShared<std::atomic_bool, ESPMode::ThreadSafe>(false);
            const auto Cancel = CancelImport;
            const FString Mapping = UsmapPath, Version = EngineVersion;
            const TWeakPtr<SAssetRegistryToolWidget> Weak = SharedThis(this);
            ImportFuture = Async(EAsyncExecution::Thread, [Weak, Files, Cancel, Mapping, Version]()
            {
                TMap<FString, TArray<FAssetData>> Results;
                TMap<FString, FString> Messages;
                TArray<FString> Processed;
                for (const FString& File : Files)
                {
                    if (Cancel->load()) break;
                    TArray<FAssetData> Assets;
                    FString Message;
                    ReadCookedAsset(File, Assets, Message, Mapping, Version, Cancel);
                    if (Cancel->load()) break;
                    Processed.Add(File);
                    Results.Add(File, MoveTemp(Assets)); Messages.Add(File, Message);
                    const int32 Done = Processed.Num(), Total = Files.Num();
                    AsyncTask(ENamedThreads::GameThread, [Weak, Done, Total]()
                    {
                        if (auto Widget = Weak.Pin()) Widget->CookedAssetsStatus = FString::Printf(TEXT("Importing %d / %d"), Done, Total);
                    });
                }
                AsyncTask(ENamedThreads::GameThread, [Weak, Processed, Cancel, Results = MoveTemp(Results), Messages = MoveTemp(Messages)]() mutable
                {
                    if (auto Widget = Weak.Pin())
                    {
                        Widget->bImporting = false;
                        Widget->ParsedImportAssets = MoveTemp(Results); Widget->ParsedImportMessages = MoveTemp(Messages);
                        Widget->bApplyingImport = true; Widget->ImportCookedFiles(Processed); Widget->bApplyingImport = false;
                        Widget->ParsedImportAssets.Reset(); Widget->ParsedImportMessages.Reset();
                        if (Cancel->load()) Widget->CookedAssetsStatus += TEXT(" Cancelled; completed files retained.");
                    }
                });
            });
            CookedAssetsStatus = FString::Printf(TEXT("Importing 0 / %d"), Files.Num());
            return;
        }
        ImportedCookedReports.Reset();
        int32 Added = 0;
        int32 Failed = 0;
        int32 MissingUexp = 0;
        int32 MissingUbulk = 0;
        TArray<FString> Errors;
        for (const FString& File : Files)
        {
            FString Message;
            TArray<FAssetData> RootAssets;
            RootAssets = ParsedImportAssets.FindRef(File);
            Message = ParsedImportMessages.FindRef(File);
            if (RootAssets.IsEmpty())
            {
                ++Failed;
                Errors.Add(Message);
                ImportedCookedReports.Add(FString::Printf(TEXT("FAIL\t%s\t%s"), *File, *Message));
                continue;
            }
            // The imported .uasset set is the source of truth.  Do not walk
            // soft-package references here; those are dependencies, not user
            // imports, and silently expanding them makes the registry contain
            // assets that were never selected.
            TArray<FAssetData> Parsed = MoveTemp(RootAssets);
            if (!Parsed.IsEmpty())
            {
                if (!IFileManager::Get().FileExists(*FPaths::ChangeExtension(File, TEXT("uexp"))))
                {
                    ++MissingUexp;
                }
                if (!IFileManager::Get().FileExists(*FPaths::ChangeExtension(File, TEXT("ubulk"))))
                {
                    ++MissingUbulk;
                }
                for (const FAssetData& Asset : Parsed)
                {
                    const FSoftObjectPath Path = Asset.GetSoftObjectPath();
                    if (!CookedAssetPaths.Contains(Path))
                    {
                        CookedAssets.Add(Asset);
                        CookedAssetPaths.Add(Path);
                        ++Added;
                    }
                    else if (FAssetData* Existing = CookedAssets.FindByPredicate([&Path](const FAssetData& Entry) { return Entry.GetSoftObjectPath() == Path; }))
                    {
                        *Existing = Asset;
                    }
                }
                CookedAssetsPath = File;
                FString Paths;
                for (const FAssetData& Asset : Parsed)
                {
                    if (!Paths.IsEmpty()) Paths += TEXT("|");
                    Paths += Asset.GetSoftObjectPath().ToString();
                }
            ImportedCookedReports.Add(FString::Printf(TEXT("OK\t%s\t%d\t%s\t%s"), *File, Parsed.Num(), *Message, *Paths));
            }
            else
            {
                ++Failed;
                Errors.Add(Message);
                ImportedCookedReports.Add(FString::Printf(TEXT("FAIL\t%s\t%s"), *File, *Message));
            }
        }
        CookedAssetsStatus = FString::Printf(TEXT("Cooked files: %d, assets added: %d, failed: %d, missing .uexp: %d, missing .ubulk: %d."), Files.Num(), Added, Failed, MissingUexp, MissingUbulk);
        if (!Errors.IsEmpty())
        {
            CookedAssetsStatus += FString::Printf(TEXT(" %s"), *FString::Join(Errors, TEXT(" | ")));
        }
    }

    FReply GenerateFromSelectedAssets()
    {
        if (bImporting) return FReply::Handled();
        return Generate(GetSelectedContentBrowserAssets(), TEXT("selected Content Browser assets"));
    }

    FReply GenerateFromAllAssets()
    {
        if (bImporting) return FReply::Handled();
        return Generate(GetAllProjectAssets(), TEXT("all /Game assets"));
    }

    FReply Generate(const TArray<FAssetData>& ProjectAssets, const TCHAR* SourceDescription)
    {
        if (BaseRegistryPath.IsEmpty())
        {
            return Fail(TEXT("Choose a base AssetRegistry.bin first."));
        }
        if (OutputRegistryPath.IsEmpty())
        {
            OutputRegistryPath = DefaultOutputFor(BaseRegistryPath);
        }
        TArray<FAssetData> AllAssets = ProjectAssets;
        AllAssets.Append(CookedAssets);
        if (AllAssets.IsEmpty())
        {
            return Fail(FString::Printf(TEXT("No usable project assets found from %s."), SourceDescription));
        }

        FString Error;
        FAssetRegistryState BaseState;
        if (!LoadRegistryFile(BaseRegistryPath, BaseState, Error))
        {
            return Fail(Error);
        }

        TSet<FSoftObjectPath> SelectedPaths;
        for (const FAssetData& Asset : AllAssets)
        {
            SelectedPaths.Add(Asset.GetSoftObjectPath());
        }
        TSet<FSoftObjectPath> CookedPaths;
        for (const FAssetData& Asset : CookedAssets)
        {
            CookedPaths.Add(Asset.GetSoftObjectPath());
        }

        FAssetRegistryState NewState;
        TSet<FSoftObjectPath> ExistingPaths;
        int32 CopiedBaseAssets = 0;
        int32 SkippedBaseAssets = 0;

        BaseState.EnumerateAllAssets([&](const FAssetData& Asset)
        {
            const FSoftObjectPath AssetPath = Asset.GetSoftObjectPath();
            if ((bOverwriteExistingAssets || (bCookedOverridesProjectAssets && CookedPaths.Contains(AssetPath))) && SelectedPaths.Contains(AssetPath))
            {
                ++SkippedBaseAssets;
                return;
            }

            NewState.AddAssetData(new FAssetData(Asset));
            ExistingPaths.Add(AssetPath);
            ++CopiedBaseAssets;
        });

        int32 AddedProjectAssets = 0;
        int32 SkippedProjectAssets = 0;
        TSet<FSoftObjectPath> AddedProjectPaths;

        for (const FAssetData& Asset : AllAssets)
        {
            const FSoftObjectPath AssetPath = Asset.GetSoftObjectPath();
            const bool bIsCooked = CookedPaths.Contains(AssetPath);
            if (AddedProjectPaths.Contains(AssetPath) && !(bIsCooked && bCookedOverridesProjectAssets))
            {
                ++SkippedProjectAssets;
                continue;
            }
            AddedProjectPaths.Add(AssetPath);

            if (!bOverwriteExistingAssets && !(bCookedOverridesProjectAssets && bIsCooked) && ExistingPaths.Contains(AssetPath))
            {
                ++SkippedProjectAssets;
                continue;
            }

            if (bCookedOverridesProjectAssets && bIsCooked && ExistingPaths.Contains(AssetPath))
            {
                bool bRemovedAsset = false;
                bool bRemovedPackage = false;
                NewState.RemoveAssetData(AssetPath, false, bRemovedAsset, bRemovedPackage);
            }
            FAssetData MergedAsset(Asset);
            if (bIsCooked)
            {
                if (const FAssetData* Original = BaseState.GetAssetByObjectPath(AssetPath))
                {
                    FAssetDataTagMap Tags;
                    for (const auto& TagPair : Original->TagsAndValues) Tags.Add(TagPair.Key, TagPair.Value.AsString());
                    for (const auto& TagPair : Asset.TagsAndValues) Tags.Add(TagPair.Key, TagPair.Value.AsString());
                    MergedAsset.SetTagsAndAssetBundles(MoveTemp(Tags));
                    if (Asset.GetChunkIDs().IsEmpty()) MergedAsset.SetChunkIDs(Original->GetChunkIDs());
                }
            }
            NewState.AddAssetData(new FAssetData(MergedAsset));
            ExistingPaths.Add(AssetPath);
            ++AddedProjectAssets;
        }

        if (!SaveRegistryFile(OutputRegistryPath, NewState, Error))
        {
            return Fail(Error);
        }

        int32 BlueprintPairCount = 0;
        for (const FAssetData& Asset : CookedAssets)
        {
            const FName ClassName = Asset.AssetClassPath.GetAssetName();
            if ((ClassName == FName(TEXT("BlueprintGeneratedClass")) ||
                 ClassName == FName(TEXT("WidgetBlueprintGeneratedClass"))) &&
                Asset.AssetName.ToString().EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
            {
                ++BlueprintPairCount;
            }
        }

        const FString ReportPath = OutputRegistryPath + TEXT(".import-report.txt");
        FString ReportText = FString::Printf(
            TEXT("AssetRegistryTool import report\nOutput=%s\nImportedCookedAssets=%d\nImportedBlueprintPairs=%d\n\n"),
            *OutputRegistryPath, CookedAssets.Num(), BlueprintPairCount);
        ReportText += FString::Join(ImportedCookedReports, TEXT("\n"));
        ReportText += TEXT("\n\nGenerated registry ObjectPaths:\n");
        for (const FAssetData& Asset : CookedAssets)
        {
            ReportText += Asset.GetSoftObjectPath().ToString();
            ReportText += TEXT("\n");
        }
        FFileHelper::SaveStringToFile(ReportText, *ReportPath);

        // Round-trip verification catches malformed output before it is copied
        // into a cooked PAK.  Every imported object path must survive the
        // serialization, and generated classes must retain their paired
        // Blueprint metadata entry.
        FAssetRegistryState VerifyState;
        if (!LoadRegistryFile(OutputRegistryPath, VerifyState, Error))
        {
            return Fail(FString::Printf(TEXT("Registry was written but could not be read back: %s"), *Error));
        }
        TSet<FSoftObjectPath> VerifiedPaths;
        VerifyState.EnumerateAllAssets([&](const FAssetData& Asset)
        {
            VerifiedPaths.Add(Asset.GetSoftObjectPath());
        });

        TArray<FString> MissingPaths;
        TArray<FString> MissingBlueprintPairs;
        for (const FAssetData& Asset : CookedAssets)
        {
            const FSoftObjectPath Path = Asset.GetSoftObjectPath();
            if (!VerifiedPaths.Contains(Path))
            {
                MissingPaths.Add(Path.ToString());
            }
            const FName ClassName = Asset.AssetClassPath.GetAssetName();
            if (ClassName == FName(TEXT("BlueprintGeneratedClass")) ||
                ClassName == FName(TEXT("WidgetBlueprintGeneratedClass")))
            {
                FString BlueprintName = Asset.AssetName.ToString();
                if (BlueprintName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
                {
                    BlueprintName.LeftChopInline(2);
                    const FSoftObjectPath BlueprintPath(
                        FString::Printf(TEXT("%s.%s"), *Asset.PackageName.ToString(), *BlueprintName));
                    if (!VerifiedPaths.Contains(BlueprintPath))
                    {
                        MissingBlueprintPairs.Add(BlueprintPath.ToString());
                    }
                }
            }
        }
        if (!MissingPaths.IsEmpty() || !MissingBlueprintPairs.IsEmpty())
        {
            FString VerificationError = FString::Printf(
                TEXT("Round-trip verification failed: missing assets=%d, missing Blueprint pairs=%d."),
                MissingPaths.Num(), MissingBlueprintPairs.Num());
            if (!MissingPaths.IsEmpty())
            {
                VerificationError += FString::Printf(TEXT(" Missing assets: %s."), *FString::Join(MissingPaths, TEXT(", ")));
            }
            if (!MissingBlueprintPairs.IsEmpty())
            {
                VerificationError += FString::Printf(TEXT(" Missing Blueprint pairs: %s."), *FString::Join(MissingBlueprintPairs, TEXT(", ")));
            }
            return Fail(VerificationError);
        }

        StatusText = FString::Printf(
            TEXT("Generated %s (%lld bytes). Base copied: %d, base overwritten: %d, project/cooked added: %d, skipped: %d. Verified imported assets: %d, Blueprint pairs: %d."),
            *OutputRegistryPath,
            IFileManager::Get().FileSize(*OutputRegistryPath),
            CopiedBaseAssets,
            SkippedBaseAssets,
            AddedProjectAssets,
            SkippedProjectAssets,
            CookedAssets.Num(),
            BlueprintPairCount);

        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(StatusText));
        return FReply::Handled();
    }

    FReply Fail(const FString& Message)
    {
        StatusText = Message;
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
        return FReply::Handled();
    }

    bool OpenFileDialog(const FText& Title, TArray<FString>& OutFiles) const
    {
        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
        if (!DesktopPlatform)
        {
            return false;
        }

        const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
        return DesktopPlatform->OpenFileDialog(
            ParentWindow,
            Title.ToString(),
            BaseRegistryPath.IsEmpty() ? FPaths::ProjectDir() : FPaths::GetPath(BaseRegistryPath),
            TEXT("AssetRegistry.bin"),
            TEXT("Asset registry (*.bin)|*.bin|All files (*.*)|*.*"),
            EFileDialogFlags::None,
            OutFiles);
    }

    bool SaveFileDialog(const FText& Title, TArray<FString>& OutFiles) const
    {
        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
        if (!DesktopPlatform)
        {
            return false;
        }

        const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
        return DesktopPlatform->SaveFileDialog(
            ParentWindow,
            Title.ToString(),
            OutputRegistryPath.IsEmpty() ? FPaths::ProjectDir() : FPaths::GetPath(OutputRegistryPath),
            OutputRegistryPath.IsEmpty() ? TEXT("AssetRegistry.generated.bin") : FPaths::GetCleanFilename(OutputRegistryPath),
            TEXT("Asset registry (*.bin)|*.bin|All files (*.*)|*.*"),
            EFileDialogFlags::None,
            OutFiles);
    }

private:
    FString UsmapPath;
    FString EngineVersion = TEXT("VER_UE5_6");
    TArray<TSharedPtr<FString>> EngineOptions;
    TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe> CancelImport;
    TFuture<void> ImportFuture;
    TMap<FString, TArray<FAssetData>> ParsedImportAssets;
    TMap<FString, FString> ParsedImportMessages;
    bool bImporting = false;
    bool bApplyingImport = false;
    FString BaseRegistryPath;
    FString OutputRegistryPath;
    FString StatusText;
    FString CookedAssetsPath;
    FString CookedAssetsStatus;
    TArray<FAssetData> CookedAssets;
    TSet<FSoftObjectPath> CookedAssetPaths;
    TArray<FString> ImportedCookedReports;
    bool bOverwriteExistingAssets = true;
    bool bCookedOverridesProjectAssets = true;
};
} // namespace AssetRegistryToolPrivate

static void InspectCookedAssetMetadata(const FString& Directory, const FString& Output)
{
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.uasset"), true, false);
    Files.Sort();
    TArray<TSharedPtr<FJsonValue>> Rows;
    int32 Failures = 0;
    for (const FString& File : Files)
    {
        TArray<FAssetData> Assets;
        FString Message;
        const bool bSuccess = AssetRegistryToolPrivate::ReadCookedAsset(File, Assets, Message);
        if (!bSuccess) ++Failures;
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("File"), File);
        Row->SetBoolField(TEXT("Success"), bSuccess);
        Row->SetStringField(TEXT("Message"), Message);
        TArray<TSharedPtr<FJsonValue>> Entries;
        for (const FAssetData& Asset : Assets)
        {
            TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("ObjectPath"), Asset.GetSoftObjectPath().ToString());
            Entry->SetStringField(TEXT("Class"), Asset.AssetClassPath.ToString());
            TSharedRef<FJsonObject> Tags = MakeShared<FJsonObject>();
            for (const auto& Tag : Asset.TagsAndValues) Tags->SetStringField(Tag.Key.ToString(), Tag.Value.AsString());
            Entry->SetObjectField(TEXT("Tags"), Tags);
            Entries.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Row->SetArrayField(TEXT("Entries"), Entries);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("FileCount"), Files.Num());
    Report->SetNumberField(TEXT("Failures"), Failures);
    Report->SetArrayField(TEXT("Results"), Rows);
    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    if (FJsonSerializer::Serialize(Report, Writer)) FFileHelper::SaveStringToFile(Json, *Output);
}

class FAssetRegistryToolModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FString InspectDirectory, InspectOutput;
        if (IsRunningCommandlet() &&
            FParse::Value(FCommandLine::Get(), TEXT("InspectCookedMetadata="), InspectDirectory) &&
            FParse::Value(FCommandLine::Get(), TEXT("MetadataOut="), InspectOutput))
        {
            InspectCookedAssetMetadata(InspectDirectory, InspectOutput);
        }
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            AssetRegistryToolPrivate::ToolTabName,
            FOnSpawnTab::CreateRaw(this, &FAssetRegistryToolModule::SpawnToolTab))
            .SetDisplayName(LOCTEXT("TabTitle", "Asset Registry Tool"))
            .SetMenuType(ETabSpawnerMenuType::Hidden);

        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAssetRegistryToolModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        if (UToolMenus::IsToolMenuUIEnabled())
        {
            UToolMenus::UnregisterOwner(this);
        }

        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AssetRegistryToolPrivate::ToolTabName);
    }

private:
    TSharedRef<SDockTab> SpawnToolTab(const FSpawnTabArgs& Args)
    {
        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [
                SNew(AssetRegistryToolPrivate::SAssetRegistryToolWidget)
            ];
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("AssetRegistryTool"));
        Section.AddMenuEntry(
            TEXT("OpenAssetRegistryTool"),
            LOCTEXT("OpenAssetRegistryTool", "Asset Registry Tool"),
            LOCTEXT("OpenAssetRegistryToolTooltip", "Open the AssetRegistry.bin rebuild tool."),
            FSlateIcon(),
            FToolMenuExecuteAction::CreateRaw(this, &FAssetRegistryToolModule::OpenToolWindow));
    }

    void OpenToolWindow(const FToolMenuContext& Context)
    {
        FGlobalTabmanager::Get()->TryInvokeTab(AssetRegistryToolPrivate::ToolTabName);
    }
};

IMPLEMENT_MODULE(FAssetRegistryToolModule, AssetRegistryTool)

#undef LOCTEXT_NAMESPACE


