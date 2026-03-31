using UnrealBuildTool;
using System.IO;
using System;

public class BlueprintPropagator : ModuleRules
{
	public BlueprintPropagator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject",
            "Engine",
            "DeveloperSettings",
            "PhysicsCore",
            "InputCore",
            "GeometryCollectionEngine",
            "AnimGraphRuntime",
            "AssetRegistry",
            "NavigationSystem",
            "AIModule",
            "GameplayTasks",
            "SlateCore", "Slate", "UMG",
            "RenderCore",
            "EnhancedInput",
            "NetCore",
            "GameplayTags",
        });

		// Header stubs
		PublicDependencyModuleNames.AddRange(new string[] {
			"DummyHeaders",
		});

		if (Target.Type == TargetRules.TargetType.Editor) {
			PublicDependencyModuleNames.AddRange(new string[] {"AnimGraph"});
		}
		PublicDependencyModuleNames.AddRange(new string[] {"FactoryGame", "SML"});
	}
}
