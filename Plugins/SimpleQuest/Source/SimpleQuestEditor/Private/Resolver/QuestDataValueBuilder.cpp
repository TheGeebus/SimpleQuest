// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestDataValueBuilder.h"

#include "GameplayTagContainer.h"
#include "SimpleQuestLog.h"

FQuestDataValue BuildQuestDataValue(const FProperty* Prop, const void* ValuePtr, const void* DefaultPtr)
{
		if (!Prop || !ValuePtr)
		{
			return FQuestDataValue::MakeEmpty();
		}

		// Q6 — Empty iff live value == CDO-constructed default. DefaultPtr MUST be non-null (a null default makes
		// Identical treat every struct prop as different). Applies to leaves and containers alike.
		if (DefaultPtr && Prop->Identical(ValuePtr, DefaultPtr, PPF_None))
		{
			return FQuestDataValue::MakeEmpty();
		}

		FQuestDataValue V;

		// Enum BEFORE numeric/byte. SimpleQuest's UENUM(uint8) enum-class props are FEnumProperty; the FByteProperty-
		// with-Enum branch only fires for TEnumAsByte / old namespaced enums. Both precede Scalar.
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const FNumericProperty* Under = EnumProp->GetUnderlyingProperty();
			V.EnumValue = Under->GetSignedIntPropertyValue(ValuePtr);
			V.Kind = EQuestDataValueKind::Enum;
			V.StringForm = EnumProp->GetEnum()->GetNameStringByValue(V.EnumValue);
			return V;
		}
		if (const FByteProperty* ByteEnum = CastField<FByteProperty>(Prop); ByteEnum && ByteEnum->Enum)
		{
			V.EnumValue = ByteEnum->GetPropertyValue(ValuePtr);
			V.Kind = EQuestDataValueKind::Enum;
			V.StringForm = ByteEnum->Enum->GetNameStringByValue(V.EnumValue);
			return V;
		}

		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			V.Kind = EQuestDataValueKind::Bool;
			V.bBool = BoolProp->GetPropertyValue(ValuePtr);
			return V;
		}

		if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
		{
			V.Kind = EQuestDataValueKind::Text;
			V.Text = TextProp->GetPropertyValue(ValuePtr);   // a real FText (loc namespace/key preserved)
			return V;
		}

		// Struct sub-cases BEFORE the generic FStructProperty -> StructLiteral fallback.
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FGameplayTag>::Get())
			{
				V.Kind = EQuestDataValueKind::Tag;
				V.Tag = *static_cast<const FGameplayTag*>(ValuePtr);
				return V;
			}
			if (StructProp->Struct == TBaseStructure<FGameplayTagContainer>::Get())
			{
				V.Kind = EQuestDataValueKind::TagContainer;
				V.TagContainer = *static_cast<const FGameplayTagContainer*>(ValuePtr);
				return V;
			}
			// Any other struct (incl. FInstancedStruct) -> opaque typed literal (Scalar holds the ExportTextItem string).
			// Guard-LOG (not STOP): surface the opacity once per (struct,prop) so a new adopter struct / bare-struct field
			// is VISIBLE, not silently opaque. Round-trips today via ImportText.
			static TSet<FString> GLoggedStructFallbacks;
			const FString StructKey = FString::Printf(TEXT("%s::%s"), *StructProp->Struct->GetName(), *Prop->GetName());
			if (!GLoggedStructFallbacks.Contains(StructKey))
			{
				GLoggedStructFallbacks.Add(StructKey);
				UE_LOG(LogSimpleQuestResolver, Warning, TEXT("QuestData: StructLiteral fallback for %s (struct '%s') — opaque "
					"to structured providers; structure-me-later candidate."), *StructKey, *StructProp->Struct->GetName());
			}
			V.Kind = EQuestDataValueKind::StructLiteral;
			Prop->ExportTextItem_Direct(V.StringForm, ValuePtr, nullptr, nullptr, PPF_None);
			return V;
		}

		// Soft object OR soft class (FSoftClassProperty : FSoftObjectProperty — the base cast covers both).
		if (const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
		{
			V.Kind = EQuestDataValueKind::Reference;
			V.StringForm = static_cast<const FSoftObjectPtr*>(ValuePtr)->ToString();
			return V;
		}

		// Array of the above.
		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			V.Kind = EQuestDataValueKind::Array;
			FScriptArrayHelper Helper(ArrayProp, ValuePtr);
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				V.Elements.Add(BuildQuestDataValue(ArrayProp->Inner, Helper.GetRawPtr(i), nullptr));
			}
			return V;
		}

		// Set of the above (sparse storage — skip invalid indices, stop once Num() live elements seen).
		if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			V.Kind = EQuestDataValueKind::Array;
			FScriptSetHelper Helper(SetProp, ValuePtr);
			for (int32 i = 0, Seen = 0; Seen < Helper.Num(); ++i)
			{
				if (!Helper.IsValidIndex(i)) continue;
				++Seen;
				V.Elements.Add(BuildQuestDataValue(SetProp->ElementProp, Helper.GetElementPtr(i), nullptr));
			}
			return V;
		}

		// Leaf value — classify Number vs String by the FProperty type (honest, from reflection; no guessing). Numeric =
		// any FNumericProperty (int/int64/float/double/raw-uint8; byte-with-enum already handled above). String = FStr/
		// FName. Both carry their string form in StringForm; the Number/String split lets a structured provider (JSON)
		// render a number natively while TSV renders both as text. Anything else (defensive) falls to String.
		V.Kind = (CastField<FNumericProperty>(Prop) != nullptr) ? EQuestDataValueKind::Number : EQuestDataValueKind::String;
		Prop->ExportTextItem_Direct(V.StringForm, ValuePtr, nullptr, nullptr, PPF_None);
		return V;
}