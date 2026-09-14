// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestInstancedChildren.h"

#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestRowRestore.h"
#include "Rewards/QuestRewardBase.h"
#include "StructUtils/InstancedStruct.h"
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

	// IDENTITY COMES FROM THE KEY, exactly as a node's QuestGuid does. The bracket segment IS this reward's GUID, and
	// it is deliberately not a cell - identity addresses the row rather than being data on it - so nothing else would
	// restore it. Without this a rebuilt reward mints a fresh identity and the round trip reports every reward as
	// changed while its behaviour is provably identical.
	// A positional key from a corpus written before identity existed simply fails to parse, leaving the GUID invalid
	// for PreSave to mint - which is the correct migration, not a fallback worth warning about.
	if (UQuestRewardBase* Reward = Cast<UQuestRewardBase>(Sub))
	{
		FString Tok;
		FGuid Parsed;
		if (SplitLeafSegment(ChildKey, Tok) && FGuid::ParseExact(Tok, EGuidFormats::Digits, Parsed))
		{
			Reward->RewardGuid = Parsed;
		}
	}

	RestoreQuestRowProperties(Sub, *Row);
	OutConsumed.Add(ChildKey);
	ReattachQuestInstancedChildren(Sub, ChildKey, Bundle, OutConsumed, OutWarnings);   // a child could itself nest
	return Sub;
}

// Resolve a script struct named by a bundle cell. Short names, same as classes - see ResolveQuestBundleClass for why
// the format carries them and what that costs. Kept separate from the class resolver rather than unified: a row naming
// a type that exists as BOTH would otherwise resolve to whichever lookup ran first.
static UScriptStruct* ResolveQuestBundleStruct(const FString& StructName)
{
	if (StructName.IsEmpty()) return nullptr;
	return FindFirstObject<UScriptStruct>(*StructName, EFindFirstObjectOptions::NativeFirst);
}

/**
 * Rebuild one FInstancedStruct's contents from its row. The struct sibling of BuildChildObject, and simpler: no
 * NewObject, no Outer, no RF flags, and no identity - a struct's contents are addressed entirely by their owner's
 * property path, so the key needs no GUID segment.
 */
static bool BuildChildStruct(FInstancedStruct& OutInstance, const FString& ChildKey, const FQuestDataBundle& Bundle, TSet<FString>& OutConsumed, TArray<FString>& OutWarnings)
{
	FString TypeName;
	const FQuestDataRow* Row = FindQuestChildRow(Bundle, ChildKey, TypeName);
	if (!Row) { OutWarnings.Add(FString::Printf(TEXT("child row missing for key '%s'"), *ChildKey)); return false; }

	// The row was found by key, but FindQuestChildRow reports the "class" cell. A struct row names its type in "struct",
	// so read it directly - and a row carrying neither is malformed rather than empty.
	const FString StructName = Row->Get(TEXT("struct"));
	UScriptStruct* Type = ResolveQuestBundleStruct(StructName);
	if (!Type)
	{
		OutWarnings.Add(FString::Printf(TEXT("could not resolve child struct '%s' for key '%s'"), *StructName, *ChildKey));
		return false;
	}

	OutInstance.InitializeAs(Type);
	RestoreQuestRowProperties(Type, OutInstance.GetMutableMemory(), *Row);
	OutConsumed.Add(ChildKey);
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
	{		
		for (const FQuestDataRow& R : TablePair.Value.Rows)
		{	
			if (R.Key.StartsWith(Open))
			{
				// DIRECTLY under, which this never had to enforce before: nothing nested deeper than one bracketed
				// segment, so StartsWith was sufficient. A decomposed struct's key ("<owner>/Rewards[<guid>]/Payload")
				// also starts with this prefix while being a child OF an element rather than an element - handing it
				// on asks BuildChildObject to resolve a "class" cell that a struct row does not carry.
				const int32 CloseIdx = R.Key.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open.Len());
				if (CloseIdx != R.Key.Len() - 1) continue;

				// ORDER COMES FROM THE index CELL now that the bracket holds identity. Parsing the bracket would be
				// worse than useless here: a GUID starting with digits Atoi's to a large plausible number, so the
				// ordering would look computed and be wrong.
				const FString IndexCell = R.Get(TEXT("index"));
				int32 Ordinal = INDEX_NONE;
				if (!IndexCell.IsEmpty())
				{
					Ordinal = FCString::Atoi(*IndexCell);
				}
				else
				{
					// A corpus written before the ordinal existed keyed by POSITION, so there the bracket is the order.
					FString Tok;
					SplitLeafSegment(R.Key, Tok);
					if (Tok.IsNumeric()) { Ordinal = FCString::Atoi(*Tok); }
				}
				Out.Add({ Ordinal, R.Key });
			}
		}
	}
	Out.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B){ return A.Key < B.Key; });
}

void ReattachQuestInstancedChildren(const UStruct* Layout, void* Container, UObject* Outer, const FString& OwnerKey,
	const FQuestDataBundle& Bundle, TSet<FString>& OutConsumed, TArray<FString>& OutWarnings)
{
	if (!Layout || !Container || !Outer) return;

	for (TFieldIterator<FProperty> It(Layout); It; ++It)
	{
		FProperty* Prop = *It;
		// Only authored instanced-bearing properties produced child rows on export (same filter shape).
		if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst)) continue;

		const FString PropPrefix = FString::Printf(TEXT("%s/%s"), *OwnerKey, *Prop->GetName());
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);

		// An FInstancedStruct property: one child row, keyed by the property path with no bracket segment.
		// THE GUARD BELOW IS ABOUT NOISE, NOT CORRECTNESS - BuildChildStruct already leaves the value untouched when no
		// row exists. What it prevents is a spurious "child row missing" warning on every LEGACY bundle, where the
		// payload arrived as a StructLiteral cell on this owner's row and the absence of a child row is expected
		// rather than wrong - and on every UNSET payload, which the export writes no row for at all.
		if (FStructProperty* AsStruct = CastField<FStructProperty>(Prop))
		{
			if (AsStruct->Struct == FInstancedStruct::StaticStruct())
			{
				FString UnusedClass;
				if (FindQuestChildRow(Bundle, PropPrefix, UnusedClass))
				{
					BuildChildStruct(*static_cast<FInstancedStruct*>(ValuePtr), PropPrefix, Bundle, OutConsumed, OutWarnings);
				}
				continue;
			}
		}

		// Array of instanced objects: rebuild elements in [i] order (children whose key starts with "<owner>/<prop>[").
		if (FArrayProperty* Arr = CastField<FArrayProperty>(Prop))
		{
			FObjectProperty* InnerObj = CastField<FObjectProperty>(Arr->Inner);
			if (!InnerObj || !Arr->Inner->HasAnyPropertyFlags(CPF_InstancedReference)) continue;

			TArray<TPair<int32, FString>> Indexed;
			GatherIndexedChildKeys(Bundle, PropPrefix, Indexed);

			// A corpus written before a reward set had a row of its own keyed the set's rewards straight off the map
			// segment, with a '.' where the set's own key now ends: "<owner>/QuestlineRewards[<key>].Rewards[i]". Read
			// that spelling too, so an older export still restores its inline questline rewards.
			if (Indexed.IsEmpty() && OwnerKey.EndsWith(TEXT("]")))
			{
				GatherIndexedChildKeys(Bundle, FString::Printf(TEXT("%s.%s"), *OwnerKey, *Prop->GetName()), Indexed);
			}

			// SILENCE IS NOT AN ASSERTION OF EMPTINESS. A source that declares no children for this property has said
			// nothing about it - the same contract a missing scalar cell carries, where RestoreQuestCell's Empty arm leaves the
			// constructed value alone. Clearing here would make silence destructive: restoring onto an owner that already
			// holds authored children would discard them for no reason the source ever gave. Declaring children still
			// replaces the contents wholesale, which is the source stating what they are.
			if (Indexed.IsEmpty()) continue;

			FScriptArrayHelper Helper(Arr, ValuePtr);
			Helper.EmptyValues();
			for (const TPair<int32, FString>& Pair : Indexed)
			{
				const int32 NewIdx = Helper.AddValue();
				if (UObject* Child = BuildChildObject(Outer, Pair.Value, Bundle, OutConsumed, OutWarnings))
					InnerObj->SetObjectPropertyValue(Helper.GetRawPtr(NewIdx), Child);
			}
			continue;
		}

		// Map of struct values that carry instanced children: the QuestlineRewards shape. Each entry is a child row of
		// its own, keyed by the map segment, and its inline rewards are children OF THAT ROW - so rebuild the map by
		// re-adding every key the rows name, restore the entry's own row onto the value (its referenced RewardSets),
		// then hand the value to this same walk as the owner of its children. That is the walk a node's Rewards go
		// through, which is what stops the nested case drifting from the flat one.
		if (FMapProperty* Map = CastField<FMapProperty>(Prop))
		{
			FStructProperty* ValStruct = CastField<FStructProperty>(Map->ValueProp);
			if (!ValStruct) continue;   // the only map shape the export writes - see IsQuestInstancedBearing

			// Child keys look like "<owner>/QuestlineRewards[<mapkey>]" and "<owner>/QuestlineRewards[<mapkey>]/Rewards[i]"
			// (or, from an older export, "<owner>/QuestlineRewards[<mapkey>].Rewards[i]"). Group by the <mapkey> segment.
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

			FScriptMapHelper Helper(Map, ValuePtr);
			Helper.EmptyValues();
			for (const FString& KeyTok : MapKeyTokens)
			{
				const int32 Pair = Helper.AddDefaultValue_Invalid_NeedsRehash();
				// Import the map KEY from its exported text (e.g. a FGameplayTag struct literal).
				Map->KeyProp->ImportText_Direct(*KeyTok, Helper.GetKeyPtr(Pair), nullptr, PPF_None);

				const FString EntryKey = FString::Printf(TEXT("%s[%s]"), *PropPrefix, *KeyTok);
				void* EntryValue = Helper.GetValuePtr(Pair);

				// The entry's own row. Absent from an older export, which had no row for the entry because the map had
				// no plain fields worth one - nothing to restore then, and its rewards below still read from the key.
				FString UnusedClass;
				if (const FQuestDataRow* EntryRow = FindQuestChildRow(Bundle, EntryKey, UnusedClass))
				{
					// The property fixes the value's type, so a row naming another is malformed - refuse it rather than
					// let whichever columns happen to share a name land.
					const FString RowStruct = EntryRow->Get(TEXT("struct"));
					if (RowStruct == ValStruct->Struct->GetName())
					{
						RestoreQuestRowProperties(ValStruct->Struct, EntryValue, *EntryRow);
						OutConsumed.Add(EntryKey);
					}
					else
					{
						OutWarnings.Add(FString::Printf(TEXT("row '%s' names struct '%s' where the property holds '%s' - its cells were not restored"),
							*EntryKey, *RowStruct, *ValStruct->Struct->GetName()));
					}
				}
				ReattachQuestInstancedChildren(ValStruct->Struct, EntryValue, Outer, EntryKey, Bundle, OutConsumed, OutWarnings);
			}
			Helper.Rehash();
			continue;
		}
	}
}

void ReattachQuestInstancedChildren(UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle, TSet<FString>& OutConsumed, TArray<FString>& OutWarnings)
{
	if (!Owner) return;
	ReattachQuestInstancedChildren(Owner->GetClass(), Owner, Owner, OwnerKey, Bundle, OutConsumed, OutWarnings);
}

