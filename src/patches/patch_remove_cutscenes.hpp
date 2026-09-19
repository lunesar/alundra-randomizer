#pragma once

#include "game_patch.hpp"
#include "../constants/flags.hpp"
#include "../constants/map_codes.hpp"

class PatchRemoveCutscenes : public GamePatch {
public:
    void alter_game_data(GameData& game_data) override
    {
        // This skips the need to talk twice to the master sage, given that he pretty much says the same thing
        // when you talk again to validate...
        game_data.add_starting_flag(FLAG_LARS_CRYPT_TALKED_WITH_MASTER_SAGE_ONCE);
    }

    void alter_datas_file(BinaryFile& data, const GameData& game_data, const RandomizerWorld& world) override
    {
        remove_cutscene_after_dark_dragon_fight(data);
        remove_post_coal_mine_cutscenes(data);
        remove_magyscar_entrance_cutscene(data);
        remove_lars_crypt_5_sages_cutscene(data);
        remove_post_wilda_night_cutscene(data);
        remove_post_nirude_cutscene(data);
        skip_nirude_moai_cutscenes(data);
    }

private:
    static void remove_cutscene_after_dark_dragon_fight(BinaryFile& data)
    {
        // Remove cutscene between Dark Dragon and Melzas 1
        constexpr uint32_t WARP_TO_SANCTUARY_CUTSCENE_ADDR = 0x47C9E15;
        constexpr uint32_t WARP_TO_MELZAS_1_ADDR = 0x67C51C7;
        ByteArray warp_to_melzas_1_instr =  data.get_bytes(WARP_TO_MELZAS_1_ADDR, WARP_TO_MELZAS_1_ADDR + 8);
        data.set_bytes(WARP_TO_SANCTUARY_CUTSCENE_ADDR, warp_to_melzas_1_instr);

        // Get rid of the HUD removal on this map change
        data.set_bytes(0x47C9E12, { 0x02, 0x03, 0x00 }); // Always branch 3 bytes forward
    }

    static void remove_post_coal_mine_cutscenes(BinaryFile& data)
    {
        // Remove "Murggs running in woods" cutscene in Overworld A2
        data.set_bytes(0x13F339, { 0x02, 0x0D, 0x00 }); // Always branch 0xD bytes forward

        // Remove the watchtower cutscene in Overworld A1
        data.set_bytes(0x105DB4, { 0x02, 0x7C, 0x00 }); // Always branch 0x7C bytes forward

        // Overworld B2 B[9]: meatballs fall when the player steps on the back-exit
        // tiles (3,20), which is also the portal into map 329. Vanilla then sets
        // flag 0x021D so B[10] keeps the boulders there. Skipping only the FlagOn
        // (below) makes them vanish after a map reload, but the fall still plays
        // every visit. Skip the whole scene so the back exit stays usable.
        data.set_bytes(0x2459AC, { 0x02, 0x5B, 0x00, 0x01, 0x01 });

        // Remove flag set on meatballs fall so they disappear on map exit + re-enter
        // if the scene above is ever reached anyway.
        data.set_bytes(0x245A04, { 0x02, 0x03, 0x00 });

        // Big rocks disappearing initially requires 2 flags:
        //  - Meatballs fall flag
        //  - Watchtower cutscene flag
        // Remove the meatballs flag requirement for big rocks disappearance
        data.set_byte(0x245A0B, 0x47); // Shorten the branch to not ignore the second branch condition anymore

        // Change the watchtower requirement to a "beat Coal Mine boss" requirement for big rocks disappearance
        data.set_word_le(0x245A50, FLAG_COAL_MINE_BOSS_BEATEN.event_code());

        // Change the watchtower requirement to a "beat Coal Mine boss" requirement for villagers cutscene to trigger
        // in Overworld B2
        data.set_word_le(0x2458A1, FLAG_COAL_MINE_BOSS_BEATEN.event_code());

        // Map 329 (Murgg reward room) replays the Zazan scene whenever
        // FLAG_COAL_MINE_BOSS_BEATEN is already on. Re-entering after leaving
        // without the chest hangs that scene. Disable the re-entry cutscene
        // (check unused flag 0x030E instead) and set the boss-beaten flag on
        // first entry so overworld rocks still disappear.
        data.set_byte(0x48DB631, 0x0E);
        data.set_byte(0x48DB755, 0x0E);
        data.set_bytes(0x48DB5C6, { 0x05, 0x0D, 0x03, 0x40, 0x02, 0x00, 0xFF, 0x01, 0x01 });
    }

    /**
     * Sometimes, a cutscene with villagers praying is triggered at Magyscar entrance.
     * This function removes that cutscene.
     */
    static void remove_magyscar_entrance_cutscene(BinaryFile& data)
    {
        // Replace "branch if flag triggering cutscene not set" by "always branch" so the cutscene never triggers
        data.set_bytes(0x2122C20, { 0x02, 0x17, 0x00 });
        data.set_bytes(0x2122C38, { 0x02, 0x2F, 0x00 });
    }

    static void remove_lars_crypt_5_sages_cutscene(BinaryFile& data)
    {
        // Open the door when master sage has been talked to, instead of after seeing the full cutscene
        // This will remove the cutscene where all the sages appear one by one
        data.set_word_le(0x63AC96, FLAG_LARS_CRYPT_SAGES_CUTSCENE_ACTIVE.event_code());
    }

    /**
     * In the vanilla game, defeating Wilda teleports you to a nighttime variant of Inoa where Bergus got kidnapped.
     * We need the bossfight to teleport us to our regular bedroom instead.
     */
    static void remove_post_wilda_night_cutscene(BinaryFile& data)
    {
        // Change the warp destination map
        data.set_word_le(0x4817A36, MAP_JESS_HOUSE_RANDOMIZER);

        // Unfreeze Alundra so he doesn't get stuck in cutscene mode (since he is meant to be unfrozen by another
        // cutscene in nighttime bedroom)
        data.set_byte(0x4817A02, 0x11);
    }

    static void remove_post_nirude_cutscene(BinaryFile& data)
    {
        // TODO: Map 17 has no portals into Nirude interiors (421-426); map 439 does.
        // Switching the E1 variant to 17 here closes the dungeon. Logic still treats
        // every nirude_lair chest as collectable, so a required item left inside
        // makes the seed unwinnable. Keep variant 439 (or copy those portals onto
        // map 17) if we want re-entry after the boss.
        ByteArray event_bytes;

        event_bytes.add_byte(0x38);                 // Set map variant
        event_bytes.add_word_le(MAP_OVERWORLD_E1);  // for Nirude exterior
        event_bytes.add_word_le(MAP_OVERWORLD_E1);  // to basic (broken) Nirude exterior instead of "dungeon in progress" variant

        event_bytes.add_byte(0x53);                  // Warp
        event_bytes.add_word_le(MAP_OVERWORLD_E1);   // to Nirude exterior
        event_bytes.add_bytes({ 0x19, 0x28, 0x00 }); // at coords 19, 28, 0

        // Trigger this event sequence on boss death
        data.set_bytes(0x47E7B57, event_bytes);
    }

    // Skip Overworld E1 Moai cinematics on maps 17 and 439.
    // STATUES_VULNERABLE must still be set here (as the skipped cinematic did).
    // Setting it at new game is what idles the statues too early; after 0x04DD
    // (first lair entry) is the vanilla timing.
    static void skip_nirude_moai_cutscenes(BinaryFile& data)
    {
        // B[13] top-left Miming cutscene: "if SAW_TOP_LEFT on, goto end" → always goto end.
        data.set_bytes(0x547548, { 0x02, 0xE4, 0x00 });
        data.set_bytes(0x5F3C554, { 0x02, 0xE4, 0x00 });

        // B[14] still waits for 0x04DD (entered the lair), then sets motion/seen
        // flags plus STATUES_VULNERABLE so the mouths can take damage.
        ByteArray flags_and_end;
        flags_and_end.add_byte(0x05);
        flags_and_end.add_word_le(0x04CF);
        flags_and_end.add_byte(0x05);
        flags_and_end.add_word_le(0x04D1);
        flags_and_end.add_byte(0x05);
        flags_and_end.add_word_le(0x04D2);
        flags_and_end.add_byte(0x05);
        flags_and_end.add_word_le(FLAG_NIRUDE_SAW_TOP_LEFT_CUTSCENE.event_code());
        flags_and_end.add_byte(0x05);
        flags_and_end.add_word_le(FLAG_SAW_NIRUDE_STATUES_ACTIVATION_CUTSCENE.event_code());
        flags_and_end.add_byte(0x05);
        flags_and_end.add_word_le(FLAG_NIRUDE_STATUES_VULNERABLE.event_code());
        flags_and_end.add_byte(0xFF);
        data.set_bytes(0x547635, flags_and_end);
        data.set_bytes(0x5F3C641, flags_and_end);
    }
};
