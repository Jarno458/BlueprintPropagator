#include "PropagatorSubsystem.h"
#include "EngineUtils.h"
#include "Subsystem/SubsystemActorManager.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableBlueprintDesigner.h"
#include "FGBlueprintProxy.h"
#include "Logging/StructuredLog.h"
#include "FGLightweightBuildableSubsystem.h"

DEFINE_LOG_CATEGORY(LogPropagatorSubsystem);

constexpr float TRANSFORM_TOLERANCE = 0.01f;

#pragma optimize("", off)

APropagatorSubsystem::APropagatorSubsystem() : Super() {
	PrimaryActorTick.bCanEverTick = false;

	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer;
}

APropagatorSubsystem* APropagatorSubsystem::Get(UWorld* world) {
	USubsystemActorManager* subsystemActorManager = world->GetSubsystem<USubsystemActorManager>();
	fgcheck(subsystemActorManager);

	return subsystemActorManager->GetSubsystemActor<APropagatorSubsystem>();
}

APropagatorSubsystem* APropagatorSubsystem::Get(UObject* worldContext) {
	UWorld* world = GEngine->GetWorldFromContextObject(worldContext, EGetWorldErrorMode::Assert);

	return Get(world);
}

void APropagatorSubsystem::BeginPlay() {
	UE_LOGFMT(LogPropagatorSubsystem, Display, "APropagatorSubsystem::BeginPlay()");
	Super::BeginPlay();

	UWorld* world = GetWorld();

	blueprintSubsystem = AFGBlueprintSubsystem::Get(world);
	lightweightSubsystem = AFGLightweightBuildableSubsystem::Get(world);
	builderSubsystem = AFGBuildableSubsystem::Get(world);
}

void APropagatorSubsystem::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);
}

void APropagatorSubsystem::OnClickBluePrintSync(AFGBuildableBlueprintDesigner* designer, FBlueprintRecord record) {
	UE_LOGFMT(LogPropagatorSubsystem, Display, "APropagatorSubsystem::OnClickBluePrintSync({0})", record.BlueprintName);

	const TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& DesignerBuildingsPerRecipe = GetDesignerBuildingReferences(designer);

	for (TActorIterator<AFGBlueprintProxy> actorIterator(GetWorld()); actorIterator; ++actorIterator) {
		AFGBlueprintProxy* bpProxy = *actorIterator;
		if (!IsValid(bpProxy) || bpProxy->GetBlueprintName().ToString() != record.BlueprintName)
			continue;

		UE_LOGFMT(LogPropagatorSubsystem, Display, "APropagatorSubsystem::OnClickBluePrintSync processing proxy of {0} at {1}", bpProxy->GetBlueprintName().ToString(), bpProxy->GetTransform().ToString());

		const TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& ProxyBuildingsPerRecipe = GetProxyBuildingReferences(bpProxy);

		FVector designerToProxyOffset;
		if (!TryCalculateDesignerToProxyOriginOffset(DesignerBuildingsPerRecipe, ProxyBuildingsPerRecipe, designerToProxyOffset))
		{
			UE_LOGFMT(LogPropagatorSubsystem, Warning, "APropagatorSubsystem::OnClickBluePrintSync multiple designer to proxy offsets with same count, cannot determine correct offset for {0} at {1}", bpProxy->GetBlueprintName().ToString(), bpProxy->GetTransform().ToString());
			continue;
		}

		TArray<FBlueprintBuildingReference> newBuildables;
		TArray<FBlueprintBuildingReference> removedBuildables;
		DetectDifferences(DesignerBuildingsPerRecipe, ProxyBuildingsPerRecipe, designerToProxyOffset, newBuildables, removedBuildables);

		RemoveRemovedBuildables(removedBuildables);

		SpawnNewBuildables(newBuildables, designerToProxyOffset, bpProxy, designer);
	}
}

TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>> APropagatorSubsystem::GetDesignerBuildingReferences(const AFGBuildableBlueprintDesigner* designer)
{
	 TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>> designerBuildingsPerRecipe;

	 FTransform designerOffset;
	 designer->GetOffsetTransform(designerOffset);

	for (AFGBuildable* buildable : designer->GetBuildablesInBlueprintDesigner()) {
		if (!IsValid(buildable))
			continue;

		FBlueprintBuildingReference info;
		info.RelativeTransform = buildable->GetTransform().GetRelativeTransform(designerOffset);
		info.BuiltWithRecipe = buildable->GetBuiltWithRecipe();
		info.BuildableClass = buildable->GetClass();
		info.Buildable = buildable;
		info.LightweightIndex = -1; // designer buildables are always normal buildables, lightweight buildables are only used in the proxy

		designerBuildingsPerRecipe.FindOrAdd(info.BuiltWithRecipe).Add(info);
	}

	return designerBuildingsPerRecipe;
}

TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>> APropagatorSubsystem::GetProxyBuildingReferences(const AFGBlueprintProxy* bpProxy) const
{
	TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>> proxyBuildingsPerRecipe;

	FTransform proxyTransform = bpProxy->GetTransform();

	for (AFGBuildable* buildable : bpProxy->GetBuildables())
	{
		if (!IsValid(buildable))
			continue;

		FBlueprintBuildingReference info;
		info.RelativeTransform = buildable->GetTransform().GetRelativeTransform(proxyTransform);
		info.BuiltWithRecipe = buildable->GetBuiltWithRecipe();
		info.BuildableClass = buildable->GetClass();
		info.Buildable = buildable;
		info.LightweightIndex = -1;

		proxyBuildingsPerRecipe.FindOrAdd(info.BuiltWithRecipe).Add(info);
	}

	for (const FBuildableClassLightweightIndices& lightweightIdentifier : bpProxy->GetLightweightClassAndIndices())
	{
		for (int32 Index : lightweightIdentifier.Indices)
		{
			const FRuntimeBuildableInstanceData* lightweightData = lightweightSubsystem->GetRuntimeDataForBuildableClassAndIndex(lightweightIdentifier.BuildableClass, Index);

			FBlueprintBuildingReference info;
			info.RelativeTransform = lightweightData->Transform.GetRelativeTransform(proxyTransform);
			info.BuiltWithRecipe = lightweightData->BuiltWithRecipe;
			info.BuildableClass = lightweightIdentifier.BuildableClass;
			info.Buildable = nullptr;
			info.LightweightIndex = Index;

			proxyBuildingsPerRecipe.FindOrAdd(info.BuiltWithRecipe).Add(info);
		}
	}

	return proxyBuildingsPerRecipe;
}

bool APropagatorSubsystem::TryCalculateDesignerToProxyOriginOffset(
	const TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& designerBuildingReferences,
	const TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& proxyBuildingReferences,
	FVector& originOffset)
{
	// guestimate the differences between designer and proxy by comparing the transforms of each building and grouping them by recipe
	TArray<TPair<FVector, int32>> designerProxyOffsetCounts;

	for (const TPair<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& designerBuildingPerRecipe : designerBuildingReferences)
	{
		if (!proxyBuildingReferences.Contains(designerBuildingPerRecipe.Key))
			continue;

		for (const FBlueprintBuildingReference& designerBuilding : designerBuildingPerRecipe.Value)
		{
			for (const FBlueprintBuildingReference& proxyBuilding : proxyBuildingReferences[designerBuildingPerRecipe.Key])
			{
				FVector offsetTransform = proxyBuilding.RelativeTransform.GetRelativeTransformReverse(designerBuilding.RelativeTransform).GetTranslation();

				bool found = false;
				for (TPair<FVector, int32>& existingOffsetCount : designerProxyOffsetCounts)
				{
					if (existingOffsetCount.Key.Equals(offsetTransform, TRANSFORM_TOLERANCE))
					{
						found = true;
						existingOffsetCount.Value++;
						break;
					}
				}
				if (!found)
				{
					designerProxyOffsetCounts.Add(TPair<FVector, int32>(offsetTransform, 1));
				}
			}
		}
	}

	FVector designerToProxyOffset;
	int32 maxCount = 0;
	int32 sameCount = 0;

	for (const TPair<FVector, int32>& offsetCount : designerProxyOffsetCounts)
	{
		if (offsetCount.Value > maxCount)
		{
			maxCount = offsetCount.Value;
			designerToProxyOffset = offsetCount.Key;
			sameCount = 1;
		}
		else if (offsetCount.Value == maxCount)
		{
			sameCount++;
		}
	}
	UE_LOGFMT(LogPropagatorSubsystem, Display, "designerToProxyOffset: {0} is used by {1} buildings", designerToProxyOffset.ToString(), maxCount);

	if (sameCount > 1)
		return false;

	originOffset = designerToProxyOffset;
	return true;
}

void APropagatorSubsystem::DetectDifferences(
	const TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& designerBuildingReferences,
	const TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& proxyBuildingReferences,
	const FVector& originOffset, 
	TArray<FBlueprintBuildingReference>& newBuildables,
	TArray<FBlueprintBuildingReference>& removedBuildables)
{
	for (const TPair<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& designerBuildingPerRecipe : designerBuildingReferences)
	{
		if (!proxyBuildingReferences.Contains(designerBuildingPerRecipe.Key))
		{
			for (const FBlueprintBuildingReference& designerBuilding : designerBuildingPerRecipe.Value)
			{
				newBuildables.Add(designerBuilding);
			}
		}
		else
		{
			// scan for building adding in the designer
			for (const FBlueprintBuildingReference& designerBuilding : designerBuildingPerRecipe.Value)
			{
				bool found = false;


				for (const FBlueprintBuildingReference& proxyBuilding : proxyBuildingReferences[designerBuildingPerRecipe.Key])
				{
					FTransform adjustProxyBuildingTransform = proxyBuilding.RelativeTransform;
					adjustProxyBuildingTransform.AddToTranslation(originOffset);

					if (designerBuilding.RelativeTransform.Equals(adjustProxyBuildingTransform, TRANSFORM_TOLERANCE))
					{
						found = true;
						break;
					}
				}

				if (!found)
					newBuildables.Add(designerBuilding);
			}

			// scan for buildings no longer part of the designer
			for (const FBlueprintBuildingReference& proxyBuilding : proxyBuildingReferences[designerBuildingPerRecipe.Key])
			{
				bool found = false;

				for (const FBlueprintBuildingReference& designerBuilding : designerBuildingPerRecipe.Value)
				{
					FTransform adjustProxyBuildingTransform = proxyBuilding.RelativeTransform;
					adjustProxyBuildingTransform.AddToTranslation(originOffset);

					if (designerBuilding.RelativeTransform.Equals(adjustProxyBuildingTransform, TRANSFORM_TOLERANCE))
					{
						found = true;
						break;
					}
				}

				if (!found)
					removedBuildables.Add(proxyBuilding);
			}
		}
	}
	for (const TPair<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& proxyBuildingPerRecipe : proxyBuildingReferences)
	{
		if (!designerBuildingReferences.Contains(proxyBuildingPerRecipe.Key))
		{
			for (const FBlueprintBuildingReference& proxyBuilding : proxyBuildingPerRecipe.Value)
			{
				removedBuildables.Add(proxyBuilding);
			}
		}
	}
}

void APropagatorSubsystem::RemoveRemovedBuildables(const TArray<FBlueprintBuildingReference>& removedBuildables)
{
	for (const FBlueprintBuildingReference& removedBuildable : removedBuildables)
	{
		if (removedBuildable.LightweightIndex >= 0)
		{
			lightweightSubsystem->RemoveByInstanceIndex(removedBuildable.BuildableClass, removedBuildable.LightweightIndex);
		}
		else if (IsValid(removedBuildable.Buildable))
		{
			AFGBuildable::Execute_Dismantle(removedBuildable.Buildable);
		}
	}
}

void APropagatorSubsystem::SpawnNewBuildables(const TArray<FBlueprintBuildingReference>& newBuildables,	const FVector& originOffset, AFGBlueprintProxy* bpProxy, AActor* buildEffectOriginSource)
{
	FTransform proxyTransform = bpProxy->GetTransform();
	// order to adjust transforms	� ChildLocal * ParentWorld = ChildWorld
	for (const FBlueprintBuildingReference& newBuildable : newBuildables)
	{
		FTransform transformRelativeToDesigner = newBuildable.RelativeTransform;
		transformRelativeToDesigner.AddToTranslation(-originOffset);
		FTransform spawnTransform = transformRelativeToDesigner * proxyTransform;

		AFGBuildable* spawnedBuildable = builderSubsystem->BeginSpawnBuildable(newBuildable.BuildableClass, spawnTransform);
		// Set all properties specific to the spawned builder class here. 
		// Beam length, fixture angle, light data, sign data, ect
		// next set standard data for all builder types
		spawnedBuildable->SetBuiltWithRecipe(newBuildable.Buildable->GetBuiltWithRecipe());
		spawnedBuildable->SetBlueprintProxy(bpProxy);
		//spawnedBuildable->SetBuildEffectActor(newBuildable.Buildable->GetBuildEffectActor());
		//FFactoryCustomizationData customizeData;
		//FFactoryCustomizationColorSlot colorSlot;
		//colorSlot.PrimaryColor = FLinearColor::Red;
		//colorSlot.SecondaryColor = FLinearColor::Red;
		//customizeData.OverrideColorData = colorSlot;
		//spawnedBuildable->ApplyCustomizationData_Implementation(customizeData);

		spawnedBuildable->PlayBuildEffectActor(buildEffectOriginSource); //could be moved to the player?
		spawnedBuildable->ExecutePlayBuildEffects();


		spawnedBuildable->FinishSpawning(spawnTransform);
	}
}

#pragma optimize("", on)
