// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "SQuestPlanPanel.h"

#include "Engine/DataTable.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "SimpleQuestLog.h"
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

FString SQuestPlanPanel::CurrentTargetPath() const
{
	return Questline.IsValid() ? Questline->GetPathName() : TargetAssetPath;
}

void SQuestPlanPanel::Construct(const FArguments& InArgs)
{
	TargetAssetPath = InArgs._TargetAssetPath;
	OnBuildPlanRequested = InArgs._OnBuildPlanRequested;
	OnApplyRequested = InArgs._OnApplyRequested;
	OnBrowseRequested = InArgs._OnBrowseRequested;
	OnNavigateRequested = InArgs._OnNavigateRequested;
	OnHoverRequested = InArgs._OnHoverRequested;
	CanBuildPlan = InArgs._CanBuildPlan;
	CanApply = InArgs._CanApply;
	ProvenanceStale = InArgs._ProvenanceStale;
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
	OnExportRequested = InArgs._OnExportRequested;
	CanExport = InArgs._CanExport;
	DestinationFolder = InArgs._DestinationFolder;
	OnDestinationFolderChanged = InArgs._OnDestinationFolderChanged;
	DestinationTable = InArgs._DestinationTable;
	OnDestinationTableChanged = InArgs._OnDestinationTableChanged;
	OnDestinationKindChanged = InArgs._OnDestinationKindChanged;
	DestinationFormatName = InArgs._DestinationFormatName;
	OnDestinationFormatChanged = InArgs._OnDestinationFormatChanged;
	DestinationMapping = InArgs._DestinationMapping;
	OnDestinationMappingChanged = InArgs._OnDestinationMappingChanged;
	OnDestinationBrowseRequested = InArgs._OnDestinationBrowseRequested;
	
	ShownSourceKind = SourceTable.Get(FSoftObjectPath()).IsValid()
		? EQuestPlanEndpointKind::DataTable
		: EQuestPlanEndpointKind::Folder;

	ShownDestinationKind = DestinationTable.Get(FSoftObjectPath()).IsValid()
		? EQuestPlanEndpointKind::DataTable
		: EQuestPlanEndpointKind::Folder;

	// Subscribe AND pull. A plan may have been computed before this tab was ever opened, and a panel that only listened
	// would sit empty beside a plan the log had already printed.
	PublishHandle = FQuestPlanBroker::Get().OnPlanPublished().AddSP(this, &SQuestPlanPanel::HandlePlanPublished);
	ClearedHandle = FQuestPlanBroker::Get().OnPlanCleared().AddSP(this, &SQuestPlanPanel::HandlePlanCleared);
	ExportHandle = FQuestPlanBroker::Get().OnExportCompleted().AddSP(this, &SQuestPlanPanel::HandleExportCompleted);
	Questline = InArgs._Questline;
	// UObject::Modify broadcasts this, so it catches edits anywhere in the asset - including inside a container's INNER
	// graph, which subscribing to the root UEdGraph's OnGraphChanged would miss entirely. Cheap: it only sets a bool, and
	// only for objects belonging to this questline.
	ModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddSP(this, &SQuestPlanPanel::HandleObjectModified);

	// Assembled before the tree because the row is built from BINDINGS rather than from members - which is the whole
	// point of the extraction, and what lets a second one be assembled the same way.
	FQuestEndpointRowArgs SourceRowArgs;
	SourceRowArgs.Label				= LOCTEXT("SourceKindLabel", "Source");
	SourceRowArgs.Kind				= &ShownSourceKind;
	SourceRowArgs.FormatCombo		= &FormatCombo;
	SourceRowArgs.Folder			= SourceFolder;
	SourceRowArgs.Table				= SourceTable;
	SourceRowArgs.FormatName		= FormatName;
	SourceRowArgs.Mapping			= MappingAsset;
	SourceRowArgs.OnFolderChanged	= OnFolderChanged;
	SourceRowArgs.OnTableChanged	= OnTableChanged;
	SourceRowArgs.OnKindChanged		= OnSourceKindChanged;
	SourceRowArgs.OnFormatChanged	= OnFormatChanged;
	SourceRowArgs.OnMappingChanged	= OnMappingChanged;
	SourceRowArgs.OnBrowseRequested	= OnBrowseRequested;
	FQuestEndpointRowArgs DestRowArgs;
	DestRowArgs.Label              = LOCTEXT("DestinationKindLabel", "Destination");
	DestRowArgs.Kind               = &ShownDestinationKind;
	DestRowArgs.FormatCombo        = &DestinationFormatCombo;
	DestRowArgs.Folder             = DestinationFolder;
	DestRowArgs.Table              = DestinationTable;
	DestRowArgs.FormatName         = DestinationFormatName;
	DestRowArgs.Mapping            = DestinationMapping;
	DestRowArgs.OnFolderChanged    = OnDestinationFolderChanged;
	DestRowArgs.OnTableChanged     = OnDestinationTableChanged;
	DestRowArgs.OnKindChanged      = OnDestinationKindChanged;
	DestRowArgs.OnFormatChanged    = OnDestinationFormatChanged;
	DestRowArgs.OnMappingChanged   = OnDestinationMappingChanged;
	DestRowArgs.OnBrowseRequested  = OnDestinationBrowseRequested;
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 0.0f)
		[
			SNew(SBorder)
			.Visibility(this, &SQuestPlanPanel::GetBlockersVisibility)
			// The FILL follows the style. Leaving it fixed would put an amber-outlined warning on a red ground, which
			// reads as an error with a decoration problem rather than as a warning.
			.BorderImage_Lambda([this]()
			{
				return BannerFillBrush(GetBlockersStyle() == EMessageStyle::Error
					? TEXT("SimpleQuest.Banner.ErrorFill")
					: TEXT("SimpleQuest.Banner.WarningFill"));
			})
			.Padding(0.0f)   // zero, so the fill's rounded edge sits exactly under the widget's outline
			[
				SNew(SWarningOrErrorBox)
				.MessageStyle(this, &SQuestPlanPanel::GetBlockersStyle)
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
			.OnItemActivated(this, &SQuestPlanPanel::HandleRowActivated)
			.OnItemHovered(this, &SQuestPlanPanel::HandleRowHovered)
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

				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 6.0f)
				[
					SNew(SSeparator).Orientation(Orient_Horizontal).Thickness(2.0f)
				]

				// THE SOURCE - WHERE THE DATA COMES FROM. Built through MakeEndpointRow so the destination row can be the SAME row
				// rather than a copy; every layout decision now lives there, stated once.
				+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 0.0f, 8.0f, 0.0f)
				[
					MakeEndpointRow(SourceRowArgs)
				]

				// THE SOURCE'S VERBS, directly beneath the endpoint they act on. All four used to share one row, which
				// read as a SEQUENCE - do this, then that - when import and export are opposite directions.
				+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 4.0f, 8.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("BuildImportPlan", "Build Import Plan"))
						.ToolTipText(LOCTEXT("BuildImportPlanTip", "Read the source above and work out what importing it would change in this questline. Nothing is written."))
						.IsEnabled_Lambda([this]() { return CanBuildPlan.Get(false); })
						.OnClicked_Lambda([this]() { OnBuildPlanRequested.ExecuteIfBound(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("ApplyImportPlan", "Apply Import Plan"))
						.ToolTipText(LOCTEXT("ApplyImportPlanTip", "Write these changes into this questline. Re-reads and re-plans first, so what runs is what you see."))
						// MUTUALLY EXCLUSIVE with its export counterpart, because there is ONE plan record per questline
						// and it points one way. Two buttons that each do one thing, rather than one whose meaning
						// depends on state nobody can see.
						.IsEnabled_Lambda([this]()
						{
							return CanApply.Get(false) && PlanDirection == EQuestPlanDirection::IntoGraph;
						})
						.OnClicked_Lambda([this]() { OnApplyRequested.ExecuteIfBound(); return FReply::Handled(); })
					]
				]

				// A RULE between the two blocks, not merely a gap. Each block is an endpoint plus the verbs that act on
				// it, and spacing alone left four rows reading as one list of controls. Same reasoning the vertical
				// separators follow inside a row: a line delimits GROUPS, and these are two.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 6.0f)
				[
					SNew(SSeparator).Orientation(Orient_Horizontal).Thickness(2.0f)
				]

				// THE DESTINATION - WHERE IT GOES. The same row, other direction: "read from A, write to B" becomes a standing
				// configuration you can SEE rather than a mode you flip.
				+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 0.0f, 8.0f, 0.0f)
				[
					MakeEndpointRow(DestRowArgs)
				]

				// THE DESTINATION'S VERBS.
				+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 4.0f, 8.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.Text_Lambda([this]()
						{
							// ASYMMETRIC WITH THE SOURCE, and honestly so: a folder is OURS, so writing it is
							// fire-and-forget and there is no plan to build. A studio's table is THEIRS, so it plans.
							return ShownDestinationKind == EQuestPlanEndpointKind::DataTable
								? LOCTEXT("BuildExportPlan", "Build Export Plan")
								: LOCTEXT("PlanExport", "Export");
						})
						.ToolTipText_Lambda([this]()
						{
							if (ShownDestinationKind != EQuestPlanEndpointKind::DataTable)
							{
								return LOCTEXT("PlanExportTip", "Write this questline out as data tables you can read, diff and edit. Goes to the destination folder above, or to the default location when none is set.");
							}
							// Names the missing half specifically. "This is unavailable" is the failure a greyed control
							// makes easy, and the whole point of greying here was to stop a guaranteed-failed press.
							if (!DestinationTable.Get(FSoftObjectPath()).IsValid())
							{
								return LOCTEXT("BuildExportNoTable", "Pick the Data Table to write into.");
							}
							if (!DestinationMapping.Get(FSoftObjectPath()).IsValid())
							{
								return LOCTEXT("BuildExportNoMapping", "Pick a Mapping. Writing into a Data Table needs a recipe saying which of that table's fields this questline's properties belong in.");
							}
							return LOCTEXT("BuildExportPlanTip", "Work out what writing this questline into that Data Table would change. Nothing is written - review it below, then apply.");
						})
						.IsEnabled_Lambda([this]()
						{
							if (!CanExport.Get(false)) { return false; }
							if (ShownDestinationKind != EQuestPlanEndpointKind::DataTable) { return true; }
							// A table write needs BOTH: somewhere to write, and a recipe saying which of that table's
							// fields this questline's properties belong in. Without either it refuses every time, so
							// the button could only ever fail - and a button that can only fail is not an affordance.
							return DestinationTable.Get(FSoftObjectPath()).IsValid()
								&& DestinationMapping.Get(FSoftObjectPath()).IsValid();
						})
						.OnClicked_Lambda([this]() { OnExportRequested.ExecuteIfBound(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						// COLLAPSED, not greyed. A folder export is ONE step - there is nothing to apply, ever, in that
						// mode - so a permanently dead button would read as broken. Grey what is temporarily
						// unavailable; collapse what is meaningless here. Same rule the format combo follows.
						.Visibility_Lambda([this]()
						{
							return ShownDestinationKind == EQuestPlanEndpointKind::DataTable
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						.Text(LOCTEXT("ApplyExportPlan", "Apply Export Plan"))
						.ToolTipText(LOCTEXT("ApplyExportPlanTip", "Write these rows into that Data Table. Re-plans first, so what runs is what you see."))
						.IsEnabled_Lambda([this]()
						{
							return CanApply.Get(false) && PlanDirection == EQuestPlanDirection::IntoTable;
						})
						.OnClicked_Lambda([this]() { OnApplyRequested.ExecuteIfBound(); return FReply::Handled(); })
					]
					// The filter rides the LAST verb row so it stays adjacent to the table it filters. Proportional
					// spacer plus a fixed box: it grows with the panel instead of sitting at one size and stranding
					// whitespace beside it.
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

	if (const FQuestPlanRecord* Existing = FQuestPlanBroker::Get().Find(CurrentTargetPath()))
	{
		HandlePlanPublished(CurrentTargetPath(), Existing->Plan);
	}
}

SQuestPlanPanel::~SQuestPlanPanel()
{
	FQuestPlanBroker::Get().OnPlanPublished().Remove(PublishHandle);
	FQuestPlanBroker::Get().OnPlanCleared().Remove(ClearedHandle);
	FQuestPlanBroker::Get().OnExportCompleted().Remove(ExportHandle);
	FCoreUObjectDelegates::OnObjectModified.Remove(ModifiedHandle);
}

TSharedRef<SWidget> SQuestPlanPanel::MakeEndpointRow(const FQuestEndpointRowArgs& Args)
{
	// A pointer to the panel's member, not a copy: this row drives live state that other controls read back.
	EQuestPlanEndpointKind* Kind = Args.Kind;
	const bool bDest = (Args.Role == EQuestEndpointRole::Destination);
	TSharedPtr<SComboBox<TSharedPtr<FString>>>* ComboSlot = Args.FormatCombo;

	// One definition each, reused by every control that follows the kind - four folder-visible controls and one table.
	auto FolderVisibility = [Kind]() { return *Kind == EQuestPlanEndpointKind::Folder    ? EVisibility::Visible : EVisibility::Collapsed; };
	auto TableVisibility  = [Kind]() { return *Kind == EQuestPlanEndpointKind::DataTable ? EVisibility::Visible : EVisibility::Collapsed; };

	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock).Text(Args.Label).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10.f))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SComboButton)
			.ToolTipText(bDest
				? LOCTEXT("DestKindTip", "Where this questline's data is written - a folder of files, or an in-engine Data Table.")
				: LOCTEXT("SourceKindTip", "Where this questline's data lives - a folder of files, or an in-engine Data Table."))
			.OnGetMenuContent_Lambda([Kind, OnKindChanged = Args.OnKindChanged]()
			{
				FMenuBuilder Menu(true, nullptr);
				auto AddKind = [Kind, &OnKindChanged, &Menu](EQuestPlanEndpointKind NewKind, const FText& Label)
				{
					Menu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Kind, OnKindChanged, NewKind]()
						{
							*Kind = NewKind;
							OnKindChanged.ExecuteIfBound(NewKind);
						})));
				};
				AddKind(EQuestPlanEndpointKind::Folder, LOCTEXT("SourceKindFolder", "Folder"));
				AddKind(EQuestPlanEndpointKind::DataTable, LOCTEXT("SourceKindTable", "Data Table"));
				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				// FIXED width: "Folder" and "Data Table" are different lengths, so an auto-sized combo shifts every
				// control after it on a kind switch. Pinning it means the stable half of the row stays put and only the
				// varying half reflows.
				SNew(SBox).WidthOverride(78.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text_Lambda([Kind]()
					{
						return *Kind == EQuestPlanEndpointKind::DataTable
							? LOCTEXT("SourceKindTable", "Data Table")
							: LOCTEXT("SourceKindFolder", "Folder");
					})
				]
			]
		]

		// SEPARATORS DELIMIT GROUPS, not every control. Kind and Mapping are each their own thing; Format and the
		// location are ONE group, because they appear, vanish and swap together with the kind - so no rule runs between.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(0.0f, 2.0f, 8.0f, 2.0f)
		[
			SNew(SSeparator).Orientation(Orient_Vertical).Thickness(1.0f)
		]

		// MAPPING SECOND, ahead of Format, which inverts the coarse-to-fine reading of those two on purpose: what
		// matters more in a row with a toggle is a STABLE PREFIX and a VARYING SUFFIX. Mapping applies to either
		// provenance and never changes shape, so it belongs with the stable half.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("MappingLabel", "Mapping"))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 2.0f, 0.0f)
		[
			SNew(SBox).MinDesiredWidth(180.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UQuestImportMapping::StaticClass())
				.AllowClear(true)
				.DisplayThumbnail(false)
				.ToolTipText(bDest
					? LOCTEXT("DestMappingTip", "Translation mapping. REQUIRED when writing into a Data Table - it says which of that table's fields this questline's properties belong in.")
					: LOCTEXT("PlanMappingTip", "Optional translation mapping. Leave it empty when the source is already in the plugin's own shape."))
				.ObjectPath_Lambda([Mapping = Args.Mapping]() { return Mapping.Get(FSoftObjectPath()).ToString(); })
				.OnObjectChanged_Lambda([OnMappingChanged = Args.OnMappingChanged](const FAssetData& Asset)
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
			.Visibility_Lambda(FolderVisibility)
			.Text(LOCTEXT("FormatLabel", "Format"))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SAssignNew(*ComboSlot, SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&FormatOptions)
			// COLLAPSED for a table, not greyed. Grey means "unavailable for now"; a Data Table's row struct is not a
			// format you could pick differently, so the control is meaningless rather than unavailable - and the row
			// reflows on a kind switch regardless, so hiding it costs no stability and returns the width to the
			// controls that do apply.
			.Visibility_Lambda(FolderVisibility)
			.ToolTipText(bDest
				? LOCTEXT("DestFormatTip", "Which format provider writes the destination folder.")
				: LOCTEXT("PlanFormatTip", "Which format provider reads the source folder. The list is every provider registered with the plugin, including any your own module adds."))			.OnComboBoxOpening_Lambda([this, ComboSlot]()
			{
				// Refreshed on open: a provider registered after this panel was built would be missing from a list
				// snapshotted at construction, and the panel would silently offer fewer formats than exist.
				RefreshFormatOptions();
				if (ComboSlot->IsValid()) { (*ComboSlot)->RefreshOptions(); }
			})
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
			{
				return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
			})
			.OnSelectionChanged_Lambda([OnFormatChanged = Args.OnFormatChanged](TSharedPtr<FString> Item, ESelectInfo::Type)
			{
				if (Item.IsValid()) { OnFormatChanged.ExecuteIfBound(*Item); }
			})
			[
				SNew(STextBlock).Text_Lambda([FormatName = Args.FormatName]()
				{
					const FString Current = FormatName.Get(FString());
					return Current.IsEmpty() ? LOCTEXT("PlanFormatNone", "Format") : FText::FromString(Current);
				})
			]
		]

		// THE LOCATION - one of two widgets, never both, and sized differently: the path stretches because it is
		// arbitrarily long, the asset picker does not because a name does not get more readable wide.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			// Named like its neighbours, but the only label whose WORD depends on the kind - everything else in this row
			// means the same thing whichever provenance is selected. "Asset" rather than "Table" deliberately: it stays
			// true for any asset-based provenance a third kind might add, and the kind combo already says which one.
			SNew(STextBlock)
			.Text_Lambda([Kind]()
			{
				return *Kind == EQuestPlanEndpointKind::DataTable
					? LOCTEXT("LocationLabelAsset", "Asset")
					: LOCTEXT("LocationLabelPath", "Path");
			})
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SEditableTextBox)
			.Visibility_Lambda(FolderVisibility)
			.HintText(bDest ? LOCTEXT("DestFolderHint", "Path to write into...") : LOCTEXT("SourceFolderHint", "Path to a folder of source data..."))
			.ToolTipText(bDest
				? LOCTEXT("DestFolderTip", "The folder Export writes into. Leave it empty for this questline's default location.")
				: LOCTEXT("SourceFolderTip", "The folder Build Plan will read. Type it or browse; both converge on one write."))
			.Text_Lambda([Folder = Args.Folder]() { return FText::FromString(Folder.Get(FString())); })
			.OnTextCommitted_Lambda([OnFolderChanged = Args.OnFolderChanged](const FText& NewText, ETextCommit::Type)
			{
				OnFolderChanged.ExecuteIfBound(NewText.ToString().TrimStartAndEnd().TrimQuotes());
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton)
			.Visibility_Lambda(FolderVisibility)
			.Text(LOCTEXT("SourceBrowse", "Browse..."))
			.ToolTipText(bDest ? LOCTEXT("DestBrowseTip", "Pick the folder Export writes into.") : LOCTEXT("SourceBrowseTip", "Pick the folder Build Plan will read."))			.OnClicked_Lambda([OnBrowseRequested = Args.OnBrowseRequested]() { OnBrowseRequested.ExecuteIfBound(); return FReply::Handled(); })
		]
		// AutoWidth, unlike the path beside it: an asset reference is a NAME and reads fine at a fixed size, while a
		// path is arbitrarily long and benefits from every pixel. The two never show together, so they can be sized to
		// what each actually needs rather than sharing one rule.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SBox).MinDesiredWidth(240.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.Visibility_Lambda(TableVisibility)
				.AllowedClass(UDataTable::StaticClass())
				.AllowClear(true)
				.DisplayThumbnail(false)
				.ToolTipText(bDest
					? LOCTEXT("DestTableTip", "The Data Table this questline's rows are written into. Its row struct decides the columns, so no format is needed.")
					: LOCTEXT("SourceTableTip", "The Data Table Build Plan will read. Its row struct supplies the columns, so no format is needed."))				.ObjectPath_Lambda([Table = Args.Table]() { return Table.Get(FSoftObjectPath()).ToString(); })
				.OnObjectChanged_Lambda([OnTableChanged = Args.OnTableChanged](const FAssetData& Asset)
				{
					OnTableChanged.ExecuteIfBound(Asset.ToSoftObjectPath());
				})
			]
		];
}

void SQuestPlanPanel::RefreshFormatOptions()
{
	FormatOptions.Reset();
	for (const FString& Name : FQuestDataFormatRegistry::Get().GetRegisteredNames())
	{
		FormatOptions.Add(MakeShared<FString>(Name));
	}
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

void SQuestPlanPanel::HandlePlanCleared(const FString& InAssetPath)
{
	if (InAssetPath != CurrentTargetPath()) { return; }

	// Everything a plan put on screen leaves together. Clearing the flags while leaving the rows would render work that
	// has already happened underneath a header saying there is no plan - which is the state this exists to prevent.
	Rows.Reset();
	if (Table.IsValid()) { Table->SetRootItems(Rows); }

	Summary = FText::GetEmpty();
	Blockers = FText::GetEmpty();
	bBlocking = false;
	LastError.Empty();
	LastFailedFormat.Empty();
	bHasPlan = false;
	bStale = false;
	bTableStale = false;

	// The export receipt SURVIVES: it reports the action that just happened, which is the one thing still true here.
}

void SQuestPlanPanel::HandlePlanPublished(const FString& InAssetPath, const FQuestInPlacePlan& Plan)
{
	if (InAssetPath != CurrentTargetPath()) { return; }

	// A new plan supersedes the last export receipt: it was a statement about an action two actions ago, and leaving it
	// beside fresh rows invites reading it as commentary on them.
	LastExportSummary.Empty();
	LastExportError.Empty();

	bStale = false;   // a fresh publish is by definition current
	bTableStale = false;
	if (const FQuestPlanRecord* Rec = FQuestPlanBroker::Get().Find(InAssetPath))
	{
		LastError = Rec->Error;
		LastFailedFormat = Rec->Provenance.FormatName;
	}
	
	// A FAILURE also arrives here - PublishFailure broadcasts through the same delegate - and it did not produce a
	// plan. Setting bHasPlan unconditionally would leave the panel rendering the previous plan's rows underneath a
	// message saying the source could not be read.
	bHasPlan = LastError.IsEmpty();
	PlanDirection = Plan.Direction;

	if (!LastError.IsEmpty())
	{
		// A failure produced no plan, so there is nothing to summarize and no refusals to report. Both are derived from
		// the STALE plan PublishFailure preserved, and rendering either would attach the previous run's blockers to
		// this run's error message.
		Summary = FText::GetEmpty();
		Blockers = FText::GetEmpty();
		bBlocking = false;
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

	// TWO GROUPS, TWO HEADINGS. A refusal stops the apply and a warning does not, so collecting them into one list under
	// "cannot be applied until these are resolved" tells a designer to go fix something that was never blocking anything.
	// The per-line REFUSED:/warning: prefixes already carried the distinction; only the heading flattened it.
	const bool bAnyBlocking = !Plan.Refusals.IsEmpty() || !Plan.AmbiguousKeys.IsEmpty();

	TArray<FString> Lines;
	if (bAnyBlocking)
	{
		Lines.Add(TEXT("This plan cannot be applied until these are resolved. Nothing would be written."));
		for (const FString& R : Plan.Refusals)      { Lines.Add(FString::Printf(TEXT("REFUSED: %s"), *R)); }
		for (const FString& K : Plan.AmbiguousKeys) { Lines.Add(FString::Printf(TEXT("CONTESTED KEY: %s"), *K)); }
	}
	if (!Plan.Warnings.IsEmpty())
	{
		if (bAnyBlocking) { Lines.Add(FString()); }   // a blank line, so the two groups cannot read as one list
		Lines.Add(TEXT("These do not stop the apply:"));
		for (const FString& W : Plan.Warnings) { Lines.Add(FString::Printf(TEXT("warning: %s"), *W)); }
	}

	Blockers = FText::FromString(FString::Join(Lines, TEXT("\n")));
	bBlocking = bAnyBlocking;
	
	RebuildRows(Plan);
}

void SQuestPlanPanel::HandleExportCompleted(const FString& InAssetPath, const FString& InSummary, const FString& Error)
{
	if (InAssetPath != CurrentTargetPath()) { return; }
	LastExportSummary = InSummary;
	LastExportError = Error;
}

void SQuestPlanPanel::HandleObjectModified(UObject* Modified)
{
	if (!bHasPlan || !Modified) { return; }

	// The questline itself, or anything living inside it - a node, an inner graph, an instanced reward.
	if (!bStale && Questline.IsValid() && (Modified == Questline.Get() || Modified->IsIn(Questline.Get())))
	{
		bStale = true;
	}

	// EITHER TABLE this panel names. An inbound plan READS one and an outbound plan WRITES the other, and a plan stops
	// being true the moment the table it concerns is touched - which nothing watched for until now. Row edits reach
	// here because FDataTableEditorUtils goes through UDataTable::Modify(), which broadcasts this delegate.
	if (!bTableStale)
	{
		const UObject* Src  = SourceTable.Get(FSoftObjectPath()).ResolveObject();
		const UObject* Dest = DestinationTable.Get(FSoftObjectPath()).ResolveObject();
		if ((Src && Modified == Src) || (Dest && Modified == Dest)) { bTableStale = true; }
	}
}

void SQuestPlanPanel::HandleRowActivated(FQuestPlanRowPtr Row)
{
	// Silently ignored for a Create, an orphan-less wiring row, or anything else with no live node — the row is not
	// broken, it simply describes something that does not exist in the graph yet. A message here would be noise on a
	// gesture the user may not even have aimed at this row.
	if (!Row.IsValid() || Row->NodeGuid.IsEmpty()) { return; }
	OnNavigateRequested.ExecuteIfBound(Row->NodeGuid);
}

void SQuestPlanPanel::HandleRowHovered(FQuestPlanRowPtr Row, bool bHovered)
{
	const FString Guid = (Row.IsValid() && bHovered) ? Row->NodeGuid : FString();

	if (bHovered)
	{
		// An empty guid is a real answer, not a no-op: hovering a Create or the wiring row means "nothing to show", and
		// leaving the previous halo lit would point at a node the cursor is no longer anywhere near.
		HoveredNodeGuid = Guid;
	}
	else
	{
		// Only OUR row may clear. Leave(old) can arrive after Enter(new), and an unconditional clear there wipes the
		// halo that was just set - the bug this guard exists for, regardless of what order Slate happens to use today.
		if (!Row.IsValid() || Row->NodeGuid != HoveredNodeGuid) { return; }
		HoveredNodeGuid.Empty();
	}

	OnHoverRequested.ExecuteIfBound(HoveredNodeGuid);
}

EVisibility SQuestPlanPanel::GetStaleVisibility() const
{
	// Three independent reasons a plan stops being true: the ASSET moved under it, the DATA it concerns moved, or the
	// SELECTION moved away from what it was built against. Any one makes what is on screen a statement about a state
	// that no longer exists.
	return (bStale || bTableStale || ProvenanceStale.Get(false)) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SQuestPlanPanel::GetStaleText() const
{
	// Named separately because the FIX differs. One sentence covering all three would send two thirds of readers
	// looking in the wrong place.
	if (bStale)
	{
		return LOCTEXT("PlanStaleAsset", "This questline has changed since this plan was computed. Build the plan again to refresh it.");
	}
	if (bTableStale)
	{
		return PlanDirection == EQuestPlanDirection::IntoTable
			? LOCTEXT("PlanStaleDest", "The data table this plan writes into has changed since the plan was computed. Build the plan again to see what would happen now.")
			: LOCTEXT("PlanStaleSrc",  "The data table this plan was read from has changed since the plan was computed. Build the plan again to refresh it.");
	}
	
	// NOT "the source above has changed". The selection and the plan also disagree when the plan was published by
	// ANOTHER SURFACE whose source this panel never adopted - which a console-built plan does every time. Nothing
	// changed; they simply differ, and saying otherwise sends someone hunting for an edit they never made.
	// Named by ROW because there are two of them now - "above" was unambiguous when there was one.
	return PlanDirection == EQuestPlanDirection::IntoTable
		? LOCTEXT("PlanStaleDestSel", "The Destination selection no longer matches what this plan was built from. Build the plan again to use it.")
		: LOCTEXT("PlanStaleSrcSel",  "The Source selection no longer matches what this plan was built from. Build the plan again to use it.");
}

void SQuestPlanPanel::RebuildRows(const FQuestInPlacePlan& Plan)
{
	// A row destroyed while hovered never sends its Leave, so the halo would stay lit over a node no row still names.
	if (!HoveredNodeGuid.IsEmpty())
	{
		HoveredNodeGuid.Empty();
		OnHoverRequested.ExecuteIfBound(HoveredNodeGuid);
	}
	
	// STreeView keys expansion by ITEM POINTER, and this used to mint a fresh row object for every entry - so every
	// re-plan collapsed the whole tree, which punishes exactly the edit-plan-edit loop the panel exists to serve.
	// Carrying the POINTER forward for a node that was already listed keeps its expansion with the tree needing to know
	// nothing about it, and without a read-back accessor the shared table view does not expose.
	TMap<FString, FQuestPlanRowPtr> Previous;
	for (const FQuestPlanRowPtr& Row : Rows)
	{
		if (Row.IsValid() && !Row->Id.IsEmpty()) { Previous.Add(Row->Id, Row); }
	}

	Rows.Reset();

	for (const FQuestNodePlanEntry& Entry : Plan.Entries)
	{
		// An unchanged match is the common case on a healthy re-import. Listing them would bury the ones that matter,
		// which is the same judgement the log makes - the two renderings agree deliberately.
		if (Entry.Action == EQuestNodePlanAction::Update && Entry.Changes.IsEmpty() && !Entry.bMoved) { continue; }

		// Reused when this node was in the previous plan, fresh otherwise. Children are rebuilt either way: a change row
		// has no children of its own, so there is no deeper expansion to preserve.
		FQuestPlanRowPtr* Existing = Previous.Find(Entry.Key);
		FQuestPlanRowPtr Node = Existing ? *Existing : MakeShared<FQuestPlanRow>();
		Node->Children.Reset();

		Node->Id          = Entry.Key;
		Node->NodeGuid    = Entry.Guid;
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
			// A change belongs to the node above it, so double-clicking one goes to the same place. The row it sits under
			// is a heading, not a different destination.
			Row->NodeGuid = Node->NodeGuid;
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
		// The one row with no entry behind it, so it gets a fixed id - it is the same row conceptually on every plan,
		// and a designer who expanded the wiring once should not have to keep re-expanding it.
		static const FString WiringId = TEXT("__connections__");
		FQuestPlanRowPtr* ExistingWiring = Previous.Find(WiringId);
		FQuestPlanRowPtr Wiring = ExistingWiring ? *ExistingWiring : MakeShared<FQuestPlanRow>();
		Wiring->Children.Reset();

		Wiring->Id     = WiringId;
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
	// Shown ahead of everything because it describes the most recent action. Cleared by the next Build Plan, which is
	// what keeps a receipt from reading as a statement about rows that arrived after it.
	if (!LastExportSummary.IsEmpty() && Rows.IsEmpty())
	{
		return FText::FromString(LastExportSummary);
	}
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
		// The status line above already states that no plan exists; this says what to DO about it.
		return LOCTEXT("NoPlanYet", "Choose a source above, then press Build Plan.");
	}
	// A REFUSED plan is not a matching one. Falling through to "already matches its source" with blockers on screen tells
	// a designer their data is fine while the reason it is not sits directly above - and an empty table is exactly the
	// state where that happens, because a refused row produces no row to list.
	if (bBlocking)
	{
		// The rows below are worth showing - they are what a designer gets once the blockers are cleared, and hiding them
		// would leave a refusal above an empty table with no way to see what the plan holds. What must NOT read as fact is
		// the COUNT: none of it happens while a blocker stands, and the plain summary states it as though it will.
		return Rows.IsEmpty()
			? LOCTEXT("PlanIsBlocked", "Nothing else to show — the blockers above are the whole plan.")
			: FText::Format(LOCTEXT("PlanIsBlockedWithRows", "Blocked — none of this runs until the blockers above are "
				"resolved. It would be: {0}"), Summary);
	}

	// A plan that exists and finds nothing is a DIFFERENT fact from no plan at all, and only one of them means the
	// asset matches its source. Saying so is what stops an empty table reading as a broken panel.
	return Rows.IsEmpty()
		? LOCTEXT("PlanIsClean", "This questline already matches its source — a re-import would change nothing.")
		: Summary;
}

FText SQuestPlanPanel::GetBlockersText() const
{
	// An export refusal outranks the plan's blockers: it describes the action just taken, and its messages are three
	// paragraphs of what-to-do that must not be scrollable-past. The plan's blockers are still there underneath once
	// the next Build Plan clears this.
	return LastExportError.IsEmpty() ? Blockers : FText::FromString(LastExportError);
}

EMessageStyle SQuestPlanPanel::GetBlockersStyle() const
{
	// An export ERROR is a failure whatever the plan holds, and it outranks here for the same reason it outranks in
	// GetBlockersText: it describes the action just taken.
	if (!LastExportError.IsEmpty()) { return EMessageStyle::Error; }

	// Warnings are informational. A plan that carries only warnings is APPLYABLE, and painting it the same red as one
	// that cannot run teaches people to dismiss the bar - at which point the refusals stop being read either.
	return bBlocking ? EMessageStyle::Error : EMessageStyle::Warning;
}

EVisibility SQuestPlanPanel::GetBlockersVisibility() const
{
	return (Blockers.IsEmpty() && LastExportError.IsEmpty()) ? EVisibility::Collapsed : EVisibility::Visible;
}

#undef LOCTEXT_NAMESPACE

