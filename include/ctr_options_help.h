#pragma once

#include <string.h>

#define CTR_OPTIONS_HELP_LIMIT 33

typedef struct
{
	const char *key;
	const char *first;
	const char *second;
} CtrOptionsHelp;

static const CtrOptionsHelp s_ctrOptionsHelp[] = {
	{"fullscreen", "Fill the whole screen.", "OFF plays in a window."},
	{"aspect_ratio", "4:3 is the original shape.", "Wider ratios show more track."},
	{"render_scale", "ORIGINAL is the PS1 resolution.", "NATIVE matches your window."},
	{"vsync", "Stops tearing. ADAPTIVE turns", "it off when the game slows."},
	{"smooth_scaling", "Softens the picture when it is", "scaled up. OFF keeps it sharp."},
	{"texture_filtering", "Smooths textures. OFF keeps the", "original blocky PS1 look."},
	{"dithering", "The PS1 dot pattern on colour", "gradients. OFF looks cleaner."},
	{"increase_draw_distance", "Draws more of the track ahead.", ""},
	{"disable_split_screen_lod", "Full detail karts in 2P to 4P.", "May slow older computers."},
	{"skip_intro", "Skips the logos at startup.", ""},
	{"skip_hints", "Skips Aku Aku and Uka Uka's", "hint text boxes in the hubs."},
	{"race_type_selector", "Ask which race on every pad, or", "only if 2 race types have checks."},
	{"skip_podium", "Skips the podium after ordinary", "races. Boss and cup podiums stay."},
	{"skip_cutscenes", "Skips boss and Oxide scenes", "after a podium."},
	{"boss_music", "Music in boss races and cups:", "the theme or the track's own."},
	{"ai_difficulty", "Opponent speed, from next race.", "Never changes what you need."},
	{"mute_when_unfocused", "Silences the game while another", "window is in front."},
	{"device_aware_labels", "A/B/X/Y on Xbox and Deck pads,", "or always PlayStation symbols."},
	{"update_check", "Tells you when a room was made", "with a newer CTR AP version."},
	{"replay_hints", "Read the hints you have seen.", ""},
	{"map_flash", "Open pads blink on the hub map.", "OFF keeps them steady."},
	{"item_feed", "IMPORTANT hides filler items", "and shop stock notices."},
	{"feed_off", "OFF hides the item feed.", "OFF also hides DeathLink popups."},
	{"item_box_colours", "Purple progression, blue useful,", "cyan filler, salmon trap."},
	{"box_seed_off", "This seed turned box colours off.", "All AP item boxes stay pink."},
	{"box_no_boxes", "This seed has no AP item boxes.", ""},
	{"box_item_locked", "Pink until AP Box Colours.", "Set OFF to keep them pink."},
	{"item_box_numbers", "Shows each AP box's number, as in", "its check name: Item Box 12."},
	{"sanity_numbers", "Numbers on crates and fruit that", "are checks. Checked ones hide."},
	{"tracker_partial_colour", "Orange marks tracker nodes with", "some checks in logic."},
	{"death_link", "What happens when another player", "dies. SEED follows the room."},
	{"dl_send_fall", "Falling off or being eaten sends", "a DeathLink to the others."},
	{"dl_send_hit", "A weapon hit sends a DeathLink", "to the others."},
	{"dl_send_loss", "Losing or leaving a race you", "need sends a DeathLink."},
	{"dl_solo", "No other players in solo.", "DeathLink stays off."},
	{"seed_prompt", "Follows the room once connected.", ""},
	{"dl_forced_receive", "Seed is OFF. A send enabled it.", ""},
	{"dl_send_master_off", "DeathLink OFF blocks all sends.", ""},
	{"dl_send_local_mode", "Uses your DeathLink mode here.", ""},
	{"trap_duration", "How long lasting traps run.", "Each player picks their own."},
	{"discord_status", "Shows your track and check count", "in Discord. No room details."},
	{"room_links", "Which program opens room links.", ""},
	{"room_links_hub", "Return to the main menu", "to change room links."},
	{"nav_record", "Saves your laps on custom tracks", "so the AI can drive them."},
	{"nav_use_recorded", "AI drives recorded laps on tracks", "that have them."},
	{"nav_repeat_laps", "Keep more than one recorded lap", "for each driver."},
	{"nav_only_mine", "AI uses only laps you recorded.", ""},
	{"nav_driver_name", "Name saved with your recordings.", ""},
	{"box_author", "Place AP item boxes on tracks.", ""},
	{"box_author_hub", "Return to the main menu", "to change Box Author Mode."},
	{"uri", "Address from the room page,", "for example archipelago.gg:38281."},
	{"slot", "Your name from the YAML.", "Case matters. Underlines mark A-Z"},
	{"password", "Leave empty if the room has none.", ""},
	{"connect", "Joins the room with these.", ""},
	{"play_solo", "Make and play a seed on your own.", ""},
	{"connection_hub", "Return to the main menu", "to change the connection."},
	{"deathlink_page", "SEED follows the room.", "A change here affects only you."},
	{"control_player", "Pick a player to choose a device,", "rebind buttons or set a deadzone."},
	{"control_shortcuts", "Change keyboard shortcuts.", ""},
	{"control_device", "Choose a device for this player.", ""},
	{"control_binding", "Select to change this binding.", ""},
	{"control_deadzone", "Ignore small stick movements.", ""},
	{"control_restore", "Restores this device's bindings.", ""},
	{"sound_fx", "Adjust the sound effects volume.", ""},
	{"sound_music", "Adjust the music volume.", ""},
	{"sound_voice", "Adjust the voice volume.", ""},
	{"sound_mode", "Choose stereo or mono sound.", ""},
	{"sound_vibrate", "Turn controller vibration on/off.", ""},
	{"sound_analog", "Set up this analog controller.", ""},
	{"sound_exit", "Back to the options page list.", ""},
	{"shortcut_picker", "Opens the racer picker in a hub.", "Default: Numpad 9."},
	{"shortcut_defaults", "Restores the AP shortcut keys.", ""},
};

static inline const CtrOptionsHelp *CTR_OptionsHelpFor(const char *key)
{
	for (unsigned i = 0; i < sizeof s_ctrOptionsHelp / sizeof s_ctrOptionsHelp[0]; ++i)
		if (strcmp(s_ctrOptionsHelp[i].key, key) == 0)
			return &s_ctrOptionsHelp[i];
	return NULL;
}
