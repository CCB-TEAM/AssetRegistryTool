#include "AssetRegistryTool.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformFileManager.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
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

class SAssetRegistryToolWidget final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAssetRegistryToolWidget) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
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
                    SNew(SUniformGridPanel)
                    .SlotPadding(FMargin(4.0f, 0.0f))

                    + SUniformGridPanel::Slot(0, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("GenerateSelected", "Generate From Selected Assets"))
                        .OnClicked(this, &SAssetRegistryToolWidget::GenerateFromSelectedAssets)
                    ]

                    + SUniformGridPanel::Slot(1, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("GenerateAll", "Generate From All /Game Assets"))
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

private:
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

    FReply GenerateFromSelectedAssets()
    {
        return Generate(GetSelectedContentBrowserAssets(), TEXT("selected Content Browser assets"));
    }

    FReply GenerateFromAllAssets()
    {
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
        if (ProjectAssets.IsEmpty())
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
        for (const FAssetData& Asset : ProjectAssets)
        {
            SelectedPaths.Add(Asset.GetSoftObjectPath());
        }

        FAssetRegistryState NewState;
        TSet<FSoftObjectPath> ExistingPaths;
        int32 CopiedBaseAssets = 0;
        int32 SkippedBaseAssets = 0;

        BaseState.EnumerateAllAssets([&](const FAssetData& Asset)
        {
            const FSoftObjectPath AssetPath = Asset.GetSoftObjectPath();
            if (bOverwriteExistingAssets && SelectedPaths.Contains(AssetPath))
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

        for (const FAssetData& Asset : ProjectAssets)
        {
            const FSoftObjectPath AssetPath = Asset.GetSoftObjectPath();
            if (AddedProjectPaths.Contains(AssetPath))
            {
                ++SkippedProjectAssets;
                continue;
            }
            AddedProjectPaths.Add(AssetPath);

            if (!bOverwriteExistingAssets && ExistingPaths.Contains(AssetPath))
            {
                ++SkippedProjectAssets;
                continue;
            }

            NewState.AddAssetData(new FAssetData(Asset));
            ExistingPaths.Add(AssetPath);
            ++AddedProjectAssets;
        }

        if (!SaveRegistryFile(OutputRegistryPath, NewState, Error))
        {
            return Fail(Error);
        }

        StatusText = FString::Printf(
            TEXT("Generated %s. Base copied: %d, base overwritten: %d, project added: %d, project skipped: %d."),
            *OutputRegistryPath,
            CopiedBaseAssets,
            SkippedBaseAssets,
            AddedProjectAssets,
            SkippedProjectAssets);

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
    FString BaseRegistryPath;
    FString OutputRegistryPath;
    FString StatusText;
    bool bOverwriteExistingAssets = true;
};
} // namespace AssetRegistryToolPrivate

class FAssetRegistryToolModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
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


