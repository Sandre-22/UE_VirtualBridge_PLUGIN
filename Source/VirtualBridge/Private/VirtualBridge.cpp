// Copyright Epic Games, Inc. All Rights Reserved.
#include "VirtualBridge.h"
#include "CoreMinimal.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "Json.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FVirtualBridgeModule"

DEFINE_LOG_CATEGORY(LogVirtualBridge);

void FVirtualBridgeModule::StartupModule()
{
	UE_LOG(LogVirtualBridge, Error, TEXT("=== VIRTUALBRIDGE PLUGIN LOADED SUCCESSFULLY! ==="));

	if (GEditor)
	{
		RegisterSelectionListener();
	}
	else
	{
		UE_LOG(LogVirtualBridge, Warning, TEXT("GEditor not ready, deferring selection listener registration"));

		// Try again after a short delay using a timer
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FVirtualBridgeModule::TryRegisterSelectionListener), 1.0f);
	}
}

void FVirtualBridgeModule::ShutdownModule()
{
	if (SelectionChangedHandle.IsValid())
	{
		USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);
		UE_LOG(LogVirtualBridge, Warning, TEXT("Selection listener removed"));
	}
	UE_LOG(LogVirtualBridge, Error, TEXT("=== VIRTUALBRIDGE PLUGIN UNLOADED ==="));
}

void FVirtualBridgeModule::OnSelectionChanged(UObject* Selection)
{
	UE_LOG(LogVirtualBridge, Error, TEXT("=== OnSelectionChanged() CALLED ==="));

	TArray<FString> SelectedPaths = GetSelectedActorPaths();
	UE_LOG(LogVirtualBridge, Error, TEXT("GetSelectedActorPaths() returned %d items"), SelectedPaths.Num());

	// Write selection to JSON file
	WriteSelectionToFile(SelectedPaths);

	if (SelectedPaths.Num() > 0)
	{
		for (const FString& Path : SelectedPaths)
		{
			UE_LOG(LogVirtualBridge, Warning, TEXT("Selected Actor: %s"), *Path);
		}
	}
	else
	{
		UE_LOG(LogVirtualBridge, Warning, TEXT("No actors selected"));
	}
}

TArray<FString> FVirtualBridgeModule::GetSelectedActorPaths()
{
	UE_LOG(LogVirtualBridge, Error, TEXT("=== GetSelectedActorPaths() CALLED ==="));

	TArray<FString> Paths;
	if (GEditor)
	{
		int32 ActorCount = 0;
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			ActorCount++;
			if (AActor* Actor = Cast<AActor>(*It))
			{
				FString ActorPath = Actor->GetPathName();
				Paths.Add(ActorPath);
				UE_LOG(LogVirtualBridge, Error, TEXT("Found Actor #%d: %s"), ActorCount, *ActorPath);
			}
		}
		UE_LOG(LogVirtualBridge, Error, TEXT("Total actors processed: %d"), ActorCount);
	}
	else
	{
		UE_LOG(LogVirtualBridge, Error, TEXT("GEditor is NULL in GetSelectedActorPaths()"));
	}

	return Paths;
}

void FVirtualBridgeModule::RegisterSelectionListener()
{
	if (GEditor && !SelectionChangedHandle.IsValid())
	{
		SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(this, &FVirtualBridgeModule::OnSelectionChanged);
		UE_LOG(LogVirtualBridge, Warning, TEXT("Selection change listener registered"));
	}
}

bool FVirtualBridgeModule::TryRegisterSelectionListener(float DeltaTime)
{
	if (GEditor)
	{
		RegisterSelectionListener();
		return false; // Stop the ticker
	}

	UE_LOG(LogVirtualBridge, Warning, TEXT("Still waiting for GEditor..."));
	return true; // Keep trying
}

void FVirtualBridgeModule::WriteSelectionToFile(const TArray<FString>& Paths)
{
	// Create JSON object
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	if (Paths.Num() > 0)
	{
		// Create JSON array for selected actors
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		for (const FString& Path : Paths)
		{
			JsonArray.Add(MakeShareable(new FJsonValueString(Path)));
		}

		JsonObject->SetArrayField("selectedActors", JsonArray);
		JsonObject->SetStringField("primarySelection", Paths[0]); // First selected actor
		JsonObject->SetNumberField("count", Paths.Num());
	}
	else
	{
		// No selection
		JsonObject->SetArrayField("selectedActors", TArray<TSharedPtr<FJsonValue>>());
		JsonObject->SetStringField("primarySelection", TEXT(""));
		JsonObject->SetNumberField("count", 0);
	}

	JsonObject->SetStringField("timestamp", FDateTime::Now().ToString());
	JsonObject->SetBoolField("hasSelection", Paths.Num() > 0);

	// Convert to JSON string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	// Write to file in project directory
	FString FilePath = FPaths::ProjectDir() + TEXT("selection.json");
	if (FFileHelper::SaveStringToFile(OutputString, *FilePath))
	{
		UE_LOG(LogVirtualBridge, Log, TEXT("Selection written to: %s"), *FilePath);
	}
	else
	{
		UE_LOG(LogVirtualBridge, Error, TEXT("Failed to write selection file"));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVirtualBridgeModule, VirtualBridge)