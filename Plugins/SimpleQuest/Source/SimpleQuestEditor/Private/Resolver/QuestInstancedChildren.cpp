// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestInstancedChildren.h"

#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestRowRestore.h"
#include "UObject/UnrealType.h"


const FQuestDataRow* FindQuestChildRow(const FQuestDataBundle& Bundle, const FString& ChildKey, FString& OutClass)
{
	for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
	{
		for (const FQuestDataRow& R : TablePair.Value.Rows)
		{
			if (R.Key == ChildKey) { OutClass = R.Get(TEXT("class")); return &R; }
		}
	}
	return nullptr;
}

// Rebuild one instanced child object from its row: NewObject<class> under Owner, restore its cells, recurse
// its own instanced properties. Returns the constructed object (or null if the row/class is missing).
static UObject* BuildChildObject(UObject* Owner, const FString& ChildKey, const FQuestDataBundle& Bundle, TSet<FString>& OutConsumed, TArray<FString>& OutWarnings)
{
	FString ClassName;
	const FQuestDataRow* Row = FindQuestChildRow(Bundle, ChildKey, ClassName);
	if (!Row) { OutWarnings.Add(FString::Printf(TEXT("child row missing for key '%s'"), *ChildKey)); return nullptr; }

	UClass* Class = ResolveQuestBundleClass(ClassName);
	if (!Class)
	{
		OutWarnings.Add(FString::Printf(TEXT("could not resolve child class '%s' for key '%s'"), *ClassName, *ChildKey));
		return nullptr;
	}

	UObject* Sub = NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional);
	RestoreQuestRowProperties(Sub, *Row);
	OutConsumed.Add(ChildKey);
	ReattachQuestInstancedChildren(Sub, ChildKey, Bundle, OutConsumed, OutWarnings);   // a child could itself nest
	return Sub;
}

// Parse a child key's LAST path segment: "<owner>/<prop>[<pos>]" or "<owner>/<prop>[<mapkey>].<sub>[<pos>]".
// Returns the leaf property name + the bracketed token; the middle path is already resolved because we arrive here
// via the property walk, not by parsing the whole path (D2's smaller-parser property).
static bool SplitLeafSegment(const FString& ChildKey, FString& OutBracketToken)
{
	int32 OpenIdx;
	if (!ChildKey.FindLastChar(TEXT('['), OpenIdx)) return false;
	int32 CloseIdx;
	if (!ChildKey.FindLastChar(TEXT(']'), CloseIdx) || CloseIdx < OpenIdx) return false;
	OutBracketToken = ChildKey.Mid(OpenIdx + 1, CloseIdx - OpenIdx - 1);
	return true;
}

/**
 * Every child row sitting directly under Prefix, ordered by its bracketed index parsed as a NUMBER. Both callers order
 * a reward GRANT SEQUENCE - one for a node's Rewards, one for the array nested inside each QuestlineRewards entry - so
 * this being one function rather than two copies is what stops the nested case drifting from the flat one.
 * Numeric, never lexical: keys are text, and as text "[10]" sorts before "[1]" (']' is 0x5D, '0' is 0x30), which puts
 * the whole teens block in front of the single digits.
 */
static void GatherIndexedChildKeys(const FQuestDataBundle& Bundle, const FString& Prefix, TArray<TPair<int32, FString>>& Out)
{
	const FString Open = Prefix + TEXT("[");
	for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
		for (const FQuestDataRow& R : TablePair.Value.Rows)
			if (R.Key.StartsWith(Open))
			{
				FString Tok;
				SplitLeafSegment(R.Key, Tok);
				Out.Add({ FCString::Atoi(*Tok), R.Key });
			}
	Out.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B){ return A.Key < B.Key; });
}

void ReattachQuestInstancedChildren(UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle, TSet<FString>& OutConsumed, TArray<FString>& OutWarnings)
{
	for (TFieldIterator<FProperty> It(Owner->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		// Only authored instanced-bearing properties produced child rows on export (same filter shape).
		if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst)) continue;

		const FString PropPrefix = FString::Printf(TEXT("%s/%s"), *OwnerKey, *Prop->GetName());

		// Array of instanced objects: rebuild elements in [i] order (children whose key starts with "<owner>/<prop>[").
		if (FArrayProperty* Arr = CastField<FArrayProperty>(Prop))
		{
			FObjectProperty* InnerObj = CastField<FObjectProperty>(Arr->Inner);
			if (!InnerObj || !Arr->Inner->HasAnyPropertyFlags(CPF_InstancedReference)) continue;

			TArray<TPair<int32, FString>> Indexed;
			GatherIndexedChildKeys(Bundle, PropPrefix, Indexed);

			// SILENCE IS NOT AN ASSERTION OF EMPTINESS. A source that declares no children for this property has said
			// nothing about it - the same contract a missing scalar cell carries, where RestoreQuestCell's Empty arm leaves the
			// constructed value alone. Clearing here would make silence destructive: restoring onto an owner that already
			// holds authored children would discard them for no reason the source ever gave. Declaring children still
			// replaces the contents wholesale, which is the source stating what they are.
			if (Indexed.IsEmpty()) continue;

			FScriptArrayHelper Helper(Arr, Prop->ContainerPtrToValuePtr<void>(Owner));
			Helper.EmptyValues();
			for (const TPair<int32, FString>& Pair : Indexed)
			{
				const int32 NewIdx = Helper.AddValue();
				if (UObject* Child = BuildChildObject(Owner, Pair.Value, Bundle, OutConsumed, OutWarnings))
					InnerObj->SetObjectPropertyValue(Helper.GetRawPtr(NewIdx), Child);
			}
			continue;
		}

		// Map<key, struct-wrapping-instanced-array>: the QuestlineRewards shape. Rebuild by re-adding each map
		// entry (key parsed from the child path's map segment) then recursing the struct's inner array.
		if (FMapProperty* Map = CastField<FMapProperty>(Prop))
		{
			// Child keys look like "<owner>/QuestlineRewards[<mapkey>].Rewards[i]". Group by the <mapkey> segment.
			TSet<FString> MapKeyTokens;
			const FString MapOpen = PropPrefix + TEXT("[");
			for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
				for (const FQuestDataRow& R : TablePair.Value.Rows)
					if (R.Key.StartsWith(MapOpen))
					{
						// extract the FIRST bracket token (the map key), which the export wrote as ExportTextItem(key).
						int32 Open, Close;
						R.Key.FindChar(TEXT('['), Open);
						R.Key.FindChar(TEXT(']'), Close);
						if (Close > Open) MapKeyTokens.Add(R.Key.Mid(Open + 1, Close - Open - 1));
					}

			if (MapKeyTokens.IsEmpty()) continue;   // see the array case: an unmentioned property is not an empty one

			FScriptMapHelper Helper(Map, Prop->ContainerPtrToValuePtr<void>(Owner));
			Helper.EmptyValues();
			for (const FString& KeyTok : MapKeyTokens)
			{
				const int32 Pair = Helper.AddDefaultValue_Invalid_NeedsRehash();
				// Import the map KEY from its exported text (e.g. a FGameplayTag struct literal).
				Map->KeyProp->ImportText_Direct(*KeyTok, Helper.GetKeyPtr(Pair), nullptr, PPF_None);
				// Recurse the VALUE struct's instanced array. The value's "owner key" for the recursion is the
				// full "<owner>/QuestlineRewards[<keytok>]" prefix so its inner Rewards[i] children resolve.
				const FString ValueOwnerKey = FString::Printf(TEXT("%s[%s]"), *PropPrefix, *KeyTok);
				// The struct value isn't a UObject, so recurse its FStructProperty fields inline:
				if (FStructProperty* ValStruct = CastField<FStructProperty>(Map->ValueProp))
				{
					for (TFieldIterator<FProperty> SIt(ValStruct->Struct); SIt; ++SIt)
					{
						if (FArrayProperty* InnerArr = CastField<FArrayProperty>(*SIt))
						{
							FObjectProperty* InnerObj = CastField<FObjectProperty>(InnerArr->Inner);
							if (!InnerObj || !InnerArr->Inner->HasAnyPropertyFlags(CPF_InstancedReference)) continue;

							const FString ArrPrefix = FString::Printf(TEXT("%s.%s"), *ValueOwnerKey, *SIt->GetName());
							TArray<TPair<int32, FString>> Indexed;
							GatherIndexedChildKeys(Bundle, ArrPrefix, Indexed);

							if (Indexed.IsEmpty()) continue;   // see the array case

							FScriptArrayHelper AH(InnerArr, SIt->ContainerPtrToValuePtr<void>(Helper.GetValuePtr(Pair)));
							AH.EmptyValues();
							for (const TPair<int32, FString>& P : Indexed)
							{
								const int32 NewIdx = AH.AddValue();
								if (UObject* Child = BuildChildObject(Owner, P.Value, Bundle, OutConsumed, OutWarnings))
									InnerObj->SetObjectPropertyValue(AH.GetRawPtr(NewIdx), Child);
							}
						}
					}
				}
			}
			Helper.Rehash();
			continue;
		}
	}
}

