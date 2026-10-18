// Copyright Radar. All Rights Reserved.

#include "Viewer/RadarValueRenderers.h"
#include "RadarUMGDiffSettings.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SHyperlink.h"
#include "Styling/AppStyle.h"
#include "Dom/JsonObject.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

namespace RadarValueRenderers
{
	FString ShortAssetName(const FString& Path)
	{
		FString P = Path;
		int32 Quote = INDEX_NONE;
		if (P.FindChar(TEXT('\''), Quote))
		{
			P = P.Mid(Quote + 1).LeftChop(1);   // Class'/Game/X.X' 형식
		}
		FString Left, Right;
		if (P.Split(TEXT(":"), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) { return Right; }
		if (P.Split(TEXT("."), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) { return Right; }
		if (P.Split(TEXT("/"), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) { return Right; }
		return P;
	}

	bool TryGetColor(const FString& Type, const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			return false;
		}
		TSharedPtr<FJsonObject> O = Value->AsObject();
		if (Type.EndsWith(TEXT(":SlateColor")))
		{
			const TSharedPtr<FJsonObject>* Spec = nullptr;
			if (!O->TryGetObjectField(TEXT("SpecifiedColor"), Spec))
			{
				return false;
			}
			O = *Spec;
		}
		double R = 0, G = 0, B = 0, A = 1;
		if (!O->TryGetNumberField(TEXT("R"), R) || !O->TryGetNumberField(TEXT("G"), G) || !O->TryGetNumberField(TEXT("B"), B))
		{
			return false;
		}
		O->TryGetNumberField(TEXT("A"), A);
		if (Type.EndsWith(TEXT(":Color")))
		{
			OutColor = FLinearColor(FColor(static_cast<uint8>(R), static_cast<uint8>(G), static_cast<uint8>(B), static_cast<uint8>(A)));
		}
		else
		{
			OutColor = FLinearColor(R, G, B, A);
		}
		return true;
	}

	TArray<TPair<FString, int32>> WordDiff(const FString& This, const FString& Other)
	{
		TArray<FString> A, B;
		This.ParseIntoArrayWS(A);
		Other.ParseIntoArrayWS(B);
		const int32 N = A.Num(), M = B.Num();
		TArray<int32> L;
		L.SetNumZeroed((N + 1) * (M + 1));
		auto At = [&L, M](int32 i, int32 j) -> int32& { return L[i * (M + 1) + j]; };
		for (int32 i = N - 1; i >= 0; --i)
		{
			for (int32 j = M - 1; j >= 0; --j)
			{
				At(i, j) = A[i] == B[j] ? At(i + 1, j + 1) + 1 : FMath::Max(At(i + 1, j), At(i, j + 1));
			}
		}
		TArray<TPair<FString, int32>> Out;
		int32 i = 0, j = 0;
		while (i < N)
		{
			if (j < M && A[i] == B[j]) { Out.Emplace(A[i], 0); ++i; ++j; }
			else if (j < M && At(i, j + 1) >= At(i + 1, j)) { ++j; }
			else { Out.Emplace(A[i], 1); ++i; }
		}
		return Out;
	}

	static void SyncContentBrowser(const FString& Path)
	{
		FString ObjectPath = Path;
		int32 Quote = INDEX_NONE;
		if (ObjectPath.FindChar(TEXT('\''), Quote))
		{
			ObjectPath = ObjectPath.Mid(Quote + 1).LeftChop(1);
		}
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
		if (Asset.IsValid())
		{
			FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets({ Asset });
		}
	}

	static TSharedRef<SWidget> Plain(const FString& Text, const FLinearColor& Color = FLinearColor::White)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).ColorAndOpacity(Color).ToolTipText(FText::FromString(Text)).AutoWrapText(true);
	}

	TSharedRef<SWidget> MakeValueCell(const FRadarPropertyDiff& Row, bool bOld)
	{
		const FRadarValue& V = bOld ? Row.Old : Row.New;
		const FRadarValue& O = bOld ? Row.New : Row.Old;
		const URadarUMGDiffSettings& S = URadarUMGDiffSettings::Get();
		if (!V.bSet)
		{
			return Plain(TEXT("—"), FLinearColor(0.4f, 0.4f, 0.4f));
		}
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		if (V.State == ERadarValueState::Default)
		{
			Box->AddSlot().AutoWidth().Padding(0, 0, 4, 0)[ Plain(TEXT("(기본)"), FLinearColor(0.5f, 0.5f, 0.5f)) ];
		}
		if (V.State == ERadarValueState::Absent)
		{
			Box->AddSlot().AutoWidth()[ Plain(TEXT("(저장 안 됨)"), FLinearColor(0.5f, 0.5f, 0.5f)) ];
			return Box;
		}

		FLinearColor Color;
		if (TryGetColor(Row.Type, V.Value, Color))
		{
			Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SBox).WidthOverride(28).HeightOverride(14)
				[
					SNew(SColorBlock).Color(Color).ShowBackgroundForAlpha(true)
				]
			];
			Box->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)[ Plain(Color.ToFColorSRGB().ToHex()) ];
			return Box;
		}

		// 잎 구조체: 성분별, 바뀐 성분만 강조 배경
		if (Row.ChangedComponents.Num() > 0 && V.Value.IsValid() && V.Value->Type == EJson::Object)
		{
			TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).UseAllottedSize(true);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : V.Value->AsObject()->Values)
			{
				const bool bChanged = Row.ChangedComponents.Contains(Pair.Key);
				FString ValueText;
				if (Pair.Value->Type == EJson::Number) { ValueText = FString::SanitizeFloat(Pair.Value->AsNumber(), 0); }
				else if (Pair.Value->Type == EJson::Object)
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Sub : Pair.Value->AsObject()->Values)
					{
						ValueText += FString::Printf(TEXT("%s%s=%s"), ValueText.IsEmpty() ? TEXT("") : TEXT(","), *Sub.Key, Sub.Value->Type == EJson::Number ? *FString::SanitizeFloat(Sub.Value->AsNumber(), 0) : *Sub.Value->AsString());
					}
				}
				else { ValueText = Pair.Value->AsString(); }
				Wrap->AddSlot().Padding(0, 0, 6, 2)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(bChanged ? S.ColorChanged.CopyWithNewOpacity(0.35f) : FLinearColor::Transparent)
					.Padding(FMargin(3, 0))
					[
						SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s %s"), *Pair.Key, *ValueText)))
					]
				];
			}
			Box->AddSlot().FillWidth(1.f)[ Wrap ];
			return Box;
		}

		// 텍스트: 단어 단위 diff + 테이블/키
		if (Row.Type == TEXT("TextProperty") && V.Value.IsValid() && V.Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> TO = V.Value->AsObject();
			FString Source, Table, TableKey, Ns, Key;
			TO->TryGetStringField(TEXT("source"), Source);
			TO->TryGetStringField(TEXT("table"), Table);
			TO->TryGetStringField(TEXT("tableKey"), TableKey);
			TO->TryGetStringField(TEXT("namespace"), Ns);
			TO->TryGetStringField(TEXT("key"), Key);
			FString OtherSource;
			if (O.bSet && O.Value.IsValid() && O.Value->Type == EJson::Object)
			{
				O.Value->AsObject()->TryGetStringField(TEXT("source"), OtherSource);
			}
			TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).UseAllottedSize(true);
			for (const TPair<FString, int32>& W : WordDiff(Source, OtherSource))
			{
				const FLinearColor Bg = W.Value == 0 ? FLinearColor::Transparent : (bOld ? S.ColorRemoved : S.ColorAdded).CopyWithNewOpacity(0.4f);
				Wrap->AddSlot().Padding(0, 0, 4, 0)
				[
					SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(Bg).Padding(FMargin(2, 0))
					[
						SNew(STextBlock).Text(FText::FromString(W.Key))
					]
				];
			}
			const FString Sub = !Table.IsEmpty() ? FString::Printf(TEXT("%s / %s"), *ShortAssetName(Table), *TableKey) : FString::Printf(TEXT("%s / %s"), *Ns, *Key);
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ Box ]
				+ SVerticalBox::Slot().AutoHeight()[ Wrap ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(Sub)).Font(FAppStyle::GetFontStyle("SmallFont")).ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))
				];
		}

		// 오브젝트·소프트 참조: 짧은 이름, 툴팁 전체 경로, 클릭 시 콘텐츠 브라우저 동기화
		const bool bAsset = !V.Asset.IsEmpty() || Row.Type.StartsWith(TEXT("SoftObjectProperty")) || Row.Type.StartsWith(TEXT("SoftClassProperty"))
			|| Row.Type.StartsWith(TEXT("ObjectProperty")) || Row.Type.StartsWith(TEXT("ClassProperty"));
		if (bAsset && V.Value.IsValid() && V.Value->Type == EJson::String)
		{
			const FString Path = V.Value->AsString();
			Box->AddSlot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(ShortAssetName(Path)))
				.ToolTipText(FText::FromString(Path))
				.OnNavigate_Lambda([Path]() { SyncContentBrowser(Path); })
			];
			return Box;
		}

		Box->AddSlot().FillWidth(1.f)[ Plain(V.Text) ];
		return Box;
	}
}

#undef LOCTEXT_NAMESPACE
