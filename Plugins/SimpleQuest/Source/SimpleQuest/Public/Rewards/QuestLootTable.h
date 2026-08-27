#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Rewards/QuestLootDataTable.h"
#include "QuestLootTable.generated.h"

/**
 * DEPRECATED - superseded by UQuestLootDataTable, which is an actual DataTable. This held its rows in an array on a
 * data asset, so loot could only be edited one row at a time in the details panel: no row editor, no CSV round-trip,
 * and invisible to the data resolver.
 *
 * It survives one release so existing assets keep loading and existing questlines keep working untouched.
 * ULootTableReward reads one only when no Loot Table is set, and warns when it does. The graph compiler also warns at
 * compile time, once per reward still pointing here, so finding what is left to convert is a compile rather than a
 * search. Removed in 0.9, along with that fallback.
 *
 * Deliberately NOT UCLASS(Deprecated), which is the obvious move and the wrong one. That specifier forces any property
 * referencing the class to be deprecated too, and a deprecated property is READ but never WRITTEN - ShouldSerializeValue
 * skips CPF_Deprecated whenever the archive is saving. A questline holding a legacy reference would load it correctly
 * and then lose it the next time it was saved, which happens on every compile. The specifier means "this data is going
 * away"; this has to keep working for one release, and those are not the same thing.
 */
UCLASS(BlueprintType)
class SIMPLEQUEST_API UQuestLootTable : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Loot")
	TArray<FQuestLootEntry> Entries;
};