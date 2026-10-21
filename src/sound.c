/* sound.c — plays the game's sound queue (tyrian2.c "The Sound Routine")
 * on the XGM2 driver's 3 PCM channels.  The PC mixes 8 channels; here
 * each PC channel goes to a fixed PCM channel with a priority: voices (PC
 * channel 3) win, then explosions/items (6, 7), then weapons and hits. */
#include "sound.h"
#include "game/game.h"
#include "gen_data/sound_table.h"
#include "gen_data/music.h"
#include "resources.h"

static const u8 pcm_channel[8] = { SOUND_PCM_CH1, SOUND_PCM_CH1, SOUND_PCM_CH1, SOUND_PCM_CH3,
                                   SOUND_PCM_CH1, SOUND_PCM_CH1, SOUND_PCM_CH2, SOUND_PCM_CH2 };
static const u8 priority[8] = { 4, 6, 3, 15, 3, 5, 9, 10 };

void sound_init(void)
{
    Z80_loadDriver(Z80_DRIVER_XGM2, TRUE);
}

void sound_flush(void)
{
    u16 i;
    for (i = 0; i < 8; i++) {
        u8 id = game.sound_queue[i];
        if (!id) continue;
        game.sound_queue[i] = 0;
        if (id <= SOUND_COUNT)
            XGM2_playPCMEx(sound_defs[id].data, sound_defs[id].len, pcm_channel[i], priority[i],
                           sound_defs[id].half_rate, FALSE);
    }
}

/* song: 1-based like the script's song numbers (play_song(n - 1)) */
void sound_music(u8 song)
{
#if MUSIC_TRACKS
    static u8 current;
    if (song == current) return;
    current = song;
    if (song == 0 || song > MUSIC_TRACKS) {
        XGM2_stop();
        return;
    }
    XGM2_load(music);
    XGM2_playTrack(song - 1);
#endif
}

/* JE_pauseGame halves music volume, leaving PCM effects alone. */
void sound_pause(bool paused)
{
    XGM2_setFMVolume(paused ? 50 : 100);
    XGM2_setPSGVolume(paused ? 50 : 100);
}
