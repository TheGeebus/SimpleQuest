// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// A sortable, filterable, resizable table - the mechanics every list-shaped editor panel needs and none of them should
// reimplement. What a row MEANS is the caller's business, but how it sorts, filters, copies and remembers its state is not.
//
// Backed by STreeView, which derives from SListView: supply a children accessor and rows nest, omit it and the table is
// flat. One code path serves both, so a panel can gain hierarchy later without changing widget.
//
// ItemType is expected to be a shared pointer (the STreeView convention) - it is used as a map key while filtering.

#include "CoreMinimal.h"
#include "Animation/CurveSequence.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SimpleCoreEditorWidgetUtils.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"

/**
 * One column. The split between GetText and MakeCell is the point of the whole type: GetText is the row's VALUE in this
 * column and drives filtering, sorting and clipboard copy; MakeCell is only its APPEARANCE. A read-only table supplies
 * the first and gets the rest for free. An editable one supplies both, and its pickers remain searchable and sortable by
 * the values they hold - which hand-rolled tables consistently lose, because nothing connects a combo box to a filter.
 */
template<typename ItemType>
struct FTableColumnDef
{
	FName  Id;
	FText  Label;
	float  FillWidth = 1.0f;
	
	/**
	 * Set above zero for a column that should neither stretch nor be dragged - an icon gutter, a status dot, a fixed
	 * button. It wins over FillWidth, and the column is then neither resizable nor persisted, because there is nothing
	 * to remember. Pair it with bSortable = false and an empty Label; a gutter carrying a sort arrow on a blank header
	 * reads as a bug rather than a control.
	 */
	float  FixedWidth = 0.0f;
	
	bool   bSortable = true;

	/**
	 * Alignment for the header and for the default text cell. Numeric columns read far better right-aligned, and a
	 * column that cannot say so forces every caller with a number to supply a MakeCell purely to move some text.
	 */
	EHorizontalAlignment HeaderAlignment = HAlign_Left;
	EHorizontalAlignment CellAlignment   = HAlign_Left;

	/** The row's value here, as text. Required: filtering, sorting and copy all read it. */
	TFunction<FText(const ItemType&)> GetText;

	/** Optional ordering. Absent compares GetText, which is right for names and wrong for numbers and dates. */
	TFunction<bool(const ItemType&, const ItemType&)> Less;

	/** Optional cell widget. Absent renders GetText as a text block with filter-match highlighting. */
	TFunction<TSharedRef<SWidget>(const ItemType&)> MakeCell;

	/**
	 * Optional per-row styling for the DEFAULT text cell. Exists so a caller can distinguish row kinds - a parent from
	 * its children, a warning from a value - without reaching for MakeCell, which would silently cost that column its
	 * filter-match highlighting. Ignored when MakeCell is supplied, since a custom cell owns its presentation entirely.
	 */
	TFunction<FSlateColor(const ItemType&)> GetTextColor;
	TFunction<FSlateFontInfo(const ItemType&)> GetFont;
};

/** One row. Generates cells from the column definitions. Nothing here knows what the data is. */
template<typename ItemType>
class SColumnTableRow : public SMultiColumnTableRow<ItemType>
{
public:
	/** Per-row clipboard actions. Declared here because the row raises them and the table forwards them. */
	DECLARE_DELEGATE_OneParam(FOnRowAction, ItemType);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FCanRowAction, ItemType);
		
	/**
	 * Paste reports whether it actually CHANGED anything. A refusal must be distinguishable from a chord that never
	 * registered, so the handler runs unconditionally, states its own reason, and answers false when it declined.
	 */
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnRowPaste, ItemType);

	SLATE_BEGIN_ARGS(SColumnTableRow<ItemType>) {}
		SLATE_ARGUMENT(ItemType, Item)
		SLATE_ARGUMENT(const TArray<FTableColumnDef<ItemType>>*, Columns)
		SLATE_ATTRIBUTE(FText, HighlightText)
		SLATE_EVENT(FOnRowAction, OnCopyRow)
		SLATE_EVENT(FOnRowPaste, OnPasteRow)
		SLATE_EVENT(FCanRowAction, CanPasteRow)

		/** Raised on an unshifted right-click, just before the base opens the menu, so the table knows what it is about. */
		SLATE_EVENT(FOnRowAction, OnRowRightClicked)

		/**
		 * Which column carries the expander. SMultiColumnTableRow deliberately drops the expander its own base class
		 * builds - "MultiColumnRows let the user decide which column should contain the expander/indenter item" - so a
		 * multi-column tree has no arrow and no indentation until a column claims it. None leaves the row flat.
		 */
		SLATE_ARGUMENT(FName, ExpanderColumnId)
		SLATE_ARGUMENT(bool, bShowExpanderWires)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
	{
		Item = InArgs._Item;
		Columns = InArgs._Columns;
		HighlightText = InArgs._HighlightText;
		OnCopyRow = InArgs._OnCopyRow;
		OnPasteRow = InArgs._OnPasteRow;
		CanPasteRow = InArgs._CanPasteRow;
		OnRowRightClicked = InArgs._OnRowRightClicked;
		ExpanderColumnId = InArgs._ExpanderColumnId;
		bShowExpanderWires = InArgs._bShowExpanderWires;
		// A copy changes nothing on screen, so the row flashes to say it happened - without it the chord is invisible.
		Pulse.AddCurve(0.0f, 0.35f);
		SMultiColumnTableRow<ItemType>::Construct(typename SMultiColumnTableRow<ItemType>::FArguments(), InOwner);
	}

	/**
	 * Shift+RMB copies this row, Shift+LMB pastes onto it - the details panel's own gesture, and the reason it lives on
	 * the ROW is that the row under the cursor is the row being acted on, with nothing to look up. Returning Handled on
	 * the right-click is what stops the context menu opening on top of a copy. Unshifted clicks fall through to the base,
	 * which still selects and still opens the menu.
	 */
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetModifierKeys().IsShiftDown() && Item.IsValid())
		{
			if (OnCopyRow.IsBound() && MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
			{
				OnCopyRow.Execute(Item);
				Pulse.Play(this->AsShared());
				return FReply::Handled();
			}
			if (OnPasteRow.IsBound() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				/**
				 * Deliberately NOT gated on CanPasteRow. Gating here made a refusal silent - the handler never ran, so
				 * nothing could say why - and silence reads as a chord that failed to register. The handler decides,
				 * states its reason, and answers whether it changed anything; only then is there something to flash.
				 * CanPasteRow still greys the menu entry, where the greying is itself the explanation.
				 */
				if (OnPasteRow.Execute(Item))
				{
					Pulse.Play(this->AsShared());
				}
				return FReply::Handled();
			}
		}
		/**
		 * Tell the table which row a menu is about to be built for. Selection cannot answer that once selection is off,
		 * and under Multi it answers a different question entirely - "a member of the selection" is not "the row you
		 * clicked". This is a hand-off valid only for the menu that opens next, not a stored selection.
		 */
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && Item.IsValid())
		{
			OnRowRightClicked.ExecuteIfBound(Item);
		}
		return SMultiColumnTableRow<ItemType>::OnMouseButtonUp(MyGeometry, MouseEvent);
	}

	/**
	 * The engine gates the HOVERED brush behind selectability - STableRow::GetBorder tests
	 * GetSelectionMode() != None - so a table that turns selection off silently loses the cursor-follows highlight too.
	 * That highlight is a readability cue, not a selection cue, and there is no reason for the two to be linked. Put it
	 * back for exactly that case and defer to the base everywhere else, so a selectable table is untouched.
	 */
	virtual const FSlateBrush* GetBorder() const override
	{
		if (this->GetSelectionMode() == ESelectionMode::None && this->IsHovered() && this->Style)
		{
			return (this->IndexInList % 2 == 0)
				? &this->Style->EvenRowBackgroundHoveredBrush
				: &this->Style->OddRowBackgroundHoveredBrush;
		}
		return SMultiColumnTableRow<ItemType>::GetBorder();
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnId) override
	{
		const FTableColumnDef<ItemType>* Column = Columns
			? Columns->FindByPredicate([&ColumnId](const FTableColumnDef<ItemType>& C){ return C.Id == ColumnId; })
			: nullptr;
		if (!Column || !Item.IsValid()) { return SNullWidget::NullWidget; }

		// A custom cell owns its presentation entirely, including whether it indicates a filter match. Only the default
		// text cell highlights, because only it knows the text IS the content.
		TSharedRef<SWidget> Inner = Column->MakeCell
			? Column->MakeCell(Item)
			: StaticCastSharedRef<SWidget>(
				SNew(SBox).VAlign(VAlign_Center).HAlign(Column->CellAlignment).Padding(FMargin(4.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(Column->GetText ? Column->GetText(Item) : FText::GetEmpty())
					.HighlightText(HighlightText)
					.ColorAndOpacity(Column->GetTextColor ? Column->GetTextColor(Item) : FSlateColor::UseForeground())
					.Font(Column->GetFont ? Column->GetFont(Item) : FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]);

		// The expander goes INSIDE the tinted border so the stripe runs unbroken across the row, and it supplies the depth
		// indent itself from the row's GetIndentLevel - an arrow and separate padding would be two things to keep in step.
		TSharedRef<SWidget> Cell = Inner;
		if (!ExpanderColumnId.IsNone() && ColumnId == ExpanderColumnId)
		{
			Cell = SNew(SHorizontalBox)
				// NO VAlign - the slot must stay VAlign_Fill (the default). SExpanderArrow computes every wire length from
				// AllottedGeometry.Size.Y, so centring it shrinks the geometry to the arrow's own height and the wires are
				// drawn against the wrong extent. The engine's STableRow and both hand-rolled trees in SimpleQuest
				// (the Questline Outliner and the Group Examiner) all leave this alone; matching them is the fix.
				+ SHorizontalBox::Slot().AutoWidth()
				[
					// this-> is load-bearing: SMultiColumnTableRow<ItemType> is a dependent base, so unqualified lookup
					// cannot see SharedThis at template definition time. The engine's own STableRow omits it because it
					// declares the member itself; a subclass template does not have that luxury.
					SNew(SExpanderArrow, this->SharedThis(this)).ShouldDrawWires(bShowExpanderWires)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					Inner
				];
		}

		// Zebra tint per cell, with the colour bound as a DELEGATE rather than a value: the attribute must evaluate at
		// paint time, by which point IndexInList has been populated. See FSimpleCoreEditorWidgetUtils.
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor(this, &SColumnTableRow::GetStripeColor)
			.Padding(0.0f)
			[
				Cell
			];
	}

private:
	FSlateColor GetStripeColor() const
	{
		const FSlateColor Base = FSimpleCoreEditorWidgetUtils::GetTableRowStripeColor(this->IndexInList);
		if (!Pulse.IsPlaying()) { return Base; }
		// Starts at the flash colour and settles back to the stripe, so the row reads as "that did something" and then
		// stops drawing attention. The border is already delegate-bound for the stripe, so this costs no extra widget.
		return FSlateColor(FMath::Lerp(FLinearColor(0.35f, 0.55f, 0.95f, 0.35f), Base.GetSpecifiedColor(), Pulse.GetLerp()));
	}

	ItemType Item;
	const TArray<FTableColumnDef<ItemType>>* Columns = nullptr;
	TAttribute<FText> HighlightText;
	FOnRowAction OnCopyRow;
	FOnRowPaste OnPasteRow;
	FCanRowAction CanPasteRow;
	FOnRowAction OnRowRightClicked;
	FName ExpanderColumnId;
	bool bShowExpanderWires = false;
	mutable FCurveSequence Pulse;
};

template<typename ItemType>
class SColumnTableView : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_TwoParams(FOnGetChildren, ItemType /*Parent*/, TArray<ItemType>& /*OutChildren*/);
	DECLARE_DELEGATE_OneParam(FOnItemSelected, ItemType);

	using FOnRowAction			= typename SColumnTableRow<ItemType>::FOnRowAction;
	using FCanRowAction			= typename SColumnTableRow<ItemType>::FCanRowAction;
	using FOnRowPaste			= typename SColumnTableRow<ItemType>::FOnRowPaste;
	using FOnExpansionChanged	= typename TSlateDelegates<ItemType>::FOnExpansionChanged;

	SLATE_BEGIN_ARGS(SColumnTableView<ItemType>)
		: _SelectionMode(ESelectionMode::Single)
		, _bShowSearchBox(true)
		, _AllowClipboardCopy(true)
		, _bDistinctHeader(false)
		, _bIndentUnderTitle(false)
	{}
		SLATE_ARGUMENT(TArray<FTableColumnDef<ItemType>>, Columns)

		/** Supply to make the table hierarchical. Omit for a flat list. */
		SLATE_EVENT(FOnGetChildren, OnGetChildren)

		/**
		 * Which column carries the expander arrow and the depth indent. Defaults to the first column that is not a fixed
		 * gutter, because an arrow in an icon-width column clips as soon as the tree is more than a level or two deep.
		 * Ignored entirely by a flat table.
		 */
		SLATE_ARGUMENT(FName, ExpanderColumnId)
		SLATE_ARGUMENT(bool, bShowExpanderWires)

		/** Fires when a row is expanded or collapsed, so a consumer can persist expansion or load children lazily. */
		SLATE_EVENT(FOnExpansionChanged, OnExpansionChanged)

		/**
		 * Multi suits a table you read data OUT of. Single suits one you EDIT, where a stale selection can be acted on
		 * without noticing.
		 */
		SLATE_ARGUMENT(ESelectionMode::Type, SelectionMode)

		/**
		 * Stable key for persisting sort column, direction and column widths. Scope it to what the user thinks they are
		 * returning to - an asset's path, not a widget instance. Empty disables persistence.
		 */
		SLATE_ARGUMENT(FString, PersistenceKey)

		SLATE_ARGUMENT(FText, FilterHintText)

		/**
		 * False suppresses the built-in search box and gives the Toolbar slot the whole band, so a consumer can place its
		 * own SSearchBox wherever it belongs and wire it to SetFilterText.
		 * WHY THIS EXISTS: the built-in box is a SIBLING of the Toolbar slot, so nothing a consumer puts in that slot can
		 * share a horizontal box with it. That is fine for a one-row toolbar and wrong for a multi-row one, where the
		 * search ends up spanning every row and belonging to none - and every workaround (vertical alignment, fill
		 * fractions, capping whatever inflates the toolbar) treats the symptom.
		 */
		SLATE_ARGUMENT(bool, bShowSearchBox)
		
		/**
		 * Optional heading. Supply it when the table sits among other content that names itself - a details panel row is
		 * the case that needs it, since a whole-row widget has no name column and is otherwise the only unlabelled thing
		 * on the page. Omit it for a table under its own tab or category header, where a second title just repeats.
		 * Use the SAME words the host filters this row by, or searching the title a user can see will hide the table.
		 */
		SLATE_ARGUMENT(FText, Title)

		/**
		 * Optional second line under the title, dimmed. For the fact a reader needs alongside the heading but that is not
		 * the heading - which source a view is reading, which session it is pinned to. An ATTRIBUTE rather than an
		 * argument because that fact usually changes while the view is open, unlike the title.
		 */
		SLATE_ATTRIBUTE(FText, Subtitle)

		SLATE_ARGUMENT(bool, AllowClipboardCopy)
	
		/**
		 * Set when the table is embedded in a details panel row. Such a row paints Colors.Panel normally and Colors.Header
		 * on hover (PropertyEditorConstants::GetRowBackgroundColor), and the stock table header IS Colors.Header - so the
		 * header is invisible for exactly as long as the cursor is over the table. This steps the header past both.
		 * Leave it off for a table under its own tab, where nothing alternates behind it.
		 */
		SLATE_ARGUMENT(bool, bDistinctHeader)
		
		/**
		 * Indent everything below the title. Opt-in and NOT inferred from Title being set, because those are different
		 * questions: a title says "this table needs naming", an indent says "this table is nested inside a host that
		 * already has its own left edge". A details panel wants both; a docked tab wants the title alone.
		 */
		SLATE_ARGUMENT(bool, bIndentUnderTitle)

		/** Extra controls beside the search box: a filter menu, a bulk action. Keeps table-specific vocabulary out of here. */
		SLATE_NAMED_SLOT(FArguments, Toolbar)

		/**
		* Shown in place of the rows when there are none. A blank table should always say WHY it is blank - "no facts
		* recorded yet" and "your filter matched nothing" are different situations and a user can act on the difference.
		*/
		SLATE_NAMED_SLOT(FArguments, EmptyState)

		SLATE_EVENT(FOnItemSelected, OnItemSelected)
		SLATE_EVENT(FOnContextMenuOpening, OnContextMenuOpening)

		/**
		 * Per-row settings copy/paste, reached by Shift+RMB / Shift+LMB and by an Edit section in the default menu. This
		 * is a DIFFERENT action from copying the table's contents, and the two never share a gesture or a menu section.
		 * Bind both or neither; a consumer supplying its own OnContextMenuOpening keeps the chords but loses the entries.
		 */
		SLATE_EVENT(FOnRowAction, OnCopyRow)
		SLATE_EVENT(FOnRowPaste, OnPasteRow)
		SLATE_EVENT(FCanRowAction, CanPasteRow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Columns				= InArgs._Columns;
		OnGetChildren		= InArgs._OnGetChildren;
		PersistenceKey		= InArgs._PersistenceKey;
		bAllowCopy			= InArgs._AllowClipboardCopy;
		OnItemSelected		= InArgs._OnItemSelected;
		OnCopyRow			= InArgs._OnCopyRow;
		OnPasteRow			= InArgs._OnPasteRow;
		CanPasteRow			= InArgs._CanPasteRow;
		OnGetChildren		= InArgs._OnGetChildren;
		bShowExpanderWires	= InArgs._bShowExpanderWires;
		// Resolved once here rather than per row. A caller that named a column gets it; otherwise the first column wide
		// enough to hold an arrow. Left None for a flat table so no row pays for a branch it can never take.
		ExpanderColumnId = InArgs._ExpanderColumnId;
		if (ExpanderColumnId.IsNone() && InArgs._OnGetChildren.IsBound())
		{
			const FTableColumnDef<ItemType>* Host = InArgs._Columns.FindByPredicate(
				[](const FTableColumnDef<ItemType>& C){ return C.FixedWidth <= 0.0f; });
			ExpanderColumnId = Host ? Host->Id : (InArgs._Columns.Num() > 0 ? InArgs._Columns[0].Id : FName());
		}

		LoadPersistedState();

		TSharedRef<SHeaderRow> Header = InArgs._bDistinctHeader
			? SNew(SHeaderRow).Style(&GetDistinctHeaderStyle())
			: SNew(SHeaderRow);
		for (const FTableColumnDef<ItemType>& Column : Columns)
		{
			const FName Id = Column.Id;
			SHeaderRow::FColumn::FArguments Args = SHeaderRow::Column(Id);
			Args.DefaultLabel(Column.Label);
			Args.HAlignHeader(Column.HeaderAlignment);

			if (Column.FixedWidth > 0.0f)
			{
				// A fixed column cannot be dragged, so it wants neither the width attribute nor a change notification -
				// and leaving OnWidthChanged unbound is precisely what keeps it fixed rather than controlled.
				Args.FixedWidth(Column.FixedWidth);
			}
			else
			{
				/**
				 * Bound as a DELEGATE, never a value. FColumn::SetWidth only writes its own Width when OnWidthChanged is
				 * UNBOUND - binding the notification makes the column controlled, so the width has to come back from us or
				 * dragging does nothing at all. Feeding it from SavedWidths is what makes a drag move the column AND persist,
				 * out of one mechanism rather than two.
				 */
				Args.FillWidth(TAttribute<float>::Create(
					TAttribute<float>::FGetter::CreateSP(this, &SColumnTableView::GetColumnFillWidth, Id)));
				Args.OnWidthChanged(FOnWidthChanged::CreateSP(this, &SColumnTableView::OnColumnWidthChanged, Id));
			}

			if (Column.bSortable)
			{
				Args.SortMode(this, &SColumnTableView::GetSortModeForColumn, Id);
				Args.OnSort(this, &SColumnTableView::OnSortColumn);
			}
			Header->AddColumn(Args);
		}
		
		// Built imperatively because the toolbar's SIZING depends on whether the built-in search is present, and that
		// cannot be expressed in the declarative chain: AutoWidth() and FillWidth() each return a reference into a
		// per-branch temporary, so a ternary over them is fragile. AddSlot is the engine's own idiom for a slot that
		// only sometimes exists.
		TSharedRef<SHorizontalBox> ToolbarBand = SNew(SHorizontalBox);
		if (InArgs._bShowSearchBox)
		{
			// Toolbar leads and the search fills the remainder, matching the existing facts views - where that order is
			// load-bearing, because their search boxes are aligned to land on identical y across views.
			ToolbarBand->AddSlot().AutoWidth()
			[
				InArgs._Toolbar.Widget
			];
			ToolbarBand->AddSlot().FillWidth(1.0f).Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
			[
				// The table brings its own search because it cannot borrow one: a details panel's filter never reaches
				// inside an embedded widget, and a dockable tab has none to borrow.
				SNew(SSearchBox)
				.HintText(InArgs._FilterHintText.IsEmpty()
					? NSLOCTEXT("SimpleCore", "TableFilterHint", "Search...")
					: InArgs._FilterHintText)
				.OnTextChanged(this, &SColumnTableView::OnFilterTextChanged)
			];
		}
		else
		{
			// The consumer owns the search and places it in its own layout, so the toolbar takes the whole band - it
			// needs real width to distribute, which an AutoWidth slot would never give it.
			ToolbarBand->AddSlot().FillWidth(1.0f)
			[
				InArgs._Toolbar.Widget
			];
		}

		ChildSlot
		[
			SNew(SVerticalBox)
			// Collapsed outright when no title is given, so the option existing costs nothing - no stray padding above a
			// table that already sits under a heading.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder)
				.Padding(FMargin(20.f, 4.f, 0.f, 4.f))
				.BorderBackgroundColor(FLinearColor::Transparent)
				[
					SNew(STextBlock)
					.Text(InArgs._Title)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.Visibility(InArgs._Title.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
			[
				SNew(STextBlock)
				.Text(InArgs._Subtitle)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				// Bound, not sampled: the subtitle tracks state that changes while the view is open, so the collapse has
				// to be re-evaluated rather than decided once the way the title's is.
				.Visibility_Lambda([Sub = InArgs._Subtitle]()
				{
					return Sub.Get(FText::GetEmpty()).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
				})
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(FMargin(InArgs._bIndentUnderTitle ? 12.f : 0.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
				[
					ToolbarBand
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot().Padding(FMargin(0.0f, 0.0f, 0.0f, 0.0f))
					[
						SAssignNew(TreeView, STreeView<ItemType>)
						.TreeItemsSource(&VisibleRoots)
						.HeaderRow(Header)
						.SelectionMode(InArgs._SelectionMode)
						.OnGenerateRow(this, &SColumnTableView::GenerateRow)
						.OnGetChildren(this, &SColumnTableView::GetVisibleChildren)
						.OnExpansionChanged(InArgs._OnExpansionChanged)
						.OnSelectionChanged(this, &SColumnTableView::HandleSelectionChanged)
						.OnContextMenuOpening(InArgs._OnContextMenuOpening.IsBound()
							? InArgs._OnContextMenuOpening
							: FOnContextMenuOpening::CreateSP(this, &SColumnTableView::MakeDefaultContextMenu))
					]
					+ SOverlay::Slot()
					.Padding(TAttribute<FMargin>::CreateLambda([Header]()
					{
						// STableViewBase puts the header INSIDE the tree, above the list, so this overlay covers it too.
						// Offset the message below it or a short table centres it straight across the column names. Bound
						// to the header's real height rather than a guessed constant, so a restyle cannot desync it.
						return FMargin(0.0f, Header->GetDesiredSize().Y, 0.0f, 0.0f);
					}))
					[
						// The box fills the overlay and centres its own content, rather than the slot centring a
						// content-sized box - the latter depends on the overlay having resolved a height first.
						SNew(SBox)
						.Visibility(this, &SColumnTableView::GetEmptyStateVisibility)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							InArgs._EmptyState.Widget
						]
					]
				]
			]
		];
	}

	/**
	 * True when the filter is hiding everything, as distinct from there being nothing to show. Lets a caller word its
	 * empty state for the situation the user is actually in.
	 */
	bool IsFilterActive() const { return !FilterText.IsEmpty(); }

	/** Expand or collapse one row, optionally its whole subtree. Safe on a flat table, where it simply does nothing. */
	void SetExpansion(const ItemType& Item, bool bExpand, bool bRecursive = false)
	{
		if (!TreeView.IsValid()) { return; }
		if (bRecursive) { ExpandRecursive(Item, bExpand); }
		else            { TreeView->SetItemExpansion(Item, bExpand); }
	}

	void SetAllExpansion(bool bExpand)
	{
		for (const ItemType& Item : VisibleRoots) { ExpandRecursive(Item, bExpand); }
	}

	/** Replace the root rows. The caller owns when data changes; this widget never polls. */
	void SetRootItems(TArray<ItemType> InItems)
	{
		RootItems = MoveTemp(InItems);
		Refresh();
	}

	/** Re-filter, re-sort and rebuild. Call after mutating rows in place. */
	void Refresh()
	{
		VisibleRoots.Reset();
		VisibleChildren.Reset();
		for (const ItemType& Item : RootItems) { BuildVisible(Item, VisibleRoots); }
		SortItems(VisibleRoots);

		if (TreeView.IsValid())
		{
			// While a filter is active every surviving row is there because it or something under it matched, so leaving
			// any of it collapsed would hide the very thing that was searched for.
			if (!FilterText.IsEmpty())
			{
				for (const ItemType& Item : VisibleRoots) { ExpandRecursive(Item, true); }
			}
			TreeView->RequestTreeRefresh();
		}
	}

	TArray<ItemType> GetSelectedItems() const
	{
		return TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<ItemType>();
	}

	void SetAllExpanded(bool bExpanded)
	{
		for (const ItemType& Item : VisibleRoots) { ExpandRecursive(Item, bExpanded); }
	}

	/**
	 * The given rows as tab-separated text with a header line - what pastes into a spreadsheet. Children of the given
	 * rows are included and indented, so a copied subtree still reads as one.
	 */
	FString GetRowsAsText(const TArray<ItemType>& Items) const
	{
		TArray<FString> Lines;
		TArray<FString> HeaderCells;
		for (const FTableColumnDef<ItemType>& Column : Columns) { HeaderCells.Add(Column.Label.ToString()); }
		Lines.Add(FString::Join(HeaderCells, TEXT("\t")));
		for (const ItemType& Item : Items) { AppendRowText(Item, 0, Lines); }
		return FString::Join(Lines, LINE_TERMINATOR);
	}

	/** Every row the current filter leaves visible. What you copy should be what you can see. */
	FString GetVisibleRowsAsText() const { return GetRowsAsText(VisibleRoots); }

	/**
	 * How many rows the current filter leaves visible, children included. For a status line that wants "N of M shown".
	 */
	int32 GetVisibleRowCount() const
	{
		int32 Count = 0;
		for (const ItemType& Item : VisibleRoots) { CountRecursive(Item, Count); }
		return Count;
	}

	/** The active filter string, so a caller can name it in its own empty-state wording. */
	FText GetFilterText() const { return FText::FromString(FilterText); }

	/** Public entry point for a consumer-owned search box. Same path the built-in one takes. */
	void SetFilterText(const FText& InText) { OnFilterTextChanged(InText); }

	/**
	 * Selected rows in VISIBLE order rather than selection order. A clipboard copy should follow what the user sees;
	 * the view's selection array is ordered by when each row was picked, which is not that.
	 */
	TArray<ItemType> GetSelectedItemsInVisibleOrder() const
	{
		TArray<ItemType> Out;
		if (!TreeView.IsValid()) { return Out; }
		for (const ItemType& Item : VisibleRoots) { AppendSelectedRecursive(Item, Out); }
		return Out;
	}

private:
	// ---- filtering ---------------------------------------------------------------------------------------------------

	bool MatchesFilter(const ItemType& Item) const
	{
		if (FilterText.IsEmpty()) { return true; }
		for (const FTableColumnDef<ItemType>& Column : Columns)
		{
			if (Column.GetText && Column.GetText(Item).ToString().Contains(FilterText)) { return true; }
		}
		return false;
	}

	void IncludeSubtree(const ItemType& Item, TArray<ItemType>& OutSiblings)
	{
		TArray<ItemType> Children;
		if (OnGetChildren.IsBound()) { OnGetChildren.Execute(Item, Children); }
		TArray<ItemType> Kids;
		for (const ItemType& Child : Children) { IncludeSubtree(Child, Kids); }
		SortItems(Kids);
		VisibleChildren.Add(Item, MoveTemp(Kids));
		OutSiblings.Add(Item);
	}

	/**
	 * True when this row survives the filter. A MATCHING row brings its whole subtree - once you have found the thing, you
	 * want to see what is in it - and a row with a matching descendant survives to lead you there.
	 */
	bool BuildVisible(const ItemType& Item, TArray<ItemType>& OutSiblings)
	{
		if (MatchesFilter(Item)) { IncludeSubtree(Item, OutSiblings); return true; }

		TArray<ItemType> Children;
		if (OnGetChildren.IsBound()) { OnGetChildren.Execute(Item, Children); }
		TArray<ItemType> Kids;
		for (const ItemType& Child : Children) { BuildVisible(Child, Kids); }
		if (Kids.Num() == 0) { return false; }

		SortItems(Kids);
		VisibleChildren.Add(Item, MoveTemp(Kids));
		OutSiblings.Add(Item);
		return true;
	}

	void GetVisibleChildren(ItemType Item, TArray<ItemType>& OutChildren)
	{
		if (const TArray<ItemType>* Found = VisibleChildren.Find(Item)) { OutChildren = *Found; }
	}

	void ExpandRecursive(const ItemType& Item, bool bExpand)
	{
		if (!TreeView.IsValid()) { return; }
		TreeView->SetItemExpansion(Item, bExpand);
		if (const TArray<ItemType>* Kids = VisibleChildren.Find(Item))
		{
			for (const ItemType& Child : *Kids) { ExpandRecursive(Child, bExpand); }
		}
	}

	void OnFilterTextChanged(const FText& InText)
	{
		FilterText = InText.ToString();
		Refresh();
	}

	EVisibility GetEmptyStateVisibility() const
	{
		return VisibleRoots.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
	}

	// ---- sorting -----------------------------------------------------------------------------------------------------

	void SortItems(TArray<ItemType>& Items) const
	{
		if (SortColumnId.IsNone()) { return; }
		const FTableColumnDef<ItemType>* Column = Columns.FindByPredicate(
			[this](const FTableColumnDef<ItemType>& C){ return C.Id == SortColumnId; });
		if (!Column || !Column->GetText) { return; }

		const bool bAscending = (SortMode == EColumnSortMode::Ascending);
		const FTableColumnDef<ItemType>* Col = Column;
		Items.Sort([Col, bAscending](const ItemType& A, const ItemType& B)
		{
			const bool bLess = Col->Less ? Col->Less(A, B)
			                             : Col->GetText(A).ToString() < Col->GetText(B).ToString();
			const bool bMore = Col->Less ? Col->Less(B, A)
			                             : Col->GetText(B).ToString() < Col->GetText(A).ToString();
			return bAscending ? bLess : bMore;
		});
	}

	EColumnSortMode::Type GetSortModeForColumn(const FName ColumnId) const
	{
		return (SortColumnId == ColumnId) ? SortMode : EColumnSortMode::None;
	}

	void OnSortColumn(EColumnSortPriority::Type, const FName& ColumnId, EColumnSortMode::Type InMode)
	{
		SortColumnId = ColumnId;
		SortMode = InMode;
		SavePersistedState();
		Refresh();
	}

	// ---- rows, selection, clipboard ----------------------------------------------------------------------------------

	TSharedRef<ITableRow> GenerateRow(ItemType Item, const TSharedRef<STableViewBase>& Owner)
	{
		return SNew(SColumnTableRow<ItemType>, Owner)
			.Item(Item)
			.Columns(&Columns)
			.ExpanderColumnId(ExpanderColumnId)
			.bShowExpanderWires(bShowExpanderWires)
			.HighlightText(this, &SColumnTableView::GetFilterTextAsText)
			.OnCopyRow(OnCopyRow)
			.OnPasteRow(OnPasteRow)
			.CanPasteRow(CanPasteRow)
			.OnRowRightClicked(FOnRowAction::CreateSP(this, &SColumnTableView::HandleRowRightClicked));
	}

	FText GetFilterTextAsText() const { return FText::FromString(FilterText); }

	/**
	 * The stock header uses Colors.Header, which is precisely the colour a details row takes on hover. This is that style
	 * with the column brushes stepped up to the next theme entries, so the header stays legible over either row state and
	 * follows the theme rather than a hardcoded grey.
	 */
	static const FHeaderRowStyle& GetDistinctHeaderStyle()
	{
		static const FHeaderRowStyle Style = []()
		{
			FHeaderRowStyle Copy = FAppStyle::Get().GetWidgetStyle<FHeaderRowStyle>("TableView.Header");
			Copy.ColumnStyle.NormalBrush      = FSlateColorBrush(FStyleColors::Dropdown);
			Copy.ColumnStyle.HoveredBrush     = FSlateColorBrush(FStyleColors::Hover);
			Copy.LastColumnStyle.NormalBrush  = FSlateColorBrush(FStyleColors::Dropdown);
			Copy.LastColumnStyle.HoveredBrush = FSlateColorBrush(FStyleColors::Hover);
			return Copy;
		}();
		return Style;
	}
	
	void HandleRowRightClicked(ItemType Item) { RightClickedItem = Item; }

	void HandleSelectionChanged(ItemType Item, ESelectInfo::Type)
	{
		OnItemSelected.ExecuteIfBound(Item);
	}

	TSharedPtr<SWidget> MakeDefaultContextMenu()
	{
		// The clicked row arrives from the row itself rather than from the selection, which means this is correct under
		// every selection mode - including None, where nothing is ever selected, and Multi, where the selection is not
		// the same question as "which row did you click".
		const ItemType Row = RightClickedItem;
		const bool bRowSection = Row.IsValid() && (OnCopyRow.IsBound() || OnPasteRow.IsBound());
		if (!bRowSection && !bAllowCopy) { return nullptr; }

		FMenuBuilder Menu(true, nullptr);
		if (bRowSection)
		{
			Menu.BeginSection(TEXT("RowEdit"), NSLOCTEXT("SimpleCore", "RowEditSection", "Edit"));
			if (OnCopyRow.IsBound())
			{
				Menu.AddMenuEntry(
					NSLOCTEXT("SimpleCore", "CopyRow", "Copy"),
					NSLOCTEXT("SimpleCore", "CopyRowTip", "Copy this row's settings."),
					FSlateIcon(FCoreStyle::Get().GetStyleSetName(), TEXT("GenericCommands.Copy")),
					FUIAction(FExecuteAction::CreateLambda([this, Row]() { OnCopyRow.ExecuteIfBound(Row); })),
					NAME_None, EUserInterfaceActionType::Button, NAME_None,
					FInputChord(EModifierKey::Shift, EKeys::RightMouseButton).GetInputText(/*bLongDisplayName*/ false));
			}
			if (OnPasteRow.IsBound())
			{
				FUIAction PasteAction(FExecuteAction::CreateLambda([this, Row]() { if (OnPasteRow.IsBound()) { OnPasteRow.Execute(Row); } }));
				if (CanPasteRow.IsBound())
				{
					PasteAction.CanExecuteAction = FCanExecuteAction::CreateLambda([this, Row]() { return CanPasteRow.Execute(Row); });
				}
				Menu.AddMenuEntry(
					NSLOCTEXT("SimpleCore", "PasteRow", "Paste"),
					NSLOCTEXT("SimpleCore", "PasteRowTip", "Paste copied settings onto this row."),
					FSlateIcon(FCoreStyle::Get().GetStyleSetName(), TEXT("GenericCommands.Paste")),
					PasteAction,
					NAME_None, EUserInterfaceActionType::Button, NAME_None,
					FInputChord(EModifierKey::Shift, EKeys::LeftMouseButton).GetInputText(/*bLongDisplayName*/ false));
			}
			Menu.EndSection();
		}

		if (bAllowCopy)
		{
			Menu.BeginSection(TEXT("Table"), NSLOCTEXT("SimpleCore", "TableSection", "Table"));
			Menu.AddMenuEntry(
				NSLOCTEXT("SimpleCore", "CopyVisibleRows", "Copy All Visible Rows"),
				NSLOCTEXT("SimpleCore", "CopyVisibleRowsTip", "Copy every row the current filter leaves visible, as tab-separated text."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SColumnTableView::CopyVisibleRows)));
			Menu.EndSection();
		}

		return Menu.MakeWidget();
	}

	void CopyVisibleRows() const
	{
		FPlatformApplicationMisc::ClipboardCopy(*GetVisibleRowsAsText());
	}

	void AppendRowText(const ItemType& Item, int32 Depth, TArray<FString>& OutLines) const
	{
		TArray<FString> Cells;
		for (int32 Idx = 0; Idx < Columns.Num(); ++Idx)
		{
			FString Cell = Columns[Idx].GetText ? Columns[Idx].GetText(Item).ToString() : FString();
			// Depth rides as leading indent on the first column, so a pasted tree still reads as one.
			if (Idx == 0 && Depth > 0) { Cell = FString::ChrN(Depth * 2, TEXT(' ')) + Cell; }
			Cells.Add(MoveTemp(Cell));
		}
		OutLines.Add(FString::Join(Cells, TEXT("\t")));

		if (const TArray<ItemType>* Kids = VisibleChildren.Find(Item))
		{
			for (const ItemType& Child : *Kids) { AppendRowText(Child, Depth + 1, OutLines); }
		}
	}
	
	void CountRecursive(const ItemType& Item, int32& InOutCount) const
	{
		++InOutCount;
		if (const TArray<ItemType>* Kids = VisibleChildren.Find(Item))
		{
			for (const ItemType& Child : *Kids) { CountRecursive(Child, InOutCount); }
		}
	}

	void AppendSelectedRecursive(const ItemType& Item, TArray<ItemType>& Out) const
	{
		if (TreeView->IsItemSelected(Item)) { Out.Add(Item); }
		if (const TArray<ItemType>* Kids = VisibleChildren.Find(Item))
		{
			for (const ItemType& Child : *Kids) { AppendSelectedRecursive(Child, Out); }
		}
	}

	/**
	 * The column's current fill proportion: whatever the user dragged it to, else its declared default. Columns stay in
	 * Fill sizing throughout, so a dragged column still rescales with the panel rather than freezing at a pixel width.
	 */
	float GetColumnFillWidth(const FName ColumnId) const
	{
		if (const float* Saved = SavedWidths.Find(ColumnId)) { return *Saved; }
		const FTableColumnDef<ItemType>* Column = Columns.FindByPredicate(
			[&ColumnId](const FTableColumnDef<ItemType>& C){ return C.Id == ColumnId; });
		return Column ? Column->FillWidth : 1.0f;
	}
	
	// ---- persistence -------------------------------------------------------------------------------------------------

	static const TCHAR* PersistSection() { return TEXT("SimpleCore.ColumnTableView"); }

	void LoadPersistedState()
	{
		if (PersistenceKey.IsEmpty() || !GConfig) { return; }
		FString Column;
		if (GConfig->GetString(PersistSection(), *(PersistenceKey + TEXT(".SortColumn")), Column, GEditorPerProjectIni))
		{
			SortColumnId = FName(*Column);
		}
		int32 Mode = 0;
		if (GConfig->GetInt(PersistSection(), *(PersistenceKey + TEXT(".SortMode")), Mode, GEditorPerProjectIni))
		{
			SortMode = static_cast<EColumnSortMode::Type>(Mode);
		}
		for (const FTableColumnDef<ItemType>& Col : Columns)
		{
			if (Col.FixedWidth > 0.0f) { continue; }   // nothing to restore, and a stale entry would outlive the change
			float Width = 0.0f;
			if (GConfig->GetFloat(PersistSection(), *(PersistenceKey + TEXT(".Width.") + Col.Id.ToString()), Width, GEditorPerProjectIni)
				&& Width > 0.0f)
			{
				SavedWidths.Add(Col.Id, Width);
			}
		}
	}

	void SavePersistedState() const
	{
		if (PersistenceKey.IsEmpty() || !GConfig) { return; }
		GConfig->SetString(PersistSection(), *(PersistenceKey + TEXT(".SortColumn")), *SortColumnId.ToString(), GEditorPerProjectIni);
		GConfig->SetInt(PersistSection(), *(PersistenceKey + TEXT(".SortMode")), static_cast<int32>(SortMode), GEditorPerProjectIni);
		for (const TPair<FName, float>& Pair : SavedWidths)
		{
			GConfig->SetFloat(PersistSection(), *(PersistenceKey + TEXT(".Width.") + Pair.Key.ToString()), Pair.Value, GEditorPerProjectIni);
		}
	}

	void OnColumnWidthChanged(float NewWidth, const FName ColumnId)
	{
		SavedWidths.Add(ColumnId, NewWidth);
		SavePersistedState();
	}

	// ---- state -------------------------------------------------------------------------------------------------------

	TArray<FTableColumnDef<ItemType>>	Columns;
	ItemType							RightClickedItem;
	FOnRowAction						OnCopyRow;
	FOnRowPaste							OnPasteRow;
	FCanRowAction						CanPasteRow;
	FOnGetChildren						OnGetChildren;
	FName								ExpanderColumnId;
	bool								bShowExpanderWires = false;
	FOnItemSelected						OnItemSelected;
	TSharedPtr<STreeView<ItemType>>		TreeView;

	TArray<ItemType>					RootItems;
	TArray<ItemType>					VisibleRoots;
	TMap<ItemType, TArray<ItemType>>	VisibleChildren;

	FString								FilterText;
	FName								SortColumnId;
	EColumnSortMode::Type				SortMode = EColumnSortMode::Ascending;
	TMap<FName, float>					SavedWidths;
	FString								PersistenceKey;
	bool								bAllowCopy = true;
};