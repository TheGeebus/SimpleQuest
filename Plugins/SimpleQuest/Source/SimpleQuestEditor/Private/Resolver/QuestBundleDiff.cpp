// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestBundleDiff.h"

#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestDataValueBuilder.h"
#include "Resolver/QuestInstancedChildren.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Resolver/QuestRowRestore.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"


/** One-line human-readable form of a cell, for the plan's before/after columns. */
static FString DescribeValue(const FQuestDataValue& Value)
{
	switch (Value.Kind)
	{
	case EQuestDataValueKind::Empty:        return TEXT("<default>");
	case EQuestDataValueKind::Tag:          return Value.Tag.IsValid() ? Value.Tag.ToString() : TEXT("<none>");
	case EQuestDataValueKind::TagContainer: return Value.TagContainer.ToStringSimple();
	case EQuestDataValueKind::Text:         return Value.Text.ToString();
	case EQuestDataValueKind::Bool:         return Value.bBool ? TEXT("true") : TEXT("false");
	case EQuestDataValueKind::Array:
	{
		TArray<FString> Parts;
		for (const FQuestDataValue& Element : Value.Elements) Parts.Add(DescribeValue(Element));
		return FString::Printf(TEXT("[%s]"), *FString::Join(Parts, TEXT(", ")));
	}
	default: return Value.StringForm;
	}
}

void DiffQuestContainerAgainstRow(const UStruct* Layout, const void* Container, const FQuestDataRow& Row,
						  const FString& PathPrefix, FQuestNodePlanEntry& Entry, FQuestInPlacePlan& OutPlan,
						  const FQuestAbsentPolicyResolver& Policies)
{
	if (!Layout || !Container) return;

	// Only materialized if a Reset policy actually fires, which most rows never do.
	TUniquePtr<FStructOnScope> StructDefaults;
	TMap<FString, FProperty*> PropByAuthoredName;
	BuildQuestPropertyIndexByAuthoredName(Layout, PropByAuthoredName);

	for (const TPair<FString, FQuestDataValue>& Cell : Row.Cells)
	{
		const FString& Column = Cell.Key;
		// Structural - describes the row, not a property. "index" is the child's position in its owner's array: real
		// meaning, but not a property OF the child, and diffing it as one would report a reorder as a value edit.
		if (Column == TEXT("class") || Column == TEXT("graph") || Column == TEXT("index")) continue;

		FProperty* Prop = PropByAuthoredName.FindRef(Column);
		if (!Prop)
		{
			// Only a column that actually CARRIES something is worth reporting as unmatched. Columns are declared for a
			// whole table, so an unbound bookkeeping column would otherwise warn once per ROW instead of once.
			if (Cell.Value.Kind != EQuestDataValueKind::Empty)
			{
				OutPlan.Warnings.Add(FString::Printf(TEXT("row '%s' column '%s' matches no property on %s - it would be ignored"),
					*Row.Key,
					*Column,
					*Layout->GetName()));
			}
			continue;
		}

		const void* LivePtr = Prop->ContainerPtrToValuePtr<void>(Container);

		// An ABSENT cell is the only place policy applies. The source declared the column and left it blank, which is a
		// statement about the default - but which default, and whether blank is permitted at all, is the recipe's call.
		// A cell that CARRIES a value overwrites the seed regardless, so policy never reaches it.
		const bool bAbsent = (Cell.Value.Kind == EQuestDataValueKind::Empty);
		const EQuestAbsentFieldPolicy Policy = bAbsent ? Policies.Resolve(FName(*Column)) : EQuestAbsentFieldPolicy::Preserve;
		if (bAbsent && Policy == EQuestAbsentFieldPolicy::Require)
		{
			OutPlan.Refusals.Add(FString::Printf(TEXT("row '%s' leaves '%s' blank, and the recipe requires a value for it"),
				*Row.Key, *Column));
			continue;
		}

		// THE SEED IS THE POLICY. From the live value, an absent cell changes nothing; from the class default, it resets.
		// The comparison below is byte-for-byte the same either way - only its starting point moves.
		const void* SeedPtr = LivePtr;
		if (bAbsent && Policy == EQuestAbsentFieldPolicy::Reset)
		{
			// Defaults come from the layout the property was RESOLVED on, not the one passed in - a property offset is
			// only meaningful against the layout that declares it. The two layout kinds differ in exactly one way and
			// nowhere else: a class HAS a default object to read from, a struct has to be constructed to produce one.
			if (const UClass* OwnerClass = Prop->GetOwnerClass())
			{
				SeedPtr = Prop->ContainerPtrToValuePtr<void>(OwnerClass->GetDefaultObject());
			}
			else if (const UScriptStruct* OwnerStruct = Cast<UScriptStruct>(Prop->GetOwnerStruct()))
			{
				// Rebuilt per use rather than cached across the loop: properties on one row struct can be declared by
				// different owners up an inheritance chain, and a single cached instance would quietly serve the wrong
				// layout to the second one. It outlives SeedPtr's use below, which is all it has to do.
				StructDefaults = MakeUnique<FStructOnScope>(OwnerStruct);
				SeedPtr = Prop->ContainerPtrToValuePtr<void>(StructDefaults->GetStructMemory());
			}
		}

		// Compare through the PROPERTY, not the neutral value: FProperty::Identical knows each type's own equality, where
		// a value-level comparison must pick one rule for a Kind that deliberately fuses several types.
		void* Scratch = FMemory::Malloc(Prop->GetSize(), Prop->GetMinAlignment());
		Prop->InitializeValue(Scratch);
		Prop->CopyCompleteValue(Scratch, SeedPtr);
		RestoreQuestCell(Prop, Scratch, Cell.Value);
		const bool bIdentical = Prop->Identical(Scratch, LivePtr, PPF_None);
		const FQuestDataValue Incoming = BuildQuestDataValue(Prop, Scratch, nullptr);
		Prop->DestroyValue(Scratch);
		FMemory::Free(Scratch);
		if (bIdentical) continue;

		// A reset says WHY the value is what it is, not just what it is. Without that, "the source blanked this field"
		// and "the policy is resetting it to a default that happens to be blank" print identically - and they are
		// different decisions a designer might want to question. The VALUE is untouched; only its description changes.
		const FString Described = DescribeValue(Incoming);
		const bool bResetToDefault = bAbsent && Policy == EQuestAbsentFieldPolicy::Reset;

		FQuestPropertyChange Change;
		Change.Property      = PathPrefix.IsEmpty() ? Column : (PathPrefix + TEXT(".") + Column);
		Change.CurrentText   = DescribeValue(BuildQuestDataValue(Prop, LivePtr, nullptr));
		Change.IncomingText  = !bResetToDefault ? Described
							 : (Described.IsEmpty() ? FString(TEXT("<default>"))
													: FString::Printf(TEXT("<default> (%s)"), *Described));
		Change.IncomingValue = Incoming;   // what apply writes - never re-derived from the row
		Entry.Changes.Add(MoveTemp(Change));
	}
}

void DiffQuestObjectAgainstRow(const UObject* Object, const FQuestDataRow& Row, const FString& PathPrefix,
						  FQuestNodePlanEntry& Entry, FQuestInPlacePlan& OutPlan, const FQuestAbsentPolicyResolver& Policies)
{
	// A UObject IS its own container - the property offsets are relative to the object pointer, exactly as they are
	// relative to a struct's memory. Only the layout has to be named separately.
	DiffQuestContainerAgainstRow(Object->GetClass(), Object, Row, PathPrefix, Entry, OutPlan, Policies);
}

/**
 * True when the bundle describes anything under this property - a row keyed exactly "<owner>/<prop>" (a direct instanced
 * object) or "<owner>/<prop>[..." (a container). This is the SAME rule the restore path applies before it touches a
 * property: a source declaring no children has said nothing about it, so the property is left alone. The plan has to
 * agree, or it describes an apply that will not happen.
 */
static bool BundleDeclaresChildrenUnder(const FQuestDataBundle& Bundle, const FString& PropPrefix)
{
	const FString Indexed = PropPrefix + TEXT("[");
	for (const TPair<FString, FQuestDataTable>& Table : Bundle.TablesByType)
	{
		for (const FQuestDataRow& Row : Table.Value.Rows)
		{
			if (Row.Key == PropPrefix || Row.Key.StartsWith(Indexed)) return true;
		}
	}
	return false;
}

void DiffQuestInstancedChildren(const UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle,
                           FQuestNodePlanEntry& Entry, FQuestInPlacePlan& OutPlan, const FQuestAbsentPolicyResolver& Policies)
{
	for (TFieldIterator<FProperty> It(Owner->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		// The same filter that decided what became a child row on the way out.
		if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst)) continue;
		if (!IsQuestInstancedBearing(Prop)) continue;

		const FString PropPrefix = FString::Printf(TEXT("%s/%s"), *OwnerKey, *Prop->GetName());
		if (!BundleDeclaresChildrenUnder(Bundle, PropPrefix)) continue;   // silence is not emptiness

		TSet<FString> LiveKeys;
		ForEachQuestInstancedChild(Prop, Prop->ContainerPtrToValuePtr<void>(Owner), OwnerKey, Prop->GetName(),
		[&](const FString& ChildKey, const FString& Path, const UObject* Child, int32 ArrayOrdinal)
	        {
	            LiveKeys.Add(ChildKey);

	            FString ChildClass;
	            const FQuestDataRow* ChildRow = FindQuestChildRow(Bundle, ChildKey, ChildClass);
	            if (!ChildRow)
	            {
	                // The source DOES describe this property's contents, and this child is not among them.
	                FQuestPropertyChange Change;
	                Change.Property     = Path;
	                Change.CurrentText  = Child->GetClass()->GetName();
	                Change.Kind         = EQuestPropertyChangeKind::ChildRemoved;
	                Change.IncomingText = TEXT("<removed>");
	                Entry.Changes.Add(MoveTemp(Change));
	                return;
	            }
	            DiffQuestObjectAgainstRow(Child, *ChildRow, Path, Entry, OutPlan, Policies);
	            DiffQuestInstancedChildren(Child, ChildKey, Bundle, Entry, OutPlan, Policies);   // a child can itself nest
	       });

		// Rows under this property with no live counterpart are additions.
		const FString Indexed = PropPrefix + TEXT("[");
		for (const TPair<FString, FQuestDataTable>& Table : Bundle.TablesByType)
		{
			for (const FQuestDataRow& Row : Table.Value.Rows)
			{
				if (Row.Key != PropPrefix && !Row.Key.StartsWith(Indexed)) continue;
				if (LiveKeys.Contains(Row.Key)) continue;
				// DIRECT children only - a grandchild's key carries a further '/' segment, and belongs to its own owner.
				if (Row.Key.RightChop(OwnerKey.Len() + 1).Contains(TEXT("/"))) continue;

				FQuestPropertyChange Change;
				Change.Property     = Row.Key.RightChop(OwnerKey.Len() + 1);
				Change.Kind         = EQuestPropertyChangeKind::ChildAdded;
				Change.CurrentText  = TEXT("<absent>");
				Change.IncomingText = Row.Get(TEXT("class"));
				Entry.Changes.Add(MoveTemp(Change));
			}
		}
	}
}

