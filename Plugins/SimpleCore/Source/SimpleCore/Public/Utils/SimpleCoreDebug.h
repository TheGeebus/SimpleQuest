#pragma once

#define LOG_IT_BABY(Color, DisplayString, ...) \
UE_LOG(LogSimpleQuest, Warning, TEXT("[%s] : ") DisplayString, UTF8_TO_TCHAR(__FUNCTION__) __VA_OPT__(,) __VA_ARGS__); \
{ if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.f, Color, *FString::Printf(TEXT("[%s] : ") DisplayString, UTF8_TO_TCHAR(__FUNCTION__) __VA_OPT__(,) __VA_ARGS__)); }

#define LOG_IT_TICK(Color, DisplayString, ...) \
UE_LOG(LogSimpleQuest, Warning, TEXT("[%s] : ") DisplayString, UTF8_TO_TCHAR(__FUNCTION__) __VA_OPT__(,) __VA_ARGS__); \
{ if (GEngine) GEngine->AddOnScreenDebugMessage(__LINE__, -1.f, Color, *FString::Printf(TEXT("[%s] : ") DisplayString, UTF8_TO_TCHAR(__FUNCTION__) __VA_OPT__(,) __VA_ARGS__)); }

#define LOG_IT_FOREVER(Color, DisplayString, ...) \
UE_LOG(LogSimpleQuest, Warning, TEXT("[%s] : ") DisplayString, UTF8_TO_TCHAR(__FUNCTION__) __VA_OPT__(,) __VA_ARGS__); \
{ if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 999999.f, Color, *FString::Printf(TEXT("[%s] : ") DisplayString, UTF8_TO_TCHAR(__FUNCTION__) __VA_OPT__(,) __VA_ARGS__)); }

