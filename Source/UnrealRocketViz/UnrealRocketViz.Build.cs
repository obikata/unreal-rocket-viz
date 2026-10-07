using UnrealBuildTool;

public class UnrealRocketViz : ModuleRules
{
	public UnrealRocketViz(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore", "Projects", "AudioExtensions", "CesiumRuntime" });
	}
}
