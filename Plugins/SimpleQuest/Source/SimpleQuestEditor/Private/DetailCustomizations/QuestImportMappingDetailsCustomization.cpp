// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "DetailCustomizations/QuestImportMappingDetailsCustomization.h"

#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/SQuestMappingBindingList.h"
#include "Resolver/SQuestMappingDiscriminatorList.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "SSearchableComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Resolver/QuestDataFormatRegistry.h"

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
	// Sample changed -> the column set may have changed, so rebuild the discriminator-column dropdown, then re-pull both
	// widgets (columns feed the binding list; discriminator values feed the value->class list). The combo caches its options
	// at build time, so rebuilding the backing array isn't enough — tell the widget to re-read it.
	RebuildDiscriminatorColumnOptions();
	if (DiscriminatorColumnCombo.IsValid()) { DiscriminatorColumnCombo->RefreshOptions(); }
	if (DiscriminatorList.IsValid())        { DiscriminatorList->RefreshRows(); }
	if (BindingList.IsValid())              { BindingList->RefreshRows(); }
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
	
	// Hide the stock discriminator column + class map: both are now driven by pickers (a typed FName + a typed TMap key were
	// the last two corruption holes). The stock Bindings array stays hidden too — the binding widget is its editor.
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuestImportMapping, DiscriminatorColumn));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuestImportMapping, ClassByDiscriminatorValue));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuestImportMapping, Bindings));

	// Populate the format dropdown from the registered providers (same source as the settings picker).
	FormatOptions.Reset();
	for (const FString& Name : FQuestDataFormatRegistry::Get().GetRegisteredNames())
	{
		FormatOptions.Add(MakeShareable(new FString(Name)));
	}
	RebuildDiscriminatorColumnOptions();

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("Bindings"), LOCTEXT("BindingsCategory", "Column Bindings"), ECategoryPriority::Important);

	// Sample-source row: an editor-only "point at a representative file to author against" control. NOT saved on the recipe.
	Category.AddCustomRow(LOCTEXT("SampleSourceFilter", "Sample Source"))
	.NameContent()[ SNew(STextBlock).Text(LOCTEXT("SampleSourceLabel", "Sample Source"))
		.ToolTipText(LOCTEXT("SampleSourceTip", "Point at a representative source file to populate the pickers below. Editor-only — not saved on this recipe."))
		.Font(IDetailLayoutBuilder::GetDetailFont()) ]
	.ValueContent().MinDesiredWidth(400.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(SSearchableComboBox)
			.OptionsSource(&FormatOptions)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> In) { return SNew(STextBlock).Text(FText::FromString(*In)); })
			.OnSelectionChanged(this, &FQuestImportMappingDetailsCustomization::OnSampleFormatChanged)
			[ SNew(STextBlock).Text(this, &FQuestImportMappingDetailsCustomization::GetSampleFormatText) ]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			.HintText(LOCTEXT("SampleFolderHint", "Source folder path..."))
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

