// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "DetailCustomizations/QuestImportMappingDetailsCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "ScopedTransaction.h"
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

TSharedRef<IDetailCustomization> FQuestImportMappingDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FQuestImportMappingDetailsCustomization);
}

TArray<FName> FQuestImportMappingDetailsCustomization::SampleSourceColumns() const
{
	if (SampleFolder.IsEmpty()) return {};   // no sample pointed at yet -> empty -> dropdowns disabled/empty (loud, not typed)
	const FQuestSourceColumns Cols = EnumerateForeignFileColumns(SampleFormatName.ToString(), SampleFolder);
	return Cols.bReadable ? Cols.Columns : TArray<FName>();
}

TArray<FString> FQuestImportMappingDetailsCustomization::SampleDiscriminatorValues() const
{
	const UQuestImportMapping* M = Mapping.Get();
	if (!M || M->DiscriminatorColumn.IsNone() || SampleFolder.IsEmpty()) return {};
	return EnumerateColumnDistinctValues(SampleFormatName.ToString(), SampleFolder, M->DiscriminatorColumn);
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

void FQuestImportMappingDetailsCustomization::OnSampleFolderCommitted(const FText& NewText, ETextCommit::Type)
{
	SampleFolder = NewText.ToString().TrimQuotes();
	RefreshFromSample();
}

void FQuestImportMappingDetailsCustomization::RefreshFromSample()
{
	// Publish the sample's columns onto the mapping so the stock-array GetOptions dropdowns (wire bindings) can pick from
	// them — the same column set the bespoke pickers use, one source of truth.
	if (UQuestImportMapping* M = Mapping.Get())
	{
		M->SampleColumnCache.Reset();
		for (const FName& Col : SampleSourceColumns()) { M->SampleColumnCache.Add(Col.ToString()); }
		M->SampleQualifierCache = SampleQualifierOptions();
	}
	
	// Sample changed -> the column set may have changed, so rebuild the discriminator-column dropdown, then re-pull both
	// widgets (columns feed the binding list; discriminator values feed the value->class list). The combo caches its options
	// at build time, so rebuilding the backing array isn't enough — tell the widget to re-read it.
	RebuildDiscriminatorColumnOptions();
	if (DiscriminatorColumnCombo.IsValid()) { DiscriminatorColumnCombo->RefreshOptions(); }
	if (KeyColumnCombo.IsValid())			{ KeyColumnCombo->RefreshOptions(); }
	if (DiscriminatorList.IsValid())        { DiscriminatorList->RefreshRows(); }
	if (BindingList.IsValid())              { BindingList->RefreshFromSource(); }

	SaveSampleToMemo();
}

void FQuestImportMappingDetailsCustomization::RebuildDiscriminatorColumnOptions()
{
	DiscriminatorColumnOptions.Reset();
	for (const FName& Col : SampleSourceColumns())
	{
		DiscriminatorColumnOptions.Add(MakeShareable(new FString(Col.ToString())));
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
	UQuestImportMapping* M = Mapping.Get();
	if (!M || !NewValue.IsValid()) return;

	const FScopedTransaction Transaction(LOCTEXT("SetDiscriminatorColumn", "Set Discriminator Column"));
	M->Modify();
	M->DiscriminatorColumn = FName(**NewValue);
	M->PostEditChange();
	OnMappingModified();
	// The discriminator column changed -> its distinct value set changed -> rebuild the value->class rows.
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
	UQuestImportMapping* M = Mapping.Get();
	if (!M || !NewValue.IsValid()) return;

	const FScopedTransaction Transaction(LOCTEXT("SetKeyColumn", "Set Key Column"));
	M->Modify();
	M->KeyColumn = FName(**NewValue);
	M->PostEditChange();
	OnMappingModified();
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

void FQuestImportMappingDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1) return;   // single-object edit only
	Mapping = Cast<UQuestImportMapping>(Objects[0].Get());
	if (!Mapping.IsValid()) return;

	RestoreSampleFromMemo();   // per-user convenience only; the recipe itself stays shape-only
	
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
	];

	// Discriminator-column picker: choose which sample column names each row's node kind (never a typed FName).
	Category.AddCustomRow(LOCTEXT("DiscriminatorFilter", "Discriminator Column"))
	.NameContent()[ SNew(STextBlock).Text(LOCTEXT("DiscriminatorLabel", "Discriminator Column"))
		.ToolTipText(LOCTEXT("DiscriminatorTip", "The sample column whose value names each row's node kind. Its distinct values become the rows below."))
		.Font(IDetailLayoutBuilder::GetDetailFont()) ]
	.ValueContent().MinDesiredWidth(300.0f)
	[
		SAssignNew(DiscriminatorColumnCombo, SSearchableComboBox)
		.OptionsSource(&DiscriminatorColumnOptions)
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> In) { return SNew(STextBlock).Text(FText::FromString(*In)); })
		.OnSelectionChanged(this, &FQuestImportMappingDetailsCustomization::OnDiscriminatorColumnChanged)
		[ SNew(STextBlock).Text(this, &FQuestImportMappingDetailsCustomization::GetDiscriminatorColumnText) ]
	];

	// Key-column picker: choose which sample column holds each row's semantic key (the studio's id/quest_key column). Optional
	// — reverse-export writes the preserved source key back into this column; empty -> exported keys default to the GUID.
	Category.AddCustomRow(LOCTEXT("KeyColumnFilter", "Key Column"))
	.NameContent()[ SNew(STextBlock).Text(LOCTEXT("KeyColumnLabel", "Key Column"))
		.ToolTipText(LOCTEXT("KeyColumnTip", "The sample column holding each row's semantic key. Optional — used by reverse-export to write your keys back instead of GUIDs."))
		.Font(IDetailLayoutBuilder::GetDetailFont()) ]
	.ValueContent().MinDesiredWidth(300.0f)
	[
		SAssignNew(KeyColumnCombo, SSearchableComboBox)
		.OptionsSource(&DiscriminatorColumnOptions)
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> In) { return SNew(STextBlock).Text(FText::FromString(*In)); })
		.OnSelectionChanged(this, &FQuestImportMappingDetailsCustomization::OnKeyColumnChanged)
		[ SNew(STextBlock).Text(this, &FQuestImportMappingDetailsCustomization::GetKeyColumnText) ]
	];

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

