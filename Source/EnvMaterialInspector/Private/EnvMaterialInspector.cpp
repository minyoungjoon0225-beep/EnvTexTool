// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnvMaterialInspector.h"

#include "Framework/Docking/TabManager.h"
#include "SBaseColorInspector.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FEnvMaterialInspectorModule"

namespace
{
	const FName BaseColorInspectorTabName(TEXT("EnvMaterialInspector.BaseColorAnalysis"));
}

void FEnvMaterialInspectorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(BaseColorInspectorTabName,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
		{
			return SNew(SDockTab)
				.TabRole(NomadTab)
				[
					SNew(SBaseColorInspector)
				];
		}))
		.SetDisplayName(LOCTEXT("TabTitle", "Base Color Analysis"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Analyze dominant colors and pixel color continuity in a Texture2D."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FEnvMaterialInspectorModule::RegisterMenus));
}

void FEnvMaterialInspectorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(BaseColorInspectorTabName);
}

void FEnvMaterialInspectorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("Tools");
	Section.AddMenuEntry(
		"OpenEnvMaterialInspector",
		LOCTEXT("MenuLabel", "Base Color Analysis"),
		LOCTEXT("MenuTooltip", "Open the Env Material Inspector Base Color analysis tool."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]
		{
			FGlobalTabmanager::Get()->TryInvokeTab(BaseColorInspectorTabName);
		})));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FEnvMaterialInspectorModule, EnvMaterialInspector)
