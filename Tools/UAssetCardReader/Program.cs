using System.Text.Json;
using UAssetAPI;
using UAssetAPI.ExportTypes;
using UAssetAPI.PropertyTypes.Structs;
using UAssetAPI.UnrealTypes;
using UAssetAPI.Unversioned;
using UAssetAPI.PropertyTypes.Objects;

if (args.Length < 1)
{
    Console.Error.WriteLine("usage: UAssetCardReader <cooked.uasset>");
    return 2;
}

try
{
    var filePath = args[0];
    var mappingPath = args.Skip(1).FirstOrDefault(a => a.StartsWith("--usmap=", StringComparison.OrdinalIgnoreCase))?.Substring(8)
        ?? Environment.GetEnvironmentVariable("KARDS_USMAP");
    if (string.IsNullOrWhiteSpace(mappingPath))
        mappingPath = Path.Combine(AppContext.BaseDirectory, "kards-5.6.1-0+++UE5+Release-5.6-Fork-2352d15b.usmap");
    if (!File.Exists(mappingPath)) throw new FileNotFoundException("The configured usmap file does not exist", mappingPath);
    Usmap? mappings = new Usmap(mappingPath);
    var versionText = args.Skip(1).FirstOrDefault(a => a.StartsWith("--engine-version=", StringComparison.OrdinalIgnoreCase))?.Substring(17)
        ?? Environment.GetEnvironmentVariable("KARDS_ENGINE_VERSION") ?? "VER_UE5_6";
    if (!Enum.TryParse<EngineVersion>(versionText, true, out var engineVersion) || !Enum.IsDefined(engineVersion) || engineVersion == EngineVersion.UNKNOWN)
        throw new ArgumentException($"Unsupported engine version: {versionText}");
    var asset = new UAsset(filePath, loadUexp: true, engineVersion: engineVersion,
        mappings: mappings,
        customSerializationFlags: CustomSerializationFlags.SkipPreloadDependencyLoading | CustomSerializationFlags.SkipParsingBytecode);
    var result = new Dictionary<string, object?>
    {
        ["file"] = Path.GetFullPath(filePath),
        ["engineVersion"] = asset.GetEngineVersion().ToString(),
        ["unversioned"] = asset.HasUnversionedProperties,
        ["mappings"] = mappingPath,
        ["exports"] = asset.Exports.Count,
        ["requiresCdo"] = false,
        ["cards"] = new List<object>(),
        ["exportDetails"] = new List<object>()
    };
    var cards = (List<object>)result["cards"]!;
    var details = (List<object>)result["exportDetails"]!;
    var generatedClasses = asset.Exports.Where(e => e.ObjectName.ToString().EndsWith("_C", StringComparison.Ordinal)
        && e.GetExportClassType().ToString() == "BlueprintGeneratedClass").ToList();
    bool IsCardParent(UAssetAPI.ExportTypes.Export current)
    {
        var visited = new HashSet<int>();
        while (current.SuperIndex != null && !current.SuperIndex.IsNull())
        {
            if (current.SuperIndex.IsImport())
                return current.SuperIndex.ToImport(asset).ObjectName.ToString().Contains("BaseCardObject", StringComparison.OrdinalIgnoreCase);
            var index = current.SuperIndex.Index - 1;
            if (!visited.Add(index) || index < 0 || index >= asset.Exports.Count) return false;
            current = asset.Exports[index];
        }
        return false;
    }
    var requiresCdo = generatedClasses.Any(IsCardParent);
    result["requiresCdo"] = requiresCdo;
    string ResolveObject(FPackageIndex index, HashSet<int>? visited = null)
    {
        if (index == null || index.IsNull()) return "None";
        visited ??= new HashSet<int>();
        if (!visited.Add(index.Index)) throw new InvalidDataException("Cyclic object outer chain");
        if (index.IsImport())
        {
            var import = index.ToImport(asset) ?? throw new InvalidDataException("Invalid import index");
            if (import.OuterIndex == null || import.OuterIndex.IsNull()) return import.ObjectName.ToString();
            return ResolveObject(import.OuterIndex, visited) + "." + import.ObjectName;
        }
        var target = index.ToExport(asset);
        if (target.OuterIndex != null && !target.OuterIndex.IsNull())
            return ResolveObject(target.OuterIndex, visited) + ":" + target.ObjectName;
        var content = Path.GetFullPath(filePath).Replace('\\', '/');
        var offset = content.LastIndexOf("/Content/", StringComparison.OrdinalIgnoreCase);
        if (offset < 0) throw new InvalidDataException("Cannot resolve package path outside Content");
        return "/Game/" + content[(offset + 9)..^7] + "." + target.ObjectName;
    }
    string FormatTag(PropertyData property)
    {
        switch (property)
        {
            case SoftObjectPropertyData soft:
                var package = soft.Value.AssetPath.PackageName?.ToString();
                var name = soft.Value.AssetPath.AssetName?.ToString();
                if (string.IsNullOrEmpty(name) || name == "None") return "None";
                var path = string.IsNullOrEmpty(package) || package == "None" ? name : package + "." + name;
                var sub = soft.Value.SubPathString?.ToString();
                return string.IsNullOrEmpty(sub) ? path : path + ":" + sub;
            case ObjectPropertyData obj:
                if (obj.Value == null || obj.Value.IsNull()) return "None";
                var objectPath = ResolveObject(obj.Value);
                if (obj.Value.IsImport())
                {
                    var import = obj.Value.ToImport(asset);
                    return import.ClassPackage + "." + import.ClassName + "'" + objectPath + "'";
                }
                throw new InvalidDataException("Searchable hard reference to local export requires class resolution");
            case EnumPropertyData enumeration:
                return enumeration.Value.ToString().Split("::").Last();
            case TextPropertyData text:
                if (text.HistoryType is TextHistoryType.Base or TextHistoryType.None)
                    return text.CultureInvariantString?.ToString() ?? "";
                if (text.HistoryType == TextHistoryType.StringTableEntry)
                {
                    // Resolve the actual source table rather than exporting its debug description.
                    var full = Path.GetFullPath(filePath).Replace('\\', '/');
                    var pos = full.LastIndexOf("/Content/", StringComparison.OrdinalIgnoreCase);
                    var tablePath = text.TableId.ToString().Split('.')[0];
                    if (pos >= 0 && tablePath.StartsWith("/Game/"))
                    {
                        var local = full[..(pos + 9)] + tablePath[6..] + ".uasset";
                        if (File.Exists(local))
                        {
                            var tableAsset = new UAsset(local, true, engineVersion, mappings,
                                CustomSerializationFlags.SkipPreloadDependencyLoading | CustomSerializationFlags.SkipParsingBytecode);
                            foreach (var table in tableAsset.Exports.OfType<StringTableExport>())
                                foreach (var key in table.Table.Keys)
                                    if (key.ToString() == text.Value?.ToString()) return table.Table[key].ToString();
                        }
                    }
                    return "<MISSING STRING TABLE ENTRY>";
                }
                throw new InvalidDataException($"Unsupported searchable text history: {text.HistoryType}");
            default:
                return property.ToString() ?? "";
        }
    }
    for (var exportIndex = 0; exportIndex < asset.Exports.Count; exportIndex++)
    {
        var export = asset.Exports[exportIndex];
        var classType = export.GetExportClassType().ToString();
        var names = new List<string>();
        if (export is NormalExport normal)
            names.AddRange(normal.Data.Select(p => p.Name.ToString()));
        details.Add(new
        {
            index = exportIndex,
            clrType = export.GetType().FullName,
            name = export.ObjectName.ToString(),
            classType,
            flags = export.ObjectFlags.ToString(),
            serialSize = export.SerialSize,
            dataCount = export is NormalExport n ? n.Data.Count : -1,
            propertyNames = names
        });

        if (!export.ObjectFlags.HasFlag(EObjectFlags.RF_ClassDefaultObject)) continue;
        if (export is RawExport && requiresCdo) throw new InvalidDataException($"UAssetAPI could not parse card CDO {export.ObjectName}; check source engine version and usmap schema");
        if (export is not NormalExport exportNormal) continue;
        var properties = new Dictionary<string, string?>();
        foreach (var property in exportNormal.Data)
        {
            var name = property.Name.ToString();
            if (name is "Name" or "title" or "faction" or "cardSet" or "isReserved" or "Image" or "historyPhoto" or
                "blowUpExplosion" or "blowUpExplosionNiagara" or "OnAttackVisualsSoft" or "OnPlayedFromHandVisualsSoft" or
                "OnPlayedFromHandVisualsNiagara")
            {
                properties[name] = FormatTag(property);
            }
        }
        cards.Add(new { name = export.ObjectName.ToString(), properties });
    }
    Console.WriteLine(JsonSerializer.Serialize(result));
    return 0;
}
catch (Exception ex)
{
    Console.Error.WriteLine(ex.ToString());
    return 1;
}
