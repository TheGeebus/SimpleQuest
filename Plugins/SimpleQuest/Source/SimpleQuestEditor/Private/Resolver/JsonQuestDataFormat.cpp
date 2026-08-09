// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/JsonQuestDataFormat.h"

#include "Resolver/QuestDataBundle.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Internationalization/Text.h"
#include "SimpleQuestLog.h"

namespace
{
	// ---- WRITE: render a structured FQuestDataValue to native JSON (from TYPED FIELDS, never CanonicalText) ----

	TSharedPtr<FJsonValue> RenderValueToJson(const FQuestDataValue& V)
	{
		switch (V.Kind)
		{
		case EQuestDataValueKind::Empty:
			return nullptr;   // caller OMITS the key — JSON-natural absence

		case EQuestDataValueKind::Tag:
			return MakeShared<FJsonValueString>(V.Tag.ToString());

		case EQuestDataValueKind::TagContainer:
		{
			TArray<TSharedPtr<FJsonValue>> Tags;
			for (const FGameplayTag& Tag : V.TagContainer)
			{
				Tags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			}
			return MakeShared<FJsonValueArray>(Tags);
		}

		case EQuestDataValueKind::Text:
		{
			// Object { ns?, key?, src } — preserve loc identity (ns+key = the NSLOCTEXT identity the census flagged as
			// must-survive). src alone is a lossy downgrade. Emit ns/key only when present (FromString texts have none).
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			const TOptional<FString> Ns = FTextInspector::GetNamespace(V.Text);
			const TOptional<FString> Key = FTextInspector::GetKey(V.Text);
			const FString* Src = FTextInspector::GetSourceString(V.Text);
			if (Ns.IsSet())  Obj->SetStringField(TEXT("ns"),  Ns.GetValue());
			if (Key.IsSet()) Obj->SetStringField(TEXT("key"), Key.GetValue());
			Obj->SetStringField(TEXT("src"), Src ? *Src : FString());
			return MakeShared<FJsonValueObject>(Obj);
		}

		case EQuestDataValueKind::Bool:
			return MakeShared<FJsonValueBoolean>(V.bBool);

		case EQuestDataValueKind::Array:
		{
			TArray<TSharedPtr<FJsonValue>> Elems;
			for (const FQuestDataValue& Elem : V.Elements)
			{
				TSharedPtr<FJsonValue> ElemJson = RenderValueToJson(Elem);
				// Array position is significant — an Empty element holds its slot as json null (unlike an omitted key).
				Elems.Add(ElemJson.IsValid() ? ElemJson : MakeShared<FJsonValueNull>());
			}
			return MakeShared<FJsonValueArray>(Elems);
		}

		case EQuestDataValueKind::Number:
			// Native JSON number (unquoted), but BYTE-PRESERVING: FJsonValueNumberString is typed EJson::Number yet
			// serializes its stored string verbatim (PreferStringRepresentation -> WriteRawJSONValue), so we never round-
			// trip through a lossy double. "42" -> 42, "42.5" -> 42.5, exact. StringForm holds the numeric text.
			return MakeShared<FJsonValueNumberString>(V.StringForm);

		case EQuestDataValueKind::String:
		case EQuestDataValueKind::Enum:
		case EQuestDataValueKind::Reference:
		case EQuestDataValueKind::StructLiteral:
		default:
			return MakeShared<FJsonValueString>(V.StringForm);
		}
	}

	// One row -> a JSON object { "key": <rowkey>, "cells": { <col>: <value>, ... } }. Cells whose value renders null
	// (Kind==Empty) are OMITTED (JSON-natural absence; import leaves the property at its default).
	TSharedPtr<FJsonObject> RenderRow(const FQuestDataRow& Row)
	{
		TSharedPtr<FJsonObject> RowObj = MakeShared<FJsonObject>();
		RowObj->SetStringField(TEXT("key"), Row.Key);

		TSharedPtr<FJsonObject> Cells = MakeShared<FJsonObject>();
		// Deterministic key order: sort column names.
		TArray<FString> Cols;
		Row.Cells.GetKeys(Cols);
		Cols.Sort();
		for (const FString& Col : Cols)
		{
			const TSharedPtr<FJsonValue> CellJson = RenderValueToJson(Row.Cells[Col]);
			if (CellJson.IsValid())   // skip Empty (null) cells
			{
				Cells->SetField(Col, CellJson);
			}
		}
		RowObj->SetObjectField(TEXT("cells"), Cells);
		return RowObj;
	}

	// ---- READ: parse native JSON -> structured FQuestDataValue (Kind proposed shallow; routing types against property) ----

	// Read a string field HONESTLY: verify the field exists AND its value is genuinely an EJson::String, rather than
	// relying on TryGetStringField's lenient coercion (which would silently stringify a number/bool from malformed or
	// hand-edited JSON). Returns false + warns on a wrong-typed or missing field; leaves Out untouched then.
	bool GetStringFieldChecked(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, FString& Out, const TCHAR* Context)
	{
		const TSharedPtr<FJsonValue> Val = Obj->TryGetField(Field);
		if (!Val.IsValid())
		{
			return false;   // absent — caller decides if that's fatal (often it's a legitimately optional field)
		}
		if (Val->Type != EJson::String)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("JsonQuestDataFormat: field '%s' in %s is not a string (EJson=%d) — ignored."),
				Field, Context, (int32)Val->Type);
			return false;
		}
		Out = Val->AsString();
		return true;
	}

	FQuestDataValue ParseValueFromJson(const TSharedPtr<FJsonValue>& Json)
	{
		FQuestDataValue V;
		if (!Json.IsValid() || Json->IsNull())
		{
			return V;   // Kind::Empty
		}

		// Dispatch on the ACTUAL JSON type, NOT the TryGet* coercions: FJsonValueString::TryGetBool/TryGetNumber return
		// TRUE for ANY string (lenient ToBool coercion), so a "which TryGet succeeds" cascade misfiles every string cell
		// as Bool. EJson::Type is the honest discriminator.
		switch (Json->Type)
		{
		case EJson::Boolean:
		{
			V.Kind = EQuestDataValueKind::Bool;
			V.bBool = Json->AsBool();
			return V;
		}
		case EJson::Array:
		{
			// EVERY array -> Kind=Array (dumb — no type guess). The destination property decides array-vs-tag-container
			// at restore time (RestoreQuestCell's array arm branches on FGameplayTagContainer vs FArray/FSetProperty).
			V.Kind = EQuestDataValueKind::Array;
			for (const TSharedPtr<FJsonValue>& Elem : Json->AsArray())
			{
				V.Elements.Add(ParseValueFromJson(Elem));
			}
			return V;
		}
		case EJson::Object:
		{
			// An object with "src" = an FText { ns?, key?, src }. Reassemble via the NSLOCTEXT buffer + ReadFromBuffer.
			const TSharedPtr<FJsonObject>& Obj = Json->AsObject();
			FString Src, Ns, Key;
			Obj->TryGetStringField(TEXT("src"), Src);
			Obj->TryGetStringField(TEXT("ns"),  Ns);
			Obj->TryGetStringField(TEXT("key"), Key);

			V.Kind = EQuestDataValueKind::Text;
			FString Buffer;
			if (!Ns.IsEmpty() || !Key.IsEmpty())
			{
				Buffer = FString::Printf(TEXT("NSLOCTEXT(\"%s\", \"%s\", \"%s\")"), *Ns, *Key, *Src.ReplaceCharWithEscapedChar());
			}
			else
			{
				Buffer = FString::Printf(TEXT("\"%s\""), *Src.ReplaceCharWithEscapedChar());
			}
			const TCHAR* BufPtr = *Buffer;
			FText Parsed;
			if (FTextStringHelper::ReadFromBuffer(BufPtr, Parsed))
			{
				V.Text = Parsed;
			}
			else
			{
				V.Text = FText::FromString(Src);
			}
			return V;
		}
		case EJson::Number:
			{
				// A native JSON number -> Kind::Number, carrying its EXACT digits as the string form (TryGetString on a
				// number value returns the source text; AsString does the same). We do NOT read AsNumber() -> that would
				// force a double and risk precision loss. The property types it on import via ImportText, same as String.
				V.Kind = EQuestDataValueKind::Number;
				Json->TryGetString(V.StringForm);
				return V;
			}
		case EJson::String:
			{
				// Bare string -> Kind::String. The property types it: tags (bare dotted form), soft-refs, enum tokens, plain
				// strings, struct-literals all ImportText fine.
				V.Kind = EQuestDataValueKind::String;
				V.StringForm = Json->AsString();
				return V;
			}
		default:
			return V;   // Kind::Empty (Null/None/unrecognized)
		}
		return V;   // Kind::Empty (unrecognized)
	}
}

bool FJsonQuestDataFormat::WriteBundle(const FQuestDataBundle& Bundle, const FString& DestFolder)
{
	IFileManager::Get().MakeDirectory(*DestFolder, /*Tree*/ true);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	// tablesByType: { "<stem>": { "columns": [...], "rows": [ {key, cells}, ... ] } }  — stems sorted for determinism.
	TSharedPtr<FJsonObject> TablesObj = MakeShared<FJsonObject>();
	TArray<FString> Stems;
	Bundle.TablesByType.GetKeys(Stems);
	Stems.Sort();
	for (const FString& Stem : Stems)
	{
		const FQuestDataTable& Table = Bundle.TablesByType[Stem];
		TSharedPtr<FJsonObject> TableObj = MakeShared<FJsonObject>();

		TArray<TSharedPtr<FJsonValue>> ColsJson;
		for (const FString& Col : Table.Columns)
		{
			ColsJson.Add(MakeShared<FJsonValueString>(Col));
		}
		TableObj->SetArrayField(TEXT("columns"), ColsJson);

		// Rows sorted by key for determinism (mirrors the TSV provider).
		TArray<FQuestDataRow> SortedRows = Table.Rows;
		SortedRows.Sort([](const FQuestDataRow& A, const FQuestDataRow& B) { return A.Key < B.Key; });
		TArray<TSharedPtr<FJsonValue>> RowsJson;
		for (const FQuestDataRow& Row : SortedRows)
		{
			RowsJson.Add(MakeShared<FJsonValueObject>(RenderRow(Row)));
		}
		TableObj->SetArrayField(TEXT("rows"), RowsJson);

		TablesObj->SetObjectField(Stem, TableObj);
	}
	Root->SetObjectField(TEXT("tablesByType"), TablesObj);

	// edges: [ {from, type, to}, ... ] — sorted for determinism (mirror the TSV edge sort).
	TArray<FQuestDataEdge> SortedEdges = Bundle.Edges;
	SortedEdges.Sort([](const FQuestDataEdge& A, const FQuestDataEdge& B)
	{
		if (A.From != B.From) return A.From < B.From;
		if (A.Type != B.Type) return A.Type < B.Type;
		return A.To < B.To;
	});
	TArray<TSharedPtr<FJsonValue>> EdgesJson;
	for (const FQuestDataEdge& E : SortedEdges)
	{
		TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
		EdgeObj->SetStringField(TEXT("from"), E.From);
		EdgeObj->SetStringField(TEXT("type"), E.Type);
		EdgeObj->SetStringField(TEXT("to"),   E.To);
		EdgesJson.Add(MakeShared<FJsonValueObject>(EdgeObj));
	}
	Root->SetArrayField(TEXT("edges"), EdgesJson);

	Root->SetNumberField(TEXT("knotsCollapsed"), Bundle.KnotsCollapsed);

	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("JsonQuestDataFormat: failed to serialize bundle."));
		return false;
	}

	const FString Path = DestFolder / TEXT("questline.json");
	if (!FFileHelper::SaveStringToFile(Out, *Path))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("JsonQuestDataFormat: failed to write '%s'."), *Path);
		return false;
	}
	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("JsonQuestDataFormat: wrote '%s'."), *Path);
	return true;
}

bool FJsonQuestDataFormat::ReadBundle(const FString& SrcFolder, FQuestDataBundle& OutBundle)
{
	const FString Path = SrcFolder / TEXT("questline.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("JsonQuestDataFormat: could not read '%s'."), *Path);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("JsonQuestDataFormat: failed to parse '%s'."), *Path);
		return false;
	}

	// tablesByType -> TablesByType
	const TSharedPtr<FJsonObject>* TablesObj = nullptr;
	if (Root->TryGetObjectField(TEXT("tablesByType"), TablesObj) && TablesObj && TablesObj->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& TablePair : (*TablesObj)->Values)
		{
			const TSharedPtr<FJsonObject>* TableObj = nullptr;
			if (!TablePair.Value->TryGetObject(TableObj) || !TableObj || !TableObj->IsValid()) continue;

			FQuestDataTable Table;
			const TArray<TSharedPtr<FJsonValue>>* ColsJson = nullptr;
			if ((*TableObj)->TryGetArrayField(TEXT("columns"), ColsJson) && ColsJson)
			{
				for (const TSharedPtr<FJsonValue>& C : *ColsJson)
				{
					if (C.IsValid() && C->Type == EJson::String) Table.Columns.Add(C->AsString());
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* RowsJson = nullptr;
			if ((*TableObj)->TryGetArrayField(TEXT("rows"), RowsJson) && RowsJson)
			{
				for (const TSharedPtr<FJsonValue>& R : *RowsJson)
				{
					const TSharedPtr<FJsonObject>* RowObj = nullptr;
					if (!R->TryGetObject(RowObj) || !RowObj || !RowObj->IsValid()) continue;

					FQuestDataRow Row;
					GetStringFieldChecked(*RowObj, TEXT("key"), Row.Key, TEXT("row"));

					const TSharedPtr<FJsonObject>* CellsObj = nullptr;
					if ((*RowObj)->TryGetObjectField(TEXT("cells"), CellsObj) && CellsObj && CellsObj->IsValid())
					{
						for (const TPair<FString, TSharedPtr<FJsonValue>>& CellPair : (*CellsObj)->Values)
						{
							Row.Cells.Add(CellPair.Key, ParseValueFromJson(CellPair.Value));
						}
					}
					Table.Rows.Add(MoveTemp(Row));
				}
			}
			OutBundle.TablesByType.Add(TablePair.Key, MoveTemp(Table));
		}
	}

	// edges
	const TArray<TSharedPtr<FJsonValue>>* EdgesJson = nullptr;
	if (Root->TryGetArrayField(TEXT("edges"), EdgesJson) && EdgesJson)
	{
		for (const TSharedPtr<FJsonValue>& E : *EdgesJson)
		{
			const TSharedPtr<FJsonObject>* EdgeObj = nullptr;
			if (!E->TryGetObject(EdgeObj) || !EdgeObj || !EdgeObj->IsValid()) continue;
			FQuestDataEdge Edge;
			GetStringFieldChecked(*EdgeObj, TEXT("from"), Edge.From, TEXT("edge"));
			GetStringFieldChecked(*EdgeObj, TEXT("type"), Edge.Type, TEXT("edge"));
			GetStringFieldChecked(*EdgeObj, TEXT("to"),   Edge.To,   TEXT("edge"));
			OutBundle.Edges.Add(Edge);
		}
	}

	// knotsCollapsed is a provenance stat; verify it's a number if present, default 0 otherwise (never fatal).
	{
		const TSharedPtr<FJsonValue> KnotsVal = Root->TryGetField(TEXT("knotsCollapsed"));
		OutBundle.KnotsCollapsed = (KnotsVal.IsValid() && KnotsVal->Type == EJson::Number)
			? (int32)KnotsVal->AsNumber() : 0;
	}
	return true;
}