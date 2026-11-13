// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "AssetRegistryToolCommandlet.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

void EmptyLinkFunctionForGeneratedCodeAssetRegistryToolCommandlet() {}

// ********** Begin Cross Module References ********************************************************
ASSETREGISTRYTOOL_API UClass* Z_Construct_UClass_UAssetRegistryToolCommandlet();
ASSETREGISTRYTOOL_API UClass* Z_Construct_UClass_UAssetRegistryToolCommandlet_NoRegister();
ENGINE_API UClass* Z_Construct_UClass_UCommandlet();
UPackage* Z_Construct_UPackage__Script_AssetRegistryTool();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UAssetRegistryToolCommandlet *********************************************
void UAssetRegistryToolCommandlet::StaticRegisterNativesUAssetRegistryToolCommandlet()
{
}
FClassRegistrationInfo Z_Registration_Info_UClass_UAssetRegistryToolCommandlet;
UClass* UAssetRegistryToolCommandlet::GetPrivateStaticClass()
{
	using TClass = UAssetRegistryToolCommandlet;
	if (!Z_Registration_Info_UClass_UAssetRegistryToolCommandlet.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			StaticPackage(),
			TEXT("AssetRegistryToolCommandlet"),
			Z_Registration_Info_UClass_UAssetRegistryToolCommandlet.InnerSingleton,
			StaticRegisterNativesUAssetRegistryToolCommandlet,
			sizeof(TClass),
			alignof(TClass),
			TClass::StaticClassFlags,
			TClass::StaticClassCastFlags(),
			TClass::StaticConfigName(),
			(UClass::ClassConstructorType)InternalConstructor<TClass>,
			(UClass::ClassVTableHelperCtorCallerType)InternalVTableHelperCtorCaller<TClass>,
			UOBJECT_CPPCLASS_STATICFUNCTIONS_FORCLASS(TClass),
			&TClass::Super::StaticClass,
			&TClass::WithinClass::StaticClass
		);
	}
	return Z_Registration_Info_UClass_UAssetRegistryToolCommandlet.InnerSingleton;
}
UClass* Z_Construct_UClass_UAssetRegistryToolCommandlet_NoRegister()
{
	return UAssetRegistryToolCommandlet::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UAssetRegistryToolCommandlet_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Commandlet \xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\n * \xef\xbf\xbd\xc3\xb7\xef\xbf\xbd:\n *   AssetRegistryTool List -AssetRegistry=Path/To/AssetRegistry.bin [-OutFile=Export.csv] [-Verbose]\n *   AssetRegistryTool Merge -HostAssetRegistry=Host.bin -AssetRegistry=Donor.bin -OutputAssetRegistry=Out.bin [-OverwriteExistingAssets]\n *   \xef\xbf\xbd\xef\xbf\xbd\xd1\xa1\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xcb\xb2\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd:\n *     -FilterClass=Blueprint[,Material,...]\n *     -FilterPaths=/Game/Path1[,/Game/Path2]\n *     -NoRecursivePaths\n */" },
#endif
		{ "IncludePath", "AssetRegistryToolCommandlet.h" },
		{ "ModuleRelativePath", "Public/AssetRegistryToolCommandlet.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Commandlet \xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\n\xef\xbf\xbd\xc3\xb7\xef\xbf\xbd:\n  AssetRegistryTool List -AssetRegistry=Path/To/AssetRegistry.bin [-OutFile=Export.csv] [-Verbose]\n  AssetRegistryTool Merge -HostAssetRegistry=Host.bin -AssetRegistry=Donor.bin -OutputAssetRegistry=Out.bin [-OverwriteExistingAssets]\n  \xef\xbf\xbd\xef\xbf\xbd\xd1\xa1\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xcb\xb2\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd:\n    -FilterClass=Blueprint[,Material,...]\n    -FilterPaths=/Game/Path1[,/Game/Path2]\n    -NoRecursivePaths" },
#endif
	};
#endif // WITH_METADATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UAssetRegistryToolCommandlet>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
UObject* (*const Z_Construct_UClass_UAssetRegistryToolCommandlet_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UCommandlet,
	(UObject* (*)())Z_Construct_UPackage__Script_AssetRegistryTool,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UAssetRegistryToolCommandlet_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UAssetRegistryToolCommandlet_Statics::ClassParams = {
	&UAssetRegistryToolCommandlet::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	0,
	0,
	0x000000A8u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UAssetRegistryToolCommandlet_Statics::Class_MetaDataParams), Z_Construct_UClass_UAssetRegistryToolCommandlet_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_UAssetRegistryToolCommandlet()
{
	if (!Z_Registration_Info_UClass_UAssetRegistryToolCommandlet.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UAssetRegistryToolCommandlet.OuterSingleton, Z_Construct_UClass_UAssetRegistryToolCommandlet_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UAssetRegistryToolCommandlet.OuterSingleton;
}
UAssetRegistryToolCommandlet::UAssetRegistryToolCommandlet(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
DEFINE_VTABLE_PTR_HELPER_CTOR(UAssetRegistryToolCommandlet);
UAssetRegistryToolCommandlet::~UAssetRegistryToolCommandlet() {}
// ********** End Class UAssetRegistryToolCommandlet ***********************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_UE_Work_kards_Plugins_AssetRegistryTool_main_Source_AssetRegistryTool_Public_AssetRegistryToolCommandlet_h__Script_AssetRegistryTool_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UAssetRegistryToolCommandlet, UAssetRegistryToolCommandlet::StaticClass, TEXT("UAssetRegistryToolCommandlet"), &Z_Registration_Info_UClass_UAssetRegistryToolCommandlet, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UAssetRegistryToolCommandlet), 2212076158U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_UE_Work_kards_Plugins_AssetRegistryTool_main_Source_AssetRegistryTool_Public_AssetRegistryToolCommandlet_h__Script_AssetRegistryTool_190375176(TEXT("/Script/AssetRegistryTool"),
	Z_CompiledInDeferFile_FID_UE_Work_kards_Plugins_AssetRegistryTool_main_Source_AssetRegistryTool_Public_AssetRegistryToolCommandlet_h__Script_AssetRegistryTool_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_UE_Work_kards_Plugins_AssetRegistryTool_main_Source_AssetRegistryTool_Public_AssetRegistryToolCommandlet_h__Script_AssetRegistryTool_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
