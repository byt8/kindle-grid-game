#ifndef XCOM_AUDIO_H
#define XCOM_AUDIO_H

/* Spoken log and sound effects for the Kindle Touch.
 *
 * Every log line the game writes is also handed to audio_line(), which turns
 * it into a row of short 22 050 Hz mono PCM clips and plays them through
 * /dev/snd/pcmC0D0p in a separate process, so the screen never waits for the
 * speaker. Clips are baked on this computer by tools/make_sound.py into
 * sound/words/ and sound/fx/; the device only ever reads files.
 *
 * Naming is by the word itself: the clip for "soldier" is sound/words/
 * soldier.pcm. A word with no file is reported on stderr, never skipped in
 * silence, so a stale sound folder shows up on the first turn it matters.
 */

enum { AUDIO_OFF = 0, AUDIO_SPEAK = 1, AUDIO_CHECK = 2 };

/* AUDIO_CHECK writes what would be spoken to stdout (one word per line) and
 * lists every missing clip on stderr; it never touches the sound device. */
int  audio_start(int mode, const char *dir);
void audio_line(const char *text);      /* speak or check one log line */
void audio_effect(const char *name);    /* shot, hit, miss, down, turn, win, lose */
int  audio_stop(void);                  /* reaps the player; 0 = it left cleanly */
int  audio_misses(void);                /* clips wanted but not found so far */
void audio_mute(int on);                /* 1 = cut the speaker now, drop what comes next */
int  audio_ismuted(void);               /* what the mute button last set */

#endif
