// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "DetailCustomizations/QuestlineGraphRewardsDetailsCustomization.h"

#include "Quests/QuestlineGraph.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Utilities/QuestTagComposer.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestOutcomeTags.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

// Reserved tag for the "fires on every completion regardless of outcome" case. Matches the runtime NAME_None any-outcome
// bucket conceptually; here it needs a real tag so it can be a map key. (If a canonical SimpleQuest.Outcome.Any tag is
// later introduced by the 0.7 Path/Outcome un-fuse, swap this to it.)
static FGameplayTag GetAnyOutcomeTag()
{
	return TAG_Outcome_AnyOutcome.GetTag();
}

TSharedRef<IDetailCustomization> FQuestlineGraphRewardsDetailsCustomization::MakeInstance()
{
	return MakeShared<FQuestlineGraphRewardsDetailsCustomization>();
}

void FQuestlineGraphRewardsDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1) return;   // per-asset; no multi-edit

	UQuestlineGraph* Graph = Cast<UQuestlineGraph>(Objects[0].Get());
	if (!Graph) return;

	CustomizedGraph = Graph;
	PropertyUtilities = DetailBuilder.GetPropertyUtilities();

	// Hide the raw map; drive it via flat custom rows + the constrained add combo below.
	const TSharedRef<IPropertyHandle> MapHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuestlineGraph, QuestlineRewards));
	DetailBuilder.HideProperty(MapHandle);

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Rewards"), LOCTEXT("RewardsCategory", "Rewards"), ECategoryPriority::Important);

	// Header row: label + "Add Reward Outcome" combo (constrained menu).
	Category.AddCustomRow(LOCTEXT("QuestlineRewardsHeaderFilter", "Questline Rewards"))
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("QuestlineRewardsHeader", "Questline-Level Rewards"))
				.ToolTipText(LOCTEXT("QuestlineRewardsHeaderTip", "Rewards granted when this questline completes via the given outcome — on every use, standalone or linked. Add an outcome (only this questline's Exit outcomes, plus Any Outcome, are selectable)."))
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SComboButton)
				.OnGetMenuContent(this, &FQuestlineGraphRewardsDetailsCustomization::BuildAddOutcomeMenu)
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("AddRewardOutcome", "Add Reward to Outcome"))
				]
			]
		];

	const TSharedPtr<IPropertyHandleMap> AsMap = MapHandle->AsMap();
	uint32 NumElements = 0;
	if (AsMap.IsValid()) AsMap->GetNumElements(NumElements);

	if (NumElements == 0)
	{
		Category.AddCustomRow(LOCTEXT("NoQuestlineRewardsFilter", "No Questline Rewards"))
			.WholeRowContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoQuestlineRewards", "No questline-level rewards. Use + Add Reward Outcome to grant rewards on this questline's completion."))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		return;
	}

	// One flat GROUP per authored outcome: header = outcome leaf (or "Any Outcome") + a remove button; body = the
	// entry's reward array, edited through its property handle (Undo/Redo + PostEditChange intact).
	const FGameplayTag AnyTag = GetAnyOutcomeTag();
	for (uint32 i = 0; i < NumElements; ++i)
	{
		const TSharedRef<IPropertyHandle> ElementHandle = AsMap->GetElement(i);
		const TSharedPtr<IPropertyHandle> KeyHandle = ElementHandle->GetKeyHandle();
		if (!KeyHandle.IsValid()) continue;

		FGameplayTag OutcomeKey;
		{
			void* KeyData = nullptr;
			if (KeyHandle->GetValueData(KeyData) == FPropertyAccess::Success && KeyData)
			{
				OutcomeKey = *static_cast<FGameplayTag*>(KeyData);
			}
		}

		const bool bIsAny    = OutcomeKey.IsValid() && OutcomeKey == AnyTag;
		const bool bIsParked = !OutcomeKey.IsValid();

		FText OutcomeLabel;
		if (bIsParked)      OutcomeLabel = LOCTEXT("ParkedOutcome", "(unassigned: re-key or remove)");
		else if (bIsAny)    OutcomeLabel = LOCTEXT("AnyOutcomeGroup", "Any Outcome");
		else                OutcomeLabel = FText::FromString(FQuestTagComposer::GetLeafSegment(OutcomeKey.GetTagName()));

		IDetailGroup& Group = Category.AddGroup(FName(*OutcomeKey.ToString()), OutcomeLabel);

		// Group header: outcome label + remove button (right).
		const bool bStale = IsOutcomeStale(OutcomeKey);

		Group.HeaderRow()
			.NameContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SImage)
					.Visibility(bStale ? EVisibility::Visible : EVisibility::Collapsed)
					.Image(FAppStyle::GetBrush("Icons.Warning"))
					.ToolTipText(LOCTEXT("StaleOutcomeTip", "No top-level Exit on this questline resolves with this outcome. These rewards will never grant. Re-key to a current outcome (the rewards carry over) or remove the entry. This is a compile error."))
				]
				+ SHorizontalBox::Slot().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(OutcomeLabel)
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.ColorAndOpacity(bStale ? FSlateColor(FLinearColor(1.f, 0.7f, 0.1f)) : FSlateColor::UseForeground())
				]
			]
			.ValueContent()
			.HAlign(HAlign_Left)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					MakeIconButton(FAppStyle::GetBrush("Icons.Edit"),
						LOCTEXT("EditOutcomeTip", "Change the outcome these rewards key on (rewards are preserved), or set (none) to free the outcome for another entry."),
						true, OutcomeKey)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MakeIconButton(FAppStyle::GetBrush("Icons.Delete"),
						LOCTEXT("RemoveOutcomeTip", "Remove this outcome's questline-level rewards."),
						false, OutcomeKey)
				]
			];

		// Body: the FQuestRewardSet.Rewards array, via the value handle's child — full inline reward-instance editor,
		// but nested ONE level under the flat outcome row instead of four.
		const TSharedPtr<IPropertyHandle> RewardsHandle = ElementHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FQuestRewardSet, Rewards));
		if (RewardsHandle.IsValid())
		{
			Group.AddPropertyRow(RewardsHandle.ToSharedRef());
		}
	}
}

TArray<FGameplayTag> FQuestlineGraphRewardsDetailsCustomization::GetAvailableOutcomes() const
{
	TArray<FGameplayTag> Available;
	UQuestlineGraph* Graph = CustomizedGraph.Get();
	if (!Graph || !Graph->QuestlineEdGraph) return Available;

	// Gather this graph's top-level Exit outcomes (same set the compiler validates against).
	for (UEdGraphNode* Node : Graph->QuestlineEdGraph->Nodes)
	{
		if (const UQuestlineNode_Exit* Exit = Cast<UQuestlineNode_Exit>(Node))
		{
			if (Exit->OutcomeTag.IsValid() && !Graph->QuestlineRewards.Contains(Exit->OutcomeTag))
			{
				Available.AddUnique(Exit->OutcomeTag);
			}
		}
	}

	// Any Outcome — always offered unless already keyed.
	if (const FGameplayTag AnyTag = GetAnyOutcomeTag(); AnyTag.IsValid() && !Graph->QuestlineRewards.Contains(AnyTag))
	{
		Available.AddUnique(AnyTag);
	}

	return Available;
}

bool FQuestlineGraphRewardsDetailsCustomization::IsOutcomeStale(const FGameplayTag& OutcomeKey) const
{
	// Stale = has rewards keyed on an outcome no current top-level Exit produces (and isn't Any Outcome). Same
	// predicate the compiler errors on — computed live here from the graph, so it flags the instant an Exit changes.
	UQuestlineGraph* Graph = CustomizedGraph.Get();
	if (!Graph || !Graph->QuestlineEdGraph) return false;
	if (OutcomeKey == GetAnyOutcomeTag()) return false;

	for (UEdGraphNode* Node : Graph->QuestlineEdGraph->Nodes)
	{
		if (const UQuestlineNode_Exit* Exit = Cast<UQuestlineNode_Exit>(Node))
		{
			if (Exit->OutcomeTag == OutcomeKey) return false;   // a current Exit produces it — not stale
		}
	}
	return true;
}

TSharedRef<SWidget> FQuestlineGraphRewardsDetailsCustomization::BuildAddOutcomeMenu()
{
	FMenuBuilder Menu(true, nullptr);
	const TArray<FGameplayTag> Available = GetAvailableOutcomes();
	const FGameplayTag AnyTag = GetAnyOutcomeTag();

	if (Available.Num() == 0)
	{
		Menu.AddWidget(
			SNew(SBox)
			.Padding(FMargin(12.f, 0.f))   // menu widgets get no item padding by default — pad so it doesn't touch the edges
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoAvailableOutcomes", "No unused Exit outcomes on this questline."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			],
			FText::GetEmpty(),
			true);
		return Menu.MakeWidget();
	}

	for (const FGameplayTag& Outcome : Available)
	{
		const FText Label = (Outcome == AnyTag)
			? LOCTEXT("AnyOutcomeMenu", "Any Outcome")
			: FText::FromString(FQuestTagComposer::GetLeafSegment(Outcome.GetTagName()));
		Menu.AddMenuEntry(
			Label, FText::FromName(Outcome.GetTagName()), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(const_cast<FQuestlineGraphRewardsDetailsCustomization*>(this), &FQuestlineGraphRewardsDetailsCustomization::OnAddOutcome, Outcome)));
	}
	return Menu.MakeWidget();
}

void FQuestlineGraphRewardsDetailsCustomization::OnAddOutcome(FGameplayTag Outcome)
{
	UQuestlineGraph* Graph = CustomizedGraph.Get();
	if (!Graph || !Outcome.IsValid() || Graph->QuestlineRewards.Contains(Outcome)) return;

	// Direct transacted mutation — the engine map handle's AddItem can't add a chosen key.
	const FScopedTransaction Transaction(LOCTEXT("AddQuestlineReward", "Add Questline Reward Outcome"));
	Graph->Modify();
	Graph->QuestlineRewards.Add(Outcome, FQuestRewardSet());
	Graph->PostEditChange();   // fire property-changed so anything listening (and the asset dirty flag) updates
	ForceRefresh();
}

void FQuestlineGraphRewardsDetailsCustomization::OnRemoveOutcome(FGameplayTag Outcome)
{
	UQuestlineGraph* Graph = CustomizedGraph.Get();
	if (!Graph || !Graph->QuestlineRewards.Contains(Outcome)) return;

	const FScopedTransaction Transaction(LOCTEXT("RemoveQuestlineReward", "Remove Questline Reward Outcome"));
	Graph->Modify();
	Graph->QuestlineRewards.Remove(Outcome);
	Graph->PostEditChange();
	ForceRefresh();
}

void FQuestlineGraphRewardsDetailsCustomization::OnRekeyOutcome(FGameplayTag OldOutcome, FGameplayTag NewOutcome)
{
	UQuestlineGraph* Graph = CustomizedGraph.Get();
	if (!Graph || !NewOutcome.IsValid() || OldOutcome == NewOutcome) return;
	if (!Graph->QuestlineRewards.Contains(OldOutcome) || Graph->QuestlineRewards.Contains(NewOutcome)) return;

	const FScopedTransaction Transaction(LOCTEXT("RekeyQuestlineReward", "Re-key Questline Reward Outcome"));
	Graph->Modify();

	// Move the reward set intact to the new key — the designer keeps every reward they authored; only the outcome changes.
	FQuestRewardSet Moved = MoveTemp(Graph->QuestlineRewards.FindChecked(OldOutcome));
	Graph->QuestlineRewards.Remove(OldOutcome);
	Graph->QuestlineRewards.Add(NewOutcome, MoveTemp(Moved));

	Graph->PostEditChange();
	ForceRefresh();
}

TSharedRef<SWidget> FQuestlineGraphRewardsDetailsCustomization::BuildRekeyMenu(FGameplayTag OldOutcome)
{
	FMenuBuilder Menu(true, nullptr);
	const TArray<FGameplayTag> Available = GetAvailableOutcomes();
	const FGameplayTag AnyTag = GetAnyOutcomeTag();

	// "(none)" — park this entry: free its outcome tag, keep the rewards, so the tag can move to another entry. Only
	// offered when THIS entry is keyed (not already parked) AND no other entry is parked (a TMap holds one None key).
	if (OldOutcome.IsValid() && !HasParkedEntry())
	{
		Menu.AddMenuEntry(
			LOCTEXT("RekeyToNone", "(none) — free this outcome"),
			LOCTEXT("RekeyToNoneTip", "Unassign this entry (rewards kept) so its outcome frees up for another entry. It won't grant and will error on compile until you re-key it."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(const_cast<FQuestlineGraphRewardsDetailsCustomization*>(this),
				&FQuestlineGraphRewardsDetailsCustomization::OnClearOutcome, OldOutcome)));
		Menu.AddMenuSeparator();
	}

	if (Available.Num() == 0)
	{
		Menu.AddWidget(
			SNew(SBox).Padding(FMargin(12.f, 6.f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoRekeyTargets", "No unused Exit outcomes to re-key to."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			],
			FText::GetEmpty(), /*bNoIndent*/ true);
		return Menu.MakeWidget();
	}

	for (const FGameplayTag& Target : Available)
	{
		const FText Label = (Target == AnyTag)
			? LOCTEXT("AnyOutcomeMenu", "Any Outcome")
			: FText::FromString(FQuestTagComposer::GetLeafSegment(Target.GetTagName()));
		Menu.AddMenuEntry(
			Label, FText::FromName(Target.GetTagName()), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(const_cast<FQuestlineGraphRewardsDetailsCustomization*>(this),
				&FQuestlineGraphRewardsDetailsCustomization::OnRekeyOutcome, OldOutcome, Target)));
	}
	return Menu.MakeWidget();
}

// Is some entry already parked (keyed on None/invalid)? A TMap holds only one such key, so at most one entry can be
// parked — a second Clear would collide on the None key and clobber the first. Used to gate the Clear affordance.
bool FQuestlineGraphRewardsDetailsCustomization::HasParkedEntry() const
{
	UQuestlineGraph* Graph = CustomizedGraph.Get();
	if (!Graph) return false;
	for (const TPair<FGameplayTag, FQuestRewardSet>& Pair : Graph->QuestlineRewards)
	{
		if (!Pair.Key.IsValid()) return true;
	}
	return false;
}

void FQuestlineGraphRewardsDetailsCustomization::OnClearOutcome(FGameplayTag Outcome)
{
	UQuestlineGraph* Graph = CustomizedGraph.Get();
	if (!Graph || !Outcome.IsValid() || !Graph->QuestlineRewards.Contains(Outcome)) return;
	if (Graph->QuestlineRewards.Contains(FGameplayTag())) return;   // a parked (None-key) entry already exists

	// Park: free the outcome tag but keep the rewards, re-keyed onto the invalid/None tag. Leaves the entry stale
	// (flagged + a compile error) until it's re-keyed to a valid outcome — intentional; parking is a mid-edit state.
	const FScopedTransaction Transaction(LOCTEXT("ClearQuestlineRewardKey", "Free Questline Reward Outcome"));
	Graph->Modify();
	FQuestRewardSet Moved = MoveTemp(Graph->QuestlineRewards.FindChecked(Outcome));
	Graph->QuestlineRewards.Remove(Outcome);
	Graph->QuestlineRewards.Add(FGameplayTag(), MoveTemp(Moved));   // FGameplayTag() = the None/parked key
	Graph->PostEditChange();
	ForceRefresh();
}

TSharedRef<SWidget> FQuestlineGraphRewardsDetailsCustomization::MakeIconButton(const FSlateBrush* Icon, const FText& Tooltip, bool bIsComboMenu, FGameplayTag OutcomeKey)
{
	// Muted-at-rest / bright-on-hover via the button style's foreground, with the image inheriting UseForeground().
	// The style is STATIC because .ButtonStyle() stores a POINTER, not a copy — a local FButtonStyle would dangle and
	// crash on paint. Same style for every icon button, so one shared instance is correct.
	static const FButtonStyle IconButtonStyle = []()
	{
		FButtonStyle S = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton");
		S.SetNormalForeground(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)));
		S.SetHoveredForeground(FSlateColor(FLinearColor::White));
		S.SetPressedForeground(FSlateColor(FLinearColor::White));
		return S;
	}();

	const TSharedRef<SImage> IconImage = SNew(SImage)
		.Image(Icon)
		.ColorAndOpacity(FSlateColor::UseForeground());

	if (bIsComboMenu)
	{
		return SNew(SComboButton)
			.HasDownArrow(false)
			.ButtonStyle(&IconButtonStyle)
			.ContentPadding(2.f)
			.ToolTipText(Tooltip)
			.OnGetMenuContent_Lambda([this, OutcomeKey]() { return BuildRekeyMenu(OutcomeKey); })
			.ButtonContent()
			[
				IconImage
			];
	}

	return SNew(SButton)
		.ButtonStyle(&IconButtonStyle)
		.ContentPadding(2.f)
		.ToolTipText(Tooltip)
		.OnClicked_Lambda([this, OutcomeKey]() { OnRemoveOutcome(OutcomeKey); return FReply::Handled(); })
		[
			IconImage
		];
}

void FQuestlineGraphRewardsDetailsCustomization::ForceRefresh()
{
	if (const TSharedPtr<IPropertyUtilities> Utils = PropertyUtilities.Pin())
	{
		Utils->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE