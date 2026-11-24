// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AlongWithYourself : ModuleRules
{
	public AlongWithYourself(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"PhysicsCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"AlongWithYourself",
			"AlongWithYourself/Variant_Platforming",
			"AlongWithYourself/Variant_Platforming/Animation",
			"AlongWithYourself/Variant_Combat",
			"AlongWithYourself/Variant_Combat/AI",
			"AlongWithYourself/Variant_Combat/Animation",
			"AlongWithYourself/Variant_Combat/Gameplay",
			"AlongWithYourself/Variant_Combat/Interfaces",
			"AlongWithYourself/Variant_Combat/UI",
			"AlongWithYourself/Variant_SideScrolling",
			"AlongWithYourself/Variant_SideScrolling/AI",
			"AlongWithYourself/Variant_SideScrolling/Gameplay",
			"AlongWithYourself/Variant_SideScrolling/Interfaces",
			"AlongWithYourself/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
