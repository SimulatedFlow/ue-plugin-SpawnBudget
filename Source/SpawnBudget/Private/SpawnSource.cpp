// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "SpawnSource.h"

#include "AI/Navigation/NavigationDataInterface.h"
#include "AI/Navigation/NavigationTypes.h"
#include "AI/NavigationSystemBase.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "SpawnBudgetLog.h"
#include "SpawnBudgetSettings.h"
#include "SpawnBudgetSubsystem.h"

ASpawnSource::ASpawnSource()
{
	// No tick. The subsystem walks every source once per frame; a source that ticked would be a hundred
	// tick functions doing the same distance test the subsystem already has to do anyway.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// Present but empty. A spline with no points costs nothing and is not used unless the scatter shape
	// asks for it - which is better than making the designer swap the actor class to get a road.
	ScatterSpline = CreateDefaultSubobject<USplineComponent>(TEXT("ScatterSpline"));
	ScatterSpline->SetupAttachment(SceneRoot);
	ScatterSpline->ClearSplinePoints(false);
	ScatterSpline->SetCanEverAffectNavigation(false);

	bReplicates = false;
	SetCanBeDamaged(false);
}

void ASpawnSource::BeginPlay()
{
	Super::BeginPlay();

	Rings.Sanitise();

	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(this))
	{
		Subsystem->RegisterSource(this);
	}
}

void ASpawnSource::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(this))
	{
		Subsystem->UnregisterSource(this);
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ASpawnSource::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Rings that cross over would mean a spawn distance further out than the despawn distance, which is a
	// guaranteed flicker. Fixed on the spot rather than reported, because there is exactly one thing the
	// designer can have meant.
	Rings.Sanitise();
}
#endif

void ASpawnSource::SetSourceEnabled(bool bNewEnabled)
{
	bSourceEnabled = bNewEnabled;
}

void ASpawnSource::DespawnAll()
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(this))
	{
		Subsystem->DespawnSource(this);
	}
}

TSubclassOf<AActor> ASpawnSource::PickClass() const
{
	float TotalWeight = 0.0f;
	for (const FSpawnBudgetClassEntry& Entry : Classes)
	{
		if (Entry.ActorClass)
		{
			TotalWeight += FMath::Max(0.0f, Entry.PickWeight);
		}
	}

	if (TotalWeight <= 0.0f)
	{
		// Either no classes at all, or every weight is zero. Fall back to the first usable entry so that a
		// source with one class and a forgotten weight still works.
		for (const FSpawnBudgetClassEntry& Entry : Classes)
		{
			if (Entry.ActorClass)
			{
				return Entry.ActorClass;
			}
		}
		return nullptr;
	}

	float Roll = FMath::FRandRange(0.0f, TotalWeight);
	for (const FSpawnBudgetClassEntry& Entry : Classes)
	{
		if (!Entry.ActorClass)
		{
			continue;
		}

		Roll -= FMath::Max(0.0f, Entry.PickWeight);
		if (Roll <= 0.0f)
		{
			return Entry.ActorClass;
		}
	}

	return Classes.Last().ActorClass;
}

bool ASpawnSource::BuildSpawnTransform(FTransform& OutTransform, const TArray<FVector>& ViewerLocations, float RingScale) const
{
	const USpawnBudgetSettings& Settings = USpawnBudgetSettings::Get();
	const int32 MaxAttempts = FMath::Max(1, Settings.MaxScatterAttempts);

	const float DespawnDistance = Rings.Despawn * FMath::Max(0.01f, RingScale);
	const float DespawnDistanceSquared = DespawnDistance * DespawnDistance;

	const FTransform SourceTransform = GetActorTransform();

	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		FVector Point = SourceTransform.GetLocation();

		switch (Scatter)
		{
		case ESpawnScatterShape::Point:
			break;

		case ESpawnScatterShape::Circle:
		{
			// Square-rooted radius, so the points are spread evenly over the area instead of piling up in
			// the middle the way a naive uniform radius does.
			const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
			const float Radius = ScatterRadius * FMath::Sqrt(FMath::FRand());
			Point += FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
			break;
		}

		case ESpawnScatterShape::Box:
		{
			const FVector Local(
				FMath::FRandRange(-ScatterExtent.X, ScatterExtent.X),
				FMath::FRandRange(-ScatterExtent.Y, ScatterExtent.Y),
				FMath::FRandRange(-ScatterExtent.Z, ScatterExtent.Z));
			Point += SourceTransform.GetRotation().RotateVector(Local);
			break;
		}

		case ESpawnScatterShape::Spline:
		{
			if (ScatterSpline && ScatterSpline->GetNumberOfSplinePoints() >= 2)
			{
				const float Length = ScatterSpline->GetSplineLength();
				const float Distance = FMath::FRandRange(0.0f, Length);
				Point = ScatterSpline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

				const FVector Right = ScatterSpline->GetRightVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
				Point += Right * FMath::FRandRange(-ScatterRadius, ScatterRadius);
			}
			break;
		}
		}

		if (!ProjectPoint(Point))
		{
			continue;
		}

		Point.Z += SpawnHeightOffset;

		// A point that is already outside the despawn ring would be spawned this frame and taken back the
		// next - full price twice, for something nobody ever sees. Draw again instead.
		if (ViewerLocations.Num() > 0 && DespawnDistance > 0.0f)
		{
			bool bWithinRing = false;
			for (const FVector& Viewer : ViewerLocations)
			{
				if (FVector::DistSquared(Viewer, Point) <= DespawnDistanceSquared)
				{
					bWithinRing = true;
					break;
				}
			}

			if (!bWithinRing)
			{
				continue;
			}
		}

		FRotator Rotation = SourceTransform.Rotator();
		if (bRandomYaw)
		{
			Rotation.Yaw = FMath::FRandRange(0.0f, 360.0f);
			Rotation.Pitch = 0.0f;
			Rotation.Roll = 0.0f;
		}

		OutTransform = FTransform(Rotation, Point, FVector::OneVector);
		return true;
	}

	return false;
}

bool ASpawnSource::ProjectPoint(FVector& InOutPoint) const
{
	if (GroundMode == ESpawnGroundMode::None)
	{
		return true;
	}

	if (GroundMode == ESpawnGroundMode::NavMesh)
	{
		// Straight through the navigation data interface, which lives in Engine. Going through the
		// NavigationSystem module would mean every project that installs this plugin gains a dependency
		// on it whether it has a navmesh or not.
		if (INavigationDataInterface* NavData = FNavigationSystem::GetNavDataForActor(*this))
		{
			FNavLocation Projected;
			if (NavData->ProjectPoint(InOutPoint, Projected, FVector(NavProjectionExtent)))
			{
				InOutPoint = Projected.Location;
				return true;
			}

			// On a navmesh and off it are different answers. Off it means "nothing could stand here", and
			// that is a rejection rather than something to paper over with a line trace.
			return false;
		}

		// No navigation data in this world at all - a test map, a level whose navmesh has not been built.
		// Fall through to the trace rather than refusing to spawn anything, so the source still works.
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Start = InOutPoint + FVector(0.0f, 0.0f, GroundTraceHeight);
	const FVector End = InOutPoint - FVector(0.0f, 0.0f, GroundTraceHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpawnBudgetGroundTrace), false, this);
	Params.bReturnPhysicalMaterial = false;

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, USpawnBudgetSettings::Get().GroundTraceChannel, Params))
	{
		InOutPoint = Hit.ImpactPoint;
		return true;
	}

	// Nothing under the point. A hole in the level, a gap in a bridge, the edge of the terrain - all
	// places an actor should not be dropped into.
	return false;
}
