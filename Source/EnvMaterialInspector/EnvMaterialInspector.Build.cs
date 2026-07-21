// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EnvMaterialInspector : ModuleRules
{
	public EnvMaterialInspector(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"ImageCore",
				"AssetRegistry",
				"InputCore",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"TextureCompressor",
			}
			);
		
		
	}
}
