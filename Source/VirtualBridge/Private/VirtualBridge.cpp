// Copyright Epic Games, Inc. All Rights Reserved.
#include "VirtualBridge.h"
#include "CoreMinimal.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Json.h"
//#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FVirtualBridgeModule"

DEFINE_LOG_CATEGORY(LogVirtualBridge);

void FVirtualBridgeModule::StartupModule()
{
	UE_LOG(LogVirtualBridge, Error, TEXT("=== VIRTUALBRIDGE PLUGIN LOADED SUCCESSFULLY! ==="));

	LoadConfig();

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
	}
	UE_LOG(LogVirtualBridge, Error, TEXT("=== VIRTUALBRIDGE PLUGIN UNLOADED ==="));
}

void FVirtualBridgeModule::LoadConfig() {
	// Load Loupedeck endpoint from config file
	FString ConfigPath = FPaths::ProjectDir() / TEXT("VirtualBridgeConfig.json");
	FString ConfigContent;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// If file exists, load it
	if (PlatformFile.FileExists(*ConfigPath))
	{
		if (FFileHelper::LoadFileToString(ConfigContent, *ConfigPath))
		{
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConfigContent);

			if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
			{
				LoupedeckEndpoint = JsonObject->GetStringField("LoupedeckEndpoint");
				UE_LOG(LogVirtualBridge, Log, TEXT("Loaded Loupedeck endpoint: %s"), *LoupedeckEndpoint);
				return; // Done
			}
		}

		// If we get here, file exists but failed to parse
		UE_LOG(LogVirtualBridge, Warning, TEXT("Config file invalid, using default endpoint."));
	}
	else
	{
		UE_LOG(LogVirtualBridge, Warning, TEXT("Config file not found, generating default config."));
	}

	// --- Default fallback ---
	LoupedeckEndpoint = TEXT("http://localhost:7070/selection");

	// Build JSON object for default config
	TSharedPtr<FJsonObject> DefaultJson = MakeShareable(new FJsonObject);
	DefaultJson->SetStringField(TEXT("LoupedeckEndpoint"), LoupedeckEndpoint);

	// Serialize JSON to string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(DefaultJson.ToSharedRef(), Writer);

	// Save to disk
	if (FFileHelper::SaveStringToFile(OutputString, *ConfigPath))
	{
		UE_LOG(LogVirtualBridge, Log, TEXT("Default config.json created at %s"), *ConfigPath);
	}
	else
	{
		UE_LOG(LogVirtualBridge, Error, TEXT("Failed to create default config.json at %s"), *ConfigPath);
	}
}

void FVirtualBridgeModule::RegisterSelectionListener() {
	if (GEditor && !SelectionChangedHandle.IsValid()) {
		SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(this, &FVirtualBridgeModule::OnSelectionChanged);
		UE_LOG(LogVirtualBridge, Warning, TEXT("Selection listener registered"))
	}
}

bool FVirtualBridgeModule::TryRegisterSelectionListener(float DeltaTime) {
	if (GEditor) {
		RegisterSelectionListener();
		return false;
	}
	return true;
}

void FVirtualBridgeModule::OnSelectionChanged(UObject* Selection)
{
	UE_LOG(LogVirtualBridge, Error, TEXT("=== OnSelectionChanged() CALLED ==="));

	TArray<FString> SelectedPaths = GetSelectedActorPaths();
	UE_LOG(LogVirtualBridge, Error, TEXT("GetSelectedActorPaths() returned %d items"), SelectedPaths.Num());

	// Write selection to JSON file
	// WriteSelectionToFile(SelectedPaths);

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

	// Send to Loupedeck
	SendSelectionToLoupedeck(SelectedPaths);
}

TArray<FString> FVirtualBridgeModule::GetSelectedActorPaths()
{
	TArray<FString> Paths;
	if (GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				Paths.Add(Actor->GetPathName());
			}
		}
	}
	return Paths;
}

void FVirtualBridgeModule::SendSelectionToLoupedeck(const TArray<FString>& Paths) {
	//create JSON payload
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	if (Paths.Num() > 0) {
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		for (const FString& Path : Paths) {
			JsonArray.Add(MakeShareable(new FJsonValueString(Path)));
		}

		JsonObject->SetArrayField("selectedActors", JsonArray);
		JsonObject->SetStringField("primarySelection", Paths[0]);
		JsonObject->SetNumberField("count", Paths.Num());
	}
	else {
		JsonObject->SetArrayField("selectedActors", TArray<TSharedPtr<FJsonValue>>());
		JsonObject->SetStringField("primarySelection", TEXT(""));
		JsonObject->SetNumberField("count", 0);
	}

	JsonObject->SetBoolField("hasSelection", Paths.Num() > 0);

	// Serialize to string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	// Create HTTP Request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb("POST");
	HttpRequest->SetURL(LoupedeckEndpoint);
	HttpRequest->SetHeader("Content-Type", "application/json");
	HttpRequest->SetContentAsString(OutputString);
	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FVirtualBridgeModule::OnHttpResponseReceived);

	if (HttpRequest->ProcessRequest()) {
		UE_LOG(LogVirtualBridge, Log, TEXT("Sent selection to Loupedeck: %s"), *OutputString);
	}
	else {
		UE_LOG(LogVirtualBridge, Error, TEXT("Failed to send HTTP request"));
	}
}

void FVirtualBridgeModule::OnHttpResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful) {
	if (bWasSuccessful && Response.IsValid())
	{
		UE_LOG(LogVirtualBridge, Log, TEXT("Loupedeck acknowledged: %d"), Response->GetResponseCode());
	}
	else
	{
		UE_LOG(LogVirtualBridge, Warning, TEXT("Failed to reach Loupedeck (is it running?)"));
	}
}


// old
//void FVirtualBridgeModule::RegisterSelectionListener()
//{
//	if (GEditor && !SelectionChangedHandle.IsValid())
//	{
//		SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(this, &FVirtualBridgeModule::OnSelectionChanged);
//		UE_LOG(LogVirtualBridge, Warning, TEXT("Selection change listener registered"));
//	}
//}
//
//bool FVirtualBridgeModule::TryRegisterSelectionListener(float DeltaTime)
//{
//	if (GEditor)
//	{
//		RegisterSelectionListener();
//		return false; // Stop the ticker
//	}
//
//	UE_LOG(LogVirtualBridge, Warning, TEXT("Still waiting for GEditor..."));
//	return true; // Keep trying
//}

//void FVirtualBridgeModule::WriteSelectionToFile(const TArray<FString>& Paths)
//{
//	// Create JSON object
//	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
//
//	if (Paths.Num() > 0)
//	{
//		// Create JSON array for selected actors
//		TArray<TSharedPtr<FJsonValue>> JsonArray;
//		for (const FString& Path : Paths)
//		{
//			JsonArray.Add(MakeShareable(new FJsonValueString(Path)));
//		}
//
//		JsonObject->SetArrayField("selectedActors", JsonArray);
//		JsonObject->SetStringField("primarySelection", Paths[0]); // First selected actor
//		JsonObject->SetNumberField("count", Paths.Num());
//	}
//	else
//	{
//		// No selection
//		JsonObject->SetArrayField("selectedActors", TArray<TSharedPtr<FJsonValue>>());
//		JsonObject->SetStringField("primarySelection", TEXT(""));
//		JsonObject->SetNumberField("count", 0);
//	}
//
//	JsonObject->SetStringField("timestamp", FDateTime::Now().ToString());
//	JsonObject->SetBoolField("hasSelection", Paths.Num() > 0);
//
//	// Convert to JSON string
//	FString OutputString;
//	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
//	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
//
//	// Write to file in project directory
//	FString FilePath = FPaths::ProjectDir() + TEXT("selection.json");
//	if (FFileHelper::SaveStringToFile(OutputString, *FilePath))
//	{
//		UE_LOG(LogVirtualBridge, Log, TEXT("Selection written to: %s"), *FilePath);
//	}
//	else
//	{
//		UE_LOG(LogVirtualBridge, Error, TEXT("Failed to write selection file"));
//	}
//}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVirtualBridgeModule, VirtualBridge)