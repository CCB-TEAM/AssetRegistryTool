// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeAssetRegistryTool_init() {}
	static FPackageRegistrationInfo Z_Registration_Info_UPackage__Script_AssetRegistryTool;
	FORCENOINLINE UPackage* Z_Construct_UPackage__Script_AssetRegistryTool()
	{
		if (!Z_Registration_Info_UPackage__Script_AssetRegistryTool.OuterSingleton)
		{
			static const UECodeGen_Private::FPackageParams PackageParams = {
				"/Script/AssetRegistryTool",
				nullptr,
				0,
				PKG_CompiledIn | 0x00000040,
				0x4B7BEDA0,
				0x58A8FF2D,
				METADATA_PARAMS(0, nullptr)
			};
			UECodeGen_Private::ConstructUPackage(Z_Registration_Info_UPackage__Script_AssetRegistryTool.OuterSingleton, PackageParams);
		}
		return Z_Registration_Info_UPackage__Script_AssetRegistryTool.OuterSingleton;
	}
	static FRegisterCompiledInInfo Z_CompiledInDeferPackage_UPackage__Script_AssetRegistryTool(Z_Construct_UPackage__Script_AssetRegistryTool, TEXT("/Script/AssetRegistryTool"), Z_Registration_Info_UPackage__Script_AssetRegistryTool, CONSTRUCT_RELOAD_VERSION_INFO(FPackageReloadVersionInfo, 0x4B7BEDA0, 0x58A8FF2D));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
