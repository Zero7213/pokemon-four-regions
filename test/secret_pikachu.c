#include "global.h"
#include "battle.h"
#include "data.h"
#include "event_data.h"
#include "load_save.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "script_pokemon_util.h"
#include "test/test.h"

TEST("Secret Pikachu: boxed layout remains save compatible")
{
    EXPECT_EQ(sizeof(struct PokemonSubstruct0), 12);
    EXPECT_EQ(sizeof(struct BoxPokemon), 80);
    EXPECT_EQ(sizeof(struct Pokemon), 100);
}

TEST("Secret Pikachu: final stats double after nature and EVs, without compounding")
{
    struct Pokemon ordinary, secret, newlyCreated;
    u32 nature = 0, level = 5, i, j, value;
    for (i = 0; i < NUM_NATURES; i++)
        for (j = 5; j <= 100; j += 95)
            PARAMETRIZE { nature = i; level = j; }

    u32 personality = GetMonPersonality(SPECIES_PIKACHU, MON_MALE, nature, RANDOM_UNOWN_LETTER);
    CreateMonWithIVs(&ordinary, SPECIES_PIKACHU, level, personality, OTID_STRUCT_PLAYER_ID, 31);
    value = 84;
    for (i = 0; i < NUM_STATS; i++)
        SetMonData(&ordinary, MON_DATA_HP_EV + i, &value);
    CalculateMonStats(&ordinary);
    secret = ordinary;
    value = UNIQUE_MON_SECRET_PIKACHU;
    SetMonData(&secret, MON_DATA_UNIQUE_ID, &value);
    for (u32 repeat = 0; repeat < 3; repeat++)
    {
        CalculateMonStats(&secret);
        for (i = 0; i < NUM_STATS; i++)
            EXPECT_EQ(GetMonData(&secret, MON_DATA_MAX_HP + i), 2 * GetMonData(&ordinary, MON_DATA_MAX_HP + i));
    }
    EXPECT(IsSecretPikachu(&secret));
    EXPECT(!IsSecretPikachu(&ordinary));
    // Identical PID/OT/IVs do not confer the identity on another Pikachu (or offspring).
    CreateMonWithIVs(&newlyCreated, SPECIES_PIKACHU, level, personality, OTID_STRUCT_PLAYER_ID, 31);
    EXPECT(!IsSecretPikachu(&newlyCreated));
    EXPECT_EQ(GetMonData(&newlyCreated, MON_DATA_UNIQUE_ID), UNIQUE_MON_NONE);
}

TEST("Secret Pikachu: damage, fainting, leveling, healing and PC round trips")
{
    struct Pokemon mon, restored;
    u32 value, hp, maxHP;
    CreateRandomMonWithIVs(&mon, SPECIES_PIKACHU, 5, 31);
    value = UNIQUE_MON_SECRET_PIKACHU;
    SetMonData(&mon, MON_DATA_UNIQUE_ID, &value);
    CalculateMonStats(&mon);
    maxHP = GetMonData(&mon, MON_DATA_MAX_HP);
    hp = maxHP - 7;
    SetMonData(&mon, MON_DATA_HP, &hp);
    value = gExperienceTables[gSpeciesInfo[SPECIES_PIKACHU].growthRate][20];
    SetMonData(&mon, MON_DATA_EXP, &value);
    CalculateMonStats(&mon);
    EXPECT_EQ(GetMonData(&mon, MON_DATA_HP), GetMonData(&mon, MON_DATA_MAX_HP) - 7);
    SetBoxMonAt(0, 0, &mon.box);
    BoxMonAtToMon(0, 0, &restored);
    EXPECT(IsSecretPikachu(&restored));
    EXPECT_EQ(GetMonData(&restored, MON_DATA_HP), GetMonData(&mon, MON_DATA_HP));
    EXPECT_EQ(GetMonData(&restored, MON_DATA_ATK), GetMonData(&mon, MON_DATA_ATK));
    // Recalculating a fainted mon must update boxed HP loss when its maximum grows.
    hp = 0;
    SetMonData(&mon, MON_DATA_HP, &hp);
    value = gExperienceTables[gSpeciesInfo[SPECIES_PIKACHU].growthRate][30];
    SetMonData(&mon, MON_DATA_EXP, &value);
    CalculateMonStats(&mon);
    SetBoxMonAt(0, 0, &mon.box);
    BoxMonAtToMon(0, 0, &restored);
    EXPECT_EQ(GetMonData(&restored, MON_DATA_HP), 0);
    EXPECT_EQ(GetMonData(&restored, MON_DATA_HP_LOST), GetMonData(&restored, MON_DATA_MAX_HP));
    HealPokemon(&restored);
    EXPECT(IsSecretPikachu(&restored));
    EXPECT_EQ(GetMonData(&restored, MON_DATA_HP), GetMonData(&restored, MON_DATA_MAX_HP));
    // Lower maximum (e.g. EV/stat recalculation) must clamp HP, never underflow.
    value = gExperienceTables[gSpeciesInfo[SPECIES_PIKACHU].growthRate][5];
    SetMonData(&restored, MON_DATA_EXP, &value);
    CalculateMonStats(&restored);
    EXPECT_EQ(GetMonData(&restored, MON_DATA_HP), GetMonData(&restored, MON_DATA_MAX_HP));
    EXPECT_EQ(GetMonData(&restored, MON_DATA_HP_LOST), 0);
    // A boxed recalculation can reduce max HP below its previously stored damage.
    // Withdrawal must clamp that damage rather than wrap unsigned current HP.
    value = gExperienceTables[gSpeciesInfo[SPECIES_PIKACHU].growthRate][30];
    SetMonData(&mon, MON_DATA_EXP, &value);
    CalculateMonStats(&mon);
    hp = 1;
    SetMonData(&mon, MON_DATA_HP, &hp);
    SetBoxMonAt(0, 0, &mon.box);
    value = gExperienceTables[gSpeciesInfo[SPECIES_PIKACHU].growthRate][5];
    SetBoxMonData(GetBoxedMonPtr(0, 0), MON_DATA_EXP, &value);
    BoxMonAtToMon(0, 0, &restored);
    EXPECT_EQ(GetMonData(&restored, MON_DATA_HP), 0);
    EXPECT_LE(GetMonData(&restored, MON_DATA_HP), GetMonData(&restored, MON_DATA_MAX_HP));
    HealBoxPokemon(GetBoxedMonPtr(0, 0));
    BoxMonAtToMon(0, 0, &restored);
    EXPECT(IsSecretPikachu(&restored));
    EXPECT_EQ(GetMonData(&restored, MON_DATA_HP), GetMonData(&restored, MON_DATA_MAX_HP));
    ZeroBoxMonAt(0, 0);
}

#if IS_FRLG
TEST("Secret Pikachu: gift gates, IVs, once-per-save flag and party save serialization")
{
    u32 i;
    struct Pokemon ordinary;
    ZeroPlayerPartyMons();
    FlagClear(FLAG_RECEIVED_SECRET_PIKACHU);
    VarSet(VAR_KANTO_PLAYER_STARTER_SPECIES, SPECIES_NONE);
    TryGiveSecretPikachu();
    EXPECT_EQ(gSpecialVar_Result, MON_CANT_GIVE);
    EXPECT(!FlagGet(FLAG_RECEIVED_SECRET_PIKACHU));

    VarSet(VAR_KANTO_PLAYER_STARTER_SPECIES, SPECIES_BULBASAUR);
    for (i = 0; i < PARTY_SIZE; i++)
        CreateRandomMon(&gParties[B_TRAINER_PLAYER][i], SPECIES_BULBASAUR, 5);
    TryGiveSecretPikachu();
    EXPECT_EQ(gSpecialVar_Result, MON_CANT_GIVE);
    EXPECT(!FlagGet(FLAG_RECEIVED_SECRET_PIKACHU));
    ZeroMonData(&gParties[B_TRAINER_PLAYER][5]);
    TryGiveSecretPikachu();
    EXPECT_EQ(gSpecialVar_Result, MON_GIVEN_TO_PARTY);
    EXPECT(FlagGet(FLAG_RECEIVED_SECRET_PIKACHU));
    EXPECT(IsSecretPikachu(&gParties[B_TRAINER_PLAYER][5]));
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_LEVEL), 5);
    EXPECT_EQ(GetNature(&gParties[B_TRAINER_PLAYER][5]), NATURE_HARDY);
    CreateRandomMonWithIVs(&ordinary, SPECIES_PIKACHU, 5, 31);
    EXPECT_NE(GetMonData(&ordinary, MON_DATA_MOVE1), MOVE_NONE);
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_MOVE1 + i), GetMonData(&ordinary, MON_DATA_MOVE1 + i));
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_PP1 + i), GetMonData(&ordinary, MON_DATA_PP1 + i));
    }
    for (i = 0; i < NUM_STATS; i++)
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_HP_IV + i), 31);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_MAX_HP), 40);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_ATK), 24);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_DEF), 20);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_SPEED), 30);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_SPATK), 22);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_SPDEF), 22);

    CopyPartyAndObjectsToSave();
    ZeroPlayerPartyMons();
    CopyPartyAndObjectsFromSave();
    EXPECT(IsSecretPikachu(&gParties[B_TRAINER_PLAYER][5]));
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_HP), 40);
    // The gift remains consumed even after the individual leaves the party entirely.
    ZeroPlayerPartyMons();
    TryGiveSecretPikachu();
    EXPECT_EQ(gSpecialVar_Result, MON_CANT_GIVE);
    EXPECT_EQ(CalculatePlayerPartyCount(), 0);
    FlagClear(FLAG_RECEIVED_SECRET_PIKACHU);
    VarSet(VAR_KANTO_PLAYER_STARTER_SPECIES, SPECIES_NONE);
}

TEST("Secret Pikachu: party movement and real flash save/reload in party and PC")
{
    struct Pokemon swap, restored;
    u32 hp = 33;
    ZeroPlayerPartyMons();
    ResetPokemonStorageSystem();
    FlagClear(FLAG_RECEIVED_SECRET_PIKACHU);
    VarSet(VAR_KANTO_PLAYER_STARTER_SPECIES, SPECIES_BULBASAUR);
    CreateRandomMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_BULBASAUR, 5);
    TryGiveSecretPikachu();
    EXPECT(IsSecretPikachu(&gParties[B_TRAINER_PLAYER][1]));
    SetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_HP, &hp);

    // Use the same full-mon copy operation used to move party members.
    CopyMon(&swap, &gParties[B_TRAINER_PLAYER][0], sizeof(swap));
    CopyMon(&gParties[B_TRAINER_PLAYER][0], &gParties[B_TRAINER_PLAYER][1], sizeof(swap));
    CopyMon(&gParties[B_TRAINER_PLAYER][1], &swap, sizeof(swap));
    EXPECT(IsSecretPikachu(&gParties[B_TRAINER_PLAYER][0]));
    EXPECT(!IsSecretPikachu(&gParties[B_TRAINER_PLAYER][1]));

    CheckForFlashMemory();
    EXPECT(gFlashMemoryPresent);
    if (!gFlashMemoryPresent)
        return;
    Save_ResetSaveCounters();
    // Actual flash-sector write/read, not just a RAM copy. Avoid the save-failure UI.
    HandleSavingData(SAVE_NORMAL);
    EXPECT_EQ(gDamagedSaveSectors, 0);
    ZeroPlayerPartyMons();
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    EXPECT_EQ(LoadGameSave(SAVE_NORMAL), SAVE_STATUS_OK);
    CopyPartyAndObjectsFromSave();
    EXPECT(FlagGet(FLAG_RECEIVED_SECRET_PIKACHU));
    EXPECT(IsSecretPikachu(&gParties[B_TRAINER_PLAYER][0]));
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP), hp);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MAX_HP), 40);

    SetBoxMonAt(2, 7, &gParties[B_TRAINER_PLAYER][0].box);
    ZeroPlayerPartyMons();
    HandleSavingData(SAVE_NORMAL);
    EXPECT_EQ(gDamagedSaveSectors, 0);
    memset(gPokemonStoragePtr, 0, sizeof(*gPokemonStoragePtr));
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    EXPECT_EQ(LoadGameSave(SAVE_NORMAL), SAVE_STATUS_OK);
    CopyPartyAndObjectsFromSave();
    EXPECT(IsSecretPikachuBox(GetBoxedMonPtr(2, 7)));
    BoxMonAtToMon(2, 7, &restored);
    ZeroBoxMonAt(2, 7);
    EXPECT_EQ(GiveScriptedMonToPlayer(&restored, PARTY_SIZE), MON_GIVEN_TO_PARTY);
    EXPECT(IsSecretPikachu(&gParties[B_TRAINER_PLAYER][0]));
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP), hp);
    for (u32 i = 0; i < NUM_STATS; i++)
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP_IV + i), 31);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_ATK), 24);
    TryGiveSecretPikachu();
    EXPECT_EQ(gSpecialVar_Result, MON_CANT_GIVE);
    EXPECT_EQ(CalculatePlayerPartyCount(), 1);
}
#endif
