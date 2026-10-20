using System.IO;
using UnrealBuildTool;

public class UnrealRocketViz : ModuleRules
{
	public UnrealRocketViz(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore", "Projects", "AudioExtensions", "CesiumRuntime", "Sockets", "Networking", "Json", "ProceduralMeshComponent" });
		// The HUD reads its fonts from disk (SUrvHudOverlay), so packaged apps need them staged.
		RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Resources/Fonts/*.ttf"), StagedFileType.UFS);
	}
}
