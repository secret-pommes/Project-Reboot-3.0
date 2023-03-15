#include "FortPlayerController.h"

#include "Rotator.h"
#include "BuildingSMActor.h"
#include "FortGameModeAthena.h"

#include "FortPlayerState.h"
#include "BuildingWeapons.h"

#include "ActorComponent.h"
#include "FortPlayerStateAthena.h"
#include "globals.h"
#include "FortPlayerControllerAthena.h"
#include "BuildingContainer.h"
#include "FortLootPackage.h"
#include "FortPickup.h"
#include "FortPlayerPawn.h"
#include <memcury.h>

void AFortPlayerController::ClientReportDamagedResourceBuilding(ABuildingSMActor* BuildingSMActor, EFortResourceType PotentialResourceType, int PotentialResourceCount, bool bDestroyed, bool bJustHitWeakspot)
{
	static auto fn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerController.ClientReportDamagedResourceBuilding");

	struct { ABuildingSMActor* BuildingSMActor; EFortResourceType PotentialResourceType; int PotentialResourceCount; bool bDestroyed; bool bJustHitWeakspot; }
	AFortPlayerController_ClientReportDamagedResourceBuilding_Params{BuildingSMActor, PotentialResourceType, PotentialResourceCount, bDestroyed, bJustHitWeakspot};

	this->ProcessEvent(fn, &AFortPlayerController_ClientReportDamagedResourceBuilding_Params);
}

void AFortPlayerController::ServerExecuteInventoryItemHook(AFortPlayerController* PlayerController, FGuid ItemGuid)
{
	auto ItemInstance = PlayerController->GetWorldInventory()->FindItemInstance(ItemGuid);
	auto Pawn = Cast<AFortPawn>(PlayerController->GetPawn());

	if (!ItemInstance || !Pawn)
		return;

	auto ItemDefinition = ItemInstance->GetItemEntry()->GetItemDefinition();

	static auto FortGadgetItemDefinitionClass = FindObject<UClass>("/Script/FortniteGame.FortGadgetItemDefinition");

	if (ItemDefinition->IsA(FortGadgetItemDefinitionClass))
	{
		static auto GetWeaponItemDefinition = FindObject<UFunction>("/Script/FortniteGame.FortGadgetItemDefinition.GetWeaponItemDefinition");

		if (GetWeaponItemDefinition)
		{
			ItemDefinition->ProcessEvent(GetWeaponItemDefinition, &ItemDefinition);
		}
		else
		{
			static auto GetDecoItemDefinition = FindObject<UFunction>("/Script/FortniteGame.FortGadgetItemDefinition.GetDecoItemDefinition");
			ItemDefinition->ProcessEvent(GetDecoItemDefinition, &ItemDefinition);
		}
	}

	if (auto DecoItemDefinition = Cast<UFortDecoItemDefinition>(ItemDefinition))
	{
		Pawn->PickUpActor(nullptr, DecoItemDefinition); // todo check ret value?
		Pawn->GetCurrentWeapon()->GetItemEntryGuid() = ItemGuid;

		static auto FortDecoTool_ContextTrapStaticClass = FindObject<UClass>("/Script/FortniteGame.FortDecoTool_ContextTrap");

		if (Pawn->GetCurrentWeapon()->IsA(FortDecoTool_ContextTrapStaticClass))
		{
			static auto ContextTrapItemDefinitionOffset = Pawn->GetCurrentWeapon()->GetOffset("ContextTrapItemDefinition");
			Pawn->GetCurrentWeapon()->Get<UObject*>(ContextTrapItemDefinitionOffset) = DecoItemDefinition;
		}

		return;
	}

	if (!ItemDefinition)
		return;

	if (auto Weapon = Pawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)ItemDefinition, ItemInstance->GetItemEntry()->GetItemGuid()))
	{

	}
}

void AFortPlayerController::ServerAttemptInteractHook(UObject* Context, FFrame* Stack, void* Ret)
{
	LOG_INFO(LogInteraction, "ServerAttemptInteract!");

	auto Params = Stack->Locals;

	static bool bIsUsingComponent = FindObject<UClass>("/Script/FortniteGame.FortControllerComponent_Interaction");

	AFortPlayerControllerAthena* PlayerController = bIsUsingComponent ? Cast<AFortPlayerControllerAthena>(((UActorComponent*)Context)->GetOwner()) :
		Cast<AFortPlayerControllerAthena>(Context);

	if (!PlayerController)
		return;

	std::string StructName = bIsUsingComponent ? "/Script/FortniteGame.FortControllerComponent_Interaction.ServerAttemptInteract" : "/Script/FortniteGame.FortPlayerController.ServerAttemptInteract";

	static auto ReceivingActorOffset = FindOffsetStruct(StructName, "ReceivingActor");
	auto ReceivingActor = *(AActor**)(__int64(Params) + ReceivingActorOffset);

	// LOG_INFO(LogInteraction, "ReceivingActor: {}", __int64(ReceivingActor));

	if (!ReceivingActor)
		return;

	// LOG_INFO(LogInteraction, "ReceivingActor Name: {}", ReceivingActor->GetFullName());

	if (auto BuildingContainer = Cast<ABuildingContainer>(ReceivingActor))
	{
		static auto bAlreadySearchedOffset = BuildingContainer->GetOffset("bAlreadySearched");
		static auto SearchBounceDataOffset = BuildingContainer->GetOffset("SearchBounceData");
		static auto bAlreadySearchedFieldMask = GetFieldMask(BuildingContainer->GetProperty("bAlreadySearched"));
		static auto SearchAnimationCountOffset = FindOffsetStruct("/Script/FortniteGame.FortSearchBounceData", "SearchAnimationCount");
		
		auto SearchBounceData = BuildingContainer->GetPtr<void>(SearchBounceDataOffset);

		if (BuildingContainer->ReadBitfieldValue(bAlreadySearchedOffset, bAlreadySearchedFieldMask))
			return;

		// LOG_INFO(LogInteraction, "bAlreadySearchedFieldMask: {}", bAlreadySearchedFieldMask);

		BuildingContainer->SetBitfieldValue(bAlreadySearchedOffset, bAlreadySearchedFieldMask, true);
		(*(int*)(__int64(SearchBounceData) + SearchAnimationCountOffset))++;

		static auto OnRep_bAlreadySearchedFn = FindObject<UFunction>("/Script/FortniteGame.BuildingContainer.OnRep_bAlreadySearched");
		BuildingContainer->ProcessEvent(OnRep_bAlreadySearchedFn);

		static auto SearchLootTierGroupOffset = BuildingContainer->GetOffset("SearchLootTierGroup");
		auto RedirectedLootTier = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode(), false)->RedirectLootTier(BuildingContainer->Get<FName>(SearchLootTierGroupOffset));

		auto LootDrops = PickLootDrops(RedirectedLootTier);

		LOG_INFO(LogInteraction, "LootDrops.size(): {}", LootDrops.size());

		FVector LocationToSpawnLoot = BuildingContainer->GetActorLocation() + BuildingContainer->GetActorRightVector() * 70.f + FVector{0, 0, 50};

		for (int i = 0; i < LootDrops.size(); i++)
		{
			auto& lootDrop = LootDrops.at(i);
			AFortPickup::SpawnPickup(lootDrop.ItemDefinition, LocationToSpawnLoot, lootDrop.Count, EFortPickupSourceTypeFlag::Container, EFortPickupSpawnSource::Unset, lootDrop.LoadedAmmo
				// , (AFortPawn*)PlayerController->GetPawn() // should we put this here?
			);
		}

		// if (BuildingContainer->ShouldDestroyOnSearch())
			// BuildingContainer->K2_DestroyActor();
	}

	return ServerAttemptInteractOriginal(Context, Stack, Ret);
}

void AFortPlayerController::ServerAttemptAircraftJumpHook(AFortPlayerController* PC, FRotator ClientRotation)
{
	if (Fortnite_Version == 17.30 && Globals::bGoingToPlayEvent)
		return; // We want to be teleported back to the UFO but we dont use chooseplayerstart

	auto PlayerController = Cast<APlayerController>(Engine_Version < 424 ? PC : ((UActorComponent*)PC)->GetOwner());

	LOG_INFO(LogDev, "PlayerController: {}", __int64(PlayerController));

	if (!PlayerController)
		return;

	// if (!PlayerController->bInAircraft) 
		// return;

	auto GameMode = (AFortGameModeAthena*)GetWorld()->GetGameMode();
	auto GameState = GameMode->GetGameStateAthena();

	static auto AircraftsOffset = GameState->GetOffset("Aircrafts");
	auto Aircrafts = GameState->GetPtr<TArray<AActor*>>(AircraftsOffset);

	if (Aircrafts->Num() <= 0)
		return;

	auto NewPawn = GameMode->SpawnDefaultPawnForHook(GameMode, (AController*)PlayerController, Aircrafts->at(0));
	PlayerController->Possess(NewPawn);

	// PC->ServerRestartPlayer();
}

void AFortPlayerController::ServerCreateBuildingActorHook(UObject* Context, FFrame* Stack, void* Ret)
{
	auto PlayerController = Cast<AFortPlayerController>(Context);

	if (!PlayerController) // ??
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);

	auto PlayerStateAthena = Cast<AFortPlayerStateAthena>(PlayerController->GetPlayerState());

	if (!PlayerStateAthena)
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);

	UClass* BuildingClass = nullptr;
	FVector BuildLocation;
	FRotator BuildRotator;
	bool bMirrored;

	if (Fortnite_Version >= 8.30)
	{
		struct FCreateBuildingActorData { uint32_t BuildingClassHandle; FVector BuildLoc; FRotator BuildRot; bool bMirrored; };
		auto CreateBuildingData = (FCreateBuildingActorData*)Stack->Locals;

		BuildLocation = CreateBuildingData->BuildLoc;
		BuildRotator = CreateBuildingData->BuildRot;
		bMirrored = CreateBuildingData->bMirrored;

		static auto BroadcastRemoteClientInfoOffset = PlayerController->GetOffset("BroadcastRemoteClientInfo");
		auto BroadcastRemoteClientInfo = PlayerController->Get(BroadcastRemoteClientInfoOffset);

		static auto RemoteBuildableClassOffset = BroadcastRemoteClientInfo->GetOffset("RemoteBuildableClass");
		BuildingClass = BroadcastRemoteClientInfo->Get<UClass*>(RemoteBuildableClassOffset);
	}
	else
	{
		struct FBuildingClassData { UClass* BuildingClass; int PreviousBuildingLevel; int UpgradeLevel; };
		struct SCBAParams { FBuildingClassData BuildingClassData; FVector BuildLoc; FRotator BuildRot; bool bMirrored; };

		auto Params = (SCBAParams*)Stack->Locals;

		BuildingClass = Params->BuildingClassData.BuildingClass;
		BuildLocation = Params->BuildLoc;
		BuildRotator = Params->BuildRot;
		bMirrored = Params->bMirrored;
	}

	// LOG_INFO(LogDev, "BuildingClass {}", __int64(BuildingClass));

	if (!BuildingClass)
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);

	TArray<ABuildingSMActor*> ExistingBuildings;
	char idk;
	static __int64 (*CantBuild)(UObject*, UObject*, FVector, FRotator, char, TArray<ABuildingSMActor*>*, char*) = decltype(CantBuild)(Addresses::CantBuild);
	bool bCanBuild = !CantBuild(GetWorld(), BuildingClass, BuildLocation, BuildRotator, bMirrored, &ExistingBuildings, &idk);

	if (!bCanBuild)
	{
		// LOG_INFO(LogDev, "cant build");
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);
	}

	for (int i = 0; i < ExistingBuildings.Num(); i++)
	{
		auto ExistingBuilding = ExistingBuildings.At(i);

		ExistingBuilding->K2_DestroyActor();
	}

	ExistingBuildings.Free();

	FTransform Transform{};
	Transform.Translation = BuildLocation;
	Transform.Rotation = BuildRotator.Quaternion();
	Transform.Scale3D = { 1, 1, 1 };

	auto BuildingActor = GetWorld()->SpawnActor<ABuildingSMActor>(BuildingClass, Transform);

	if (!BuildingActor)
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);

	// static auto OwnerPersistentIDOffset = BuildingActor->GetOffset("OwnerPersistentID");
	// BuildingActor->Get<int>(OwnerPersistentIDOffset) = PlayerStateAthena->GetWorldPlayerId();

	BuildingActor->SetPlayerPlaced(true);
	BuildingActor->InitializeBuildingActor(PlayerController, BuildingActor, true);
	BuildingActor->SetTeam(PlayerStateAthena->GetTeamIndex());

	return ServerCreateBuildingActorOriginal(Context, Stack, Ret);
}

void AFortPlayerController::ServerAttemptInventoryDropHook(AFortPlayerController* PlayerController, FGuid ItemGuid, int Count)
{
	LOG_INFO(LogDev, "ServerAttemptInventoryDropHook!");

	auto Pawn = PlayerController->GetMyFortPawn();

	if (Count < 0 || !Pawn)
		return;

	auto WorldInventory = PlayerController->GetWorldInventory();
	auto ReplicatedEntry = WorldInventory->FindReplicatedEntry(ItemGuid);

	if (!ReplicatedEntry)
		return;

	auto ItemDefinition = Cast<UFortWorldItemDefinition>(ReplicatedEntry->GetItemDefinition());

	if (!ItemDefinition || !ItemDefinition->CanBeDropped())
		return;

	auto Pickup = AFortPickup::SpawnPickup(ItemDefinition, Pawn->GetActorLocation(), Count,
		EFortPickupSourceTypeFlag::Player, EFortPickupSpawnSource::Unset, ReplicatedEntry->GetLoadedAmmo(), Pawn);

	if (!Pickup)
		return;

	bool bShouldUpdate = false;

	if (!WorldInventory->RemoveItem(ItemGuid, &bShouldUpdate, Count))
		return;

	if (bShouldUpdate)
		WorldInventory->Update();
}

void AFortPlayerController::ServerPlayEmoteItemHook(AFortPlayerController* PlayerController, UObject* EmoteAsset)
{
	auto PlayerState = (AFortPlayerStateAthena*)PlayerController->GetPlayerState();
	auto Pawn = PlayerController->GetPawn();

	if (!EmoteAsset || !PlayerState || !Pawn)
		return;

	UObject* AbilityToUse = nullptr;

	static auto AthenaSprayItemDefinitionClass = FindObject<UClass>("/Script/FortniteGame.AthenaSprayItemDefinition");

	if (EmoteAsset->IsA(AthenaSprayItemDefinitionClass))
	{
		static auto SprayGameplayAbilityDefault = FindObject("/Game/Abilities/Sprays/GAB_Spray_Generic.Default__GAB_Spray_Generic_C");
		AbilityToUse = SprayGameplayAbilityDefault;
	}

	if (!AbilityToUse)
	{
		static auto EmoteGameplayAbilityDefault = FindObject("/Game/Abilities/Emotes/GAB_Emote_Generic.Default__GAB_Emote_Generic_C");
		AbilityToUse = EmoteGameplayAbilityDefault;
	}

	if (!AbilityToUse)
		return;

	int outHandle = 0;

	FGameplayAbilitySpec* Spec = MakeNewSpec((UClass*)AbilityToUse, EmoteAsset, true);

	static unsigned int* (*GiveAbilityAndActivateOnce)(UAbilitySystemComponent * ASC, int* outHandle, __int64 Spec)
		= decltype(GiveAbilityAndActivateOnce)(Addresses::GiveAbilityAndActivateOnce);

	GiveAbilityAndActivateOnce(PlayerState->GetAbilitySystemComponent(), &outHandle, __int64(Spec));
}

uint8 ToDeathCause(const FGameplayTagContainer& TagContainer, bool bWasDBNO = false)
{
	static auto ToDeathCauseFn = FindObject<UFunction>("/Script/FortniteGame.FortPlayerStateAthena.ToDeathCause");

	if (ToDeathCauseFn)
	{
		struct
		{
			FGameplayTagContainer                       InTags;                                                   // (ConstParm, Parm, OutParm, ReferenceParm, NativeAccessSpecifierPublic)
			bool                                               bWasDBNO;                                                 // (Parm, ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
			uint8_t                                        ReturnValue;                                              // (Parm, OutParm, ZeroConstructor, ReturnParm, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
		} AFortPlayerStateAthena_ToDeathCause_Params{ TagContainer, bWasDBNO };

		AFortPlayerStateAthena::StaticClass()->ProcessEvent(ToDeathCauseFn, &AFortPlayerStateAthena_ToDeathCause_Params);

		return AFortPlayerStateAthena_ToDeathCause_Params.ReturnValue;
	}

	static bool bHaveFoundAddress = false;

	static uint64 Addr = 0;

	if (!bHaveFoundAddress)
	{
		bHaveFoundAddress = true;

		if (Engine_Version == 420)
			Addr = Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 0F B6 FA 48 8B D9 E8 ? ? ? ? 33 F6 48 89 74 24").Get();

		if (!Addr)
		{
			LOG_WARN(LogPlayer, "Failed to find ToDeathCause address!");
			return 0;
		}
	}

	if (!Addr)
	{
		return 0;
	}

	static uint8 (*sub_7FF7AB499410)(FGameplayTagContainer TagContainer, char bWasDBNOIg) = decltype(sub_7FF7AB499410)(Addr);
	return sub_7FF7AB499410(TagContainer, bWasDBNO);
}

void AFortPlayerController::ClientOnPawnDiedHook(AFortPlayerController* PlayerController, void* DeathReport)
{
	auto DeadPawn = Cast<AFortPlayerPawn>(PlayerController->GetPawn());
	auto DeadPlayerState = Cast<AFortPlayerStateAthena>(PlayerController->GetPlayerState());
	auto KillerPawn = Cast<AFortPlayerPawn>(*(AFortPawn**)(__int64(DeathReport) + MemberOffsets::DeathReport::KillerPawn));

	if (!DeadPawn)
		return ClientOnPawnDiedOriginal(PlayerController, DeathReport);

	static auto DeathInfoStruct = FindObject<UStruct>("/Script/FortniteGame.DeathInfo");
	static auto DeathInfoStructSize = DeathInfoStruct->GetPropertiesSize();

	auto DeathLocation = DeadPawn->GetActorLocation();

	static auto FallDamageEnumValue = 1;

	auto DeathInfo = (void*)(__int64(DeadPlayerState) + MemberOffsets::FortPlayerStateAthena::DeathInfo); // Alloc<void>(DeathInfoStructSize);
	RtlSecureZeroMemory(DeathInfo, DeathInfoStructSize);

	auto Tags = *(FGameplayTagContainer*)(__int64(DeathReport) + MemberOffsets::DeathReport::Tags);

	// LOG_INFO(LogDev, "Tags: {}", Tags.ToStringSimple(true));

	auto DeathCause = ToDeathCause(Tags, false);

	LOG_INFO(LogDev, "DeathCause: {}", (int)DeathCause);

	*(bool*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::bDBNO) = DeadPawn->IsDBNO();
	*(uint8*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::DeathCause) = DeathCause;
	*(FVector*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::DeathLocation) = DeathLocation;

	if (MemberOffsets::DeathInfo::DeathTags != 0)
		*(FGameplayTagContainer*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::DeathTags) = Tags;

	if (MemberOffsets::DeathInfo::bInitialized != 0)
		*(bool*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::bInitialized) = true;

	if (DeathCause == FallDamageEnumValue)
	{
		static auto LastFallDistanceOffset = DeadPawn->GetOffset("LastFallDistance");
		*(float*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::Distance) = DeadPawn->Get<float>(LastFallDistanceOffset);
	}
	else
	{
		*(float*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::Distance) = KillerPawn ? KillerPawn->GetDistanceTo(DeadPawn) : 0;
	}

	static auto PawnDeathLocationOffset = DeadPlayerState->GetOffset("PawnDeathLocation");
	DeadPlayerState->Get<FVector>(PawnDeathLocationOffset) = DeathLocation;

	static auto OnRep_DeathInfoFn = FindObject<UFunction>("/Script/FortniteGame.FortPlayerStateAthena.OnRep_DeathInfo");
	DeadPlayerState->ProcessEvent(OnRep_DeathInfoFn);

	auto WorldInventory = PlayerController->GetWorldInventory();
	
	if (!WorldInventory)
		return ClientOnPawnDiedOriginal(PlayerController, DeathReport);

	auto& ItemInstances = WorldInventory->GetItemList().GetItemInstances();

	for (int i = 0; i < ItemInstances.Num(); i++)
	{
		auto ItemInstance = ItemInstances.at(i);

		if (!ItemInstance)
			continue;

		auto ItemEntry = ItemInstance->GetItemEntry();
		auto WorldItemDefinition = Cast<UFortWorldItemDefinition>(ItemEntry->GetItemDefinition());
		
		if (!WorldItemDefinition)
			continue;

		if (!WorldItemDefinition->ShouldDropOnDeath())
			continue;

		AFortPickup::SpawnPickup(WorldItemDefinition, DeathLocation, ItemEntry->GetCount(), EFortPickupSourceTypeFlag::Player, EFortPickupSpawnSource::PlayerElimination,
			ItemEntry->GetLoadedAmmo());
	}

	return ClientOnPawnDiedOriginal(PlayerController, DeathReport);
}

void AFortPlayerController::ServerBeginEditingBuildingActorHook(AFortPlayerController* PlayerController, ABuildingSMActor* BuildingActorToEdit)
{
	if (!BuildingActorToEdit || !BuildingActorToEdit->IsPlayerPlaced())
		return;

	auto Pawn = Cast<AFortPawn>(PlayerController->GetPawn(), false);

	if (!Pawn)
		return;

	static auto EditToolDef = FindObject<UFortWeaponItemDefinition>("/Game/Items/Weapons/BuildingTools/EditTool.EditTool");

	if (auto EditTool = Cast<AFortWeap_EditingTool>(Pawn->EquipWeaponDefinition(EditToolDef, FGuid{})))
	{
		BuildingActorToEdit->GetEditingPlayer() = PlayerController->GetPlayerState();
		// Onrep?

		EditTool->GetEditActor() = BuildingActorToEdit;
		// Onrep?
	}
}

void AFortPlayerController::ServerEditBuildingActorHook(AFortPlayerController* PlayerController, ABuildingSMActor* BuildingActorToEdit, UClass* NewBuildingClass, int RotationIterations, char bMirrored)
{
	auto PlayerState = (AFortPlayerState*)PlayerController->GetPlayerState();

	if (!BuildingActorToEdit || !NewBuildingClass || BuildingActorToEdit->IsDestroyed() || BuildingActorToEdit->GetEditingPlayer() != PlayerState)
		return;

	// if (!PlayerState || PlayerState->GetTeamIndex() != BuildingActorToEdit->GetTeamIndex()) 
		// return;

	BuildingActorToEdit->GetEditingPlayer() = nullptr;

	static ABuildingSMActor* (*BuildingSMActorReplaceBuildingActor)(ABuildingSMActor*, __int64, UClass*, int, int, uint8_t, AFortPlayerController*) =
		decltype(BuildingSMActorReplaceBuildingActor)(Addresses::ReplaceBuildingActor);

	if (auto BuildingActor = BuildingSMActorReplaceBuildingActor(BuildingActorToEdit, 1, NewBuildingClass, 
		BuildingActorToEdit->GetCurrentBuildingLevel(), RotationIterations, bMirrored, PlayerController))
	{
		BuildingActor->SetPlayerPlaced(true);

		// if (auto PlayerState = Cast<AFortPlayerStateAthena>(PlayerController->GetPlayerState()))
			// BuildingActor->SetTeam(PlayerState->GetTeamIndex());
	}
}

void AFortPlayerController::ServerEndEditingBuildingActorHook(AFortPlayerController* PlayerController, ABuildingSMActor* BuildingActorToStopEditing)
{
	auto Pawn = PlayerController->GetMyFortPawn();

	if (!BuildingActorToStopEditing || !Pawn
		|| BuildingActorToStopEditing->GetEditingPlayer() != PlayerController->GetPlayerState()
		|| BuildingActorToStopEditing->IsDestroyed())
		return;

	auto EditTool = Cast<AFortWeap_EditingTool>(Pawn->GetCurrentWeapon());

	BuildingActorToStopEditing->GetEditingPlayer() = nullptr;
	// BuildingActorToStopEditing->OnRep_EditingPlayer();

	if (EditTool)
	{
		EditTool->GetEditActor() = nullptr;
		// EditTool->OnRep_EditActor();
	}
}