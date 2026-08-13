// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestRowRestore.h"

#include "GameplayTagContainer.h"
#include "Internationalization/Text.h"
#include "Misc/PackageName.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestDataValueBuilder.h"
#include "SimpleQuestLog.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

// Declared ahead of RestoreQuestCell because the two are mutually recursive: a cell whose Kind is Array dispatches here,
// and each element goes back through the public entry point so container elements get the identical typed-write treatment
// as a top-level cell.
static bool RestoreArrayCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value);

/**
 * Returns whether it actually WROTE. Void was the bug: ApplyQuestChangesToObject did Modify() -> RestoreQuestCell() ->
 * ++Written with nothing between, so every decline below still dirtied the package and counted a change that did
 * not happen. A plan/apply pipeline reporting work it did not do is the failure this whole arc exists to prevent.
 * The Array arm reports through RestoreArrayCell, which fails the whole call if any element declined - a container
 * rebuilt into something other than what the source asked for is not a clean write.
 */
bool RestoreQuestCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value)
{
	switch (Value.Kind)
	{
	case EQuestDataValueKind::Empty:
		{
			return false;   // leave the constructed default (Q6 symmetry). Not a failure - the source said nothing.
		}

	case EQuestDataValueKind::Tag:
		{
			// A cell's Kind and its destination are paired by NAME alone - a mapping binds any column onto any property and
			// nothing type-checks the pair - so "it is a struct" does not justify the cast. Writing a tag over an unrelated
			// struct is type confusion, and a caller restoring onto an exactly-sized buffer turns the larger write into a heap
			// overflow. Same identity test the array path applies. A mismatch is an authoring error: report it, change nothing.
			if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				if (StructProp->Struct == TBaseStructure<FGameplayTag>::Get())
				{
					*static_cast<FGameplayTag*>(ValuePtr) = Value.Tag;   // typed; inverse of the export reinterpret read
					return true;
				}
			}
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("RestoreCell: a tag value was bound to '%s', which is not an FGameplayTag - left at its default."),
				*Prop->GetName());
			return false;
		}

	case EQuestDataValueKind::TagContainer:
		{
			if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				if (StructProp->Struct == TBaseStructure<FGameplayTagContainer>::Get())
				{
					*static_cast<FGameplayTagContainer*>(ValuePtr) = Value.TagContainer;
					return true;
				}
			}
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("RestoreCell: a tag-container value was bound to '%s', which is not an FGameplayTagContainer - left at its default."),
				*Prop->GetName());
			return false;
		}

	case EQuestDataValueKind::Text:
		{
			if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
			{
				TextProp->SetPropertyValue(ValuePtr, Value.Text);   // typed - carries loc ns/key, no buffer round-trip
				return true;
			}
			return false;
		}

	case EQuestDataValueKind::Bool:
		{
			if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				BoolProp->SetPropertyValue(ValuePtr, Value.bBool);
				return true;
			}
			return false;
		}

	case EQuestDataValueKind::Array:
		{
			// container-type branch handled inside (array/set); recurses, and reports a partial element write as failure
			return RestoreArrayCell(Prop, ValuePtr, Value);
		}

	case EQuestDataValueKind::Number:
	case EQuestDataValueKind::String:
	case EQuestDataValueKind::Enum:
	case EQuestDataValueKind::Reference:
	case EQuestDataValueKind::StructLiteral:
	default:
		{
			// String-carrying Kinds - all route through StringForm -> ImportText, which types against the property (a
			// numeric property parses "3" fine; the Number/String distinction is a provider-render concern, not a
			// restore concern). Value.StringForm holds the cell string (== today's CellText for the TSV path). The FText
			// special-case is PRESERVED here because TSV delivers FText cells as Kind=Scalar (a Scalar holding NSLOCTEXT);
			// routing them to ImportText instead of ReadFromBuffer would regress TSV. JSON never lands here for FText (it
			// uses Kind=Text above), so this branch only ever sees TSV's FText-as-Scalar - exactly the old behavior.
			if (Value.StringForm.IsEmpty()) return false;
			if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
			{
				FText Parsed;
				const TCHAR* Buffer = *Value.StringForm;
				if (FTextStringHelper::ReadFromBuffer(Buffer, Parsed))
				{
					TextProp->SetPropertyValue(ValuePtr, Parsed);
					return true;
				}
				return false;
			}
			// ImportText_Direct returns null when it could not parse the string against this property - the one decline
			// on this path that was previously indistinguishable from a successful write.
			return Prop->ImportText_Direct(*Value.StringForm, ValuePtr, /*OwnerObject*/ nullptr, PPF_None) != nullptr;
		}
	}
}

/**
 * Returns whether it wrote. An element that declines makes the whole call report FALSE even though the container was
 * structurally rebuilt: the array did change, but not into what the source asked for, and a caller that counted it as
 * a clean write would be reporting a value it does not hold. Over-reporting failure is the safe direction for a
 * signal whose entire job is to stop silent partial success.
 */
static bool RestoreArrayCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value)
{
	// A structured provider (JSON) delivers an FGameplayTagContainer as a Kind=Array of tag-string elements. The
	// destination property's type decides: a container gets the elements requested as tags + assigned directly (the
	// wrapped "(GameplayTags=..)" ImportText form can't be reconstructed from bare tag strings, so type it here).
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (StructProp->Struct == TBaseStructure<FGameplayTagContainer>::Get())
		{
			FGameplayTagContainer Container;
			for (const FQuestDataValue& Elem : Value.Elements)
			{
				const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Elem.StringForm), false);
				if (Tag.IsValid()) Container.AddTag(Tag);
			}
			*static_cast<FGameplayTagContainer*>(ValuePtr) = Container;
			return true;
		}
	}
	if (const FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
	{
		FScriptArrayHelper Helper(ArrProp, ValuePtr);
		Helper.EmptyValues();
		bool bAllWrote = true;
		for (const FQuestDataValue& Elem : Value.Elements)
		{
			const int32 Idx = Helper.AddValue();
			bAllWrote &= RestoreQuestCell(ArrProp->Inner, Helper.GetRawPtr(Idx), Elem);
		}
		return bAllWrote;
	}
	if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
	{
		FScriptSetHelper Helper(SetProp, ValuePtr);
		Helper.EmptyElements();
		bool bAllWrote = true;
		for (const FQuestDataValue& Elem : Value.Elements)
		{
			const int32 Idx = Helper.AddDefaultValue_Invalid_NeedsRehash();
			bAllWrote &= RestoreQuestCell(SetProp->ElementProp, Helper.GetElementPtr(Idx), Elem);
		}
		Helper.Rehash();
		return bAllWrote;
	}
	// Unexpected destination for an Array Kind - leave default (defensive; a structured provider shouldn't emit this).
	return false;
}

// Apply every cell in Row to Target's matching UPROPERTY by column name. Skips structural columns (key/class/graph)
// and any column with no matching property (defensive - a stale table column shouldn't abort the import).
void RestoreQuestRowProperties(UObject* Target, const FQuestDataRow& Row)
{
	for (const TPair<FString, FQuestDataValue>& Cell : Row.Cells)
	{
		const FString& Col = Cell.Key;
		if (Col == TEXT("class") || Col == TEXT("graph")) continue;
		FProperty* Prop = Target->GetClass()->FindPropertyByName(FName(*Col));
		if (!Prop) continue;
		// RestoreQuestCell types the value against the property (structured providers write typed fields directly; string-
		// carrying Kinds route through ImportText on Scalar) - see the switch(Kind) in RestoreQuestCell.
		RestoreQuestCell(Prop, Prop->ContainerPtrToValuePtr<void>(Target), Cell.Value);
	}
}

// Resolve a class named by a bundle cell. The file carries SHORT names deliberately - they are what a studio reads and
// diffs, and qualifying them would trade the format's legibility for the reader's convenience. But UClass::TryFindTypeSlow
// captures AND SYMBOLICATES a ten-frame stack walk for every short name it is handed (CoreUObject's deprecation nudge),
// which on a corpus import is a per-row cost rather than a one-off. Try the qualified form against the packages our own
// types live in first; anything else - an adopter's module, a Blueprint-generated "<Name>_C", a full /Game/... path -
// falls through to exactly the behaviour that was there before.
UClass* ResolveQuestBundleClass(const FString& ClassName)
{
	if (ClassName.IsEmpty()) return nullptr;

	if (!FPackageName::IsShortPackageName(ClassName))
	{
		if (UClass* Direct = FindObject<UClass>(nullptr, *ClassName)) return Direct;
		return LoadObject<UClass>(nullptr, *ClassName);
	}

	static const TCHAR* ScriptPackages[] = { TEXT("/Script/SimpleQuest"), TEXT("/Script/SimpleQuestEditor"), TEXT("/Script/SimpleCore") };
	for (const TCHAR* Package : ScriptPackages)
	{
		if (UClass* Found = FindObject<UClass>(nullptr, *FString::Printf(TEXT("%s.%s"), Package, *ClassName))) return Found;
	}

	if (UClass* Found = UClass::TryFindTypeSlow<UClass>(ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous)) return Found;
	return LoadObject<UClass>(nullptr, *ClassName);
}

// Re-express an incoming cell in the SAME typed form a live property produces, so the two can be compared meaningfully.
// A text provider hands us Kind::String for a tag, enum or reference property, while the node's current value builds as
// Kind::Tag / Kind::Enum / Kind::Reference. Comparing those directly would mark every property changed on every import.
// Routing the cell through the real restore path onto scratch memory and rebuilding it from the property normalizes both
// sides through one definition, whatever representation the provider happened to use.
FQuestDataValue TypeQuestCellLikeProperty(const FProperty* Prop, const FQuestDataValue& Cell, const void* SeedPtr)
{
	void* Scratch = FMemory::Malloc(Prop->GetSize(), Prop->GetMinAlignment());
	Prop->InitializeValue(Scratch);
	// Start from what the apply step would start from. RestoreQuestCell's non-writing arms leave the seed in place, which is
	// what makes an absent cell mean "unchanged" rather than "reset to zero" - and zero is not even the right default,
	// since a property with an in-class initializer is non-zero on a freshly constructed object.
	// CopyCompleteValue, not CopySingleValue: the buffer is sized at GetSize(), which includes ArrayDim.
	if (SeedPtr) { Prop->CopyCompleteValue(Scratch, SeedPtr); }
	RestoreQuestCell(Prop, Scratch, Cell);
	// Null default: we want the value the source would actually produce, never collapsed to Empty for being at-default.
	FQuestDataValue Typed = BuildQuestDataValue(Prop, Scratch, nullptr);
	Prop->DestroyValue(Scratch);
	FMemory::Free(Scratch);
	return Typed;
}

void BuildQuestPropertyIndexByAuthoredName(const UStruct* Layout, TMap<FString, FProperty*>& OutByName)
{
	if (!Layout) { return; }
	for (TFieldIterator<FProperty> It(Layout); It; ++It)
	{
		OutByName.Add(Layout->GetAuthoredNameForField(*It), *It);
	}
}

