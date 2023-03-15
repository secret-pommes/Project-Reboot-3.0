#pragma once

#include "Object.h"

class AActor : public UObject
{
public:
	struct FTransform GetTransform();

	AActor* GetOwner();
	struct FVector GetActorLocation();
	struct FVector GetActorRightVector();
	void K2_DestroyActor();
	class UActorComponent* GetComponentByClass(class UClass* ComponentClass);
	float GetDistanceTo(AActor* OtherActor);
	struct FRotator GetActorRotation();
	void FlushNetDormancy();
	bool TeleportTo(const FVector& DestLocation, const FRotator& DestRotation);

	static class UClass* StaticClass();
};