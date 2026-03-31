#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "FGBlueprintSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "PropagatorSubsystem.generated.h"

class AFGBuildableSubsystem;
DECLARE_LOG_CATEGORY_EXTERN(LogPropagatorSubsystem, Log, All);

struct FBlueprintBuildingReference
{
	TSubclassOf<UFGRecipe> BuiltWithRecipe;

	FTransform RelativeTransform;

	TSubclassOf<AFGBuildable> BuildableClass;

	AFGBuildable* Buildable;

	int32 LightweightIndex;
};


UCLASS()
class APropagatorSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	APropagatorSubsystem();

	static APropagatorSubsystem* Get(class UWorld* world);
	UFUNCTION(BlueprintPure, Category = "Schematic", DisplayName = "Get Blueprint Propagator Subsystem", Meta = (DefaultToSelf = "worldContext"))
	static APropagatorSubsystem* Get(UObject* worldContext);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	AFGBlueprintSubsystem* blueprintSubsystem;
	AFGLightweightBuildableSubsystem* lightweightSubsystem;
	AFGBuildableSubsystem* builderSubsystem;

public:
	UFUNCTION(BlueprintCallable)
	void OnClickBluePrintSync(AFGBuildableBlueprintDesigner* designer, FBlueprintRecord record);

private:
	static TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>> GetDesignerBuildingReferences(const AFGBuildableBlueprintDesigner* designer);
	TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>> GetProxyBuildingReferences(const AFGBlueprintProxy* bpProxy) const;

	static bool TryCalculateDesignerToProxyOriginOffset(const TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& designerBuildingReferences, const TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& proxyBuildingReferences, FVector& originOffset);

	static void DetectDifferences(const TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& designerBuildingReferences, const TMap<TSubclassOf<UFGRecipe>, TArray<FBlueprintBuildingReference>>& proxyBuildingReferences, const FVector& originOffset, TArray<FBlueprintBuildingReference>& newBuildables, TArray<FBlueprintBuildingReference>& removedBuildables);

	void RemoveRemovedBuildables(const TArray<FBlueprintBuildingReference>& removedBuildables);
	void SpawnNewBuildables(const TArray<FBlueprintBuildingReference>& newBuildables, const FVector& originOffset, AFGBlueprintProxy* bpProxy, AActor* buildEffectOriginSource);
};
