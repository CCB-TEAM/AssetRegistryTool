# AssetRegistryTool

UE 5.6.1 editor plugin for building a new `AssetRegistry.bin` from an existing registry plus assets in the current project.

## Editor usage

1. Copy this plugin folder to `YourProject/Plugins/AssetRegistryTool`, or keep it as an engine/project plugin.
2. Enable the plugin and rebuild the project if Unreal asks.
3. Open the editor menu: `Tools > Asset Registry Tool`.
4. Choose a base `AssetRegistry.bin`.
5. Choose the output path for the generated registry.
6. Select assets in the Content Browser and click `Generate From Selected Assets`, or click `Generate From All /Game Assets`.

## Cooked asset import

The tool can also import cooked `.uasset` files without loading them as editable
editor assets. Use `Add .uasset...`, `Add cooked folder...`, or drag `.uasset`
files from Explorer onto the tool panel. The reader uses UE 5.6's native
`FPackageReader` and derives `/Game/...` from the nearest `Content` directory.
Matching `.uexp` and `.ubulk` files are checked and reported when absent.
The importer also follows cooked soft-package references when the referenced
package is present beside the selected file's `Content` tree, so common asset
dependencies can be registered in the same pass.

Imported cooked asset data is merged with the base registry and normal project
assets. The cooked override option controls conflicts with project assets; the
existing base-registry overwrite option controls conflicts with the base file.
This feature only writes registry metadata. It does not convert cooked packages
into editor-editable assets.

The generated file contains the base registry plus the chosen project assets. The overwrite checkbox controls whether project assets replace entries with the same object path that already exist in the base registry.

## Commandlet usage

The older commandlet flow is still present for compatibility.

List a registry:

```powershell
UnrealEditor-Cmd.exe "X:/Project/Project.uproject" -run=AssetRegistryTool List -AssetRegistry="X:/AssetRegistry.bin" -OutFile="X:/AssetRegistry.json"
```

Merge registries:

```powershell
UnrealEditor-Cmd.exe "X:/Project/Project.uproject" -run=AssetRegistryTool Merge -HostAssetRegistry="X:/Host/AssetRegistry.bin" -AssetRegistry="X:/Donor/AssetRegistry.bin" -OutputAssetRegistry="X:/Out/AssetRegistry.bin" -FilterPaths="/Game/L10N" -OverwriteExistingAssets
```

## Notes

- The editor tool reads assets visible to the current UE project asset registry, so save and refresh assets before exporting.
- For cooked game modding, always test the generated `AssetRegistry.bin` in the target game. Some games depend on cooked package metadata or custom loading behavior that cannot be inferred from editor assets alone.
