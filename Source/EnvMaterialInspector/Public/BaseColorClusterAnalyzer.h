// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;

enum class EHueWeightMode : uint8
{
	None,
	Saturation,
	SaturationValue
};

struct ENVMATERIALINSPECTOR_API FHueDistributionSettings
{
	int32 HueBinCount = 360;
	float SaturationThreshold = 0.02f;
	float ValueThreshold = 0.05f;
	float UniformityHalfRangeDegrees = 15.0f;
	EHueWeightMode WeightMode = EHueWeightMode::Saturation;
};

struct ENVMATERIALINSPECTOR_API FHueDistributionResult
{
	FHueDistributionSettings AppliedSettings;
	/** Shannon entropy normalized to the 0-1 range. */
	float NormalizedEntropy = 0.0f;
	/** Unweighted chromatic pixel ratio within +/- UniformityHalfRangeDegrees of the dominant hue. */
	float HueUniformity = 0.0f;
	float ChromaticPixelRatio = 0.0f;
	int32 DominantHueBin = INDEX_NONE;
	float DominantHueDegrees = 0.0f;
	float DominantMeanSaturation = 0.0f;
	float DominantMeanValue = 0.0f;
	/** Dominant bin weight divided by total histogram weight. */
	float DominantHueCoverage = 0.0f;
	/** Dominant bin pixel count divided by all chromatic pixels. */
	float DominantHuePixelCoverage = 0.0f;
	int64 ValidPixelCount = 0;
	int64 ChromaticPixelCount = 0;
	/** Raw weighted bin values, not normalized probabilities. */
	TArray<float> Histogram;
	/** Unweighted chromatic pixel count for each hue bin. */
	TArray<int64> PixelHistogram;
};

struct ENVMATERIALINSPECTOR_API FSaturationAnalysisResult
{
	/** Median display-space HSV saturation of visible, non-near-black pixels. */
	float MedianSaturation = 0.0f;
	int64 ValidPixelCount = 0;
};

struct ENVMATERIALINSPECTOR_API FLightnessAnalysisResult
{
	/** Median display-space Rec.709 luma. */
	float MedianLuminance = 0.0f;
	/** Fifth percentile of display-space Rec.709 luma. */
	float P05Luminance = 0.0f;
	/** Ninety-fifth percentile of display-space Rec.709 luma. */
	float P95Luminance = 0.0f;
	/** Robust display-space luminance range: P95 - P05. */
	float RobustLuminanceRange = 0.0f;
	int64 ValidPixelCount = 0;
};

struct ENVMATERIALINSPECTOR_API FTilingPreviewResult
{
	enum class ESeamVisibility : uint8
	{
		NotVisible,
		NearViewOnly,
		PersistentSeam
	};

	struct FSeamVisibilityMetrics
	{
		/** Seam line energy divided by nearby parallel-line energy. Around 1 means normal. */
		float NearViewLineSaliency = 1.0f;
		float MidViewLineSaliency = 1.0f;
		float FarViewLineSaliency = 1.0f;
		/** 0 means matching boundary structure; 1 means strongly decorrelated structure. */
		float StructuralBreak = 0.0f;
		/** Provisional 60% reference only; does not affect the visibility classification. */
		bool bExceedsStructuralBreakReference = false;
		ESeamVisibility Visibility = ESeamVisibility::NotVisible;
	};

	FSeamVisibilityMetrics LeftRightVisibility;
	FSeamVisibilityMetrics TopBottomVisibility;
	int32 PreviewWidth = 0;
	int32 PreviewHeight = 0;
	/** Display-space BGRA colors for a size-limited 2x2 tiled preview. */
	TArray<FColor> PreviewPixels;
};

/** One coverage-sorted color cluster produced by Base Color analysis. */
struct ENVMATERIALINSPECTOR_API FBaseColorCluster
{
	int32 Id = INDEX_NONE;
	FLinearColor Center = FLinearColor::Black;
	int32 PixelCount = 0;
	float Coverage = 0.0f;
};

enum class EPixelVariationPattern : uint8
{
	HarmoniousFineDetail,
	PersistentMosaicVariation,
	SmoothContinuous,
	LargeColorBlockStructure
};

struct ENVMATERIALINSPECTOR_API FPixelVariationResult
{
	/** Actual source-pixel scales used after normalization to a 2048px reference dimension. */
	int32 MicroOffset = 1;
	int32 MesoOffset = 4;
	int32 MacroOffset = 16;
	int32 FarViewBlockSize = 16;
	int32 HeatmapBlockSize = 8;
	/** 95th percentile of resolution-normalized micro-scale sRGB neighbor distance. */
	float P95NeighborDifference = 0.0f;
	/** Jump rates measured at source-pixel offsets equivalent to 1, 4 and 16 px at 2048 resolution. */
	float MicroJumpRate = 0.0f;
	float MesoJumpRate = 0.0f;
	float MacroJumpRate = 0.0f;
	/** Metrics measured after averaging each resolution-normalized source region to one color. */
	float FarViewP95BlockDifference = 0.0f;
	float FarViewCohesion = 1.0f;
	float FarViewJumpRate = 0.0f;
	/** Fraction of the local P95 color difference removed by normalized far-view averaging. */
	float DetailDissolutionRatio = 0.0f;
	EPixelVariationPattern Pattern = EPixelVariationPattern::SmoothContinuous;
};

/** Quantitative and spatial results for one analyzed Texture2D. */
struct ENVMATERIALINSPECTOR_API FBaseColorAnalysisResult
{
	int32 SourceWidth = 0;
	int32 SourceHeight = 0;
	int32 AnalysisWidth = 0;
	int32 AnalysisHeight = 0;
	TArray<FBaseColorCluster> Clusters;

	/** Row-major cluster IDs; contains AnalysisWidth * AnalysisHeight entries. */
	TArray<int32> ClusterIDMap;
	/** Resolution-normalized micro-scale local jump-density scores, row-major. */
	TArray<float> ColorVariationHeatmap;
	/** Resolution-normalized meso-scale local jump-density scores, row-major. */
	TArray<float> MesoVariationHeatmap;
	/** Resolution-normalized macro-scale local jump-density scores, row-major. */
	TArray<float> MacroVariationHeatmap;
	int32 ColorVariationHeatmapWidth = 0;
	int32 ColorVariationHeatmapHeight = 0;
	FPixelVariationResult PixelVariation;
	/** Normalized 8x8-block neighbor circular-hue contrast scores, row-major. */
	TArray<float> HueContrastHeatmap;
	int32 HueContrastHeatmapWidth = 0;
	int32 HueContrastHeatmapHeight = 0;
	FHueDistributionResult HueDistribution;
	FSaturationAnalysisResult SaturationAnalysis;
	FLightnessAnalysisResult LightnessAnalysis;
	FTilingPreviewResult TilingPreview;
};

/** Editor-only TextureSource Base Color analyzer. */
class ENVMATERIALINSPECTOR_API FBaseColorClusterAnalyzer
{
public:
	static bool AnalyzeTexture(
		UTexture2D* Texture,
		int32 ClusterCount,
		FBaseColorAnalysisResult& OutResult,
		FText& OutError,
		const FHueDistributionSettings& HueSettings = FHueDistributionSettings());
};
