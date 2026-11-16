// Scenes and the main loop body: title, the wilderness view (the world map walked at 1:1), the combat view
// (the generated terrain strip with enemies), and the game-over screen.
#pragma once
#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "assets.h"
#include "platform.h"
#include "sound.h"
#include "strip.h"
#include "world.h"

namespace dof {

enum { kPageHero = 1, kPageClerical = 2, kPageMagic = 3, kPageUse = 4, kPageTake = 6, kPageDrop = 10, kPageQuest = 13, kPageXp = 16, kPageSettings = 17, kPageEquip = 20, kPageEquipPick = 21, kPageInventory = 22 };   // menu pages

struct JumpSpec { int distance = 112, climb = 48; double speed = 1.0, height_scale = 1.0; };  // speed: sideways speed of the jump as a multiple of the walking speed  // climb: the highest ledge (px) this hero can jump up onto  // updated controls (data/jumps.json)

struct Rules {
    double tick_hz = 18.2065;
    int ticks_per_step = 3;
    int walk_step = 8;
    double fast_walk = 60;   // updated controls: walking speed in px per second (the original's 8 px per 0.165 s is 48)
    JumpSpec jump;           // updated controls: the default jump and the measured ground profile of the original's arc
    std::vector<int> jump_profile{0, 6, 11, 15, 17, 18, 17, 16, 15, 14, 13, 11, 7, 2, 0};
    std::vector<std::pair<std::string, JumpSpec>> hero_jumps;  // by the start of the hero's name
    int start_x = 1400, start_y = 1200;
    std::vector<int> walk_frames{0, 1, 2, 3};
    int blocked_mask = 0x0f, blocked_value = 1;
    // combat (mostly estimates, see data/rules.json)
    int hero_dmg_min = 1, hero_dmg_max = 8, hero_reach = 34, enemy_cooldown = 3, enemy_aggro = 480, encounter_count = 3;
    std::vector<std::string> encounter_types{"msva"};
    std::vector<int> attack_frames{2, 3};
    unsigned seed = 12345;
};

struct PanelSlot { int x, y; };

struct HeroPoses { std::vector<int> jump{18, 19}, ranged{15, 16}, high{12, 13, 14}, low{28, 29, 30}; int crouch = 9, melee = 11; };  // frame numbers in a hero's sheet (data/hero_frames.json)


struct Hero {
    std::string name, sprite;
    int hp = 0, hp_max = 0, clip = 0, armor = 6, xp = 0;
    std::vector<int> abilities;
    Actor actor;
    bool nomad = false;
    int thac0 = 17, to_hit = 0, hand_dice = 1, hand_sides = 8, hand_bonus = 0;
    std::string hand_name = "Longsword", range_name;
    int range_dice = 1, range_sides = 6, range_bonus = 0, ammo = 0, ammo_bonus = 0;   // the ranged weapon, and the quiver (updated controls: shared pack)
    bool ring = false;     // has a Ring of Protection (never given up)
    int ring_bonus = 0;    // how much it improves the armor class
    bool shield = false;   // carries the Shield +1
    bool plate = false;    // wears the plate armor (armor class 3; only Caramon and Sturm)
};

struct ItemStat { std::string slot; std::vector<std::string> heroes; int dice = 1, sides = 8, bonus = 0, shots = 0; };

struct MonsterType {
    std::string id;
    std::string name;
    int xp = 10;
    int hp = 10, dmg_min = 1, dmg_max = 3, speed = 4, reach = 32;
    // the original table (game.exe data 0x2986): hit points = hp_dice x d6 + hp_bonus, damage = d(dmg_sides) - 1 + dmg_bonus
    int ac = 6;
    int slot = 2, hp_dice = 1, hp_bonus = 0, dmg_sides = 4, dmg_bonus = 1, to_hit = 3;
    std::vector<int> walk{0, 1, 2, 3}, attack{4, 5};
    std::vector<int> death;   // the frames of a death on the sheet, standing to lying (the troll's: it rises again)
    Actor actor;
};

struct Wild {  // a creature walking about the wilderness; touching it starts a fight
    int x = 0, y = 0, type = 0, kind = 2, dx = 0, dy = 0, timer = 0, anim = 0, face = 4;
    bool npc = false, seen = false, joins = false;  // people (nomads, Gilthanas) talk or join; the rest are monsters
    int msg = -1, hero = -1;   // msg: what the person says; hero: index of the hero or nomad who joins (type - 20)
    std::string who;           // or a hero by name (Princess Laurana, met on the last leg to Sla-Mori's entrance)
};

struct GroundItem {  // an item lying in the world (data/items_table.json)
    int id = 0, qty = 0, level = 1, x = 0, y = 0, row = 0, in_chest = -1;
    bool taken = false, opened = false;
    bool invisible = false;   // shown (and takeable) only while Detect treasure is in effect
    bool thrown = false;   // an arrow, spear, axe or stone that was fired and came down: picking it up gives one shot back
};

struct Level {  // an interior (levels 2 to 7): cells of 8x8 tiles from utokens/utiles
    int w = 0, h = 0;
    std::vector<int> cells;
};

struct Enemy {
    int type = 0, x = 0, y = 0, hp = 0, anim = 0, cooldown = 0;
    bool left = true, striking = false;
    int frozen = 0;
    int appear = 0;       // steps left of fading in (they come out of the distance: they do nothing until they are solid)
    int venom = 0;        // a Kapak's blade is envenomed for this many more steps
    int rcd = 0, hpmax = 0;   // ranged-attack cooldown, hit points at spawn (trolls regenerate)
    int tag = -1;         // index in ienemies_ while a dungeon fight runs
    int lx = 0, ly = 0;   // position in the level (interior creatures); x is the position along the strip while the fight runs
    int hlx = -1, hly = -1, ax = 0;   // where it started (level px) and that point along the strip: a dragon's head is tied to its body there by the neck
    int refugee = -1;     // a Man met on the road: -1 not decided yet, 1 a refugee (peaceful: press A beside him and he joins), 0 hostile (he is hit, or he was not a refugee)
    bool nocorpse = false, burned = false, revived = false, turned = false;   // turned: frozen by Turn Undead ("Turned" shows over its head)
    //   // revived: a troll that got up again (no experience for it twice)
    //   // drowned (leaves nothing); killed by fire (a troll then stays dead)
};

struct Corpse {   // a creature that has just died: it dithers away; a troll crashes into a green pile and gets up again
    Enemy e;
    int t = 0;    // steps since it died (6 a second)
    bool troll = false;
    int kind = 0;   // how it dies: 0 dithers away, 1 crumbles to dust (Baaz), 2 melts into a puddle of acid (Kapak)
};

struct Npc {  // a person to rescue (Laurana, type 0x1d of the creature table)
    int type = 29, lx = 0, ly = 0, level = 3;
    int kind = 0, variant = 0;   // kind 0: Laurana; 1: a prisoner in a cell (variant picks what she says)
    bool done = false;
};

class Game {
public:
    bool init(const std::string& root);
    // One rendered frame: read input, run as many logic steps as time requires, draw, present.
    // Returns false when the player quits.
    bool frame(Platform& p);
    void set_start(int x, int y) { px_ = x; py_ = y; fix_start(); }
    std::string debug() const;
    Sound& sound() { return sound_; }
    Assets& assets() { return assets_; }

private:
    enum class Mode { Title, World, Combat, GameOver, Menu, Ending, Help, Debug };
    void step(const Input& in);
    void step_combat(const Input& in);
    // A friendly person met on the map: the view switches to the side view, the person dithers in as the party walks towards them, then they speak and join.
    struct Talker { bool active = false, talked = false; int x = 0, hero = -1, msg = -1, appear = 0, wild_x = 0, wild_y = 0; std::string who; } talk_;
    void start_talk(size_t wild_index);
    void step_talk();
    void join_by_index(int idx);
    void join_by_name(const std::string& name);
    const Hero* talker_hero() const;
    bool move_party(const Input& in, int px, bool up_edge, bool down_edge, int jump_in, bool fast);
    void frame_move(const Input& in, double dt_ms);
    bool foe_in_reach(bool face);               // a living foe the leader can hit now (turns to face it)
    bool take_item(int idx, bool from_menu);    // put a ground item in the pack (false if it could not be taken)
    bool open_or_shut(bool open, std::string& msg);   // a door in this cell or (opening only) a chest within reach
    bool context_action();
    int refugees_recruited_ = 0;                      // true if it took something or opened something
    bool snd_prev_sel_ = false, snd_prev_back_ = false;
    bool mouse_live_ = true; int mouse_ax_ = 0, mouse_ay_ = 0;   // menus follow the mouse or the keys / controller, whichever was used last
    // What the party says over a fallen hero (src/farewell.cpp, data/farewells.json): after the fight, one line at a time at the top of the view.
    struct Speech { std::string who, text; int steps = 0; bool action = false; };
    std::deque<Speech> speech_q_; Speech speech_cur_; int speech_steps_ = 0;
    std::set<std::string> mourned_, farewell_ready_; std::map<std::string, std::vector<std::string>> farewell_; std::map<std::string, std::pair<std::string, std::string>> profile_; bool farewell_loaded_ = false;
    void farewell_step();                // notices deaths, waits for the fight to end, then queues the eulogy
    void queue_farewell(const std::string& dead);
    void draw_speech();
    bool any_foes_left() const;
    std::string nearest_town() const;
    bool act_eat_ = false;                       // A was used as "confirm" in a menu: not an attack until it is released
    bool act_used_ = false;                      // controller A with no foe near: take what lies here, else open a door or chest
    std::map<int, ItemStat> istats_;       // data/item_stats.json: who can use an item and what it does
    bool auto_equip(int item_id, std::string& msg, int shots = -1);
    // Equip and Inventory (src/equip.cpp)
    int equip_slot_ = 0;
    int item_id_by_name(const std::string& name) const;
    std::string equip_slot_text(const Hero& h, int slot) const;
    std::vector<int> equip_candidates(const Hero& h, int slot) const;
    bool equip_from_pack(Hero& h, int slot, int inv_index, std::string& msg);
    void step_equip(const Input& in, bool edge, bool up, bool down, bool cancel);
    void draw_equip();
    void draw_inventory();   // updated controls: the item goes to the hero who gains most; false = it stays in the shared pack
    bool item_usable_by(const ItemStat& s, const Hero& h) const;
    HeroPoses poses_default_;
    std::vector<std::pair<std::string, HeroPoses>> poses_;
    const HeroPoses& poses_for(const std::string& sprite) const;
    // Music (updated build): rendered loops played by the page (web/shell.html Module.feMusic); Settings: Enhanced or the original PC speaker tune
    bool music_enhanced_ = true;
    bool music_on_ = true, sfx_on_ = true;   // Settings: music and sound effects each on or off (V still mutes everything)
    std::string music_track_, music_sent_;
    bool music_muted_sent_ = false;
    std::string desired_music() const;
    void update_music();
    void play_title();                     // the original's title tune, only when the enhanced music is off
    void set_music_enhanced(bool on);
    bool pending_start_ = false, gate_told_ = false;
    bool pending_map_ = false, menu_direct_ = false;   // Back: the quest map straight from play; backing out then closes the menu
    long box_since_ = 0; std::string box_shown_; int box_hold_ = 45;   // frame a message first showed; the least frames it stays up
    int box_scroll_ = 0; std::string box_seen_;   // a message longer than the scroll scrolls (up / down, the confirm key pages down, then closes)
    bool act_prev_ = false, pending_attack_ = false;
    int last_mx_ = -1, last_my_ = -1;
    void translate_input(Input& in);   // updated controls: gamepad A and the mouse become attack / take / open / confirm
    void draw_title();
    // title screen (src/title.cpp)
    void step_title(const Input& in);
    void set_title_page(int p);
    void go_title();                  // back to the title menu (after the ending or a lost game)
    void start_new_game();
    void title_load();
    void title_activate(int item);
    uint8_t pal_idx(uint8_t r, uint8_t g, uint8_t b) const;
    void dim_rect(int x, int y, int w, int h, int level);
    void fancy_text(int x, int y, const std::string& s, int scale, int spacing, const std::vector<uint8_t>& grad, uint8_t outline, uint8_t shadow);
    int title_page_ = 0, title_sel_ = 0, title_frames_ = 0, title_page_frames_ = 0, slide_ = 0;   // page: 0 publishers, 1 menu, 2 credits, 3 intro, 4 settings
    // F1: the list of commands (keyboard for the original controls, gamepad for the updated ones); scrolls
    // F10: the debug menu (cheats for testing)
    void step_debug(const Input& in); void draw_debug(); void debug_warp_entrance();
    Mode debug_return_ = Mode::World; int debug_sel_ = 0;
    bool ghost_ = false;                // debug menu: walk through walls and everything else that blocks (places, doors and links still work)
    bool no_encounters_ = false;        // debug menu: no random encounters outdoors (the lairs, the places of interest and the dungeons' creatures stay)
    int debug_scroll_ = 0;
    bool y_exit_any_ = false;           // debug menu: Y (the view switch) leaves any fight at once, as it used to
    int last_engaged_ = -100000;        // the last logic step in which something was fought (a blow struck or taken, a spell, a shot)
    bool run_ = false;                  // debug menu: the party walks five times as fast (only the party)
    int run_factor() const { return run_ ? 5 : 1; }
    void open_help(); void step_help(const Input& in); void draw_help();
    std::vector<std::string> help_lines() const;
    Mode help_return_ = Mode::World; int help_scroll_ = 0; bool help_close_ = false;
    void request_exit();             // leave the game: the exit page in the browser, quit natively
    bool quit_ = false;
    void load_scores(); void save_scores();
    bool quick_talk_ = false;         // DOF_QUICK_TALK: a person speaks on the map at once (older tests)
    bool quick_title_ = false;        // DOF_QUICK_TITLE: any key starts the game at once (the scripted tests)
    void draw_world();
    void draw_combat();
    void draw_panel();
    void draw_gameover();
    void fix_start();
    void enter_world();
    void toggle_view();
    void spawn_encounter();
    void spawn_encounter_at(int count, const std::vector<int>& slots = {});
    void spawn_wild();
    void join_party(const Hero& h);
    void armies_win();
    void equip(Hero& h);
    int str_bonus(const Hero& h) const;
    void spawn_roaming_monsters(int bx, int by);
    int last_bx_ = -999, last_by_ = -999;
    std::string name_buf_;                 // high score name being typed
    bool qualifies_ = false;
    void load_items();
    std::vector<int> items_near(bool chests_only) const;
    std::string item_name(int id) const;
    void cast(int school, int spell);
    void apply_heal(int amount, bool revive);
    void step_wild();
    void enter_level(int n, int cell_x = -1, int cell_y = -1);
    void check_links();
    void army_tick();                       // one step of the dragon armies' advance (game.exe fn 538C)
    void draw_quest_map();
    void load_settings();
    void save_settings();
    void draw_settings();
    void step_settings(const Input& in);
    int ground_y(int x) const;   // top of the first blocking tile below the top of the strip at this x (game.exe fn 672A)
    // interior side view (game.exe fn 54CC): the level's cells along the party's row or column become a strip of 4 block rows
    void build_istrip();
    void draw_interior();
    int istrip_x() const { return iv_ns_ ? (int)levels_[level_].h * 128 - 1 - iy_ : ix_; }
    void set_istrip_x(int sx);
    bool itable_loaded_ = false;
    std::vector<int> itab_ew_, itab_ns_, iexits_, isecret_;
    bool iv_ns_ = false;                   // viewing along a column (north/south) instead of a row
    int istrip_[4][256] = {};
    int istrip_len_ = 0, istart_ = 0, iend_ = 0;
    long istrip_key_ = -1;
    void draw_scroll(const std::string& title, int lines);
    bool interior() const { return level_ >= 2; }
    bool level_can_move(int x, int y, int dx, int dy) const;
    int level_tile(int x, int y) const;
    void step_menu(const Input& in);
    std::vector<int> menu_entries() const;   // the main menu's entries (indices into kMenu; 19 = Controls): fewer with the updated controls
    void draw_menu();
    void draw_box();
    void show_box(int message_index);
    int roll_hp(const MonsterType& t);
    void hit_sound(int id) { play(id); }
    void set_leader(int i);
    void reset_party();
    void save_game();
    void load_game();
    int rnd(int lo, int hi);
    bool leader_alive() const { return party_[leader_].hp > 0; }

    Assets assets_;
    World world_;
    Strip strip_;
    Sound sound_;
    Platform* plat_ = nullptr;
    void play(int id);
    void stop_sound();
    Rules rules_;
    Mode mode_ = Mode::Title;
    Screen screen_;
    Image title_, panel_;
    Palette title_pal_{};
    std::vector<Image> wtiles_, clips_;
    std::vector<Hero> party_;
    std::vector<MonsterType> mtypes_;
    std::vector<Enemy> enemies_;
    std::vector<Wild> wild_;
    std::vector<GroundItem> items_;
    std::vector<std::string> item_names_;
    std::vector<int> inv_;                  // the party's pack: item ids
    bool used_[16] = {false};               // clerical memorized spells spent since the last rest
    int eff_armor(const Hero& h) const;   // the armor class with the shield and the Staff of Magius (+3 protection) counted
    // Points of interest (src/poi.cpp, data/pois.json), Elistan, the scrolls
    struct Poi { std::string name, text, icon; int accent = 15; int level = 1, x = 0, y = 0, heal = 0, xp = 0; std::vector<std::pair<int, int>> guard; std::vector<int> items; bool town = false; };   // guard: (monster slot, how many)
    std::vector<Poi> pois_; bool pois_loaded_ = false; std::set<std::string> poi_done_;
    std::vector<int> poi_reward_; int poi_reward_xp_ = 0; std::string poi_reward_text_;
    int choice_ = 0;                    // a yes / no question on the message box (1: heal Elistan?): A says yes, B no
    int elistan_state_ = 0, elistan_talk_ = 0; bool elistan_near_ = false;
    // A lair is fought in a small cave (a side-scrolling room of dirt-coloured dungeon wall, bounded at both ends); however the party moves inside, it comes out on the very tile of the lair.
    bool lair_active_ = false; int lair_x0_ = 0, lair_x1_ = 0, lair_ground_ = 0;
    int lair_chest_x_ = 0; bool lair_chest_taken_ = false, lair_msg_ = false; std::string lair_poi_name_, poi_block_;   // poi_block_: a lair just left without its treasure does not start again until the party has stepped away from it
    //   // the loot lies in a chest in the cave; the party stays until it has taken it or presses Y
    std::vector<Image> cave_tiles_;
    void draw_lair_background(int cam);
    void draw_lair_minimap();   // top right in a lair: the cave as a bar with the portal, the archway, the chest, the foes and the party
    void draw_lair_map();       // the full-screen version (Back)
    int lair_mid() const { return lair_x0_ + (lair_x1_ - lair_x0_) / 2 + 20; }   // the archway between the two rooms
    void draw_poi_icon(const Poi& p, int sx, int sy);   // the little picture of a place of interest on the overland map
    void load_pois(); void check_pois(); void trigger_poi(Poi& p);
    std::string give_item_text(int id); void start_fight_here(const std::vector<int>& slots, bool lair = false, const std::string& lair_name = ""); void give_poi_reward();
    Image elistan_img_; const Image& elistan_image();
    void elistan_step(const Npc& p); void choice_answer(bool yes); bool use_scroll(int id);
    // The hero is held (Stuck: a Baaz turned to metal with the weapon in it; Paralyzed: a Kapak's venom): he cannot act until it wears off or another hero takes the lead.
    int hold_ = 0, hold_who_ = -1, hold_kind_ = 0;
    bool held() const { return hold_ > 0 && hold_who_ == leader_; }
    void hold_hero(int steps, int kind);
    static constexpr size_t kPackMax = 100000;   // the shared pack holds a great deal now (it held 12, which the lairs' loot overflowed)
    int detect_ = 0;                    // steps left of Detect treasure: invisible items show
    bool dead_told_ = false;
    int elf_cell_ = -1;                 // the cell the elves last looked for secret doors in
    void elves_search();                // the elves of the party (Tanis, Gilthanas, Laurana) may notice a secret door as the party passes
    bool entrance_known_ = false;       // the X for Sla-Mori's entrance shows on the quest map (Laurana told the party, or the party found out)
    bool item_hidden(const GroundItem& g) const { return g.invisible && detect_ <= 0; }
    int disks_ = 100, staff_ = 100, ward_ = 0, ward_kind_ = 0;   // ward_kind_: 1 Protection from Evil, 2 Prayer (for the timer display)
    Mode menu_return_ = Mode::World;
    bool turbo_ = false;                    // tool-assisted mode: no waits (see docs)
    std::vector<Image> figs_, figs0_, figs2_, utiles_;
    int facing_ = 0;   // wilderness figure direction: 0 east/west, 1 north, 2 south
    std::vector<std::array<uint16_t, 64>> utokens_;
    std::vector<Level> levels_;   // index = level number
    std::vector<int> armor_table_, pool_;
    std::vector<std::string> texts_;
    int level_ = 1, ix_ = 0, iy_ = 0;       // current level and the party's position inside an interior
    int menu_item_ = 0, menu_page_ = -1, menu_scroll_ = 0, menu_hero_ = 0;
    std::vector<int> kills_by_type_;
    struct Link { int from, to, mode, flags, x1, y1, x2, y2, dx, dy; };
    std::vector<Link> links_;
    long menu_open_step_ = 0;
    bool cancel_prev_ = false;
    int pending_jump_ = 0;
    // quest map and the dragon armies (data/quest_map.json)
    uint8_t qm_terrain_[480] = {}, qm_armies_[480] = {}, qm_factor_[128] = {};
    int qm_countdown_ = 360, qm_top_ = 0;
    long army_ticks_ = 0;
    std::vector<Image> sbits_;
    int overland_type(int x = -1, int y = -1);
    std::set<int> freed_;               // prisoners already freed (level * 10000 + cell y * 100 + cell x)
    Image prisoner_img_;                // a captive woman: Goldmoon's sprite in a tattered dress, staffless, in chains (built on first use)
    const Image& prisoner_image();
    std::vector<Corpse> corpses_;
    void reap_dead();                                   // dead creatures leave the fight (becoming corpses); step_corpses lets trolls regenerate
    void step_corpses();
    bool troll_pending() const;
    Image corpse_image(const Corpse& c, int& dither) const;   // the sprite to draw: dither level 0-16 (16 = solid)
    Image fire_[3][8];                  // the army fires of the quest map (sbits 1-3) animated: eight frames each, made from the original sprites
    void build_fire_frames();
    int warp_level_ = 0, warp_cx_ = 0, warp_cy_ = 0;
    int bless_ = 0, swing_cd_ = 0, ward_pts_ = 0;         // Bless / Prayer (+1 to hit) steps left, hero swing cooldown
    int quest_flags_ = 0x0f;               // game.exe data 0x304E: bit 0 women, 1 children, 2 Wyrmslayer, 3 Laurana still to do; all clear = quest complete
    int ending_page_ = 0, reveal_ = 0;     // reveal_: steps left that Find traps / Detect invisible shows hidden traps
    std::vector<std::string> epi_complete_, epi_incomplete_;
    std::vector<Hero> all_heroes_;
    std::vector<Enemy> ienemies_;          // creatures of the current level (data/level_creatures.json)
    std::vector<Npc> npcs_;
    std::vector<int> item_values_;
    std::vector<std::pair<std::string, int>> highscores_;
    void spawn_level_creatures(int n);
    void step_interior_fight(const Input& in);
    void check_story_items();
    void start_ending();
    void step_ending(const Input& in);
    void draw_ending();
    bool god_mode_ = false;                 // Settings: God mode (infinite hit points and spells, nobody dies)
    bool god() const { return god_ || god_mode_; }
    void apply_god();
    bool god_ = false;                      // test cheat: nobody takes damage (DOF_GOD)
    // Missiles in flight (updated controls): a fired arrow, spear, axe or stone flies from the hero to the target and lands as an item on the ground.
    struct Missile { double x0 = 0, y0 = 0, x1 = 0, y1 = 0, t = 0, dur = 0.5; int kind = 0, target = -1, dmg = 0; bool hit = false, left = false; };
    std::vector<Missile> missiles_;
    // Floating damage numbers over the foes in the side view ("Miss" when the blow missed): they rise and fade.
    struct Floater { double x = 0, y = 0, t = 0; int amount = 0, height = 32; };
    std::vector<Floater> floaters_;
    // Hit effects: a small burst of sparks where a foe (white and yellow) or a hero (red) is struck, 0.3 s; and the sound effects (src/fx.cpp).
    struct Spark { double x = 0, y = 0, t = 0; int height = 32; bool pc = false; };
    std::vector<Spark> sparks_;
    // Spell visuals (the sprites of missle.dat: type t uses records 2 * (t - 1) flying and 2 * (t - 1) + 1 for the burst): a bolt flies straight from the caster to each foe it hits,
    // then a burst plays there (a fireball also explodes); the Final Strike fills the whole view with an explosion.
    struct Bolt { double x0 = 0, x1 = 0, y0 = 0, y1 = 0, t = 0, dur = 0.3; int fly = 0, hit = 0, th = 32; bool left = false, big = false; int boom = -1, impact = -1; };
    struct Burst { double x = 0, y = 0, t = 0, dur = 0.45; int rec = 0, th = 32; };
    // What a damaging spell or weapon does where it strikes: kind 0 an energy splash (Magic Missile), 1 flames (Burning Hands, the Flame Tongue), 2 a huge explosion (Fireball),
    // 3 a zap of electricity (Lightning Bolt), 4 a heavy thump of light (Spiritual Hammer). t starts below zero for a delay.
    struct Impact { double x = 0, y = 0, t = 0, dur = 0.4; int th = 32, kind = 0; };
    std::vector<Impact> impacts_;
    void add_impact(const Enemy& e, int kind, double delay = 0);
    std::vector<Bolt> bolts_; std::vector<Burst> bursts_;
    // A trap that goes off: a big flat slab of stone falls on whoever stands there; it lies where it fell until the party leaves the cell.
    struct Slab { int level = 0, cx = 0, cy = 0; double sx = 0, t = 0; bool landed = false; int dmg = 0; };
    std::vector<Slab> slabs_;
    void draw_dragon_neck(const Enemy& e, int cx);   // the neck from the dragon's body to its head
    void draw_slab(int sx_screen, int feet, double fall_t);   // a stone slab at the party's feet (fall_t < 1: still falling)
    struct Lightning { double x0 = 0, y = 0, t = -1; bool left = false; };   // a lightning bolt crossing the view
    Lightning lightning_;
    bool cast_lightning();                             // the Lightning Bolt scroll: through every foe in a line, off the screen
    double final_t_ = -1;                              // Final Strike: seconds since the blast started (-1 = none)
    bool staff_broken_ = false;                        // the Staff of Magius is gone for good after the Final Strike
    std::map<int, std::vector<Image>> spell_img_;
    const std::vector<Image>& spell_frames(int rec);
    static double bolt_time(double dx) { return std::clamp(std::abs(dx) / 380.0, 0.3, 0.8); }   // how long a spell takes to fly that far
    void delay_effects(size_t f0, size_t s0, size_t c0, double hx);   // damage numbers, sparks and deaths wait for the spell to arrive
    void launch_spell(int fly_rec, int hit_rec, const std::vector<Enemy*>& targets, bool big, int boom_fx = -1, int impact = -1);
    void draw_spell_fx(int cam_x, bool interior_view, int cam_y);
    void add_spark(const Enemy& e);
    void add_spark_hero();
    void fx(int id, double gain = 1.0);                // play a synthesized effect (respects the Sound effects switch)
    int weapon_class(const Hero& h) const;             // 0 sword, 1 staff (also Tasslehoff's hoopak), 2 axe
    void add_floater(const Enemy& e, int amount);   // amount -1 = Miss, -2 = Drowned
    int mt_height(const Enemy& e) const { return e.type >= 0 && e.type < (int)mtypes_.size() ? mtypes_[e.type].actor.height : 32; }
    void update_floaters(double dt_ms);
    void draw_floaters(int cam_x, bool interior_view, int cam_y);
    std::map<int, std::vector<uint8_t>> dmap_;   // the dungeon map per level: 0 unknown, 1 seen from a corridor, 2 explored
    void draw_minimap();
    void draw_dungeon_map();
    std::string pend_text_; int pend_back_ = 0;   // typed characters and Backspaces since the last logic step (a frame between steps used to throw them away)
    int end_cool_ = 0;                    // steps to ignore the keys after a page turn (one press must turn one page)
    Image end_pax_, end_child_;           // the two scene pictures of the original (the fortress, the freed prisoners), shown in the ending
    std::vector<std::pair<int, int>> dungeon_route(int sx, int sy, int tx, int ty) const;   // the quickest way through the level, through shut and secret doors too
    void draw_spell_timers();                     // side views: how long Find Traps, Protection from Evil, Bless and Prayer have left                          // top right in the dungeons: a scrolling top-down map, Castlevania style
    Image chest_closed_, chest_open_;                  // small pictures of a treasure chest, drawn in the game's colours
    std::vector<std::vector<Image>> missile_img_;      // arrow, arrow +2, spear, axe, stone
    void update_missiles(double dt_ms);
    void draw_missiles(int cam_x, bool interior_view, int cam_y);
    void draw_ground_things(int cam_x, bool interior_view, int cam_y);
    int strip_of_world(int gx, int gy) const;          // strip x of a world point on the current side-view line
    void world_of_strip(int sx, int* gx, int* gy) const;
    void drop_thrown(int kind, int strip_x);
    bool water_at(int strip_x) const;                  // a river in the side view: a pit two tiles below the ground (the water is at its bottom)
    bool thrown_on_strip() const;                      // fired ammunition lying on the side-view line (the view waits for it to be picked up)
    bool ranged_ok_ = false;
    int ranged_pose_ = 0, caster_step_ = 0;   // steps left of the ranged pose; how far up its spells the caster has climbed in this fight
    bool pending_ranged_ = false;
    size_t hero_frame(const Hero& h);        // which frame of the hero's sheet to draw: walking, jumping, attacking
    bool ranged_attack();                     // LT: the ranged weapon (needs ammunition) or the casters' spells
    bool caster_attack(const Hero& h);
    void damage_enemy(Enemy& e, int dmg);
    int aim_ = 0;                       // attack aim: 1 high, 0 straight, -1 low (keypad 7/9, 4/6, 1/3 with +; the stick up / down on the gamepad)
    double jump_p_ = 0;                 // how far the jump has gone (0..1): picks the rising or falling pose
    long render_frames_ = 0;
    bool moved_frames_ = false, fm_up_ = false, fm_down_ = false, fm_moving_ = false;   // updated controls: per-frame movement state
    double jump_t_ = -1, move_acc_ = 0, jump_dur_ = 650, jump_scale_ = 1;
    JumpSpec jump_for(const Hero& h) const;
    int jump_from_ = 0, jump_to_ = 0, jump_g0_ = 0, jump_h_ = 0, walk_dist_ = 0, pending_cycle_ = 0;
    bool orig_controls_ = false;            // the original's keypad scheme (8/2 turn the side view); off = arrows: up jumps, down turns
    int gfx_mode_ = 0;                     // 0 = original 16-colour; 256-colour is reserved for later
    bool backdrop_ = true;   // Settings: the far land behind the side view (updated controls only)
    std::vector<uint8_t> backdrop_dom_;
    void draw_backdrop(int cam_x, int cam_y);
    bool ctl_drop_ = false; int ctl_drop_sel_ = 0;   // the controls-type drop-down in Settings
    int settings_focus_ = 0, mouse_x_ = -1, mouse_y_ = -1;
    bool pend_click_ = false, click_now_ = false;
    int click_x_ = -1, click_y_ = -1;
    int move_dx_ = 0, move_dy_ = 1;        // last direction walked outdoors (the figure faces it)
    std::vector<Image> qtiles_;
    bool edges_[6] = {false, false, false, false, false, false};
    bool pend_dir_[6] = {false, false, false, false, false, false}, fprev_[6] = {false, false, false, false, false, false}, menu_edges_ = false;
    bool from_wild_ = false, confirm_prev_ = false, pending_confirm_ = false;
    int victory_steps_ = 0, pending_level_ = 0;
    std::string box_;                 // a message box (scroll) waiting for a key
    bool pending_rest_ = false, intro_shown_ = false;
    int wild_count_ = 14, wild_sight_ = 160, wild_speed_ = 2, attack_period_ = 4;
    std::vector<PanelSlot> slots_;
    std::vector<double> bar2_;
    PanelSlot compass_{0, 0};
    int compass_clip_ = -1;
    int leader_ = 0;
    int px_ = 0, py_ = 0;   // world position of the party's 16x16 body (top-left)
    bool face_left_ = false, attacking_ = false;
    int anim_ = 0, xp_ = 0, kills_ = 0;
    double acc_ms_ = 0, last_ms_ = -1;
    long steps_ = 0;
    bool pending_any_ = false, pending_toggle_ = false;
    int pending_select_ = 0;
    bool pending_save_ = false, pending_load_ = false;
    int cx_ = 0, cy_ = 296;      // party position inside the combat strip (strip pixels); y starts in row 2
    bool east_west_ = true;      // which way the strip runs: the axis the party last travelled along
    unsigned rng_ = 1;
    std::string message_;
    int message_steps_ = 0;
};

}  // namespace dof
