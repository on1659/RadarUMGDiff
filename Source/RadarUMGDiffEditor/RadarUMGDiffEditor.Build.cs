// Copyright Radar. All Rights Reserved.

using UnrealBuildTool;

public class RadarUMGDiffEditor : ModuleRules
{
	public RadarUMGDiffEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"Kismet",
			"UMG",
			"UMGEditor",
			"GraphEditor",
			"ToolMenus",
			"ContentBrowser",
			"Projects",
			"PropertyEditor",
			"EditorStyle",
			"InputCore",
			"Json",
			"JsonUtilities",
			"MovieScene",
			"MovieSceneTracks",
			"BlueprintGraph",
			"SourceControl",
			"DeveloperSettings",
			"DesktopPlatform",
			"AssetRegistry",
		});

		// FRevisionInfo 가 있는 모듈이 엔진 버전마다 다르다 (dev 문서 §3.2).
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 2)
		{
			PrivateDependencyModuleNames.Add("AssetDefinition");
		}
		else
		{
			PrivateDependencyModuleNames.Add("AssetTools");
		}
	}
}
