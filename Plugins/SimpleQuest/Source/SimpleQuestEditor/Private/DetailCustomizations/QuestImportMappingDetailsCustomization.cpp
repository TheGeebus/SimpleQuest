// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "DetailCustomizations/QuestImportMappingDetailsCustomization.h"

#include "DesktopPlatformModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDesktopPlatform.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/TransactionObjectEvent.h"
#include "ScopedTransaction.h"
#include "SimpleQuestLog.h"
#include "SSearchableComboBox.h"
#include "Resolver/QuestDataFormatRegistry.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestResolverEditorMemo.h"
#include "Resolver/SQuestMappingBindingList.h"
#include "Resolver/SQuestMappingDiscriminatorList.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "QuestImportMappingDetails"

namespace
{
	// Versioned magic first lines, so a paste refuses clipboard content that is not ours rather than half-parsing it.
	const FString SampleSourceHeader(TEXT("SimpleQuestSampleSource/1"));
	const FString SourceColumnHeader(TEXT("SimpleQuestSourceColumn/1"));

	/** Read "Key=Value" lines sitting under a magic header. False when the header does not match at all. */
	bool ParseTaggedPayload(const FString& Text, const FString& Header, TMap<FString, FString>& OutFields)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		if (Lines.Num() < 1 || Lines[0].TrimStartAndEnd() != Header) { return false; }
		for (int32 i = 1; i < Lines.Num(); ++i)
		{
			FString Key, Value;
			if (Lines[i].Split(TEXT("="), &Key, &Value))
			{
				OutFields.Add(Key.TrimStartAndEnd(), Value.TrimStartAndEnd());
			}
		}
		return true;
	}
}

TSharedRef<IDetailCustomization> FQuestImportMappingDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FQuestImportMappingDetailsCustomization);
}

void FQuestImportMappingDetailsCustomization::RefreshSampleColumnCache()
{
	CachedSampleColumns.Reset();
	CachedSampleKeyColumn = NAME_None;

	if (SampleFolder.IsEmpty()) return;   // no sample pointed at yet -> empty -> dropdowns disabled/empty (loud, not typed)

	// This parses every file in the sample folder, which is why it lives where the sample CHANGES rather than where columns
	// are READ: the accessors below run once per widget rebuild and there are several widgets.
	const FQuestSourceColumns Cols = EnumerateForeignFileColumns(SampleFormatName.ToString(), SampleFolder);
	if (!Cols.bReadable) return;

	CachedSampleColumns   = Cols.Columns;
	CachedSampleKeyColumn = Cols.KeyColumn;
}

TArray<FName> FQuestImportMappingDetailsCustomization::SampleSourceColumns() const
{
	return CachedSampleColumns;
}

void FQuestImportMappingDetailsCustomization::RefreshDiscriminatorValueCache()
{
	CachedDiscriminatorValues.Reset();

	const UQuestImportMapping* M = Mapping.Get();
	if (!M || M->DiscriminatorColumn.IsNone() || SampleFolder.IsEmpty()) return;

	// Another whole-folder read, which is why it lives here and not in the accessor - the discriminator list asks for
	// these every time it rebuilds, and it rebuilds whenever anything about the sample or the recipe moves.
	CachedDiscriminatorValues = EnumerateColumnDistinctValues(SampleFormatName.ToString(), SampleFolder, M->DiscriminatorColumn);
}

TArray<FString> FQuestImportMappingDetailsCustomization::SampleDiscriminatorValues() const
{
	return CachedDiscriminatorValues;
}

TArray<FString> FQuestImportMappingDetailsCustomization::SampleQualifierOptions() const
{
	// The structural flow pins every graph has, plus the combinator outputs a feeds-prereq wire can leave from.
	TArray<FString> Out = { TEXT("Any Outcome"), TEXT("Entered"), TEXT("Deactivated"), TEXT("Out"), TEXT("PrereqOut") };

	const UQuestImportMapping* M = Mapping.Get();
	if (!M || SampleFolder.IsEmpty()) return Out;

	// Outcome pins are declared by the OBJECTIVE CLASS a row carries, so the real vocabulary comes from the sample's own
	// ObjectiveClass column: read its distinct values, load each class, and ask it for the paths it exposes.
	const FQuestColumnBinding* ObjBinding = M->Bindings.FindByPredicate(
		[](const FQuestColumnBinding& B) { return B.TargetProperty == TEXT("ObjectiveClass"); });
	if (!ObjBinding || ObjBinding->SourceColumn.IsNone()) return Out;

	for (const FString& ClassPath : EnumerateColumnDistinctValues(SampleFormatName.ToString(), SampleFolder, ObjBinding->SourceColumn))
	{
		UClass* ObjClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (!ObjClass) continue;
		for (const FObjectivePathDescriptor& Desc : FSimpleQuestEditorUtilities::DiscoverObjectivePaths(ObjClass))
		{
			if (!Desc.Identity.IsNone()) Out.AddUnique(FSimpleQuestEditorUtilities::GetOutcomeLabel(Desc.Identity).ToString());
		}
	}
	return Out;
}

FText FQuestImportMappingDetailsCustomization::GetSampleFormatText() const { return FText::FromName(SampleFormatName); }

void FQuestImportMappingDetailsCustomization::OnSampleFormatChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type)
{
	if (NewValue.IsValid()) { SampleFormatName = FName(**NewValue); RefreshFromSample(); }
}

FText FQuestImportMappingDetailsCustomization::GetSampleFolderText() const { return FText::FromString(SampleFolder); }

FReply FQuestImportMappingDetailsCustomization::OnBrowseForSampleFolder()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop) { return FReply::Handled(); }

	// Open where they already are, so re-picking a sibling folder is one click rather than a walk from the drive root.
	const FString DefaultPath = (!SampleFolder.IsEmpty() && IFileManager::Get().DirectoryExists(*SampleFolder))
		? SampleFolder : FPaths::ProjectDir();

	FString Picked;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (!Desktop->OpenDirectoryDialog(ParentWindow, LOCTEXT("BrowseSampleDirTitle", "Choose a folder of source files").ToString(),
		DefaultPath, Picked))
	{
		return FReply::Handled();   // cancelled - leave the current folder alone
	}

	SampleFolder = Picked;
	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Sample folder picked: '%s'."), *SampleFolder);
	RefreshFromSample();   // re-reads the columns, rebuilds both pickers and both lists, and saves the memo
	return FReply::Handled();
}

void FQuestImportMappingDetailsCustomization::OnSampleFolderCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	// Slate commits a text box TWICE for one edit - once for the Enter, and again for the focus change behind it - and the
	// second carries the same string and no new intent. Dropped, but ONLY the focus one: committing an UNCHANGED path with
	// Enter is how a designer says "I edited the file, read it again", and that has to keep working.
	const FString Committed = NewText.ToString().TrimQuotes();
	if (CommitType != ETextCommit::OnEnter && Committed == SampleFolder)
	{
		UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Sample folder commit ignored: '%s' is already the sample."), *Committed);
		return;
	}

	SampleFolder = Committed;
	RefreshFromSample();
}

void FQuestImportMappingDetailsCustomization::RefreshFromSample()
{
	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Mapping panel: RefreshFromSample ('%s' as %s)."),
		SampleFolder.IsEmpty() ? TEXT("(unset)") : *SampleFolder, *SampleFormatName.ToString());

	// Read the sample ONCE, here, before anything below asks for a column. Everything downstream - both pickers, both lists
	// and the mapping's own cache - then describes the same snapshot of the folder rather than re-reading it apiece.
	RefreshSampleColumnCache();
	RefreshDiscriminatorValueCache();

	// Publish the sample's columns onto the mapping so the stock-array GetOptions dropdowns (wire bindings) can pick from
	// them - the same column set the bespoke pickers use, one source of truth.
	if (UQuestImportMapping* M = Mapping.Get())
	{
		M->SampleColumnCache.Reset();
		for (const FName& Col : SampleSourceColumns()) { M->SampleColumnCache.Add(Col.ToString()); }
		M->SampleQualifierCache = SampleQualifierOptions();
	}
	
	// Sample changed -> the column set may have changed, so rebuild the discriminator-column dropdown, then re-pull both
	// widgets (columns feed the binding list; discriminator values feed the value->class list). The combo caches its options
	// at build time, so rebuilding the backing array isn't enough - tell the widget to re-read it.
	RebuildDiscriminatorColumnOptions();
	RebuildKeyColumnOptions();
	if (DiscriminatorColumnCombo.IsValid()) { DiscriminatorColumnCombo->RefreshOptions(); }
	if (KeyColumnCombo.IsValid())			{ KeyColumnCombo->RefreshOptions(); }
	if (DiscriminatorList.IsValid())        { DiscriminatorList->RefreshRows(); }
	if (BindingList.IsValid())              { BindingList->RefreshFromSource(); }

	SaveSampleToMemo();
}

// The key column is OPTIONAL, so its picker needs a way to say "none" - parenthesised so it cannot be mistaken for a
// column of that name.
static const TCHAR* GQuestKeyColumnNoneLabel = TEXT("(none)");

void FQuestImportMappingDetailsCustomization::RebuildDiscriminatorColumnOptions()
{
	DiscriminatorColumnOptions.Reset();
	for (const FName& Col : SampleSourceColumns())
	{
		DiscriminatorColumnOptions.Add(MakeShareable(new FString(Col.ToString())));
	}
}

void FQuestImportMappingDetailsCustomization::RebuildKeyColumnOptions()
{
	KeyColumnOptions.Reset();
	KeyColumnOptions.Add(MakeShareable(new FString(GQuestKeyColumnNoneLabel)));   // leads, so clearing is the first thing offered

	// The sample's own key column leads the real entries: it is the expected answer, and for a folder source it is the ONLY
	// column the provider will actually read the key from.
	if (!CachedSampleKeyColumn.IsNone())
	{
		KeyColumnOptions.Add(MakeShareable(new FString(CachedSampleKeyColumn.ToString())));
	}
	for (const FName& Col : CachedSampleColumns)
	{
		KeyColumnOptions.Add(MakeShareable(new FString(Col.ToString())));
	}
}

FText FQuestImportMappingDetailsCustomization::GetDiscriminatorColumnText() const
{
	const UQuestImportMapping* M = Mapping.Get();
	return (M && !M->DiscriminatorColumn.IsNone()) ? FText::FromName(M->DiscriminatorColumn)
		: LOCTEXT("PickDiscriminator", "Select a column...");
}

void FQuestImportMappingDetailsCustomization::OnDiscriminatorColumnChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type)
{
	if (NewValue.IsValid()) { ApplyDiscriminatorColumn(FName(**NewValue)); }
}

void FQuestImportMappingDetailsCustomization::ApplyDiscriminatorColumn(FName Column)
{
	UQuestImportMapping* M = Mapping.Get();
	if (!M || M->DiscriminatorColumn == Column) return;   // compare against the ASSET, never a widget's idea of state

	const FScopedTransaction Transaction(LOCTEXT("SetDiscriminatorColumn", "Set Discriminator Column"));
	M->Modify();
	M->DiscriminatorColumn = Column;
	M->PostEditChange();
	OnMappingModified();
	// The discriminator column changed -> its distinct value set changed. Re-read BEFORE the rows rebuild, or the list
	// repopulates from the previous column's values - which is the failure a cache introduces if its second invalidation
	// point is missed, and the reason this one is not lazy.
	RefreshDiscriminatorValueCache();
	if (DiscriminatorList.IsValid()) { DiscriminatorList->RefreshRows(); }
}

FText FQuestImportMappingDetailsCustomization::GetKeyColumnText() const
{
	const UQuestImportMapping* M = Mapping.Get();
	return (M && !M->KeyColumn.IsNone()) ? FText::FromName(M->KeyColumn)
		: LOCTEXT("PickKeyColumn", "Select a column...");
}

void FQuestImportMappingDetailsCustomization::OnKeyColumnChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type)
{
	// The clear entry is the only option that is not a column name, so it maps to None rather than to a column called
	// "(none)". Everything downstream already treats None as "no key column" - this just gives the field a way to say it.
	if (NewValue.IsValid())
	{
		ApplyKeyColumn(*NewValue == GQuestKeyColumnNoneLabel ? NAME_None : FName(**NewValue));
	}
}

void FQuestImportMappingDetailsCustomization::ApplyKeyColumn(FName Column)
{
	UQuestImportMapping* M = Mapping.Get();
	if (!M || M->KeyColumn == Column) return;   // compare against the ASSET, never a widget's idea of state
	
	const FScopedTransaction Transaction(LOCTEXT("SetKeyColumn", "Set Key Column"));
	M->Modify();
	M->KeyColumn = Column;
	M->PostEditChange();
	OnMappingModified();
}

bool FQuestImportMappingDetailsCustomization::IsPastableColumn(FName Column, const TArray<TSharedPtr<FString>>& Options) const
{
	if (Column.IsNone()) { return true; }   // empty means "clear it", which is always legal
	const FString AsText = Column.ToString();
	// The CACHED options, not a fresh enumeration - re-reading would parse the whole sample folder for a validity check.
	return Options.ContainsByPredicate([&AsText](const TSharedPtr<FString>& Opt) { return Opt.IsValid() && *Opt == AsText; });
}

void FQuestImportMappingDetailsCustomization::CopySampleSource() const
{
	const FString Payload = FString::Printf(TEXT("%s%sFormat=%s%sFolder=%s"),
		*SampleSourceHeader, LINE_TERMINATOR, *SampleFormatName.ToString(), LINE_TERMINATOR, *SampleFolder);
	FPlatformApplicationMisc::ClipboardCopy(*Payload);

	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Sample source copied: %s at '%s'."),
		*SampleFormatName.ToString(), SampleFolder.IsEmpty() ? TEXT("(unset)") : *SampleFolder);
}

void FQuestImportMappingDetailsCustomization::PasteSampleSource()
{
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	TMap<FString, FString> Fields;
	const FString* Format = nullptr;
	const FString* Folder = nullptr;
	if (ParseTaggedPayload(Text, SampleSourceHeader, Fields))
	{
		Format = Fields.Find(TEXT("Format"));
		Folder = Fields.Find(TEXT("Folder"));
	}
	if (!Format || !Folder)
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Sample source paste refused: the clipboard does not hold a copied sample source."));
		return;
	}
	// A paste must not reach a state the pickers could not: the format has to be one the registry offers, and a folder
	// that does not exist would leave every picker below silently empty with nothing saying why.
	if (!FormatOptions.ContainsByPredicate([Format](const TSharedPtr<FString>& O) { return O.IsValid() && *O == *Format; }))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Sample source paste refused: '%s' is not a registered format."), **Format);
		return;
	}
	if (!Folder->IsEmpty() && !IFileManager::Get().DirectoryExists(**Folder))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Sample source paste refused: '%s' does not exist."), **Folder);
		return;
	}

	SampleFormatName = FName(**Format);
	SampleFolder = *Folder;
	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Sample source pasted: %s at '%s'."),
		*SampleFormatName.ToString(), SampleFolder.IsEmpty() ? TEXT("(unset)") : *SampleFolder);

	RefreshFromSample();   // re-reads the columns, rebuilds both pickers and both lists, and saves the memo
}

void FQuestImportMappingDetailsCustomization::CopyColumnValue(FName Column) const
{
	const FString Payload = FString::Printf(TEXT("%s%sColumn=%s"), *SourceColumnHeader, LINE_TERMINATOR,
		Column.IsNone() ? TEXT("") : *Column.ToString());
	FPlatformApplicationMisc::ClipboardCopy(*Payload);

	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Source column copied: '%s'."),
		Column.IsNone() ? TEXT("(none)") : *Column.ToString());
}

void FQuestImportMappingDetailsCustomization::PasteDiscriminatorColumn()
{
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	TMap<FString, FString> Fields;
	const FString* Value = ParseTaggedPayload(Text, SourceColumnHeader, Fields) ? Fields.Find(TEXT("Column")) : nullptr;
	if (!Value)
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Discriminator column paste refused: the clipboard does not hold a copied column."));
		return;
	}
	const FName Column = Value->IsEmpty() ? NAME_None : FName(**Value);
	if (!IsPastableColumn(Column, DiscriminatorColumnOptions))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Discriminator column paste refused: '%s' is not in the current sample."), **Value);
		return;
	}
	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Discriminator column pasting: '%s'."), **Value);
	ApplyDiscriminatorColumn(Column);
}

void FQuestImportMappingDetailsCustomization::PasteKeyColumn()
{
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	TMap<FString, FString> Fields;
	const FString* Value = ParseTaggedPayload(Text, SourceColumnHeader, Fields) ? Fields.Find(TEXT("Column")) : nullptr;
	if (!Value)
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Key column paste refused: the clipboard does not hold a copied column."));
		return;
	}
	const FName Column = Value->IsEmpty() ? NAME_None : FName(**Value);
	if (!IsPastableColumn(Column, KeyColumnOptions))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Key column paste refused: '%s' is not in the current sample."), **Value);
		return;
	}
	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Key column pasting: '%s'."), **Value);
	ApplyKeyColumn(Column);
}

void FQuestImportMappingDetailsCustomization::RestoreSampleFromMemo()
{
	const UQuestImportMapping* M = Mapping.Get();
	if (!M) return;
	const FString Key = FSoftObjectPath(M).ToString();
	if (const FString* Folder = UQuestResolverEditorMemo::Get()->SampleFolderByMapping.Find(Key))
	{
		SampleFolder = *Folder;
	}
	if (const FString* Format = UQuestResolverEditorMemo::Get()->SampleFormatByMapping.Find(Key))
	{
		if (!Format->IsEmpty()) { SampleFormatName = FName(**Format); }
	}
}

void FQuestImportMappingDetailsCustomization::SaveSampleToMemo() const
{
	const UQuestImportMapping* M = Mapping.Get();
	if (!M) return;
	const FString Key = FSoftObjectPath(M).ToString();
	UQuestResolverEditorMemo* Memo = UQuestResolverEditorMemo::Get();
	Memo->SampleFolderByMapping.Add(Key, SampleFolder);
	Memo->SampleFormatByMapping.Add(Key, SampleFormatName.ToString());
	Memo->SaveConfig();
}

void FQuestImportMappingDetailsCustomization::OnMappingModified()
{
	if (UQuestImportMapping* M = Mapping.Get())
	{
		M->MarkPackageDirty();
	}
	// A class-map change can add/remove mapped classes -> the binding list's property rows depend on the mapped classes,
	// so refresh it too (this is the live-refresh-on-class-map-change that used to require a reopen).
	if (BindingList.IsValid()) { BindingList->RefreshRows(); }
	// (Readiness banner re-validation lands with the next cut.)
}

void FQuestImportMappingDetailsCustomization::PostUndo(bool bSuccess)
{
	RefreshListsAfterTransaction();
}

void FQuestImportMappingDetailsCustomization::PostRedo(bool bSuccess)
{
	RefreshListsAfterTransaction();
}

bool FQuestImportMappingDetailsCustomization::MatchesContext(const FTransactionContext& InContext,
	const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const
{
	const UQuestImportMapping* M = Mapping.Get();
	if (!M) { return false; }
	for (const TPair<UObject*, FTransactionObjectEvent>& Pair : TransactionObjectContexts)
	{
		if (Pair.Key == M) { return true; }
	}
	return false;
}

void FQuestImportMappingDetailsCustomization::RefreshListsAfterTransaction()
{
	/**
	 * BOTH lists, unlike OnMappingModified which re-reads only the bindings: an undo can restore a discriminator entry,
	 * and the discriminator list's own rows are built from those. Neither MarkPackageDirty nor a source re-read belongs
	 * here - the transaction system owns the dirty flag, and the sample source is editor-side state an undo never touched.
	 */
	if (DiscriminatorList.IsValid()) { DiscriminatorList->RefreshRows(); }
	if (BindingList.IsValid())       { BindingList->RefreshRows(); }

	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Mapping panel refreshed after an undo/redo."));
}

void FQuestImportMappingDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1) return;   // single-object edit only
	Mapping = Cast<UQuestImportMapping>(Objects[0].Get());
	if (!Mapping.IsValid()) return;

	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Mapping panel: CustomizeDetails."));

	RestoreSampleFromMemo();      // per-user convenience only; the recipe itself stays shape-only
	RefreshSampleColumnCache();   // the memo just named a sample; everything below reads columns and none of it should re-read them
	RefreshDiscriminatorValueCache();
	
	// Hide the stock discriminator column + class map: both are now driven by pickers (a typed FName + a typed TMap key were
	// the last two corruption holes). The stock Bindings array stays hidden too — the binding widget is its editor.
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuestImportMapping, DiscriminatorColumn));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuestImportMapping, DiscriminatorClasses));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuestImportMapping, Bindings));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuestImportMapping, KeyColumn));

	// Populate the format dropdown from the registered providers (same source as the settings picker).
	FormatOptions.Reset();
	for (const FString& Name : FQuestDataFormatRegistry::Get().GetRegisteredNames())
	{
		FormatOptions.Add(MakeShareable(new FString(Name)));
	}
	RebuildDiscriminatorColumnOptions();
	RebuildKeyColumnOptions();

	// Publish the sample's columns onto the mapping so the stock-array GetOptions dropdowns (wire bindings) can pick from
	// them — the same column set the bespoke pickers use, one source of truth.
	if (UQuestImportMapping* M = Mapping.Get())
	{
		M->SampleColumnCache.Reset();
		for (const FName& Col : SampleSourceColumns()) { M->SampleColumnCache.Add(Col.ToString()); }
		M->SampleQualifierCache = SampleQualifierOptions();
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("Bindings"), LOCTEXT("BindingsCategory", "Column Bindings"), ECategoryPriority::Important);

	// Sample-source row: an editor-only "point at a representative file to author against" control. NOT saved on the recipe.
	Category.AddCustomRow(LOCTEXT("SampleSourceFilter", "Sample Source"))
	.NameContent()[ SNew(STextBlock).Text(LOCTEXT("SampleSourceLabel", "Sample Source"))
		.ToolTipText(LOCTEXT("SampleSourceTip", "Point at a folder of representative source files. Reads every file of the chosen format and unions their columns to populate the pickers below. Editor-only — not saved on this recipe."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent().MinDesiredWidth(400.0f)
	[
		SNew(SHorizontalBox)
		// "Format" label + picker
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("SampleFormatLabel", "Format")).Font(IDetailLayoutBuilder::GetDetailFont())
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
		[
			SNew(SSearchableComboBox)
			.OptionsSource(&FormatOptions)
			// Fire on every pick. The combo caches its own SelectedItem and drops a re-pick that matches it, but anything
			// that writes the value WITHOUT going through the combo - a paste, an undo - leaves that cache stale, and then
			// re-selecting the previous value looks like "no change" and silently does nothing.
			.bAlwaysSelectItem(true)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> In) { return SNew(STextBlock).Text(FText::FromString(*In)); })
			.OnSelectionChanged(this, &FQuestImportMappingDetailsCustomization::OnSampleFormatChanged)
			[ SNew(STextBlock).Text(this, &FQuestImportMappingDetailsCustomization::GetSampleFormatText) ]
		]
		// "Directory" label + path field
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("SampleDirectoryLabel", "Directory")).Font(IDetailLayoutBuilder::GetDetailFont())
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			.HintText(LOCTEXT("SampleDirHint", "Folder of source files..."))
			.Text(this, &FQuestImportMappingDetailsCustomization::GetSampleFolderText)
			.OnTextCommitted(this, &FQuestImportMappingDetailsCustomization::OnSampleFolderCommitted)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.ToolTipText(LOCTEXT("BrowseSampleDirTip", "Browse for a folder of source files."))
			.OnClicked(this, &FQuestImportMappingDetailsCustomization::OnBrowseForSampleFolder)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(TEXT("Icons.FolderOpen")))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]
	]
	.CopyAction(FUIAction(FExecuteAction::CreateSP(this, &FQuestImportMappingDetailsCustomization::CopySampleSource)))
	.PasteAction(FUIAction(FExecuteAction::CreateSP(this, &FQuestImportMappingDetailsCustomization::PasteSampleSource)));

	// Discriminator-column picker: choose which sample column names each row's node kind (never a typed FName).
	Category.AddCustomRow(LOCTEXT("DiscriminatorFilter", "Discriminator Column"))
	.NameContent()[ SNew(STextBlock).Text(LOCTEXT("DiscriminatorLabel", "Discriminator Column"))
		.ToolTipText(LOCTEXT("DiscriminatorTip", "The sample column whose value names each row's node kind. Its distinct values become the rows below."))
		.Font(IDetailLayoutBuilder::GetDetailFont()) ]
	.ValueContent().MinDesiredWidth(300.0f)
	[
		SAssignNew(DiscriminatorColumnCombo, SSearchableComboBox)
		.OptionsSource(&DiscriminatorColumnOptions)
		.bAlwaysSelectItem(true)   // see the Format combo above - external writes leave the combo's own cache stale
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> In) { return SNew(STextBlock).Text(FText::FromString(*In)); })
		.OnSelectionChanged(this, &FQuestImportMappingDetailsCustomization::OnDiscriminatorColumnChanged)
		[ SNew(STextBlock).Text(this, &FQuestImportMappingDetailsCustomization::GetDiscriminatorColumnText) ]
	]
	.CopyAction(FUIAction(FExecuteAction::CreateLambda([this]()
		{ if (const UQuestImportMapping* M = Mapping.Get()) { CopyColumnValue(M->DiscriminatorColumn); } })))
	.PasteAction(FUIAction(FExecuteAction::CreateSP(this, &FQuestImportMappingDetailsCustomization::PasteDiscriminatorColumn)));

	// Key-column picker: choose which sample column holds each row's semantic key (the studio's id/quest_key column). Optional
	// — reverse-export writes the preserved source key back into this column; empty -> exported keys default to the GUID.
	Category.AddCustomRow(LOCTEXT("KeyColumnFilter", "Key Column"))
	.NameContent()[ SNew(STextBlock).Text(LOCTEXT("KeyColumnLabel", "Key Column"))
		.ToolTipText(LOCTEXT("KeyColumnTip", "The sample column holding each row's semantic key. Optional — used by reverse-export to write your keys back instead of GUIDs."))
		.Font(IDetailLayoutBuilder::GetDetailFont()) ]
	.ValueContent().MinDesiredWidth(300.0f)
	[
		SAssignNew(KeyColumnCombo, SSearchableComboBox)
		.OptionsSource(&KeyColumnOptions)
		.bAlwaysSelectItem(true)   // see the Format combo above - external writes leave the combo's own cache stale
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> In) { return SNew(STextBlock).Text(FText::FromString(*In)); })
		.OnSelectionChanged(this, &FQuestImportMappingDetailsCustomization::OnKeyColumnChanged)
		[ SNew(STextBlock).Text(this, &FQuestImportMappingDetailsCustomization::GetKeyColumnText) ]
	]
	.CopyAction(FUIAction(FExecuteAction::CreateLambda([this]()
		{ if (const UQuestImportMapping* M = Mapping.Get()) { CopyColumnValue(M->KeyColumn); } })))
	.PasteAction(FUIAction(FExecuteAction::CreateSP(this, &FQuestImportMappingDetailsCustomization::PasteKeyColumn)));

	// Discriminator value -> class list: one row per distinct value found in the discriminator column, class-picker only.
	FQuestMappingDiscriminatorListConfig DiscConfig;
	DiscConfig.Mapping = Mapping;
	DiscConfig.DistinctValueProvider = [this]() { return SampleDiscriminatorValues(); };
	DiscConfig.OnMappingModified = FSimpleDelegate::CreateSP(this, &FQuestImportMappingDetailsCustomization::OnMappingModified);

	Category.AddCustomRow(LOCTEXT("DiscriminatorListFilter", "Row Kinds"))
	[
		SAssignNew(DiscriminatorList, SQuestMappingDiscriminatorList).Config(DiscConfig)
	];

	// Column -> property binding list (anchored on the mapped classes' authored properties).
	FQuestMappingBindingListConfig Config;
	Config.Mapping = Mapping;
	Config.SourceColumnProvider = [this]() { return SampleSourceColumns(); };
	Config.OnMappingModified = FSimpleDelegate::CreateSP(this, &FQuestImportMappingDetailsCustomization::OnMappingModified);

	Category.AddCustomRow(LOCTEXT("BindingListFilter", "Column Bindings"))
	[
		SAssignNew(BindingList, SQuestMappingBindingList).Config(Config)
	];
}

#undef LOCTEXT_NAMESPACE

