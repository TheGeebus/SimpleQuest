// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "DetailCustomizations/QuestlineNodeEntryDetailsCustomization.h"

#include "Nodes/QuestlineNode_Entry.h"
#include "Types/IncomingSignalPinSpec.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

TSharedRef<IDetailCustomization> FQuestlineNodeEntryDetailsCustomization::MakeInstance()
{
	return MakeShared<FQuestlineNodeEntryDetailsCustomization>();
}

void FQuestlineNodeEntryDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
	if (CustomizedObjects.Num() != 1) return; // multi-edit not supported — specs are per-instance

	UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(CustomizedObjects[0].Get());
	if (!EntryNode) return;

	CustomizedNode = EntryNode;
	PropertyUtilities = DetailBuilder.GetPropertyUtilities();

	/**
	 * Hide the raw IncomingSignals array — our custom rows below drive the same data via child handles. The property handle
	 * is still retrieved here so we can reach per-element bExposed handles through IPropertyHandleArray below, which keeps
	 * Undo/Redo and PostEditChangeProperty firing naturally on toggle.
	 */
	const TSharedRef<IPropertyHandle> IncomingSignalsHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuestlineNode_Entry, IncomingSignals));
	DetailBuilder.HideProperty(IncomingSignalsHandle);

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Quest"), LOCTEXT("QuestCategory", "Quest"), ECategoryPriority::Important);

	// Section header with Refresh button on the right.
	Category.AddCustomRow(LOCTEXT("IncomingSignalsHeaderFilter", "Incoming Signals"))
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(LOCTEXT("IncomingSignalsHeaderLabel", "Incoming Signals"))
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
						.Text(LOCTEXT("RefreshFromSources", "Refresh from Sources"))
						.ToolTipText(LOCTEXT("RefreshFromSourcesTooltip", "Scan parent graphs for outcome sources routing into this graph and import them as specs. Equivalent to the right-click 'Import Outcome Pins' context action."))
						.OnClicked(this, &FQuestlineNodeEntryDetailsCustomization::OnRefreshClicked)
				]
		];

	const TSharedPtr<IPropertyHandleArray> ArrayHandle = IncomingSignalsHandle->AsArray();
	if (!ArrayHandle.IsValid()) return;

	uint32 NumElements = 0;
	ArrayHandle->GetNumElements(NumElements);

	if (NumElements == 0)
	{
		Category.AddCustomRow(LOCTEXT("IncomingSignalsEmptyFilter", "No Incoming Signals"))
			.WholeRowContent()
			[
				SNew(STextBlock)
					.Text(LOCTEXT("IncomingSignalsEmpty", "No incoming signals. Use Refresh from Sources (or right-click 'Import Outcome Pins') to discover signals routed into this graph from parent graphs."))
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		return;
	}

	/**
	 * Bucket specs by SourceNodeGuid so the same-source family clusters. The group-label grading mirrors BuildDisambiguatedPinName:
	 * bare source label when unique across sources; "SourceLabel (AssetName)" when two different sources share a label across
	 * different parent assets. Sorted alphabetically by source label for stable presentation across compiles.
	 */
	TMap<FGuid, TArray<int32>> BySource;
	for (int32 i = 0; i < EntryNode->IncomingSignals.Num(); ++i)
	{
		const FIncomingSignalPinSpec& Spec = EntryNode->IncomingSignals[i];
		if (!Spec.SourceNodeGuid.IsValid()) continue; // defensive — PostLoad already scrubs these
		BySource.FindOrAdd(Spec.SourceNodeGuid).Add(i);
	}

	// Detect cross-asset source-label ambiguity once, up front.
	TSet<FString> AmbiguousLabels;
	{
		TMap<FString, TSet<FSoftObjectPath>> LabelAssets;
		for (const auto& [Guid, Indices] : BySource)
		{
			const FIncomingSignalPinSpec& Spec = EntryNode->IncomingSignals[Indices[0]];
			LabelAssets.FindOrAdd(Spec.CachedSourceLabel).Add(Spec.ParentAsset);
		}
		for (const auto& [Label, Assets] : LabelAssets)
		{
			if (Assets.Num() > 1) AmbiguousLabels.Add(Label);
		}
	}

	TArray<FGuid> SourceOrder;
	BySource.GetKeys(SourceOrder);
	SourceOrder.Sort([EntryNode, &BySource](const FGuid& A, const FGuid& B)
	{
		const FIncomingSignalPinSpec& SpecA = EntryNode->IncomingSignals[BySource[A][0]];
		const FIncomingSignalPinSpec& SpecB = EntryNode->IncomingSignals[BySource[B][0]];
		return SpecA.CachedSourceLabel.Compare(SpecB.CachedSourceLabel, ESearchCase::IgnoreCase) < 0;
	});

	for (const FGuid& SourceGuid : SourceOrder)
	{
		const TArray<int32>& SpecIndices = BySource[SourceGuid];
		if (SpecIndices.Num() == 0) continue;

		const FIncomingSignalPinSpec& RepSpec = EntryNode->IncomingSignals[SpecIndices[0]];
		const FString SourceLabel = RepSpec.CachedSourceLabel.IsEmpty() ? TEXT("Unknown") : RepSpec.CachedSourceLabel;

		FString GroupLabel = SourceLabel;
		if (AmbiguousLabels.Contains(SourceLabel))
		{
			const FString AssetName = RepSpec.ParentAsset.IsNull() ? TEXT("Local") : RepSpec.ParentAsset.GetAssetName();
			GroupLabel = FString::Printf(TEXT("%s (%s)"), *SourceLabel, *AssetName);
		}

		IDetailGroup& Group = Category.AddGroup(FName(*FString::Printf(TEXT("Source_%s"), *SourceGuid.ToString(EGuidFormats::Digits))), FText::FromString(GroupLabel));

		/**
		 * Within a source group, order any-outcome row first (it's the most general), then specific outcomes alphabetical
		 * by tag string. Stable across compiles because source label and tag identity are stable.
		 */
		TArray<int32> OrderedIndices = SpecIndices;
		OrderedIndices.Sort([EntryNode](int32 A, int32 B)
		{
			const FIncomingSignalPinSpec& SpecA = EntryNode->IncomingSignals[A];
			const FIncomingSignalPinSpec& SpecB = EntryNode->IncomingSignals[B];
			const bool bAnyA = !SpecA.Outcome.IsValid();
			const bool bAnyB = !SpecB.Outcome.IsValid();
			if (bAnyA != bAnyB) return bAnyA;
			return SpecA.Outcome.ToString() < SpecB.Outcome.ToString();
		});

		for (int32 Index : OrderedIndices)
		{
			const TSharedPtr<IPropertyHandle> ElementHandle = ArrayHandle->GetElement(Index);
			if (!ElementHandle.IsValid()) continue;
			const TSharedPtr<IPropertyHandle> ExposedHandle = ElementHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FIncomingSignalPinSpec, bExposed));
			if (!ExposedHandle.IsValid()) continue;

			const FIncomingSignalPinSpec& Spec = EntryNode->IncomingSignals[Index];
			const bool bIsAny = !Spec.Outcome.IsValid();

			FText OutcomeLabel;
			FText RowTooltip;
			if (bIsAny)
			{
				OutcomeLabel = LOCTEXT("AnyOutcomeRow", "Any Outcome");
				RowTooltip = LOCTEXT("AnyOutcomeRowTooltip", "Exposes a pin that fires whenever this source triggers entry, regardless of which specific outcome arrived.");
			}
			else
			{
				const FString FullTag = Spec.Outcome.GetTagName().ToString();
				int32 LastDot;
				const FString Leaf = FullTag.FindLastChar(TEXT('.'), LastDot) ? FullTag.Mid(LastDot + 1) : FullTag;
				OutcomeLabel = FText::FromString(Leaf);
				RowTooltip = FText::Format(LOCTEXT("SpecificOutcomeRowTooltip", "Exposes a pin that fires only when this source triggers entry with outcome '{0}'."), FText::FromString(FullTag));
			}

			Group.AddWidgetRow()
				.NameContent()
				[
					SNew(STextBlock)
						.Text(OutcomeLabel)
						.ToolTipText(RowTooltip)
				]
				.ValueContent()
				[
					ExposedHandle->CreatePropertyValueWidget()
				];
		}
	}
}

FReply FQuestlineNodeEntryDetailsCustomization::OnRefreshClicked()
{
	if (UQuestlineNode_Entry* Node = CustomizedNode.Get())
	{
		Node->ImportOutcomePinsFromParent();

		// Force panel re-layout so freshly-imported specs appear without requiring a selection cycle.
		if (const TSharedPtr<IPropertyUtilities> Utils = PropertyUtilities.Pin())
		{
			Utils->ForceRefresh();
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE