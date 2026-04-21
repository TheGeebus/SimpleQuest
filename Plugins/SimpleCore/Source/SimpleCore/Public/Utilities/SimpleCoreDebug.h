// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#if !UE_BUILD_SHIPPING

#include "Utilities/SimpleCoreLog.h"

#define LOG_IT_BABY(Color, DisplayString, ...) \
do { \
const FString _SQMsg = FString::Printf(TEXT("[%s] : ") DisplayString, UTF8_TO_TCHAR(__FUNCTION__) __VA_OPT__(,) __VA_ARGS__); \
UE_LOG(LogSimpleCore, Warning, TEXT("%s"), *_SQMsg); \
if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.f, Color, _SQMsg); \
} while(0)

#define LOG_IT_TICK(Color, DisplayString, ...) \
do { \
const FString _SQMsg = FString::Printf(TEXT("[%s] : ") DisplayString, UTF8_TO_TCHAR(__FUNCTION__) __VA_OPT__(,) __VA_ARGS__); \
UE_LOG(LogSimpleCore, Warning, TEXT("%s"), *_SQMsg); \
if (GEngine) GEngine->AddOnScreenDebugMessage((int32)HashCombine(GetTypeHash(FString(TEXT(__FILE__))), __LINE__), -1.f, Color, _SQMsg); \
} while(0)

#define LOG_IT_FOREVER(Color, DisplayString, ...) \
do { \
const FString _SQMsg = FString::Printf(TEXT("[%s] : ") DisplayString, UTF8_TO_TCHAR(__FUNCTION__) __VA_OPT__(,) __VA_ARGS__); \
UE_LOG(LogSimpleCore, Warning, TEXT("%s"), *_SQMsg); \
if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 999999.f, Color, _SQMsg); \
} while(0)

#else

#define LOG_IT_BABY(Color, DisplayString, ...)
#define LOG_IT_TICK(Color, DisplayString, ...)
#define LOG_IT_FOREVER(Color, DisplayString, ...)

#endif
