// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Settings/SimpleCoreSettings.h"
#include "Utilities/SimpleCoreLog.h"

namespace
{
	ELogVerbosity::Type ToELogVerbosity(ESimpleCoreLogVerbosity Verbosity)
	{
		switch (Verbosity)
		{
		case ESimpleCoreLogVerbosity::NoLogging:   return ELogVerbosity::NoLogging;
		case ESimpleCoreLogVerbosity::Fatal:       return ELogVerbosity::Fatal;
		case ESimpleCoreLogVerbosity::Error:       return ELogVerbosity::Error;
		case ESimpleCoreLogVerbosity::Warning:     return ELogVerbosity::Warning;
		case ESimpleCoreLogVerbosity::Display:     return ELogVerbosity::Display;
		case ESimpleCoreLogVerbosity::Log:         return ELogVerbosity::Log;
		case ESimpleCoreLogVerbosity::Verbose:     return ELogVerbosity::Verbose;
		case ESimpleCoreLogVerbosity::VeryVerbose: return ELogVerbosity::VeryVerbose;
		default:                                   return ELogVerbosity::Log;
		}
	}
}

void USimpleCoreSettings::ApplyLogVerbosity() const
{
	LogSimpleCore.SetVerbosity(ToELogVerbosity(LogSimpleCoreVerbosity));
}

#if WITH_EDITOR
void USimpleCoreSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyLogVerbosity();
}
#endif

