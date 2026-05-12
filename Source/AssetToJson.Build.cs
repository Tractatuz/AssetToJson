using UnrealBuildTool;

public class AssetToJson : ModuleRules
{
	public AssetToJson(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AnimGraph",
			"BlueprintGraph",
			"InputCore",
			"Json",
			"Kismet",
			"MovieScene",
			"TaskEvidence",
			"UMG",
			"UMGEditor",
			"UnrealEd"
		});
	}
}
