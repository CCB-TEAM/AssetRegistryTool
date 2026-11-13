# Bundled UAssetAPI

Upstream: https://github.com/atenfyr/UAssetAPI

Source revision: 33ef77e5a309062ea80b4a939f34ae8579c2d3bb

The plugin bundles the UAssetAPI source under UAssetAPI/ and the runtime
assemblies under Tools/UAssetCardReader/bin/Release/net10.0/ (relative to
the plugin root). No external UAssetGUI checkout is needed.

Local build adjustment: the upstream Git revision lookup uses the pinned
revision instead, allowing builds outside a Git checkout.

From the plugin root, build the reader with:

```powershell
dotnet build Tools/UAssetCardReader/UAssetCardReader.csproj -c Release
```

Building requires the .NET 10 SDK and NuGet package restore (or cached
packages). Running the current framework-dependent reader requires the
.NET 10 runtime. Newtonsoft.Json and ZstdSharp runtime assemblies are
copied into the reader output by the build.

KARDS .usmap mappings are separate game-specific data, not part of UAssetAPI.
Select the appropriate mapping and source engine version in the plugin.

See LICENSE and NOTICE.md for upstream licensing and notices.
