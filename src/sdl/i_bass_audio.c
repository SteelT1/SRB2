#include "bass.h"
#include "bassmix.h"
#include "bass_fx.h"
#include "SDL.h"
#include "../doomstat.h"
#include "../i_sound.h"
#include "../z_zone.h"
#include "../byteptr.h"
#include "../s_sound.h"
#include "../w_wad.h"
#include "i_bass_audio.h"

UINT8 sound_started = false;
static boolean songpaused;
static UINT8 music_volume, sfx_volume, internal_volume;
static const char* BassErrorCodeToString(int bass_errorcode);
HSTREAM music_stream;
HSTREAM bassmixer;
HSTREAM sfxsample;
BASS_CHANNELINFO musicinfo;

sfxenum_t sfxhandle[NUMSFX];
static UINT8 music_volume, sfx_volume, internal_volume;
static float loop_point;
static boolean songpaused;
static UINT32 music_bytes;
static boolean is_looping;
// fading
static boolean is_fading;
static UINT8 fading_source;
static UINT8 fading_target;
static UINT32 fading_timer;
static UINT32 fading_duration;
static INT32 fading_id;
static void (*fading_callback)(void);
static boolean fading_nocleanup;

// Ideally this wouldn't need to exist, however BASS has no function for it, so we gotta do it ourselves.
const char *bec2str[] = {
	"All is OK",
	"Memory error",
	"Can't open the file",
	"Can't find a free/valid driver",
	"The sample buffer was lost",
	"Invalid handle",
	"Unsupported sample format",
	"Invalid position",
	"BASS_Init has not been successfully called",
	"BASS_Start has not been successfully called",
	"Already initialized/paused/whatever",
	"Not paused",
	"Not an audio track",
	"Can't get a free channel",
	"An illegal type was specified",
	"An illegal parameter was specified",
	"No 3D support",
	"No EAX support",
	"Illegal device number",
	"Not playing",
	"Illegal sample rate",
	"The stream is not a file stream",
	"No hardware voices available",
	"The MOD music has no sequence data",
	"No internet connection could be opened",
	"Couldn't create the file",
	"Effects are not available",
	"The channel is playing",
	"Requested data is not available",
	"The channel is a 'decoding channel",
	"A sufficient DirectX version is not installed",
	"Connection timedout",
	"Unsupported file format",
	"Unavailable speaker",
	"Invalid BASS version",
	"Codec is not available/supported",
	"The channel/file has ended",
	"The device is busy"
};

static void var_cleanup(void)
{
	loop_point = 0.0f;
	music_bytes = fading_source = fading_target =\
	 fading_timer = fading_duration = 0;

	songpaused = is_looping =\
	 is_fading = false;

	// HACK: See music_loop, where we want the fade timing to proceed after a non-looping
	// song has stopped playing
	if (!fading_nocleanup)
		fading_callback = NULL;
	else
		fading_nocleanup = false; // use it once, set it back immediately

	internal_volume = 100;
}

static const char* BassErrorCodeToString(int bass_errorcode)
{
	if (bass_errorcode == BASS_ERROR_UNKNOWN) // Special case
		return "unknown";
	return bec2str[bass_errorcode];
}

void I_StartupSound(void)
{
	static long bassver;
	//I_Assert(!sound_started);
	if (sound_started)
		return;

	(void)internal_volume;
	(void)music_volume;
	(void)sfx_volume;

	bassver = BASS_GetVersion();

	CONS_Printf("Compiled with BASS version: %s\n", BASSVERSIONTEXT);
	CONS_Printf("Linked with BASS version: %ld.%ld\n", bassver/100/100/100, bassver/100/100%100);

	// Initalize BASS and the output device
	if (!BASS_Init(-1, 44100, BASS_DEVICE_STEREO|BASS_DEVICE_LATENCY|BASS_DEVICE_FREQ, NULL, NULL))
	{
		CONS_Alert(CONS_ERROR, "Error initializing BASS: %s\n", BassErrorCodeToString(BASS_ErrorGetCode()));
		// call to start audio failed -- we do not have it
		return;
	}
	else
		CONS_Printf("BASS was successfully initialized\n");

	fading_nocleanup = false;

	var_cleanup();

	music_stream = 0;
	music_volume = sfx_volume = 0;

	bassmixer = BASS_Mixer_StreamCreate(44100, 2, BASS_MIXER_NONSTOP);

	if (!bassmixer)
	{
		CONS_Alert(CONS_ERROR, "Error creating BASS mixer: %s\n", BassErrorCodeToString(BASS_ErrorGetCode()));
		return;
	}

	BASS_ChannelSetAttribute(bassmixer, BASS_ATTRIB_BUFFER, 0);
	BASS_ChannelPlay(bassmixer, false);

	sound_started = true;
	songpaused = false;
}

void I_ShutdownSound(void)
{
	if (!sound_started)
		return; // not an error condition
	sound_started = false;

	BASS_ChannelStop(bassmixer);
	BASS_Free();
}

void I_UpdateSound(void){};


/// ------------------------
/// SFX
/// ------------------------

static void *ds2sample(void *stream)
{
	UINT16 ver,freq;
	UINT32 samples, i, newsamples;
	UINT8 *sound;
	char *bsfx = NULL;

	SINT8 *s;
	INT16 *d;
	INT16 o;
	fixed_t step, frac;

	// lump header
	ver = READUINT16(stream); // sound version format?
	if (ver != 3) // It should be 3 if it's a doomsound...
		return 0; // onos! it's not a doomsound!
	freq = READUINT16(stream);
	samples = READUINT32(stream);

	// convert from signed 8bit ???hz to signed 16bit 44100hz.
	switch(freq)
	{
	case 44100:
		if (samples >= UINT32_MAX>>2)
			return NULL; // would wrap, can't store.
		newsamples = samples;
		break;
	case 22050:
		if (samples >= UINT32_MAX>>3)
			return NULL; // would wrap, can't store.
		newsamples = samples<<1;
		break;
	case 11025:
		if (samples >= UINT32_MAX>>4)
			return NULL; // would wrap, can't store.
		newsamples = samples<<2;
		break;
	default:
		frac = (44100 << FRACBITS) / (UINT32)freq;
		if (!(frac & 0xFFFF)) // other solid multiples (change if FRACBITS != 16)
			newsamples = samples * (frac >> FRACBITS);
		else // strange and unusual fractional frequency steps, plus anything higher than 44100hz.
			newsamples = FixedMul(FixedDiv(samples, freq), 44100) + 1; // add 1 to counter truncation.
		if (newsamples >= UINT32_MAX>>2)
			return NULL; // would and/or did wrap, can't store.
		break;
	}

	sound = Z_Malloc(newsamples<<2, PU_SOUND, NULL); // samples * frequency shift * bytes per sample * channels

	s = (SINT8 *)stream;
	d = (INT16 *)sound;

	i = 0;
	switch(freq)
	{
	case 44100: // already at the same rate? well that makes it simple.
		while(i++ < samples)
		{
			o = ((INT16)(*s++)+0x80)<<8; // changed signedness and shift up to 16 bits
			*d++ = o; // left channel
			*d++ = o; // right channel
		}
		break;
	case 22050: // unwrap 2x
		while(i++ < samples)
		{
			o = ((INT16)(*s++)+0x80)<<8; // changed signedness and shift up to 16 bits
			*d++ = o; // left channel
			*d++ = o; // right channel
			*d++ = o; // left channel
			*d++ = o; // right channel
		}
		break;
	case 11025: // unwrap 4x
		while(i++ < samples)
		{
			o = ((INT16)(*s++)+0x80)<<8; // changed signedness and shift up to 16 bits
			*d++ = o; // left channel
			*d++ = o; // right channel
			*d++ = o; // left channel
			*d++ = o; // right channel
			*d++ = o; // left channel
			*d++ = o; // right channel
			*d++ = o; // left channel
			*d++ = o; // right channel
		}
		break;
	default: // convert arbitrary hz to 44100.
		step = 0;
		frac = ((UINT32)freq << FRACBITS) / 44100 + 1; //Add 1 to counter truncation.
		while (i < samples)
		{
			o = (INT16)(*s+0x80)<<8; // changed signedness and shift up to 16 bits
			while (step < FRACUNIT) // this is as fast as I can make it.
			{
				*d++ = o; // left channel
				*d++ = o; // right channel
				step += frac;
			}
			do {
				i++; s++;
				step -= FRACUNIT;
			} while (step >= FRACUNIT);
		}
		break;
	}

	sfxsample = BASS_StreamCreateFile(true, sound, 0, ((UINT8*)d-sound), BASS_STREAM_DECODE);

	if (sfxsample)
	{
		if (BASS_ChannelGetData(sfxsample, bsfx, BASS_DATA_FIXED))
			return bsfx;
	}
	return NULL;
}

void *I_GetSfx(sfxinfo_t *sfx)
{
	void *lump;
	char *bsfx = NULL;

	if (sfx->lumpnum == LUMPERROR)
		sfx->lumpnum = S_GetSfxLumpNum(sfx);
	sfx->length = W_LumpLength(sfx->lumpnum);

	lump = W_CacheLumpNum(sfx->lumpnum, PU_SOUND);

	// convert from standard DoomSound format.
	bsfx = ds2sample(sfx);

	if (bsfx)
	{
		Z_Free(lump);
		return bsfx;
	}

	sfxsample = BASS_StreamCreateFile(true, lump, 0, sfx->length, BASS_STREAM_DECODE);

	if (sfxsample)
	{
		if (BASS_ChannelGetData(sfxsample, bsfx, BASS_DATA_FIXED))
			return bsfx;
	}
	return NULL;
}

void I_FreeSfx(sfxinfo_t *sfx)
{
	if (sfx->data)
		Z_Free(sfx->data);
	sfx->data = NULL;
	sfx->lumpnum = LUMPERROR;
}

INT32 I_StartSound(sfxenum_t id, UINT8 vol, UINT8 sep, UINT8 pitch, UINT8 priority, INT32 channel)
{
	float realvolume;
	vol += 1;
	realvolume = (sfx_volume / 31.0) * (vol / 256.0);
	sfxhandle[id] = sfxsample;
	//Mix_SetPanning(handle, min((UINT16)(0xff-sep)<<1, 0xff), min((UINT16)(sep)<<1, 0xff));
	if (sfxhandle[id])
	{
		//BASS_ChannelSetAttribute(sfxhandle[id], BASS_ATTRIB_PAN, sep);
		BASS_ChannelSetAttribute(sfxhandle[id], BASS_ATTRIB_VOL, realvolume);
		BASS_Mixer_StreamAddChannel(bassmixer, sfxhandle[id], BASS_MIXER_CHAN_NORAMPIN);
	}
	(void)pitch;
	(void)priority; // priority and channel management is handled by SRB2...
	(void)channel;
	(void)sep;
	return (INT32)sfxhandle[id];
}

void I_StopSound(INT32 handle)
{
	BASS_ChannelStop(handle);
}

boolean I_SoundIsPlaying(INT32 handle)
{
	if (BASS_ChannelIsActive(handle) == BASS_ACTIVE_PLAYING)
		return true;
	return false;
}

void I_UpdateSoundParams(INT32 handle, UINT8 vol, UINT8 sep, UINT8 pitch)
{
	float realvolume;
	vol += 1;
	realvolume = (sfx_volume / 31.0) * (vol / 256.0);
	//Mix_SetPanning(handle, min((UINT16)(0xff-sep)<<1, 0xff), min((UINT16)(sep)<<1, 0xff));
	//BASS_ChannelSetAttribute(handle, BASS_ATTRIB_PAN, sep);
	BASS_ChannelSetAttribute(handle, BASS_ATTRIB_VOL, realvolume);
	(void)pitch;
	(void)sep;
}

void I_SetSfxVolume(UINT8 volume)
{
	sfx_volume = volume;
}

/// ------------------------
/// Music Utilities
/// ------------------------

static float get_real_volume(UINT8 volume)
{
#ifdef _WIN32
	if (I_SongType() == MU_MID)
		// HACK: Until we stop using native MIDI,
		// disable volume changes
		return 31.0/31.0; // volume = 31
	else
#endif
		// convert volume to BASS's 0...1.0 scale
		// then apply internal_volume as a percentage
		return (volume/31.0) * (UINT32)internal_volume / 100;
}

static UINT32 get_adjusted_position(UINT32 position)
{
	// all in milliseconds
	UINT32 length = I_GetSongLength();
	UINT32 looppoint = I_GetSongLoopPoint();
	if (length)
		return position >= length ? (position % (length-looppoint)) : position;
	else
		return position;
}

static void do_fading_callback(void)
{
	if (fading_callback)
		(*fading_callback)();
	fading_callback = NULL;
}

/// ------------------------
/// Music Hooks
/// ------------------------

static void CALLBACK count_music_bytes(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	(void)handle;
	(void)channel;
	(void)user;
	(void)data;

	if (!music_stream || I_SongType() == MU_GME || I_SongType() == MU_MOD || I_SongType() == MU_MID)
		return;
	music_bytes++;
}

static void CALLBACK music_loop(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	(void)handle;
	(void)channel;
	(void)data;
	(void)user;
	if (is_looping)
	{
		BASS_ChannelSetPosition(music_stream, loop_point*44100.0L*4, BASS_POS_BYTE);
		music_bytes = (UINT32)(loop_point*44100.0L*4); //assume 44.1khz, 4-byte length (see I_GetSongPosition)
	}
	else
	{
		// HACK: Let fade timing proceed beyond the end of a
		// non-looping song. This is a specific case where the timing
		// should persist after stopping a song, so I don't believe
		// this should apply every time the user stops a song.
		// This is auto-unset in var_cleanup, called by I_StopSong
		fading_nocleanup = true;
		I_StopSong();
	}
}

static UINT32 music_fade(UINT32 interval, void *param)
{
	(void)param;

	if (!is_fading ||
		internal_volume == fading_target ||
		fading_duration == 0)
	{
		I_StopFadingSong();
		do_fading_callback();
		return 0;
	}
	else if (songpaused) // don't decrement timer
		return interval;
	else if ((fading_timer -= 10) <= 0)
	{
		internal_volume = fading_target;
		BASS_ChannelSetAttribute(music_stream, BASS_ATTRIB_VOL, get_real_volume(music_volume));
		I_StopFadingSong();
		do_fading_callback();
		return 0;
	}
	else
	{
		UINT8 delta = abs(fading_target - fading_source);
		fixed_t factor = FixedDiv(fading_duration - fading_timer, fading_duration);
		if (fading_target < fading_source)
			internal_volume = max(min(internal_volume, fading_source - FixedMul(delta, factor)), fading_target);
		else if (fading_target > fading_source)
			internal_volume = min(max(internal_volume, fading_source + FixedMul(delta, factor)), fading_target);
		BASS_ChannelSetAttribute(music_stream, BASS_ATTRIB_VOL, get_real_volume(music_volume));
		return interval;
	}
}

/// ------------------------
//  MUSIC SYSTEM
/// ------------------------

void I_InitMusic(void){}

void I_ShutdownMusic(void)
{
	I_UnloadSong();
}

/// ------------------------
//  MUSIC PROPERTIES
/// ------------------------

musictype_t I_SongType(void)
{
	BASS_ChannelGetInfo(music_stream, &musicinfo); // get info

	if (musicinfo.ctype == BASS_CTYPE_STREAM_VORBIS)
		return MU_OGG;
	else if (musicinfo.ctype == BASS_CTYPE_STREAM_MP3 || musicinfo.ctype == BASS_CTYPE_STREAM_MP1 || musicinfo.ctype == BASS_CTYPE_STREAM_MP2)
		return MU_MP3;
	else if (musicinfo.ctype == BASS_CTYPE_STREAM_WAV)
		return MU_WAV;
	return MU_NONE;
}

boolean I_SongPlaying(void)
{
	return (music_stream != 0);
}

boolean I_SongPaused(void)
{
	return songpaused;
}

/// ------------------------
//  MUSIC EFFECTS
/// ------------------------

boolean I_SetSongSpeed(float speed)
{
	(void)speed;
	return false;
}

/// ------------------------
//  MUSIC SEEKING
/// ------------------------

UINT32 I_GetSongLength(void)
{
	QWORD len;
	double time;
	if (!music_stream || I_SongType() == MU_MOD || I_SongType() == MU_MID)
		return 0;
	else
	{
		len = BASS_ChannelGetLength(music_stream, BASS_POS_BYTE); // the length in bytes
		time = BASS_ChannelBytes2Seconds(music_stream, len); // the length in seconds
		return (UINT32)(time*1000); // the length in miliseconds;
	}
}

boolean I_SetSongLoopPoint(UINT32 looppoint)
{
	if (!music_stream || I_SongType() == MU_GME || I_SongType() == MU_MOD || I_SongType() == MU_MID || !is_looping)
		return false;
	else
	{
		UINT32 length = I_GetSongLength();

		if (length > 0)
			looppoint %= length;

		loop_point = max((float)(looppoint / 1000.0L), 0);
		return true;
	}
}

UINT32 I_GetSongLoopPoint(void)
{
	if (!music_stream || I_SongType() == MU_MOD || I_SongType() == MU_MID)
		return 0;
	else
		return loop_point * 1000;
}

boolean I_SetSongPosition(UINT32 position)
{
	UINT32 length;

    if (!music_stream || I_SongType() == MU_MID)
		return false;
	else
	{
		length = I_GetSongLength(); // get it in MS
		if (length)
			position = get_adjusted_position(position);

		if (BASS_ChannelSetPosition(music_stream, BASS_ChannelSeconds2Bytes(music_stream, position/1000.0L), BASS_POS_BYTE))
			music_bytes = (UINT32)(position/1000.0L*44100.0L*4);
		else
			music_bytes = 0;

		return true;
	}
}

UINT32 I_GetSongPosition(void)
{
	if (!music_stream || I_SongType() == MU_MID)
		return 0;
	else
   		return BASS_ChannelBytes2Seconds(music_stream, BASS_ChannelGetPosition(music_stream, BASS_POS_BYTE)) * 1000;
}

/// ------------------------
//  MUSIC PLAYBACK
/// ------------------------

boolean I_PlaySong(boolean looping)
{
	is_looping = looping;
	if (!BASS_Mixer_StreamAddChannel(bassmixer, music_stream, BASS_MIXER_CHAN_BUFFER|BASS_MIXER_CHAN_NORAMPIN))
	{
		CONS_Alert(CONS_ERROR, "I_PlaySong: %s\n", BassErrorCodeToString(BASS_ErrorGetCode()));
		return false;
	}

	if (music_stream && is_looping)
	{
		BASS_ChannelFlags(music_stream, BASS_SAMPLE_LOOP, BASS_SAMPLE_LOOP); // set the LOOP flag
		BASS_Mixer_ChannelSetSync(music_stream, BASS_SYNC_POS|BASS_SYNC_MIXTIME|BASS_SYNC_THREAD, BASS_ChannelGetLength(music_stream, BASS_POS_BYTE), music_loop, NULL); // set mix-time POS sync at loop end
	}

	BASS_Mixer_ChannelSetSync(music_stream, BASS_SYNC_MIXTIME|BASS_SYNC_THREAD|BASS_SYNC_POS, 0, count_music_bytes, NULL);
	return true;
}

void I_StopSong(void)
{
	if (music_stream)
	{
		BASS_Mixer_ChannelFlags(music_stream, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
		BASS_Mixer_ChannelSetPosition(music_stream, 0, 0);
	}

	var_cleanup();
}

void I_PauseSong(void)
{
	if (!BASS_Mixer_ChannelFlags(music_stream, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE))
		CONS_Alert(CONS_ERROR, "I_PauseSong: %s\n", BassErrorCodeToString(BASS_ErrorGetCode()));
	songpaused = true;
}

void I_ResumeSong(void)
{
	if (!BASS_Mixer_ChannelFlags(music_stream, 0, BASS_MIXER_CHAN_PAUSE))
		CONS_Alert(CONS_ERROR, "I_ResumeSong: %s\n", BassErrorCodeToString(BASS_ErrorGetCode()));
	songpaused = false;
}

void I_UnloadSong(void)
{
	I_StopSong();

	if (music_stream)
	{
		BASS_Mixer_ChannelRemove(music_stream);
		BASS_StreamFree(music_stream);
		music_stream = 0;
	}
}

boolean I_LoadSong(char *data, size_t len)
{
	const char *key1 = "LOOP";
	const char *key2 = "POINT=";
	const char *key3 = "MS=";
	const size_t key1len = strlen(key1);
	const size_t key2len = strlen(key2);
	const size_t key3len = strlen(key3);
	char *p = data;

	if (music_stream)
		I_UnloadSong();

	// always do this whether or not a music already exists
	var_cleanup();

	music_stream = BASS_StreamCreateFile(true, data, 0, len, BASS_STREAM_DECODE);

	if (!music_stream)
	{
		CONS_Alert(CONS_ERROR, "BASS_StreamCreateFile: %s\n", BassErrorCodeToString(BASS_ErrorGetCode()));
		return false;
	}

	// Find the OGG loop point.
	loop_point = 0.0f;

	while ((UINT32)(p - data) < len)
	{
		if (fpclassify(loop_point) == FP_ZERO && !strncmp(p, key1, key1len))
		{
			p += key1len; // skip LOOP
			if (!strncmp(p, key2, key2len)) // is it LOOPPOINT=?
			{
				p += key2len; // skip POINT=
				loop_point = (float)((44.1L+atoi(p)) / 44100.0L); // LOOPPOINT works by sample count.
				// because SDL_Mixer is USELESS and can't even tell us
				// something simple like the frequency of the streaming music,
				// we are unfortunately forced to assume that ALL MUSIC is 44100hz.
				// This means a lot of tracks that are only 22050hz for a reasonable downloadable file size will loop VERY badly.
			}
			else if (!strncmp(p, key3, key3len)) // is it LOOPMS=?
			{
				p += key3len; // skip MS=
				loop_point = (float)(atoi(p) / 1000.0L); // LOOPMS works by real time, as miliseconds.
				// Everything that uses LOOPMS will work perfectly with SDL_Mixer.
			}
		}

		if (fpclassify(loop_point) != FP_ZERO) // Got what we needed
			break;
		else // continue searching
			p++;
	}

	return true;
}

void I_SetMusicVolume(UINT8 volume)
{
	if (!I_SongPlaying())
		return;

#ifdef _WIN32
	if (I_SongType() == MU_MID)
		// HACK: Until we stop using native MIDI,
		// disable volume changes
		music_volume = 31;
	else
#endif
		music_volume = volume;

	BASS_ChannelSetAttribute(music_stream, BASS_ATTRIB_VOL, get_real_volume(music_volume));
}

boolean I_SetSongTrack(int track)
{
	(void)track;
	return false;
}

/// ------------------------
//  MUSIC FADING
/// ------------------------

void I_SetInternalMusicVolume(UINT8 volume)
{
	internal_volume = volume;
	if (!I_SongPlaying())
		return;
	BASS_ChannelSetAttribute(music_stream, BASS_ATTRIB_VOL, get_real_volume(music_volume));
}

void I_StopFadingSong(void)
{
	if (fading_id)
		SDL_RemoveTimer(fading_id);
	is_fading = false;
	fading_source = fading_target = fading_timer = fading_duration = fading_id = 0;
}

boolean I_FadeSongFromVolume(UINT8 target_volume, UINT8 source_volume, UINT32 ms, void (*callback)(void))
{
	INT16 volume_delta;

	source_volume = min(source_volume, 100);
	volume_delta = (INT16)(target_volume - source_volume);

	I_StopFadingSong();

	if (!ms && volume_delta)
	{
		I_SetInternalMusicVolume(target_volume);
		if (callback)
			(*callback)();
		return true;

	}
	else if (!volume_delta)
	{
		if (callback)
			(*callback)();
		return true;
	}

	// Round MS to nearest 10
	// If n - lower > higher - n, then round up
	ms = (ms - ((ms / 10) * 10) > (((ms / 10) * 10) + 10) - ms) ?
		(((ms / 10) * 10) + 10) // higher
		: ((ms / 10) * 10); // lower

	if (!ms)
		I_SetInternalMusicVolume(target_volume);
	else if (source_volume != target_volume)
	{
		fading_id = SDL_AddTimer(10, music_fade, NULL);
		if (fading_id)
		{
			is_fading = true;
			fading_timer = fading_duration = ms;
			fading_source = source_volume;
			fading_target = target_volume;
			fading_callback = callback;

			if (internal_volume != source_volume)
				I_SetInternalMusicVolume(source_volume);
		}
	}

	return is_fading;
}


boolean I_FadeSong(UINT8 target_volume, UINT32 ms, void (*callback)(void))
{
	return I_FadeSongFromVolume(target_volume, internal_volume, ms, callback);
}

boolean I_FadeOutStopSong(UINT32 ms)
{
	return I_FadeSongFromVolume(0, internal_volume, ms, &I_StopSong);
}

boolean I_FadeInPlaySong(UINT32 ms, boolean looping)
{
	if (I_PlaySong(looping))
		return I_FadeSongFromVolume(100, 0, ms, NULL);
	else
		return false;
}