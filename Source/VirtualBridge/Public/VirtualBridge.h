// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVirtualBridge, Log, All);

class FVirtualBridgeModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
    FDelegateHandle SelectionChangedHandle;

    void OnSelectionChanged(UObject* Selection);
    TArray<FString> GetSelectedActorPaths();
    void RegisterSelectionListener();
    bool TryRegisterSelectionListener(float);

    void WriteSelectionToFile(const TArray<FString>& Paths);
};
