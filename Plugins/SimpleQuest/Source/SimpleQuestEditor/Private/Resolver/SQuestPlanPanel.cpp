// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "SQuestPlanPanel.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/QuestPlanBroker.h"
#include "Styling/StyleColors.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

namespace
{
	// What the reader needs to know first is the OPERATION, and "Update" is the one the plan model uses for a node that
	// is being relocated. The distinction matters more to a designer than to the model, so it is drawn here.
	FString PlanRowActionText(const FQuestNodePlanEntry& Entry)
	{
		switch (Entry.Action)
		{
		case EQuestNodePlanAction::Create: return TEXT("CREATE");
		case EQuestNodePlanAction::Orphan: return TEXT("ORPHAN");
		default: return Entry.bMoved ? TEXT("MOVE") : TEXT("UPDATE");
		}
	}

	// A key with no live node is left as-is rather than blanked: an unresolvable endpoint is exactly the case a reader
	// needs to see spelled out, and hiding it behind "(unknown)" would lose the only identifier they could search for.
	FString EdgeEndpointName(const FQuestInPlacePlan& Plan, const FString& Key)
	{
		const FString* Label = Plan.LabelByKey.Find(Key);
		return Label ? *Label : Key;
	}
}

void SQuestPlanPanel::Construct(const FArguments& InArgs)
{
	TargetAssetPath = InArgs._TargetAssetPath;
	OnRebuildRequested = InArgs._OnRebuildRequested;
	OnApplyRequested = InArgs._OnApplyRequested;
	CanApply = InArgs._CanApply;
	OnChooseSourceRequested = InArgs._OnChooseSourceRequested;
	SourceLabel = InArgs._SourceLabel;

	// Subscribe AND pull. A plan may have been computed before this tab was ever opened, and a panel that only listened
	// would sit empty beside a plan the log had already printed.
	PublishHandle = FQuestPlanBroker::Get().OnPlanPublished().AddSP(this, &SQuestPlanPanel::HandlePlanPublished);
	Questline = InArgs._Questline;
	// UObject::Modify broadcasts this, so it catches edits anywhere in the asset - including inside a container's INNER
	// graph, which subscribing to the root UEdGraph's OnGraphChanged would miss entirely. Cheap: it only sets a bool, and
	// only for objects belonging to this questline.
	ModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddSP(this, &SQuestPlanPanel::HandleObjectModified);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(STextBlock).Text(this, &SQuestPlanPanel::GetSummaryText).AutoWrapText(true)
		]

		// Refusals and warnings are NOT rows. They belong to no node, and a refusal is whole-plan fatal - nothing at all
		// applies while one stands - so burying them among the entries would let a designer scroll past the reason the
		// plan cannot run.
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 4.0f)
		[
			SNew(SBorder)
			.Visibility(this, &SQuestPlanPanel::GetBlockersVisibility)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FStyleColors::Warning)
			.Padding(6.0f)
			[
				SNew(STextBlock).Text(this, &SQuestPlanPanel::GetBlockersText).AutoWrapText(true)
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 4.0f)
		[
			SNew(SBorder)
			.Visibility(this, &SQuestPlanPanel::GetStaleVisibility)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FStyleColors::AccentYellow)
			.Padding(6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PlanStale", "This questline has changed since this plan was computed. Re-run the import to refresh it."))
				.AutoWrapText(true)
			]
		]

		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f)
		[
			SAssignNew(Table, SColumnTableView<FQuestPlanRowPtr>)
			.Columns(MakeColumns())
			.OnGetChildren_Lambda([](FQuestPlanRowPtr Item, TArray<FQuestPlanRowPtr>& Out)
			{
				if (Item.IsValid()) { Out = Item->Children; }
			})
			.ExpanderColumnId(TEXT("Action"))
			.bShowExpanderWires(false)
			.SelectionMode(ESelectionMode::None)
			.PersistenceKey(TEXT("QuestPlanPanel"))
			.FilterHintText(LOCTEXT("PlanFilterHint", "Filter nodes and properties..."))
			.EmptyState()
			[
				SNew(STextBlock).Text(this, &SQuestPlanPanel::GetSummaryText)
			]
			.Title(FText::FromString(FPaths::GetBaseFilename(TargetAssetPath)))
			.Toolbar()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("PlanChooseSource", "Choose Source..."))
					.ToolTipText(LOCTEXT("PlanChooseSourceTip", "Pick a folder of source data and build a plan from it."))
					.OnClicked_Lambda([this]() { OnChooseSourceRequested.ExecuteIfBound(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("PlanRebuild", "Rebuild"))
					.ToolTipText(LOCTEXT("PlanRebuildTip", "Re-read the same source and recompute this plan."))
					.IsEnabled_Lambda([this]() { return !SourceLabel.Get(FText::GetEmpty()).IsEmpty() && bHasPlan; })
					.OnClicked_Lambda([this]() { OnRebuildRequested.ExecuteIfBound(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("PlanApply", "Apply"))
					.ToolTipText(LOCTEXT("PlanApplyTip", "Perform these changes. Re-reads and re-plans first, so what runs is what you see."))
					.IsEnabled_Lambda([this]() { return CanApply.Get(false); })
					.OnClicked_Lambda([this]() { OnApplyRequested.ExecuteIfBound(); return FReply::Handled(); })
				]
				// Named, because Rebuild is otherwise a promise about a folder the panel never showed you.
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(SourceLabel)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				]
			]
		]
	];

	if (const FQuestPlanRecord* Existing = FQuestPlanBroker::Get().Find(TargetAssetPath))
	{
		HandlePlanPublished(TargetAssetPath, Existing->Plan);
	}
}

SQuestPlanPanel::~SQuestPlanPanel()
{
	FQuestPlanBroker::Get().OnPlanPublished().Remove(PublishHandle);
	FCoreUObjectDelegates::OnObjectModified.Remove(ModifiedHandle);
}

TArray<FTableColumnDef<FQuestPlanRowPtr>> SQuestPlanPanel::MakeColumns() const
{
	TArray<FTableColumnDef<FQuestPlanRowPtr>> Columns;

	FTableColumnDef<FQuestPlanRowPtr>& ActionCol = Columns.AddDefaulted_GetRef();
	ActionCol.Id       = TEXT("Action");
	ActionCol.Label    = LOCTEXT("PlanColAction", "Action");
	ActionCol.FillWidth = 0.24f;
	ActionCol.GetText  = [](const FQuestPlanRowPtr& R){ return R.IsValid() ? FText::FromString(R->Action) : FText::GetEmpty(); };

	FTableColumnDef<FQuestPlanRowPtr>& NameCol = Columns.AddDefaulted_GetRef();
	NameCol.Id        = TEXT("Name");
	NameCol.Label     = LOCTEXT("PlanColName", "Node / Property");
	NameCol.FillWidth = 0.42f;
	NameCol.GetText   = [](const FQuestPlanRowPtr& R){ return R.IsValid() ? FText::FromString(R->Name) : FText::GetEmpty(); };

	FTableColumnDef<FQuestPlanRowPtr>& DetailCol = Columns.AddDefaulted_GetRef();
	DetailCol.Id        = TEXT("Detail");
	DetailCol.Label     = LOCTEXT("PlanColDetail", "Detail");
	DetailCol.FillWidth = 0.40f;
	DetailCol.GetText   = [](const FQuestPlanRowPtr& R){ return R.IsValid() ? FText::FromString(R->Detail) : FText::GetEmpty(); };

	// Parent rows carry the operation; child rows are its detail. Rendering them identically made a wiring "remove" read
	// with the same weight as an "ORPHAN", so depth is carried by WEIGHT and COLOUR as well as indentation - the same
	// thing the Questline Outliner does, and for the same reason.
	auto NodeIsParent = [](const FQuestPlanRowPtr& R){ return R.IsValid() && R->Kind == FQuestPlanRow::EKind::Node; };
	auto RowFont  = [NodeIsParent](const FQuestPlanRowPtr& R)
	{
		return FCoreStyle::GetDefaultFontStyle(NodeIsParent(R) ? "Bold" : "Regular", 9);
	};
	auto RowColor = [NodeIsParent](const FQuestPlanRowPtr& R)
	{
		return NodeIsParent(R) ? FSlateColor::UseForeground() : FSlateColor(FLinearColor(0.27f, 0.27f, 0.27f));
	};

	for (FTableColumnDef<FQuestPlanRowPtr>& C : Columns)
	{
		C.GetFont = RowFont;
		C.GetTextColor = RowColor;
	}

	return Columns;
}

void SQuestPlanPanel::HandlePlanPublished(const FString& InAssetPath, const FQuestInPlacePlan& Plan)
{
	if (InAssetPath != TargetAssetPath) { return; }

	bHasPlan = true;
	bStale = false;   // a fresh plan is by definition current

	Summary = FText::FromString(FString::Printf(
	TEXT("%d update(s), %d with changes  |  %d created  |  %d orphaned  |  %d connections added, %d removed  |  %d untouched"),
		Plan.CountOf(EQuestNodePlanAction::Update),
		Plan.ChangedNodeCount(),
		Plan.CountOf(EQuestNodePlanAction::Create),
		Plan.CountOf(EQuestNodePlanAction::Orphan),
		Plan.AddedEdges.Num(),
		Plan.RemovedEdges.Num(),
		Plan.UntouchedNodeCount));

	TArray<FString> Lines;
	for (const FString& R : Plan.Refusals) { Lines.Add(FString::Printf(TEXT("REFUSED: %s"), *R)); }
	for (const FString& K : Plan.AmbiguousKeys) { Lines.Add(FString::Printf(TEXT("CONTESTED KEY: %s"), *K)); }
	for (const FString& W : Plan.Warnings) { Lines.Add(FString::Printf(TEXT("warning: %s"), *W)); }
	if (!Plan.Refusals.IsEmpty() || !Plan.AmbiguousKeys.IsEmpty())
	{
		Lines.Insert(TEXT("This plan cannot be applied until these are resolved. Nothing would be written."), 0);
	}
	Blockers = FText::FromString(FString::Join(Lines, TEXT("\n")));

	RebuildRows(Plan);
}

void SQuestPlanPanel::HandleObjectModified(UObject* Modified)
{
	if (!bHasPlan || bStale || !Modified || !Questline.IsValid()) { return; }
	// The asset itself, or anything living inside it - a node, an inner graph, an instanced reward.
	if (Modified == Questline.Get() || Modified->IsIn(Questline.Get()))
	{
		bStale = true;
	}
}

EVisibility SQuestPlanPanel::GetStaleVisibility() const
{
	return bStale ? EVisibility::Visible : EVisibility::Collapsed;
}

void SQuestPlanPanel::RebuildRows(const FQuestInPlacePlan& Plan)
{
	Rows.Reset();

	for (const FQuestNodePlanEntry& Entry : Plan.Entries)
	{
		// An unchanged match is the common case on a healthy re-import. Listing them would bury the ones that matter,
		// which is the same judgement the log makes - the two renderings agree deliberately.
		if (Entry.Action == EQuestNodePlanAction::Update && Entry.Changes.IsEmpty() && !Entry.bMoved) { continue; }

		FQuestPlanRowPtr Node = MakeShared<FQuestPlanRow>();
		Node->Kind        = FQuestPlanRow::EKind::Node;
		Node->Action      = PlanRowActionText(Entry);
		Node->Name        = Entry.Label.IsEmpty() ? Entry.Key : Entry.Label;
		Node->bStructural = Entry.bMoved;
		Node->Detail      = Entry.bMoved
			? FString::Printf(TEXT("%s  —  %s → %s"), *Entry.ClassName, *Entry.CurrentGraphLabel, *Entry.GraphLabel)
			: FString::Printf(TEXT("%s  —  %s"), *Entry.ClassName,
				Entry.Action == EQuestNodePlanAction::Orphan ? *Entry.CurrentGraphLabel : *Entry.GraphLabel);

		for (const FQuestPropertyChange& Change : Entry.Changes)
		{
			FQuestPlanRowPtr Row = MakeShared<FQuestPlanRow>();
			Row->Kind   = FQuestPlanRow::EKind::Change;
			Row->Name   = Change.Property;
			Row->Detail = FString::Printf(TEXT("'%s' → '%s'"), *Change.CurrentText, *Change.IncomingText);
			switch (Change.Kind)
			{
			case EQuestPropertyChangeKind::ChildAdded:   Row->Action = TEXT("+ child"); break;
			case EQuestPropertyChangeKind::ChildRemoved: Row->Action = TEXT("- child"); break;
			default: break;
			}
			Node->Children.Add(Row);
		}

		Rows.Add(Node);
	}

	// Wiring belongs to no node, so it gets one synthetic parent rather than being scattered or dropped. A plan that
	// rewires but changes no property would otherwise render as an empty table beside a summary claiming edge changes.
	if (!Plan.AddedEdges.IsEmpty() || !Plan.RemovedEdges.IsEmpty())
	{
		FQuestPlanRowPtr Wiring = MakeShared<FQuestPlanRow>();
		Wiring->Kind   = FQuestPlanRow::EKind::Node;
		Wiring->Action = TEXT("WIRING");
		Wiring->Name   = TEXT("Connections");
		Wiring->Detail = FString::Printf(TEXT("%d added, %d removed"), Plan.AddedEdges.Num(), Plan.RemovedEdges.Num());

		for (const FQuestDataEdge& E : Plan.RemovedEdges)
		{
			FQuestPlanRowPtr Row = MakeShared<FQuestPlanRow>();
			Row->Kind = FQuestPlanRow::EKind::Change;
			Row->Action = TEXT("remove");
			Row->Name = E.Type;
			Row->Detail = FString::Printf(TEXT("%s → %s"), *EdgeEndpointName(Plan, E.From), *EdgeEndpointName(Plan, E.To));
			Wiring->Children.Add(Row);
		}
		for (const FQuestDataEdge& E : Plan.AddedEdges)
		{
			FQuestPlanRowPtr Row = MakeShared<FQuestPlanRow>();
			Row->Kind = FQuestPlanRow::EKind::Change;
			Row->Action = TEXT("add");
			Row->Name = E.Type;
			Row->Detail = FString::Printf(TEXT("%s → %s"), *EdgeEndpointName(Plan, E.From), *EdgeEndpointName(Plan, E.To));
			Wiring->Children.Add(Row);
		}
		Rows.Add(Wiring);
	}

	if (Table.IsValid()) { Table->SetRootItems(Rows); }
}

FText SQuestPlanPanel::GetSummaryText() const
{
	if (!bHasPlan)
	{
		return LOCTEXT("NoPlanYet", "No plan has been computed for this questline. Run an in-place import to preview one.");
	}
	// A plan that exists and finds nothing is a DIFFERENT fact from no plan at all, and only one of them means the
	// asset matches its source. Saying so is what stops an empty table reading as a broken panel.
	return Rows.IsEmpty()
		? LOCTEXT("PlanIsClean", "This questline already matches its source — a re-import would change nothing.")
		: Summary;
}

FText SQuestPlanPanel::GetBlockersText() const { return Blockers; }

EVisibility SQuestPlanPanel::GetBlockersVisibility() const
{
	return Blockers.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

#undef LOCTEXT_NAMESPACE

