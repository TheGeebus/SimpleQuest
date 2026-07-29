// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestReflectionUtils.h"

bool IsAuthoredConfigProperty(const FProperty* Prop)
{
#if WITH_EDITOR
	const bool bOptedIn = Prop->HasMetaData(TEXT("QuestExport"));
#else
	const bool bOptedIn = false;
#endif
	if (!Prop->HasAnyPropertyFlags(CPF_Edit) && !bOptedIn) return false;
	return !Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst);
}

TArray<FName> GetAuthoredPropertyNames(const UClass* Class)
{
	TArray<FName> Names;
	if (!Class) return Names;
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		if (IsAuthoredConfigProperty(*It)) Names.Add(It->GetFName());
	}
	return Names;
}