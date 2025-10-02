// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Http.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVirtualBridge, Log, All);

class FVirtualBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
    FDelegateHandle SelectionChangedHandle;
    FString LoupedeckEndpoint;

    void OnSelectionChanged(UObject* Selection);
    TArray<FString> GetSelectedActorPaths();
    void RegisterSelectionListener();
    bool TryRegisterSelectionListener(float);

    //void WriteSelectionToFile(const TArray<FString>& Paths);
    void SendSelectionToLoupedeck(const TArray<FString>& Paths);
    void OnHttpResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    void LoadConfig();
};
