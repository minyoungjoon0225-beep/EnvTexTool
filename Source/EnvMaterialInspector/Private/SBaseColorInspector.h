// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseColorClusterAnalyzer.h"
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class SWidgetSwitcher;
class UTexture2D;
struct FAssetData;

class SBaseColorInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBaseColorInspector) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void HandleTextureChanged(const FAssetData& AssetData);
	FReply HandleAnalyzeClicked();
	bool CanAnalyze() const;
	void ShowMessage(const FText& Message, const FSlateColor& Color);
	bool BuildQuantizedPreview(const FBaseColorAnalysisResult& Result);
	bool BuildColorVariationHeatmapPreview(
		const TArray<float>& Heatmap, int32 Width, int32 Height, const FName& TextureName,
		TStrongObjectPtr<UTexture2D>& OutTexture, FSlateBrush& OutBrush);
	bool BuildHueContrastHeatmapPreview(const FBaseColorAnalysisResult& Result);
	bool BuildTilingPreview(const FBaseColorAnalysisResult& Result);

	TWeakObjectPtr<UTexture2D> SelectedTexture;
	int32 ClusterCount = 16;
	FHueDistributionSettings HueSettings;
	TSharedPtr<SVerticalBox> ResultsBox;
	TStrongObjectPtr<UTexture2D> QuantizedPreviewTexture;
	FSlateBrush QuantizedPreviewBrush;
	TStrongObjectPtr<UTexture2D> ColorVariationHeatmapTexture;
	FSlateBrush ColorVariationHeatmapBrush;
	TStrongObjectPtr<UTexture2D> MesoVariationHeatmapTexture;
	FSlateBrush MesoVariationHeatmapBrush;
	TStrongObjectPtr<UTexture2D> MacroVariationHeatmapTexture;
	FSlateBrush MacroVariationHeatmapBrush;
	TStrongObjectPtr<UTexture2D> HueContrastHeatmapTexture;
	FSlateBrush HueContrastHeatmapBrush;
	TStrongObjectPtr<UTexture2D> TilingPreviewTexture;
	FSlateBrush TilingPreviewBrush;
	TSharedPtr<SWidgetSwitcher> PreviewSwitcher;
	int32 ActivePreviewIndex = 1;
};
