// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseColorClusterAnalyzer.h"

#include "Algo/Sort.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureDefines.h"
#include "ImageCore.h"
#include "TextureCompressorModule.h"

#define LOCTEXT_NAMESPACE "BaseColorClusterAnalyzer"

namespace
{
	constexpr int32 MaxKMeansIterations = 32;
	constexpr float ConvergenceThresholdSquared = 1.e-8f;

	float DistanceSquared(const FLinearColor& A, const FLinearColor& B)
	{
		return FMath::Square(A.R - B.R) + FMath::Square(A.G - B.G) + FMath::Square(A.B - B.B);
	}

	void BuildAnalysisPixels(const FImage& LinearImage, TArray<FLinearColor>& OutPixels)
	{
		const TArrayView64<const FLinearColor> LinearPixels = LinearImage.AsRGBA32F();
		OutPixels.SetNumUninitialized(LinearPixels.Num());
		for (int64 PixelIndex = 0; PixelIndex < LinearPixels.Num(); ++PixelIndex)
		{
			const FColor SRGB = LinearPixels[PixelIndex].ToFColorSRGB();
			OutPixels[PixelIndex] = FLinearColor(
				SRGB.R / 255.0f, SRGB.G / 255.0f, SRGB.B / 255.0f, 1.0f);
		}
	}

	float PixelDistance(const FLinearColor& A, const FLinearColor& B)
	{
		return FMath::Sqrt(
			FMath::Square(A.R - B.R) +
			FMath::Square(A.G - B.G) +
			FMath::Square(A.B - B.B)) / FMath::Sqrt(3.0f);
	}

	void BuildColorVariationHeatmap(const TArray<FLinearColor>& Pixels, int32 Width, int32 Height,
		FBaseColorAnalysisResult& OutResult)
	{
		constexpr int32 BlockSize = 8;
		constexpr float ModerateThreshold = 12.0f / 255.0f;
		constexpr int32 DifferenceHistogramBins = 512;

		FPixelVariationResult& Variation = OutResult.PixelVariation;
		TArray<int64> DifferenceHistogram;
		DifferenceHistogram.Init(0, DifferenceHistogramBins);
		int64 MicroEdgeCount = 0;

		auto AccumulateScale = [&](int32 Offset, bool bCollectDetailedMetrics)
		{
			int64 EdgeCount = 0;
			int64 JumpCount = 0;
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					const int32 Index = Y * Width + X;
					auto AddEdge = [&](int32 NeighborIndex)
					{
						const FLinearColor& A = Pixels[Index];
						const FLinearColor& B = Pixels[NeighborIndex];
						++EdgeCount;
						if (bCollectDetailedMetrics)
						{
							const float Distance = PixelDistance(A, B);
							JumpCount += Distance >= ModerateThreshold;
							const int32 Bin = FMath::Clamp(
								FMath::FloorToInt(Distance * DifferenceHistogramBins),
								0, DifferenceHistogramBins - 1);
							++DifferenceHistogram[Bin];
						}
						else
						{
							const float DistanceSquaredNormalized = (
								FMath::Square(A.R - B.R) +
								FMath::Square(A.G - B.G) +
								FMath::Square(A.B - B.B)) / 3.0f;
							JumpCount += DistanceSquaredNormalized >= FMath::Square(ModerateThreshold);
						}
					};
					if (X + Offset < Width) AddEdge(Index + Offset);
					if (Y + Offset < Height) AddEdge(Index + Offset * Width);
				}
			}
			if (bCollectDetailedMetrics) MicroEdgeCount = EdgeCount;
			return EdgeCount > 0
				? static_cast<float>(JumpCount) / static_cast<float>(EdgeCount)
				: 0.0f;
		};

		Variation.MicroJumpRate = AccumulateScale(1, true);
		Variation.MesoJumpRate = AccumulateScale(4, false);
		Variation.MacroJumpRate = AccumulateScale(16, false);
		if (MicroEdgeCount > 0)
		{
			const int64 P95Target = FMath::CeilToInt64(MicroEdgeCount * 0.95);
			int64 Accumulated = 0;
			for (int32 Bin = 0; Bin < DifferenceHistogramBins; ++Bin)
			{
				Accumulated += DifferenceHistogram[Bin];
				if (Accumulated >= P95Target)
				{
					Variation.P95NeighborDifference =
						static_cast<float>(Bin + 1) / DifferenceHistogramBins;
					break;
				}
			}
		}

		// Simulate a distant mip-like view by averaging source colors inside 16x16
		// regions before comparing adjacent regions. Fine detail that blends at a
		// distance disappears here, while persistent mosaic blocks remain distinct.
		constexpr int32 FarViewBlockSize = 16;
		constexpr float FarViewLowDifference = 16.0f / 255.0f;
		constexpr float FarViewHighDifference = 22.0f / 255.0f;
		const int32 FarViewWidth = FMath::DivideAndRoundUp(Width, FarViewBlockSize);
		const int32 FarViewHeight = FMath::DivideAndRoundUp(Height, FarViewBlockSize);
		TArray<FLinearColor> FarViewColors;
		FarViewColors.SetNumZeroed(FarViewWidth * FarViewHeight);
		for (int32 BlockY = 0; BlockY < FarViewHeight; ++BlockY)
		{
			for (int32 BlockX = 0; BlockX < FarViewWidth; ++BlockX)
			{
				FLinearColor Sum = FLinearColor::Black;
				int32 PixelCount = 0;
				const int32 EndX = FMath::Min((BlockX + 1) * FarViewBlockSize, Width);
				const int32 EndY = FMath::Min((BlockY + 1) * FarViewBlockSize, Height);
				for (int32 Y = BlockY * FarViewBlockSize; Y < EndY; ++Y)
				{
					for (int32 X = BlockX * FarViewBlockSize; X < EndX; ++X)
					{
						Sum += Pixels[Y * Width + X];
						++PixelCount;
					}
				}
				FarViewColors[BlockY * FarViewWidth + BlockX] =
					PixelCount > 0 ? Sum / static_cast<float>(PixelCount) : FLinearColor::Black;
			}
		}

		TArray<int64> FarViewDifferenceHistogram;
		FarViewDifferenceHistogram.Init(0, DifferenceHistogramBins);
		int64 FarViewEdgeCount = 0;
		int64 FarViewJumpCount = 0;
		float FarViewP95BlockDifference = 0.0f;
		for (int32 Y = 0; Y < FarViewHeight; ++Y)
		{
			for (int32 X = 0; X < FarViewWidth; ++X)
			{
				const int32 Index = Y * FarViewWidth + X;
				auto AddFarViewEdge = [&](int32 NeighborIndex)
				{
					const float Distance = PixelDistance(FarViewColors[Index], FarViewColors[NeighborIndex]);
					++FarViewEdgeCount;
					FarViewJumpCount += Distance >= ModerateThreshold;
					const int32 Bin = FMath::Clamp(
						FMath::FloorToInt(Distance * DifferenceHistogramBins), 0, DifferenceHistogramBins - 1);
					++FarViewDifferenceHistogram[Bin];
				};
				if (X + 1 < FarViewWidth) AddFarViewEdge(Index + 1);
				if (Y + 1 < FarViewHeight) AddFarViewEdge(Index + FarViewWidth);
			}
		}
		if (FarViewEdgeCount > 0)
		{
			Variation.FarViewJumpRate = static_cast<float>(FarViewJumpCount) / FarViewEdgeCount;
			const int64 P95Target = FMath::CeilToInt64(FarViewEdgeCount * 0.95);
			int64 Accumulated = 0;
			for (int32 Bin = 0; Bin < DifferenceHistogramBins; ++Bin)
			{
				Accumulated += FarViewDifferenceHistogram[Bin];
				if (Accumulated >= P95Target)
				{
					FarViewP95BlockDifference =
						static_cast<float>(Bin + 1) / DifferenceHistogramBins;
					break;
				}
			}
		}
		Variation.FarViewP95BlockDifference = FarViewP95BlockDifference;
		const float CohesionT = FMath::Clamp(
			(FarViewP95BlockDifference - FarViewLowDifference) /
			(FarViewHighDifference - FarViewLowDifference), 0.0f, 1.0f);
		Variation.FarViewCohesion = 1.0f - CohesionT * CohesionT * (3.0f - 2.0f * CohesionT);
		if (Variation.P95NeighborDifference > UE_SMALL_NUMBER)
		{
			Variation.DetailDissolutionRatio = FMath::Clamp(
				1.0f - FarViewP95BlockDifference / Variation.P95NeighborDifference,
				0.0f, 1.0f);
		}
		else
		{
			// A flat source has no detail to dissolve.
			Variation.DetailDissolutionRatio = 0.0f;
		}

		constexpr float HighMicroRateThreshold = 0.30f;
		constexpr float HighCohesionThreshold = 0.60f;
		const bool bHighMicroRate = Variation.MicroJumpRate >= HighMicroRateThreshold;
		const bool bHighFarViewCohesion = Variation.FarViewCohesion >= HighCohesionThreshold;
		if (bHighMicroRate && bHighFarViewCohesion)
		{
			Variation.Pattern = EPixelVariationPattern::HarmoniousFineDetail;
		}
		else if (bHighMicroRate && !bHighFarViewCohesion)
		{
			Variation.Pattern = EPixelVariationPattern::PersistentMosaicVariation;
		}
		else if (!bHighMicroRate && bHighFarViewCohesion)
		{
			Variation.Pattern = EPixelVariationPattern::SmoothContinuous;
		}
		else
		{
			Variation.Pattern = EPixelVariationPattern::LargeColorBlockStructure;
		}

		OutResult.ColorVariationHeatmapWidth = FMath::DivideAndRoundUp(Width, BlockSize);
		OutResult.ColorVariationHeatmapHeight = FMath::DivideAndRoundUp(Height, BlockSize);
		OutResult.ColorVariationHeatmap.Init(0.0f,
			OutResult.ColorVariationHeatmapWidth * OutResult.ColorVariationHeatmapHeight);
		OutResult.MesoVariationHeatmap.Init(0.0f, OutResult.ColorVariationHeatmap.Num());
		OutResult.MacroVariationHeatmap.Init(0.0f, OutResult.ColorVariationHeatmap.Num());

		auto BuildScaleHeatmap = [&](int32 PreviewOffset, TArray<float>& OutHeatmap)
		{
			for (int32 BlockY = 0; BlockY < OutResult.ColorVariationHeatmapHeight; ++BlockY)
			{
				for (int32 BlockX = 0; BlockX < OutResult.ColorVariationHeatmapWidth; ++BlockX)
				{
					int32 JumpCount = 0;
					int32 EdgeCount = 0;
					const int32 StartX = BlockX * BlockSize;
					const int32 StartY = BlockY * BlockSize;
					const int32 EndX = FMath::Min((BlockX + 1) * BlockSize, Width);
					const int32 EndY = FMath::Min((BlockY + 1) * BlockSize, Height);
					for (int32 Y = StartY; Y < EndY; ++Y)
					{
						for (int32 X = StartX; X < EndX; ++X)
						{
							const int32 Index = Y * Width + X;
							const FLinearColor& Pixel = Pixels[Index];
							if (X + PreviewOffset < Width)
							{
								JumpCount += PixelDistance(Pixel, Pixels[Index + PreviewOffset]) >= ModerateThreshold;
								++EdgeCount;
							}
							if (Y + PreviewOffset < Height)
							{
								JumpCount += PixelDistance(Pixel, Pixels[Index + PreviewOffset * Width]) >= ModerateThreshold;
								++EdgeCount;
							}
						}
					}
					OutHeatmap[BlockY * OutResult.ColorVariationHeatmapWidth + BlockX] =
						EdgeCount > 0 ? static_cast<float>(JumpCount) / EdgeCount : 0.0f;
				}
			}
		};
		BuildScaleHeatmap(1, OutResult.ColorVariationHeatmap);
		BuildScaleHeatmap(4, OutResult.MesoVariationHeatmap);
		BuildScaleHeatmap(16, OutResult.MacroVariationHeatmap);
	}

	void BuildHueContrastHeatmap(const FImage& LinearImage, int32 Width, int32 Height,
		const FHueDistributionSettings& Settings, FBaseColorAnalysisResult& OutResult)
	{
		constexpr int32 BlockSize = 8;
		OutResult.HueContrastHeatmapWidth = FMath::DivideAndRoundUp(Width, BlockSize);
		OutResult.HueContrastHeatmapHeight = FMath::DivideAndRoundUp(Height, BlockSize);
		OutResult.HueContrastHeatmap.Init(0.0f,
			OutResult.HueContrastHeatmapWidth * OutResult.HueContrastHeatmapHeight);
		const float SaturationThreshold = FMath::Clamp(Settings.SaturationThreshold, 0.0f, 1.0f);
		const float ValueThreshold = FMath::Max(0.0f, Settings.ValueThreshold);
		const TArrayView64<const FLinearColor> LinearPixels = LinearImage.AsRGBA32F();

		for (int32 BlockY = 0; BlockY < OutResult.HueContrastHeatmapHeight; ++BlockY)
		{
			for (int32 BlockX = 0; BlockX < OutResult.HueContrastHeatmapWidth; ++BlockX)
			{
				const int32 StartX = BlockX * BlockSize;
				const int32 StartY = BlockY * BlockSize;
				const int32 LocalWidth = FMath::Min(BlockSize, Width - StartX);
				const int32 LocalHeight = FMath::Min(BlockSize, Height - StartY);
				TArray<FLinearColor, TInlineAllocator<BlockSize * BlockSize>> HSVPixels;
				HSVPixels.SetNumUninitialized(LocalWidth * LocalHeight);
				for (int32 LocalY = 0; LocalY < LocalHeight; ++LocalY)
				{
					for (int32 LocalX = 0; LocalX < LocalWidth; ++LocalX)
					{
						HSVPixels[LocalY * LocalWidth + LocalX] = LinearPixels[
							(StartY + LocalY) * Width + StartX + LocalX].GetClamped(0.0f, 1.0f).LinearRGBToHSV();
					}
				}

				float SquaredScoreSum = 0.0f;
				int32 EdgeCount = 0;
				auto AddHueEdge = [&](int32 A, int32 B)
				{
					const FLinearColor& HSV_A = HSVPixels[A];
					const FLinearColor& HSV_B = HSVPixels[B];
					++EdgeCount;
					if (HSV_A.G < SaturationThreshold || HSV_B.G < SaturationThreshold ||
						HSV_A.B < ValueThreshold || HSV_B.B < ValueThreshold)
					{
						return;
					}
					const float DirectDistance = FMath::Abs(HSV_A.R - HSV_B.R);
					const float CircularDistance = FMath::Min(DirectDistance, 360.0f - DirectDistance);
					// Treat 90 degrees as full-strength hue contrast. Low-saturation
					// pixels are filtered above instead of continuously suppressing valid colors.
					const float EdgeScore = FMath::Clamp(CircularDistance / 90.0f, 0.0f, 1.0f);
					SquaredScoreSum += EdgeScore * EdgeScore;
				};

				for (int32 LocalY = 0; LocalY < LocalHeight; ++LocalY)
				{
					for (int32 LocalX = 0; LocalX < LocalWidth; ++LocalX)
					{
						const int32 Index = LocalY * LocalWidth + LocalX;
						if (LocalX + 1 < LocalWidth) AddHueEdge(Index, Index + 1);
						if (LocalY + 1 < LocalHeight) AddHueEdge(Index, Index + LocalWidth);
					}
				}
				OutResult.HueContrastHeatmap[BlockY * OutResult.HueContrastHeatmapWidth + BlockX] =
					EdgeCount > 0 ? FMath::Sqrt(SquaredScoreSum / EdgeCount) : 0.0f;
			}
		}
	}

	void AnalyzeHueDistribution(const FImage& LinearImage, const FHueDistributionSettings& Settings,
		FHueDistributionResult& OutResult)
	{
		const int32 BinCount = FMath::Clamp(Settings.HueBinCount, 6, 360);
		const float SaturationThreshold = FMath::Clamp(Settings.SaturationThreshold, 0.0f, 1.0f);
		const float ValueThreshold = FMath::Max(0.0f, Settings.ValueThreshold);
		const float UniformityHalfRange = FMath::Clamp(Settings.UniformityHalfRangeDegrees, 0.0f, 180.0f);
		OutResult = FHueDistributionResult();
		OutResult.AppliedSettings = Settings;
		OutResult.AppliedSettings.HueBinCount = BinCount;
		OutResult.AppliedSettings.SaturationThreshold = SaturationThreshold;
		OutResult.AppliedSettings.ValueThreshold = ValueThreshold;
		OutResult.AppliedSettings.UniformityHalfRangeDegrees = UniformityHalfRange;
		OutResult.Histogram.Init(0.0f, BinCount);
		OutResult.PixelHistogram.Init(0, BinCount);
		TArray<double> HistogramWeights;
		HistogramWeights.Init(0.0, BinCount);
		TArray<double> WeightedSaturationSums;
		WeightedSaturationSums.Init(0.0, BinCount);
		TArray<double> WeightedValueSums;
		WeightedValueSums.Init(0.0, BinCount);

		double TotalWeight = 0.0;
		const TArrayView64<const FLinearColor> LinearPixels = LinearImage.AsRGBA32F();
		for (const FLinearColor& LinearPixel : LinearPixels)
		{
			if (LinearPixel.A <= 0.0f)
			{
				continue;
			}

			++OutResult.ValidPixelCount;
			const FLinearColor HSV = LinearPixel.GetClamped(0.0f, 1.0f).LinearRGBToHSV();
			const float Saturation = FMath::Clamp(HSV.G, 0.0f, 1.0f);
			const float Value = FMath::Clamp(HSV.B, 0.0f, 1.0f);
			if (Saturation < SaturationThreshold || Value < ValueThreshold)
			{
				continue;
			}

			++OutResult.ChromaticPixelCount;
			float Weight = 1.0f;
			if (Settings.WeightMode == EHueWeightMode::Saturation)
			{
				Weight = Saturation;
			}
			else if (Settings.WeightMode == EHueWeightMode::SaturationValue)
			{
				Weight = Saturation * Value;
			}

			const float WrappedHue = FMath::Fmod(FMath::Max(0.0f, HSV.R), 360.0f);
			const int32 Bin = FMath::Clamp(FMath::FloorToInt(WrappedHue / 360.0f * BinCount), 0, BinCount - 1);
			HistogramWeights[Bin] += Weight;
			++OutResult.PixelHistogram[Bin];
			WeightedSaturationSums[Bin] += Saturation * Weight;
			WeightedValueSums[Bin] += Value * Weight;
			TotalWeight += Weight;
		}

		if (OutResult.ValidPixelCount > 0)
		{
			OutResult.ChromaticPixelRatio = static_cast<float>(OutResult.ChromaticPixelCount) /
				static_cast<float>(OutResult.ValidPixelCount);
		}
		if (TotalWeight <= UE_DOUBLE_SMALL_NUMBER)
		{
			return;
		}

		double Entropy = 0.0;
		double DominantWeight = -1.0;
		for (int32 Bin = 0; Bin < BinCount; ++Bin)
		{
			const double BinWeight = HistogramWeights[Bin];
			OutResult.Histogram[Bin] = static_cast<float>(BinWeight);
			if (BinWeight > DominantWeight)
			{
				DominantWeight = BinWeight;
				OutResult.DominantHueBin = Bin;
			}
			if (BinWeight > 0.0f)
			{
				const double Probability = BinWeight / TotalWeight;
				Entropy -= Probability * FMath::Log2(Probability);
			}
		}

		OutResult.NormalizedEntropy = BinCount > 1
			? static_cast<float>(Entropy / FMath::Log2(static_cast<double>(BinCount)))
			: 0.0f;
		OutResult.DominantHueCoverage = static_cast<float>(DominantWeight / TotalWeight);
		if (DominantWeight > UE_DOUBLE_SMALL_NUMBER)
		{
			OutResult.DominantMeanSaturation = static_cast<float>(
				WeightedSaturationSums[OutResult.DominantHueBin] / DominantWeight);
			OutResult.DominantMeanValue = static_cast<float>(
				WeightedValueSums[OutResult.DominantHueBin] / DominantWeight);
		}
		OutResult.DominantHuePixelCoverage = OutResult.ChromaticPixelCount > 0
			? static_cast<float>(OutResult.PixelHistogram[OutResult.DominantHueBin]) /
				static_cast<float>(OutResult.ChromaticPixelCount)
			: 0.0f;
		OutResult.DominantHueDegrees = (OutResult.DominantHueBin + 0.5f) * 360.0f / BinCount;

		int64 UniformHuePixelCount = 0;
		for (const FLinearColor& LinearPixel : LinearPixels)
		{
			if (LinearPixel.A <= 0.0f)
			{
				continue;
			}
			const FLinearColor HSV = LinearPixel.GetClamped(0.0f, 1.0f).LinearRGBToHSV();
			const float Saturation = FMath::Clamp(HSV.G, 0.0f, 1.0f);
			const float Value = FMath::Clamp(HSV.B, 0.0f, 1.0f);
			if (Saturation < SaturationThreshold || Value < ValueThreshold)
			{
				continue;
			}
			const float PixelHue = FMath::Fmod(FMath::Max(0.0f, HSV.R), 360.0f);
			const float DirectDistance = FMath::Abs(PixelHue - OutResult.DominantHueDegrees);
			const float CircularDistance = FMath::Min(DirectDistance, 360.0f - DirectDistance);
			if (CircularDistance <= UniformityHalfRange)
			{
				++UniformHuePixelCount;
			}
		}
		OutResult.HueUniformity = OutResult.ChromaticPixelCount > 0
			? static_cast<float>(UniformHuePixelCount) / static_cast<float>(OutResult.ChromaticPixelCount)
			: 0.0f;
	}

	int32 FindNearestCenter(const FLinearColor& Pixel, const TArray<FLinearColor>& Centers)
	{
		int32 BestIndex = 0;
		float BestDistance = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Centers.Num(); ++Index)
		{
			const float Distance = DistanceSquared(Pixel, Centers[Index]);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				BestIndex = Index;
			}
		}
		return BestIndex;
	}

	void InitializeCenters(const TArray<FLinearColor>& Pixels, int32 K, TArray<FLinearColor>& OutCenters)
	{
		// Deterministic farthest-point initialization avoids unstable editor results.
		FLinearColor Mean = FLinearColor::Black;
		for (const FLinearColor& Pixel : Pixels)
		{
			Mean += Pixel;
		}
		Mean /= static_cast<float>(Pixels.Num());
		OutCenters.Add(Pixels[FindNearestCenter(Mean, Pixels)]);

		while (OutCenters.Num() < K)
		{
			int32 FarthestPixel = 0;
			float FarthestDistance = -1.0f;
			for (int32 PixelIndex = 0; PixelIndex < Pixels.Num(); ++PixelIndex)
			{
				const int32 Nearest = FindNearestCenter(Pixels[PixelIndex], OutCenters);
				const float Distance = DistanceSquared(Pixels[PixelIndex], OutCenters[Nearest]);
				if (Distance > FarthestDistance)
				{
					FarthestDistance = Distance;
					FarthestPixel = PixelIndex;
				}
			}
			OutCenters.Add(Pixels[FarthestPixel]);
		}
	}
}

bool FBaseColorClusterAnalyzer::AnalyzeTexture(UTexture2D* Texture, int32 ClusterCount,
	FBaseColorAnalysisResult& OutResult, FText& OutError, const FHueDistributionSettings& HueSettings)
{
	OutResult = FBaseColorAnalysisResult();
	if (!Texture)
	{
		OutError = LOCTEXT("NoTexture", "Select a Texture2D to analyze.");
		return false;
	}

	const ETextureSourceFormat Format = Texture->Source.GetFormat();
	if (Format != TSF_BGRA8)
	{
		OutError = FText::Format(LOCTEXT("UnsupportedFormat", "Unsupported TextureSource format ({0}). The MVP supports Unreal's 8-bit BGRA source format."),
			FText::AsNumber(static_cast<int32>(Format)));
		return false;
	}

	OutResult.SourceWidth = Texture->Source.GetSizeX();
	OutResult.SourceHeight = Texture->Source.GetSizeY();
	if (OutResult.SourceWidth <= 0 || OutResult.SourceHeight <= 0)
	{
		OutError = LOCTEXT("InvalidDimensions", "The texture has no valid source image.");
		return false;
	}

	FImage SourceImage;
	if (!Texture->Source.GetMipImage(SourceImage, 0))
	{
		OutError = LOCTEXT("ReadFailed", "TextureSource mip 0 could not be read. The source art may be unavailable.");
		return false;
	}

	// Let ImageCore honor the TextureSource gamma metadata, then apply the same
	// non-destructive color adjustments used by the texture build pipeline.
	FImage LinearImage;
	SourceImage.CopyTo(LinearImage, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
	FTextureBuildSettings BuildSettings;
	BuildSettings.ColorAdjustment.AdjustBrightness = Texture->AdjustBrightness;
	BuildSettings.ColorAdjustment.AdjustBrightnessCurve = Texture->AdjustBrightnessCurve;
	BuildSettings.ColorAdjustment.AdjustVibrance = Texture->AdjustVibrance;
	BuildSettings.ColorAdjustment.AdjustSaturation = Texture->AdjustSaturation;
	BuildSettings.ColorAdjustment.AdjustRGBCurve = Texture->AdjustRGBCurve;
	BuildSettings.ColorAdjustment.AdjustHue = Texture->AdjustHue;
	BuildSettings.ColorAdjustment.AdjustMinAlpha = Texture->AdjustMinAlpha;
	BuildSettings.ColorAdjustment.AdjustMaxAlpha = Texture->AdjustMaxAlpha;
	BuildSettings.bUseNewMipFilter = Texture->bUseNewMipFilter;
	ITextureCompressorModule::AdjustImageColors(LinearImage, BuildSettings);
	AnalyzeHueDistribution(LinearImage, HueSettings, OutResult.HueDistribution);

	// Continuity must use the original source grid. Downsampling would remove
	// high-frequency transitions and change the neighbor topology.
	OutResult.AnalysisWidth = OutResult.SourceWidth;
	OutResult.AnalysisHeight = OutResult.SourceHeight;
	BuildHueContrastHeatmap(LinearImage, OutResult.AnalysisWidth, OutResult.AnalysisHeight, HueSettings, OutResult);

	TArray<FLinearColor> Pixels;
	BuildAnalysisPixels(LinearImage, Pixels);
	BuildColorVariationHeatmap(Pixels, OutResult.AnalysisWidth, OutResult.AnalysisHeight, OutResult);
	const int32 MaximumK = FMath::Min(32, Pixels.Num());
	const int32 K = FMath::Clamp(ClusterCount, FMath::Min(8, MaximumK), MaximumK);
	TArray<FLinearColor> Centers;
	InitializeCenters(Pixels, K, Centers);
	OutResult.ClusterIDMap.SetNumUninitialized(Pixels.Num());

	for (int32 Iteration = 0; Iteration < MaxKMeansIterations; ++Iteration)
	{
		TArray<FLinearColor> Sums;
		Sums.Init(FLinearColor::Black, K);
		TArray<int32> Counts;
		Counts.Init(0, K);

		for (int32 PixelIndex = 0; PixelIndex < Pixels.Num(); ++PixelIndex)
		{
			const int32 Id = FindNearestCenter(Pixels[PixelIndex], Centers);
			OutResult.ClusterIDMap[PixelIndex] = Id;
			Sums[Id] += Pixels[PixelIndex];
			++Counts[Id];
		}

		float MaximumMovement = 0.0f;
		for (int32 Id = 0; Id < K; ++Id)
		{
			if (Counts[Id] > 0)
			{
				const FLinearColor NewCenter = Sums[Id] / static_cast<float>(Counts[Id]);
				MaximumMovement = FMath::Max(MaximumMovement, DistanceSquared(Centers[Id], NewCenter));
				Centers[Id] = NewCenter;
			}
		}
		if (MaximumMovement <= ConvergenceThresholdSquared)
		{
			break;
		}
	}

	TArray<int32> Counts;
	Counts.Init(0, K);
	for (int32 PixelIndex = 0; PixelIndex < Pixels.Num(); ++PixelIndex)
	{
		OutResult.ClusterIDMap[PixelIndex] = FindNearestCenter(Pixels[PixelIndex], Centers);
		const int32 Id = OutResult.ClusterIDMap[PixelIndex];
		++Counts[Id];
	}

	// Choose a real source pixel nearest to each centroid instead of exposing
	// an averaged synthetic color as the representative.
	TArray<int32> RepresentativePixelIndices;
	RepresentativePixelIndices.Init(INDEX_NONE, K);
	TArray<float> RepresentativeDistances;
	RepresentativeDistances.Init(TNumericLimits<float>::Max(), K);
	for (int32 PixelIndex = 0; PixelIndex < Pixels.Num(); ++PixelIndex)
	{
		const int32 Id = OutResult.ClusterIDMap[PixelIndex];
		const float Distance = DistanceSquared(Pixels[PixelIndex], Centers[Id]);
		if (Distance < RepresentativeDistances[Id])
		{
			RepresentativeDistances[Id] = Distance;
			RepresentativePixelIndices[Id] = PixelIndex;
		}
	}

	TArray<int32> SortedOldIds;
	for (int32 Id = 0; Id < K; ++Id) SortedOldIds.Add(Id);
	SortedOldIds.Sort([&Counts](int32 A, int32 B) { return Counts[A] > Counts[B]; });
	TArray<int32> OldToNew;
	OldToNew.SetNum(K);
	for (int32 NewId = 0; NewId < K; ++NewId)
	{
		const int32 OldId = SortedOldIds[NewId];
		OldToNew[OldId] = NewId;
		FBaseColorCluster& Cluster = OutResult.Clusters.AddDefaulted_GetRef();
		Cluster.Id = NewId;
		if (RepresentativePixelIndices[OldId] != INDEX_NONE)
		{
			const FLinearColor& Representative = Pixels[RepresentativePixelIndices[OldId]];
			const FColor RepresentativeSRGB(
				FMath::RoundToInt(Representative.R * 255.0f),
				FMath::RoundToInt(Representative.G * 255.0f),
				FMath::RoundToInt(Representative.B * 255.0f),
				255);
			Cluster.Center = FLinearColor::FromSRGBColor(RepresentativeSRGB);
		}
		else
		{
			const FLinearColor& Center = Centers[OldId];
			Cluster.Center = FLinearColor::FromSRGBColor(FColor(
				FMath::RoundToInt(Center.R * 255.0f),
				FMath::RoundToInt(Center.G * 255.0f),
				FMath::RoundToInt(Center.B * 255.0f),
				255));
		}
		Cluster.PixelCount = Counts[OldId];
		Cluster.Coverage = static_cast<float>(Counts[OldId]) / Pixels.Num();
	}
	for (int32& Id : OutResult.ClusterIDMap) Id = OldToNew[Id];

	return true;
}

#undef LOCTEXT_NAMESPACE
