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
	/** 95th percentile of one-pixel sRGB neighbor distance. */
	float P95NeighborDifference = 0.0f;
	/** Moderate jump rates measured at 1, 4 and 16 pixel offsets. */
	float MicroJumpRate = 0.0f;
	float MesoJumpRate = 0.0f;
	float MacroJumpRate = 0.0f;
	/** Metrics measured after averaging each 16x16 source region to one color. */
	float FarViewP95BlockDifference = 0.0f;
	float FarViewCohesion = 1.0f;
	float FarViewJumpRate = 0.0f;
	/** Fraction of the local P95 color difference removed by 16x16 averaging. */
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
	/** Local 8x8-block jump-density scores at 1 px, row-major. */
	TArray<float> ColorVariationHeatmap;
	/** Local 8x8-block jump-density scores at 4 px, row-major. */
	TArray<float> MesoVariationHeatmap;
	/** Local 8x8-block jump-density scores at 16 px, row-major. */
	TArray<float> MacroVariationHeatmap;
	int32 ColorVariationHeatmapWidth = 0;
	int32 ColorVariationHeatmapHeight = 0;
	FPixelVariationResult PixelVariation;
	/** Normalized 8x8-block neighbor circular-hue contrast scores, row-major. */
	TArray<float> HueContrastHeatmap;
	int32 HueContrastHeatmapWidth = 0;
	int32 HueContrastHeatmapHeight = 0;
	FHueDistributionResult HueDistribution;
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
