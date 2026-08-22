// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class SpawnBudget : ModuleRules
{
	public SpawnBudget(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module and nothing else.
		//
		// Deliberately NOT here:
		//   UMG            - the product is a spawner. The statistics box is drawn on UCanvas from AHUD so
		//                    it survives a cooked Shipping build; the demo map's buttons are UMG assets in
		//                    Content that call the Blueprint library, exactly as a project would.
		//   Niagara        - nothing here is a particle.
		//   Chaos          - the only physics touched is one optional downward line trace per scatter
		//                    point, which is a plain world query and needs no physics module of its own.
		//   NavigationSystem - the optional navmesh projection goes through INavigationDataInterface,
		//                    which lives in Engine. A project without a navmesh loses the projection and
		//                    keeps everything else, instead of gaining a dependency it never asked for.
		//   UnrealEd       - everything here ships. There is no editor module, so nothing can go missing
		//                    between what a designer places in the editor and what the packaged game runs.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		// RenderCore gives us GWhiteTexture, the one-pixel texture the statistics box is tiled from.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
		});
	}
}
