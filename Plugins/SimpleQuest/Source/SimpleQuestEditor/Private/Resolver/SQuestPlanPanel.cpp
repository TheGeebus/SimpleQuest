// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "SQuestPlanPanel.h"

#include "Engine/DataTable.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "SWarningOrErrorBox.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/QuestDataFormatRegistry.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestPlanBroker.h"
#include "Styling/StyleColors.h"
#include "Styling/SlateStyleRegistry.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
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

	// Reached by NAME through the registry, matching how the class icon is looked up, so the panel needs no handle on
	// the editor module. Null is survivable: SBorder just draws nothing, which degrades to the outline-only look
	// rather than crashing if the style set ever stops registering.
	const FSlateBrush* BannerFillBrush(const FName BrushName)
	{
		const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(TEXT("SimpleQuestStyle"));
		return Style ? Style->GetBrush(BrushName) : nullptr;
	}
}

void SQuestPlanPanel::Construct(const FArguments& InArgs)
{
	TargetAssetPath = InArgs._TargetAssetPath;
	OnBuildPlanRequested = InArgs._OnBuildPlanRequested;
	OnApplyRequested = InArgs._OnApplyRequested;
	OnBrowseRequested = InArgs._OnBrowseRequested;
	CanBuildPlan = InArgs._CanBuildPlan;
	CanApply = InArgs._CanApply;
	SourceStale = InArgs._SourceStale;
	PlanProvenance = InArgs._PlanProvenance;
	SourceFolder = InArgs._SourceFolder;
	OnFolderChanged = InArgs._OnFolderChanged;
	SourceTable = InArgs._SourceTable;
	OnTableChanged = InArgs._OnTableChanged;
	OnSourceKindChanged = InArgs._OnSourceKindChanged;
	FormatName = InArgs._FormatName;
	OnFormatChanged = InArgs._OnFormatChanged;
	MappingAsset = InArgs._MappingAsset;
	OnMappingChanged = InArgs._OnMappingChanged;
	
	ShownSourceKind = SourceTable.Get(FSoftObjectPath()).IsValid()
		? EQuestPlanSourceKind::DataTable
		: EQuestPlanSourceKind::Folder;

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

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 0.0f)
		[
			SNew(SBorder)
			.Visibility(this, &SQuestPlanPanel::GetBlockersVisibility)
			.BorderImage(BannerFillBrush(TEXT("SimpleQuest.Banner.ErrorFill")))
			.Padding(0.0f)   // zero, so the fill's rounded edge sits exactly under the widget's outline
			[
				SNew(SWarningOrErrorBox)
				.MessageStyle(EMessageStyle::Error)
				.Message(this, &SQuestPlanPanel::GetBlockersText)
				.AutoWrapText(true)
				.Padding(FMargin(16.0f, 6.0f, 8.0f, 6.f))
				.IconSize(FVector2D(20.0f, 20.0f))
			]		
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 0.0f)
		[
			SNew(SBorder)
			.Visibility(this, &SQuestPlanPanel::GetStaleVisibility)
			.BorderImage(BannerFillBrush(TEXT("SimpleQuest.Banner.WarningFill")))
			.Padding(0.0f)
			[
				SNew(SWarningOrErrorBox)
				.MessageStyle(EMessageStyle::Warning)
				.Message(this, &SQuestPlanPanel::GetStaleText)
				.AutoWrapText(true)
				.Padding(FMargin(16.0f, 6.0f, 8.0f, 6.f))
				.IconSize(FVector2D(20.0f, 20.0f))
			]
		]
		// User Controls - both rows
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
			.bShowSearchBox(false)
			.EmptyState()
			[
				SNew(STextBlock).Text(this, &SQuestPlanPanel::GetSummaryText)
			]
			.Title(FText::FromString(FPaths::GetBaseFilename(TargetAssetPath)))
			// Named beneath the asset, because Rebuild is otherwise a promise about a folder the panel never showed you.
			// Provenance, not selection: what the rows below are a statement about. Collapses when no plan exists.
			.Subtitle(TAttribute<FText>::Create([this]() { return PlanProvenance.Get(FText::GetEmpty()); }))
			.Toolbar()
			[
				SNew(SVerticalBox)

				// ROW ONE - WHERE THE DATA IS, ordered coarse to fine: which provenance, how it parses, what its
				// columns mean, then the one specific artifact. The location goes last because it is the only
				// variable-width control and can absorb the slack out to the right edge.
				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 4.f, 8.f, 0.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("SourceKindLabel", "Source"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SComboButton)
						.ToolTipText(LOCTEXT("SourceKindTip", "Where this questline's data lives - a folder of files, or an in-engine Data Table."))
						.OnGetMenuContent_Lambda([this]()
						{
							FMenuBuilder Menu(true, nullptr);
							auto AddKind = [this, &Menu](EQuestPlanSourceKind Kind, const FText& Label)
							{
								Menu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([this, Kind]()
									{
										ShownSourceKind = Kind;
										OnSourceKindChanged.ExecuteIfBound(Kind);
									})));
							};
							AddKind(EQuestPlanSourceKind::Folder, LOCTEXT("SourceKindFolder", "Folder"));
							AddKind(EQuestPlanSourceKind::DataTable, LOCTEXT("SourceKindTable", "Data Table"));
							return Menu.MakeWidget();
						})
						.ButtonContent()
						[
							// FIXED width: "Folder" and "Data Table" are different lengths, so an auto-sized combo shifts
							// every control after it on a kind switch. Pinning it means the stable half of the row stays
							// put and only the varying half reflows.
							SNew(SBox).WidthOverride(78.0f).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Text_Lambda([this]()
								{
									return ShownSourceKind == EQuestPlanSourceKind::DataTable
										? LOCTEXT("SourceKindTable", "Data Table")
										: LOCTEXT("SourceKindFolder", "Folder");
								})
							]
						]
					]

					// SEPARATORS DELIMIT GROUPS, not every control. Source and Mapping are each their own thing; Format
					// and the location are ONE group, because they appear, vanish and swap together with the kind - so no
					// rule runs between them.
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(0.0f, 2.0f, 8.0f, 2.0f)
					[
						SNew(SSeparator).Orientation(Orient_Vertical).Thickness(1.0f)
					]

					// MAPPING SECOND, ahead of Format, which inverts the coarse-to-fine reading of those two on purpose:
					// what matters more in a row with a toggle is a STABLE PREFIX and a VARYING SUFFIX. Mapping applies to
					// either provenance and never changes shape, so it belongs with the stable half.
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("MappingLabel", "Mapping"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SBox).MinDesiredWidth(180.0f)
						[
							SNew(SObjectPropertyEntryBox)
							.AllowedClass(UQuestImportMapping::StaticClass())
							.AllowClear(true)
							.DisplayThumbnail(false)
							.ToolTipText(LOCTEXT("PlanMappingTip", "Optional translation mapping. Leave it empty when the source is already in the plugin's own shape."))
							.ObjectPath_Lambda([this]() { return MappingAsset.Get(FSoftObjectPath()).ToString(); })
							.OnObjectChanged_Lambda([this](const FAssetData& Asset)
							{
								OnMappingChanged.ExecuteIfBound(Asset.ToSoftObjectPath());
							})
						]
					]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(0.0f, 2.0f, 8.0f, 2.0f)
					[
						SNew(SSeparator).Orientation(Orient_Vertical).Thickness(1.0f)
					]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock)
						.Visibility(this, &SQuestPlanPanel::GetFolderRowVisibility)
						.Text(LOCTEXT("FormatLabel", "Format"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SAssignNew(FormatCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&FormatOptions)
						// COLLAPSED for a table, not greyed. Grey means "unavailable for now"; a Data Table's row struct
						// is not a format you could pick differently, so the control is meaningless rather than
						// unavailable - and the row reflows on a kind switch regardless, so hiding it costs no stability
						// and returns the width to the controls that do apply.
						.Visibility(this, &SQuestPlanPanel::GetFolderRowVisibility)
						.ToolTipText(LOCTEXT("PlanFormatTip", "Which format provider reads the source folder. The list is every provider registered with the plugin, including any your own module adds."))
						.OnComboBoxOpening_Lambda([this]()
						{
							// Refreshed on open: a provider registered after this panel was built would be missing from a
							// list snapshotted at construction, and the panel would silently offer fewer formats than exist.
							RefreshFormatOptions();
							if (FormatCombo.IsValid()) { FormatCombo->RefreshOptions(); }
						})
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
						{
							return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
						{
							if (Item.IsValid()) { OnFormatChanged.ExecuteIfBound(*Item); }
						})
						[
							SNew(STextBlock).Text(this, &SQuestPlanPanel::GetFormatButtonText)
						]
					]

					// THE LOCATION - one of two widgets, never both, and sized differently: the path stretches because
					// it is arbitrarily long, the asset picker does not because a name does not get more readable wide.
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						// Named like its neighbours, but the only label whose WORD depends on the kind - everything else in
						// this row means the same thing whichever provenance is selected. "Asset" rather than "Table"
						// deliberately: it stays true for any asset-based provenance a third kind might add, and the kind
						// combo two controls to the left already says which one.
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return ShownSourceKind == EQuestPlanSourceKind::DataTable
								? LOCTEXT("LocationLabelAsset", "Asset")
								: LOCTEXT("LocationLabelPath", "Path");
						})
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SEditableTextBox)
						.Visibility(this, &SQuestPlanPanel::GetFolderRowVisibility)
						.HintText(LOCTEXT("SourceFolderHint", "Path to a folder of source data..."))
						.ToolTipText(LOCTEXT("SourceFolderTip", "The folder Build Plan will read. Type it or browse; both converge on one write."))
						.Text_Lambda([this]() { return FText::FromString(SourceFolder.Get(FString())); })
						.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
						{
							OnFolderChanged.ExecuteIfBound(NewText.ToString().TrimStartAndEnd().TrimQuotes());
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.Visibility(this, &SQuestPlanPanel::GetFolderRowVisibility)
						.Text(LOCTEXT("SourceBrowse", "Browse..."))
						.ToolTipText(LOCTEXT("SourceBrowseTip", "Pick the folder Build Plan will read."))
						.OnClicked_Lambda([this]() { OnBrowseRequested.ExecuteIfBound(); return FReply::Handled(); })
					]
					// AutoWidth, unlike the path beside it: an asset reference is a NAME and reads fine at a fixed size,
					// while a path is arbitrarily long and benefits from every pixel. The two never show together, so
					// they can be sized to what each actually needs rather than sharing one rule.
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SBox).MinDesiredWidth(240.0f)
						[
							SNew(SObjectPropertyEntryBox)
							.Visibility(this, &SQuestPlanPanel::GetTableRowVisibility)
							.AllowedClass(UDataTable::StaticClass())
							.AllowClear(true)
							.DisplayThumbnail(false)
							.ToolTipText(LOCTEXT("SourceTableTip", "The Data Table Build Plan will read. Its row struct supplies the columns, so no format is needed."))
							.ObjectPath_Lambda([this]() { return SourceTable.Get(FSoftObjectPath()).ToString(); })
							.OnObjectChanged_Lambda([this](const FAssetData& Asset)
							{
								OnTableChanged.ExecuteIfBound(Asset.ToSoftObjectPath());
							})
						]
					]
				]

				// ROW TWO - WHAT TO DO ABOUT IT, in workflow order: read it, then commit.
				// Export joins this row to the left of a separator once it exists.
				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 4.f, 8.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("PlanBuild", "Build Plan"))
						.ToolTipText(LOCTEXT("PlanBuildTip", "Read the source above and work out what re-importing would change. Nothing is written."))
						.IsEnabled_Lambda([this]() { return CanBuildPlan.Get(false); })
						.OnClicked_Lambda([this]() { OnBuildPlanRequested.ExecuteIfBound(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("PlanApply", "Apply"))
						.ToolTipText(LOCTEXT("PlanApplyTip", "Perform these changes. Re-reads and re-plans first, so what runs is what you see."))
						.IsEnabled_Lambda([this]() { return CanApply.Get(false); })
						.OnClicked_Lambda([this]() { OnApplyRequested.ExecuteIfBound(); return FReply::Handled(); })
					]
					// Proportional spacer plus a fixed box: the filter grows with the panel instead of
					// sitting at one size and stranding whitespace beside it. 0.35 / 0.65 leaves the verbs a gap without
					// letting the filter run the full width on a wide panel.
					+ SHorizontalBox::Slot().FillWidth(0.35f)[ SNullWidget::NullWidget ]
					+ SHorizontalBox::Slot().FillWidth(0.65f).VAlign(VAlign_Center)
					[
						SNew(SBox).MinDesiredWidth(200.0f)
						[
							SNew(SSearchBox)
							.HintText(LOCTEXT("PlanFilterHint", "Filter nodes and properties..."))
							.OnTextChanged_Lambda([this](const FText& InText)
							{
								if (Table.IsValid()) { Table->SetFilterText(InText); }
							})
						]
					]
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

void SQuestPlanPanel::RefreshFormatOptions()
{
	FormatOptions.Reset();
	for (const FString& Name : FQuestDataFormatRegistry::Get().GetRegisteredNames())
	{
		FormatOptions.Add(MakeShared<FString>(Name));
	}
}

FText SQuestPlanPanel::GetFormatButtonText() const
{
	const FString Current = FormatName.Get(FString());
	return Current.IsEmpty() ? LOCTEXT("PlanFormatNone", "Format") : FText::FromString(Current);
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

	bStale = false;   // a fresh publish is by definition current
	if (const FQuestPlanRecord* Rec = FQuestPlanBroker::Get().Find(InAssetPath))
	{
		LastError = Rec->Error;
		LastFailedFormat = Rec->Source.FormatName;
	}
	// A FAILURE also arrives here - PublishFailure broadcasts through the same delegate - and it did not produce a
	// plan. Setting bHasPlan unconditionally would leave the panel rendering the previous plan's rows underneath a
	// message saying the source could not be read.
	bHasPlan = LastError.IsEmpty();

	if (!LastError.IsEmpty())
	{
		// A failure produced no plan, so there is nothing to summarize and no refusals to report. Both are derived from
		// the STALE plan PublishFailure preserved, and rendering either would attach the previous run's blockers to
		// this run's error message.
		Summary = FText::GetEmpty();
		Blockers = FText::GetEmpty();
		Rows.Reset();
		if (Table.IsValid()) { Table->SetRootItems(Rows); }
		return;
	}

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
	// Two independent reasons a plan stops being true: the ASSET moved under it, or the SELECTION moved away from what
	// it was built against. Either one makes what is on screen a statement about a state that no longer exists.
	return (bStale || SourceStale.Get(false)) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SQuestPlanPanel::GetStaleText() const
{
	// Named separately because the fix differs: one wants a re-plan, the other wants you to notice you changed the
	// source. A single sentence covering both would send half the readers looking in the wrong place.
	if (bStale)
	{
		return LOCTEXT("PlanStaleAsset", "This questline has changed since this plan was computed. Build Plan again to refresh it.");
	}
	return LOCTEXT("PlanStaleSource", "The source above has changed since this plan was computed. Build Plan again to refresh it.");
}

EVisibility SQuestPlanPanel::GetFolderRowVisibility() const
{
	return ShownSourceKind == EQuestPlanSourceKind::Folder ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SQuestPlanPanel::GetTableRowVisibility() const
{
	return ShownSourceKind == EQuestPlanSourceKind::DataTable ? EVisibility::Visible : EVisibility::Collapsed;
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
	if (!LastError.IsEmpty())
	{
		// Names the format that FAILED, not the one the combo currently shows. A console run has its own format
		// selection, so the two can legitimately disagree - and a panel reporting its own combo's format for someone
		// else's failure would send a designer looking for a bug in the wrong reading.
		return FText::Format(LOCTEXT("PlanFailed", "Could not read the source as {0} — {1}. Nothing was changed."),
			FText::FromString(LastFailedFormat.IsEmpty() ? TEXT("?") : LastFailedFormat),
			FText::FromString(LastError));
	}
	if (!bHasPlan)
	{
		return LOCTEXT("NoPlanYet", "No plan has been computed for this questline. Choose a source above to build one.");
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

