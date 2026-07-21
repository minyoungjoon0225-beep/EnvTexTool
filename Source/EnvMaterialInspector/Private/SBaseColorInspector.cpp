// Copyright Epic Games, Inc. All Rights Reserved.

#include "SBaseColorInspector.h"

#include "AssetRegistry/AssetData.h"
#include "BaseColorClusterAnalyzer.h"
#include "Engine/Texture2D.h"
#include "Styling/AppStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "PropertyCustomizationHelpers.h"
#include "Rendering/DrawElements.h"

#define LOCTEXT_NAMESPACE "SBaseColorInspector"

class SDraggableMosaicViewport : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDraggableMosaicViewport) {}
		SLATE_ARGUMENT(const FSlateBrush*, Image)
		SLATE_ARGUMENT(FVector2D, ImageSize)
		SLATE_ARGUMENT(float, InitialZoom)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Image = InArgs._Image;
		ImageSize = InArgs._ImageSize;
		InitialZoom = FMath::Max(1.0f, InArgs._InitialZoom);
		Zoom = InitialZoom;
		SetClipping(EWidgetClipping::ClipToBounds);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(720.0f, 520.0f);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor::Black);
		if (Image)
		{
			const FVector2D DrawSize = ImageSize * Zoom;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(DrawSize, FSlateLayoutTransform(PanOffset)),
				Image, ESlateDrawEffect::None, FLinearColor::White);
		}
		return LayerId + 1;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			LastCursorPosition = MouseEvent.GetScreenSpacePosition();
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && HasMouseCapture())
		{
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (HasMouseCapture())
		{
			const FVector2D CurrentPosition = MouseEvent.GetScreenSpacePosition();
			PanOffset += CurrentPosition - LastCursorPosition;
			LastCursorPosition = CurrentPosition;
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const FVector2D Cursor = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const FVector2D ImagePoint = (Cursor - PanOffset) / Zoom;
		Zoom = FMath::Clamp(Zoom * FMath::Pow(1.2f, MouseEvent.GetWheelDelta()), 1.0f, 32.0f);
		PanOffset = Cursor - ImagePoint * Zoom;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			Zoom = InitialZoom;
			PanOffset = FVector2D::ZeroVector;
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	const FSlateBrush* Image = nullptr;
	FVector2D ImageSize = FVector2D::ZeroVector;
	FVector2D PanOffset = FVector2D::ZeroVector;
	FVector2D LastCursorPosition = FVector2D::ZeroVector;
	float InitialZoom = 8.0f;
	float Zoom = 8.0f;
};

namespace
{
	TSharedRef<SWidget> MakeLabelValue(const FText& Label, const FText& Value,
		const FText& ChineseTooltip = FText::GetEmpty())
	{
		return SNew(SHorizontalBox)
			.ToolTipText(ChineseTooltip)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock).Text(Label).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Text(Value)
			];
	}

	FText FormatPercent(float Fraction)
	{
		FNumberFormattingOptions Options;
		Options.SetMaximumFractionalDigits(1);
		return FText::AsPercent(Fraction, &Options);
	}

	FText FormatCoverage(float Fraction)
	{
		FNumberFormattingOptions Options;
		Options.SetMaximumFractionalDigits(1);
		return FText::AsNumber(Fraction * 100.0f, &Options);
	}

	FText FormatMetric(float Value)
	{
		FNumberFormattingOptions Options;
		Options.SetMinimumFractionalDigits(3);
		Options.SetMaximumFractionalDigits(3);
		return FText::AsNumber(Value, &Options);
	}

}

void SBaseColorInspector::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(12.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Base Color Analysis"))
					.ToolTipText(LOCTEXT("TitleTooltip", "基础颜色分析"))
					.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("Texture", "Texture"))
						.ToolTipText(LOCTEXT("TextureTooltip", "纹理"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UTexture2D::StaticClass())
						.ObjectPath_Lambda([this]()
						{
							return SelectedTexture.IsValid() ? SelectedTexture->GetPathName() : FString();
						})
						.OnObjectChanged(this, &SBaseColorInspector::HandleTextureChanged)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("ClusterCount", "Cluster Count"))
						.ToolTipText(LOCTEXT("ClusterCountTooltip", "聚类数量"))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SNumericEntryBox<int32>)
						.MinValue(8).MaxValue(32).MinSliderValue(8).MaxSliderValue(32)
						.Value_Lambda([this]() { return TOptional<int32>(ClusterCount); })
						.OnValueChanged_Lambda([this](int32 Value) { ClusterCount = FMath::Clamp(Value, 8, 32); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HueSettingsHeading", "Hue Distribution Settings"))
					.ToolTipText(LOCTEXT("HueSettingsTooltip", "色相分布设置"))
					.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("HueBinsSetting", "Hue Bin Count"))
						.ToolTipText(LOCTEXT("HueBinsTooltip", "色相分箱数量"))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SNumericEntryBox<int32>)
						.MinValue(6).MaxValue(360).MinSliderValue(12).MaxSliderValue(180)
						.Value_Lambda([this]() { return TOptional<int32>(HueSettings.HueBinCount); })
						.OnValueChanged_Lambda([this](int32 Value) { HueSettings.HueBinCount = FMath::Clamp(Value, 6, 360); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("WeightModeSetting", "Weight Mode"))
						.ToolTipText(LOCTEXT("WeightModeTooltip", "权重模式"))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text_Lambda([this]()
						{
							switch (HueSettings.WeightMode)
							{
							case EHueWeightMode::None: return LOCTEXT("WeightNone", "None");
							case EHueWeightMode::SaturationValue: return LOCTEXT("WeightSaturationValue", "Saturation x Value");
							default: return LOCTEXT("WeightSaturation", "Saturation");
							}
						})
						.ToolTipText(LOCTEXT("WeightValueTooltip", "无 / 饱和度 / 饱和度 × 明度"))
						.OnClicked_Lambda([this]()
						{
							const uint8 NextMode = (static_cast<uint8>(HueSettings.WeightMode) + 1) % 3;
							HueSettings.WeightMode = static_cast<EHueWeightMode>(NextMode);
							return FReply::Handled();
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Analyze", "Analyze"))
					.ToolTipText(LOCTEXT("AnalyzeTooltip", "分析"))
					.IsEnabled(this, &SBaseColorInspector::CanAnalyze)
					.OnClicked(this, &SBaseColorInspector::HandleAnalyzeClicked)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(ResultsBox, SVerticalBox)
				]
			]
		]
	];
}

void SBaseColorInspector::HandleTextureChanged(const FAssetData& AssetData)
{
	SelectedTexture = Cast<UTexture2D>(AssetData.GetAsset());
	ResultsBox->ClearChildren();
}

bool SBaseColorInspector::CanAnalyze() const
{
	return SelectedTexture.IsValid();
}

void SBaseColorInspector::ShowMessage(const FText& Message, const FSlateColor& Color)
{
	ResultsBox->ClearChildren();
	ResultsBox->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Text(Message).ColorAndOpacity(Color).AutoWrapText(true)
	];
}

bool SBaseColorInspector::BuildQuantizedPreview(const FBaseColorAnalysisResult& Result)
{
	constexpr int32 MosaicBlockSize = 8;
	QuantizedPreviewTexture.Reset();
	QuantizedPreviewBrush.SetResourceObject(nullptr);
	if (Result.AnalysisWidth <= 0 || Result.AnalysisHeight <= 0 ||
		Result.ClusterIDMap.Num() != Result.AnalysisWidth * Result.AnalysisHeight)
	{
		return false;
	}

	const int32 MosaicWidth = FMath::DivideAndRoundUp(Result.AnalysisWidth, MosaicBlockSize);
	const int32 MosaicHeight = FMath::DivideAndRoundUp(Result.AnalysisHeight, MosaicBlockSize);
	UTexture2D* Texture = UTexture2D::CreateTransient(
		MosaicWidth, MosaicHeight, PF_B8G8R8A8, TEXT("BaseColorQuantizedPreview"));
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
	{
		return false;
	}

	Texture->SRGB = true;
	Texture->Filter = TF_Nearest;
	Texture->NeverStream = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	FColor* Destination = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!Destination)
	{
		Mip.BulkData.Unlock();
		return false;
	}

	for (int32 MosaicY = 0; MosaicY < MosaicHeight; ++MosaicY)
	{
		for (int32 MosaicX = 0; MosaicX < MosaicWidth; ++MosaicX)
		{
			TArray<int32, TInlineAllocator<16>> ClusterCounts;
			ClusterCounts.Init(0, Result.Clusters.Num());
			const int32 EndX = FMath::Min((MosaicX + 1) * MosaicBlockSize, Result.AnalysisWidth);
			const int32 EndY = FMath::Min((MosaicY + 1) * MosaicBlockSize, Result.AnalysisHeight);
			for (int32 SourceY = MosaicY * MosaicBlockSize; SourceY < EndY; ++SourceY)
			{
				for (int32 SourceX = MosaicX * MosaicBlockSize; SourceX < EndX; ++SourceX)
				{
					const int32 Id = Result.ClusterIDMap[SourceY * Result.AnalysisWidth + SourceX];
					if (ClusterCounts.IsValidIndex(Id)) ++ClusterCounts[Id];
				}
			}
			int32 DominantId = 0;
			for (int32 Id = 1; Id < ClusterCounts.Num(); ++Id)
			{
				if (ClusterCounts[Id] > ClusterCounts[DominantId]) DominantId = Id;
			}
			Destination[MosaicY * MosaicWidth + MosaicX] = Result.Clusters.IsValidIndex(DominantId)
				? Result.Clusters[DominantId].Center.ToFColorSRGB()
				: FColor::Black;
		}
	}
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	QuantizedPreviewTexture.Reset(Texture);
	QuantizedPreviewBrush.SetResourceObject(Texture);
	QuantizedPreviewBrush.ImageSize = FVector2D(MosaicWidth, MosaicHeight);
	QuantizedPreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	return true;
}

bool SBaseColorInspector::BuildColorVariationHeatmapPreview(
	const TArray<float>& Heatmap, int32 HeatmapWidth, int32 HeatmapHeight, const FName& TextureName,
	TStrongObjectPtr<UTexture2D>& OutTexture, FSlateBrush& OutBrush)
{
	OutTexture.Reset();
	OutBrush.SetResourceObject(nullptr);
	if (HeatmapWidth <= 0 || HeatmapHeight <= 0 ||
		Heatmap.Num() != HeatmapWidth * HeatmapHeight)
	{
		return false;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(
		HeatmapWidth, HeatmapHeight, PF_B8G8R8A8, TextureName);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
	{
		return false;
	}

	Texture->SRGB = true;
	Texture->Filter = TF_Nearest;
	Texture->NeverStream = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	FColor* Destination = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!Destination)
	{
		Mip.BulkData.Unlock();
		return false;
	}

	for (int32 Index = 0; Index < Heatmap.Num(); ++Index)
	{
		const float Variation = FMath::Clamp(Heatmap[Index], 0.0f, 1.0f);
		// Linear mapping: preview darkness directly corresponds to jump density.
		const uint8 Gray = static_cast<uint8>(FMath::RoundToInt((1.0f - Variation) * 255.0f));
		Destination[Index] = FColor(Gray, Gray, Gray, 255);
	}
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	OutTexture.Reset(Texture);
	OutBrush.SetResourceObject(Texture);
	OutBrush.ImageSize = FVector2D(HeatmapWidth, HeatmapHeight);
	OutBrush.DrawAs = ESlateBrushDrawType::Image;
	return true;
}

bool SBaseColorInspector::BuildHueContrastHeatmapPreview(const FBaseColorAnalysisResult& Result)
{
	HueContrastHeatmapTexture.Reset();
	HueContrastHeatmapBrush.SetResourceObject(nullptr);
	const int32 HeatmapWidth = Result.HueContrastHeatmapWidth;
	const int32 HeatmapHeight = Result.HueContrastHeatmapHeight;
	if (HeatmapWidth <= 0 || HeatmapHeight <= 0 ||
		Result.HueContrastHeatmap.Num() != HeatmapWidth * HeatmapHeight)
	{
		return false;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(
		HeatmapWidth, HeatmapHeight, PF_B8G8R8A8, TEXT("BaseColorHueContrastHeatmap"));
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
	{
		return false;
	}

	Texture->SRGB = true;
	Texture->Filter = TF_Nearest;
	Texture->NeverStream = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	FColor* Destination = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!Destination)
	{
		Mip.BulkData.Unlock();
		return false;
	}
	for (int32 Index = 0; Index < Result.HueContrastHeatmap.Num(); ++Index)
	{
		const float Contrast = FMath::Clamp(Result.HueContrastHeatmap[Index], 0.0f, 1.0f);
		// Display-only gamma makes small, meaningful hue changes visible while
		// preserving the fixed 0-1 analysis score for cross-texture comparison.
		const float DisplayContrast = FMath::Pow(Contrast, 0.35f);
		const uint8 Gray = static_cast<uint8>(FMath::RoundToInt((1.0f - DisplayContrast) * 255.0f));
		Destination[Index] = FColor(Gray, Gray, Gray, 255);
	}
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	HueContrastHeatmapTexture.Reset(Texture);
	HueContrastHeatmapBrush.SetResourceObject(Texture);
	HueContrastHeatmapBrush.ImageSize = FVector2D(HeatmapWidth, HeatmapHeight);
	HueContrastHeatmapBrush.DrawAs = ESlateBrushDrawType::Image;
	return true;
}

FReply SBaseColorInspector::HandleAnalyzeClicked()
{
	FBaseColorAnalysisResult Result;
	FText Error;
	if (!FBaseColorClusterAnalyzer::AnalyzeTexture(SelectedTexture.Get(), ClusterCount, Result, Error, HueSettings))
	{
		ShowMessage(Error, FSlateColor(FLinearColor(0.9f, 0.2f, 0.15f)));
		return FReply::Handled();
	}
	FText VariationPatternText;
	FText VariationPatternTooltip;
	FText ClassificationBasisText;
	FText ClassificationBasisTooltip;
	switch (Result.PixelVariation.Pattern)
	{
	case EPixelVariationPattern::HarmoniousFineDetail:
		VariationPatternText = LOCTEXT("HarmoniousFineDetailPattern", "Harmonious fine detail");
		VariationPatternTooltip = LOCTEXT("HarmoniousFineDetailPatternTooltip", "和谐细节型");
		ClassificationBasisText = FText::Format(
			LOCTEXT("HarmoniousClassificationBasis", "Micro {0} >= 30%; Cohesion {1} >= 60%"),
			FormatPercent(Result.PixelVariation.MicroJumpRate),
			FormatPercent(Result.PixelVariation.FarViewCohesion));
		ClassificationBasisTooltip = FText::Format(
			LOCTEXT("HarmoniousClassificationBasisTooltip", "相邻1px跳变率 {0} 较高，但远距离融合度 {1} 也较高，说明细节丰富且缩小后能够融合为和谐整体。"),
			FormatPercent(Result.PixelVariation.MicroJumpRate),
			FormatPercent(Result.PixelVariation.FarViewCohesion));
		break;
	case EPixelVariationPattern::PersistentMosaicVariation:
		VariationPatternText = LOCTEXT("PersistentMosaicPattern", "Persistent mosaic variation");
		VariationPatternTooltip = LOCTEXT("PersistentMosaicPatternTooltip", "持续马赛克型");
		ClassificationBasisText = FText::Format(
			LOCTEXT("PersistentMosaicClassificationBasis", "Micro {0} >= 30%; Cohesion {1} < 60%"),
			FormatPercent(Result.PixelVariation.MicroJumpRate),
			FormatPercent(Result.PixelVariation.FarViewCohesion));
		ClassificationBasisTooltip = FText::Format(
			LOCTEXT("PersistentMosaicClassificationBasisTooltip", "相邻1px跳变率 {0} 较高，同时远距离融合度 {1} 较低，说明颜色边界在缩小后仍可能持续存在。"),
			FormatPercent(Result.PixelVariation.MicroJumpRate),
			FormatPercent(Result.PixelVariation.FarViewCohesion));
		break;
	case EPixelVariationPattern::LargeColorBlockStructure:
		VariationPatternText = LOCTEXT("LargeColorBlockPattern", "Large color-block structure");
		VariationPatternTooltip = LOCTEXT("LargeColorBlockPatternTooltip", "大色块结构型");
		ClassificationBasisText = FText::Format(
			LOCTEXT("LargeColorBlockClassificationBasis", "Micro {0} < 30%; Cohesion {1} < 60%"),
			FormatPercent(Result.PixelVariation.MicroJumpRate),
			FormatPercent(Result.PixelVariation.FarViewCohesion));
		ClassificationBasisTooltip = FText::Format(
			LOCTEXT("LargeColorBlockClassificationBasisTooltip", "相邻1px跳变率 {0} 较低，但远距离融合度 {1} 也较低，说明局部较平滑但存在缩小后仍可见的大色块结构。"),
			FormatPercent(Result.PixelVariation.MicroJumpRate),
			FormatPercent(Result.PixelVariation.FarViewCohesion));
		break;
	default:
		VariationPatternText = LOCTEXT("SmoothContinuousPattern", "Smooth / locally continuous");
		VariationPatternTooltip = LOCTEXT("SmoothContinuousPatternTooltip", "平滑连续型");
		ClassificationBasisText = FText::Format(
			LOCTEXT("SmoothClassificationBasis", "Micro {0} < 30%; Cohesion {1} >= 60%"),
			FormatPercent(Result.PixelVariation.MicroJumpRate),
			FormatPercent(Result.PixelVariation.FarViewCohesion));
		ClassificationBasisTooltip = FText::Format(
			LOCTEXT("SmoothClassificationBasisTooltip", "相邻1px跳变率 {0} 较低，远距离融合度 {1} 较高，说明局部连续且缩小后整体和谐。"),
			FormatPercent(Result.PixelVariation.MicroJumpRate),
			FormatPercent(Result.PixelVariation.FarViewCohesion));
		break;
	}

	ResultsBox->ClearChildren();
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 8.0f)
	[
		SNew(STextBlock).Text(LOCTEXT("SummaryHeading", "Analysis Summary"))
		.ToolTipText(LOCTEXT("SummaryTooltip", "分析摘要")).Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("SourceResolution", "Texture Resolution"),
			FText::Format(LOCTEXT("ResolutionFormat", "{0} x {1}"), Result.SourceWidth, Result.SourceHeight),
			LOCTEXT("SourceResolutionTooltip", "纹理分辨率"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("AnalysisResolution", "Analysis Resolution"),
			FText::Format(LOCTEXT("AnalysisResolutionFormat", "{0} x {1}"), Result.AnalysisWidth, Result.AnalysisHeight),
			LOCTEXT("AnalysisResolutionTooltip", "分析分辨率"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("TotalPixels", "Total Pixel Count"), FText::AsNumber(Result.ClusterIDMap.Num()),
			LOCTEXT("TotalPixelsTooltip", "总像素数"))
	];
	TSharedRef<SVerticalBox> ClusterColumn = SNew(SVerticalBox);
	ClusterColumn->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 6.0f)
	[
		SNew(STextBlock).Text(LOCTEXT("ClustersHeading", "Color Clusters"))
		.ToolTipText(LOCTEXT("ClustersTooltip", "颜色聚类"))
		.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
	];
	TSharedRef<SUniformGridPanel> ClusterGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 6.0f, 6.0f));
	for (int32 ClusterIndex = 0; ClusterIndex < Result.Clusters.Num(); ++ClusterIndex)
	{
		const FBaseColorCluster& Cluster = Result.Clusters[ClusterIndex];
		const FColor DisplayColor = Cluster.Center.ToFColorSRGB();
		ClusterGrid->AddSlot(ClusterIndex % 2, ClusterIndex / 2)
		[
			SNew(SBorder).Padding(7.0f).BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 9.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(38.0f).HeightOverride(38.0f)
					[
						SNew(SColorBlock).Color(Cluster.Center).ShowBackgroundForAlpha(false)
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(STextBlock).Text(FText::Format(LOCTEXT("ClusterDescription", "Cluster {0}\nRGB({1}, {2}, {3})"),
						Cluster.Id + 1, DisplayColor.R, DisplayColor.G, DisplayColor.B))
					.ToolTipText(FText::Format(LOCTEXT("ClusterTooltipFormat", "聚类 {0}"), Cluster.Id + 1))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Justification(ETextJustify::Right)
					.Text(FText::Format(LOCTEXT("CoverageDescription", "{0}%\n{1} pixels"),
						FormatCoverage(Cluster.Coverage), FText::AsNumber(Cluster.PixelCount)))
					.ToolTipText(LOCTEXT("CoverageTooltip", "覆盖率和像素数量"))
				]
			]
		];
	}
	ClusterColumn->AddSlot().AutoHeight()[ClusterGrid];

	TSharedRef<SVerticalBox> PreviewColumn = SNew(SVerticalBox);
	ActivePreviewIndex = 1;
	const bool bHasClusterPreview = BuildQuantizedPreview(Result);
	const bool bHasMicroVariationHeatmap = BuildColorVariationHeatmapPreview(
		Result.ColorVariationHeatmap, Result.ColorVariationHeatmapWidth, Result.ColorVariationHeatmapHeight,
		FName(TEXT("BaseColorMicroVariationHeatmap")), ColorVariationHeatmapTexture, ColorVariationHeatmapBrush);
	const bool bHasMesoVariationHeatmap = BuildColorVariationHeatmapPreview(
		Result.MesoVariationHeatmap, Result.ColorVariationHeatmapWidth, Result.ColorVariationHeatmapHeight,
		FName(TEXT("BaseColorMesoVariationHeatmap")), MesoVariationHeatmapTexture, MesoVariationHeatmapBrush);
	const bool bHasMacroVariationHeatmap = BuildColorVariationHeatmapPreview(
		Result.MacroVariationHeatmap, Result.ColorVariationHeatmapWidth, Result.ColorVariationHeatmapHeight,
		FName(TEXT("BaseColorMacroVariationHeatmap")), MacroVariationHeatmapTexture, MacroVariationHeatmapBrush);
	const bool bHasHueContrastHeatmap = BuildHueContrastHeatmapPreview(Result);
	if (bHasClusterPreview || bHasMicroVariationHeatmap || bHasMesoVariationHeatmap ||
		bHasMacroVariationHeatmap || bHasHueContrastHeatmap)
	{
		PreviewColumn->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("ContinuityPreviewHeading", "Continuity Preview"))
			.ToolTipText(LOCTEXT("ContinuityPreviewTooltip", "连续性预览"))
			.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
		];
		PreviewColumn->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ClusterMapMode", "Cluster Map"))
				.ToolTipText(LOCTEXT("ClusterMapTooltip", "聚类图"))
				.ButtonColorAndOpacity_Lambda([this]()
				{
					return ActivePreviewIndex == 0
						? FSlateColor(FLinearColor(0.18f, 0.42f, 0.75f, 1.0f))
						: FSlateColor(FLinearColor::White);
				})
				.IsEnabled(bHasClusterPreview)
				.OnClicked_Lambda([this]()
				{
					ActivePreviewIndex = 0;
					if (PreviewSwitcher.IsValid()) PreviewSwitcher->SetActiveWidgetIndex(0);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("MicroHeatmap", "1 px"))
				.ToolTipText(LOCTEXT("MicroHeatmapTooltip", "1 像素跳变密度"))
				.ButtonColorAndOpacity_Lambda([this]()
				{
					return ActivePreviewIndex == 1
						? FSlateColor(FLinearColor(0.18f, 0.42f, 0.75f, 1.0f))
						: FSlateColor(FLinearColor::White);
				})
				.IsEnabled(bHasMicroVariationHeatmap)
				.OnClicked_Lambda([this]()
				{
					ActivePreviewIndex = 1;
					if (PreviewSwitcher.IsValid()) PreviewSwitcher->SetActiveWidgetIndex(1);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("MesoHeatmap", "4 px"))
				.ToolTipText(LOCTEXT("MesoHeatmapTooltip", "4 像素跳变密度"))
				.ButtonColorAndOpacity_Lambda([this]()
				{
					return ActivePreviewIndex == 2
						? FSlateColor(FLinearColor(0.18f, 0.42f, 0.75f, 1.0f))
						: FSlateColor(FLinearColor::White);
				})
				.IsEnabled(bHasMesoVariationHeatmap)
				.OnClicked_Lambda([this]()
				{
					ActivePreviewIndex = 2;
					if (PreviewSwitcher.IsValid()) PreviewSwitcher->SetActiveWidgetIndex(2);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("MacroHeatmap", "16 px"))
				.ToolTipText(LOCTEXT("MacroHeatmapTooltip", "16 像素跳变密度"))
				.ButtonColorAndOpacity_Lambda([this]()
				{
					return ActivePreviewIndex == 3
						? FSlateColor(FLinearColor(0.18f, 0.42f, 0.75f, 1.0f))
						: FSlateColor(FLinearColor::White);
				})
				.IsEnabled(bHasMacroVariationHeatmap)
				.OnClicked_Lambda([this]()
				{
					ActivePreviewIndex = 3;
					if (PreviewSwitcher.IsValid()) PreviewSwitcher->SetActiveWidgetIndex(3);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("HueContrastMode", "Hue Contrast"))
				.ToolTipText(LOCTEXT("HueContrastTooltip", "色相对比"))
				.ButtonColorAndOpacity_Lambda([this]()
				{
					return ActivePreviewIndex == 4
						? FSlateColor(FLinearColor(0.18f, 0.42f, 0.75f, 1.0f))
						: FSlateColor(FLinearColor::White);
				})
				.IsEnabled(bHasHueContrastHeatmap)
				.OnClicked_Lambda([this]()
				{
					ActivePreviewIndex = 4;
					if (PreviewSwitcher.IsValid()) PreviewSwitcher->SetActiveWidgetIndex(4);
					return FReply::Handled();
				})
			]
		];
		PreviewSwitcher = SNew(SWidgetSwitcher)
			+ SWidgetSwitcher::Slot()
			[
				SNew(SDraggableMosaicViewport)
				.Image(&QuantizedPreviewBrush)
				.ImageSize(QuantizedPreviewBrush.ImageSize)
				.InitialZoom(8.0f)
			]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SDraggableMosaicViewport)
				.Image(&ColorVariationHeatmapBrush)
				.ImageSize(ColorVariationHeatmapBrush.ImageSize)
				.InitialZoom(8.0f)
			]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SDraggableMosaicViewport)
				.Image(&MesoVariationHeatmapBrush)
				.ImageSize(MesoVariationHeatmapBrush.ImageSize)
				.InitialZoom(8.0f)
			]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SDraggableMosaicViewport)
				.Image(&MacroVariationHeatmapBrush)
				.ImageSize(MacroVariationHeatmapBrush.ImageSize)
				.InitialZoom(8.0f)
			]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SDraggableMosaicViewport)
				.Image(&HueContrastHeatmapBrush)
				.ImageSize(HueContrastHeatmapBrush.ImageSize)
				.InitialZoom(8.0f)
			];
		if (bHasMicroVariationHeatmap)
		{
			PreviewSwitcher->SetActiveWidgetIndex(1);
		}
		PreviewColumn->AddSlot().AutoHeight()
		[
			SNew(SBox).WidthOverride(520.0f).HeightOverride(430.0f)
			[
				SNew(SBorder).Padding(1.0f).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor::Black)
				[
					PreviewSwitcher.ToSharedRef()
				]
			]
		];
		PreviewColumn->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewHelp", "Left-drag to pan, mouse wheel to zoom, double-click to reset. Darker = higher jump density."))
			.ToolTipText(LOCTEXT("PreviewHelpTooltip", "左键拖动平移，滚轮缩放，双击重置。颜色越黑表示跳变密度越高。"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}

	ResultsBox->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 14.0f, 0.0f)
		[
			ClusterColumn
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			PreviewColumn
		]
	];

	const FPixelVariationResult& Variation = Result.PixelVariation;
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 6.0f)
	[
		SNew(STextBlock).Text(LOCTEXT("PixelVariationHeading", "Pixel Variation Analysis"))
		.ToolTipText(LOCTEXT("PixelVariationTooltip", "像素变化分析"))
		.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("VariationPattern", "Variation Pattern"), VariationPatternText,
			FText::Format(LOCTEXT("VariationPatternRowTooltip", "变化模式：{0}"), VariationPatternTooltip))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("ClassificationBasis", "Classification Basis"), ClassificationBasisText,
			ClassificationBasisTooltip)
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("P95NeighborDifference", "P95 Local Pixel Difference"),
			FText::Format(LOCTEXT("P95DifferenceFormat", "{0} ({1}/255)"),
				FormatMetric(Variation.P95NeighborDifference),
				FText::AsNumber(FMath::RoundToInt(Variation.P95NeighborDifference * 255.0f))),
			LOCTEXT("P95NeighborTooltip", "P95 局部像素差异"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("FarViewP95BlockDifference", "Far-view P95 Block Difference (16x16 average)"),
			FText::Format(LOCTEXT("FarViewP95DifferenceFormat", "{0} ({1}/255)"),
				FormatMetric(Variation.FarViewP95BlockDifference),
				FText::AsNumber(FMath::RoundToInt(Variation.FarViewP95BlockDifference * 255.0f))),
			LOCTEXT("FarViewP95BlockDifferenceTooltip",
				"远距离 P95 色块差异：将每个 16×16 区域平均为一个颜色后，相邻平均色块 RGB 距离的第 95 百分位。"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("FarViewCohesion", "Far-view Cohesion (16x16 average)"),
			FormatPercent(Variation.FarViewCohesion),
			LOCTEXT("FarViewCohesionTooltip", "远距离融合度：先将每个 16×16 区域融合为平均色，再衡量相邻平均色块是否连续。"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("FarViewJumpRate", "Far-view Jump Rate (16x16 average)"),
			FormatPercent(Variation.FarViewJumpRate),
			LOCTEXT("FarViewJumpRateTooltip", "远距离色块跳变率：相邻 16×16 平均色块差异超过 12/255 的比例。"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("DetailDissolutionRatio", "Detail Dissolution Ratio"),
			FormatPercent(Variation.DetailDissolutionRatio),
			LOCTEXT("DetailDissolutionRatioTooltip",
				"细节消解率：16×16 平均后消失的局部 P95 颜色差异比例。数值越高，表示细节在远处越容易融合。"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("MicroJumpRate", "Micro Jump Rate (1 px)"),
			FormatPercent(Variation.MicroJumpRate), LOCTEXT("MicroJumpTooltip", "相邻1px跳变率"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("MesoJumpRate", "Meso Jump Rate (4 px)"),
			FormatPercent(Variation.MesoJumpRate), LOCTEXT("MesoJumpTooltip", "相邻4px跳变率"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("MacroJumpRate", "Macro Jump Rate (16 px)"),
			FormatPercent(Variation.MacroJumpRate), LOCTEXT("MacroJumpTooltip", "相邻16px跳变率"))
	];

	const FHueDistributionResult& Hue = Result.HueDistribution;
	const int32 HueBinCount = Hue.Histogram.Num();
	const float HueBinWidth = HueBinCount > 0 ? 360.0f / HueBinCount : 0.0f;
	const FLinearColor DominantHueColor = Hue.DominantHueBin != INDEX_NONE
		? FLinearColor(Hue.DominantHueDegrees, Hue.DominantMeanSaturation, Hue.DominantMeanValue, 1.0f).HSVToLinearRGB()
		: FLinearColor::Transparent;
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 6.0f)
	[
		SNew(STextBlock).Text(LOCTEXT("HueDistributionHeading", "Hue Distribution"))
		.ToolTipText(LOCTEXT("HueDistributionTooltip", "色相分布"))
		.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("HueEntropy", "Normalized Hue Entropy"), FormatMetric(Hue.NormalizedEntropy),
			LOCTEXT("HueEntropyTooltip", "归一化色相熵"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(
			FText::Format(LOCTEXT("HueUniformityLabel", "Hue Uniformity (+/- {0} degrees)"),
				FText::AsNumber(Hue.AppliedSettings.UniformityHalfRangeDegrees)),
			FormatPercent(Hue.HueUniformity), LOCTEXT("HueUniformityTooltip", "色相统一度"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("ChromaticRatio", "Chromatic Pixel Ratio"), FormatPercent(Hue.ChromaticPixelRatio),
			LOCTEXT("ChromaticRatioTooltip", "彩色像素比例"))
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(SHorizontalBox)
		.ToolTipText(LOCTEXT("DominantHueTooltip", "主导色相范围"))
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DominantHue", "Dominant Hue Range"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(28.0f).HeightOverride(18.0f)
			.Visibility(Hue.DominantHueBin != INDEX_NONE ? EVisibility::Visible : EVisibility::Collapsed)
			[
				SNew(SColorBlock).Color(DominantHueColor).ShowBackgroundForAlpha(false)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Hue.DominantHueBin != INDEX_NONE
				? FText::Format(LOCTEXT("HueDegreesFormat", "{0}-{1} degrees, center {2} (bin {3}/{4})"),
					FText::AsNumber(Hue.DominantHueBin * HueBinWidth),
					FText::AsNumber((Hue.DominantHueBin + 1) * HueBinWidth),
					FText::AsNumber(Hue.DominantHueDegrees),
					Hue.DominantHueBin + 1,
					HueBinCount)
				: LOCTEXT("NoDominantHue", "No chromatic pixels"))
		]
	];
	ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		MakeLabelValue(LOCTEXT("DominantPixelCoverage", "Dominant Hue Coverage"),
			FormatPercent(Hue.DominantHuePixelCoverage), LOCTEXT("DominantCoverageTooltip", "主导色相覆盖率"))
	];
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
