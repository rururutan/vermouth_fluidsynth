/* midiout_fluidsynth.c -- VERMOUTH MIDI output backend using FluidSynth 2.x
 *
 * This translation unit implements the minimal MIDI backend surface declared in
 * vermouth.h. midimod_*() stores module-level configuration such as the SoundFont
 * path, while midiout_*() owns an independent FluidSynth instance for playback.
 *
 * Design notes:
 * - The exported API remains compatible with the original VERMOUTH caller.
 * - FluidSynth is used as an in-memory renderer only; this DLL does not open a
 *   FluidSynth audio driver.
 * - Packed WinMM-style short messages are dispatched to FluidSynth note, control,
 *   program, pressure and pitch-bend APIs.
 * - SysEx messages are forwarded to fluid_synth_sysex() after removing F0/F7,
 *   which is the format expected by FluidSynth. Unhandled GM/GS reset and a small
 *   set of GS master controls are handled locally.
 * - PCM is rendered by fluid_synth_write_float() and converted to the SINT32
 *   interleaved stereo stream expected by the existing VERMOUTH host glue.
 */

#include "compiler.h"
#include "vermouth.h"

#include <fluidsynth.h>

#define VMFS_MAX_BLOCK     4096
#define VMFS_OUTPUT_SCALE    8192.0f     /* FluidSynth float output, normally around +/-1, to legacy SINT32 mixer scale. */
#define VMFS_DEFAULT_SYNTH_GAIN 2.00     /* FluidSynth synth.gain. Keep headroom because FluidSynth effects can saturate easily. */

/* GS system common defaults. */
#define VMFS_GS_MASTER_VOLUME_DEFAULT 127
#define VMFS_GS_MASTER_BALANCE_CENTER 64

typedef struct {
    UINT              samprate;     /* Must match the first field of the public MIDIMOD view. */
    char             *sf2path_utf8;
    fluid_settings_t *settings;
} VMFS_MODULE;

typedef struct {
    UINT              samprate;     /* Must match the first field of the public MIDIHDL view. */
    UINT              worksize;
    fluid_settings_t *settings;
    fluid_synth_t    *synth;
    int               sfont_id;
    SINT32           *out;
    float            *fbuf;
    float             out_scale;
    UINT8             master_volume;   /* GS Master Volume: 0..127, default 127 */
    UINT8             master_balance;  /* GS Master Balance/Pan: 0..127, 64=center */
} VMFS_HANDLE;

/* Return Roland DT1 checksum validity.
 * DT1 checksum covers address bytes + data bytes, but not manufacturer/model/command,
 * and the checksum byte itself is the final byte before F7. */
static int vmfs_roland_checksum_ok(const UINT8 *addr_data, UINT addr_data_size, UINT8 checksum)
{
    UINT i;
    UINT sum = 0;
    if (addr_data == NULL || addr_data_size == 0) return 0;
    for (i = 0; i < addr_data_size; i++) {
        sum += addr_data[i];
    }
    return (UINT8)((128 - (sum & 0x7f)) & 0x7f) == checksum;
}

/* Parse locally supported master-level SysEx messages.
 *
 * Supported:
 *   GS DT1 Master Volume  : F0 41 dd 42 12 40 00 04 vv sum F7
 *   GS DT1 Master Balance : F0 41 dd 42 12 40 00 06 vv sum F7
 *
 * q/qsize must be the SysEx body after stripping F0/F7.
 * Returns 1 if this backend consumed the message. */
static int vmfs_handle_master_sysex(VMFS_HANDLE *h, const UINT8 *q, UINT qsize)
{
    UINT8 addr_h, addr_m, addr_l, value, checksum;

    if (h == NULL || q == NULL) return 0;

    /* Roland GS DT1: 41 dev 42 12 aa aa aa data checksum */
    if (qsize >= 10 &&
        q[0] == 0x41 && q[3] == 0x42 && q[4] == 0x12) {
        addr_h = q[5];
        addr_m = q[6];
        addr_l = q[7];
        value = q[8] & 0x7f;
        checksum = q[9] & 0x7f;

        if (!vmfs_roland_checksum_ok(q + 5, 4, checksum)) {
            return 0;
        }

        if (addr_h == 0x40 && addr_m == 0x00 && addr_l == 0x04) {
            h->master_volume = value;
            return 1;
        }

        if (addr_h == 0x40 && addr_m == 0x00 && addr_l == 0x06) {
            h->master_balance = value;
            return 1;
        }
    }

    return 0;
}

static void vmfs_reset_master_controls(VMFS_HANDLE *h)
{
    if (h == NULL) return;
    h->master_volume = VMFS_GS_MASTER_VOLUME_DEFAULT;
    h->master_balance = VMFS_GS_MASTER_BALANCE_CENTER;
}

static void vmfs_get_master_mix(const VMFS_HANDLE *h, float *gain_l, float *gain_r)
{
    float vol;
    float bal;

    if (gain_l == NULL || gain_r == NULL) return;

    vol = 1.0f;
    bal = (float)VMFS_GS_MASTER_BALANCE_CENTER;

    if (h != NULL) {
        vol = (float)h->master_volume / 127.0f;
        bal = (float)h->master_balance;
    }

    if (bal < 64.0f) {
        *gain_l = vol;
        *gain_r = vol * (bal / 64.0f);
    } else if (bal > 64.0f) {
        *gain_l = vol * ((127.0f - bal) / 63.0f);
        *gain_r = vol;
    } else {
        *gain_l = vol;
        *gain_r = vol;
    }
}

/* Convert the UTF-16 path used by the Windows-side DLL code to UTF-8 for FluidSynth. */

static char *vmfs_wchar_to_utf8(const wchar_t *src)
{
#if defined(_WIN32)
    int len;
    char *dst;
    if (src == NULL) return NULL;
    len = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    dst = (char *)_MALLOC((UINT)len, "vmfs_path");
    if (dst == NULL) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, len, NULL, NULL) <= 0) {
        _MFREE(dst);
        return NULL;
    }
    return dst;
#else
    size_t len;
    char *dst;
    if (src == NULL) return NULL;
    len = wcstombs(NULL, src, 0);
    if (len == (size_t)-1) return NULL;
    dst = (char *)_MALLOC((UINT)(len + 1), "vmfs_path");
    if (dst == NULL) return NULL;
    wcstombs(dst, src, len + 1);
    return dst;
#endif
}

/* Create settings for a FluidSynth instance.
 * A fresh settings object is created for each MIDIHDL because runtime gain
 * updates are per playback handle. */
static fluid_settings_t *vmfs_create_settings(UINT samprate)
{
    fluid_settings_t *s = new_fluid_settings();
    if (s == NULL) return NULL;

    fluid_settings_setnum(s, "synth.sample-rate", (double)(samprate ? samprate : 44100));
    fluid_settings_setnum(s, "synth.gain", VMFS_DEFAULT_SYNTH_GAIN);
    fluid_settings_setint(s, "synth.midi-channels", 16);
    fluid_settings_setint(s, "synth.audio-channels", 1);
//    fluid_settings_setint(s, "synth.audio-groups", 1);
//    fluid_settings_setint(s, "synth.effects-channels", 2);      /* reverb + chorus */
//    fluid_settings_setint(s, "synth.reverb.active", 1);
//    fluid_settings_setint(s, "synth.chorus.active", 1);

    /* No FluidSynth audio driver is used; rendering is pulled explicitly into memory. */
    return s;
}

/* Reset the synth to the project default GM-ish state.
 * This intentionally keeps all 16 channels active; ch10 is set to percussion. */
static void vmfs_initialize_synth(fluid_synth_t *synth)
{
    int ch;
    if (synth == NULL) return;

    fluid_synth_system_reset(synth);

    /* Make the GM defaults explicit and force channel 10 to percussion. */
    for (ch = 0; ch < 16; ch++) {
        fluid_synth_cc(synth, ch, 7, 100);      /* volume */
        fluid_synth_cc(synth, ch, 10, 64);      /* pan */
        fluid_synth_cc(synth, ch, 11, 127);     /* expression */
    }
    fluid_synth_bank_select(synth, 9, 128);
    fluid_synth_program_change(synth, 9, 0);
}

/* Store module-level immutable data. The actual fluid_synth_t is created in
 * midiout_create() so each MIDIHDL has independent voice/controller state. */
MIDIMOD midimod_create(wchar_t *sf2name, UINT samprate)
{
    VMFS_MODULE *m;
    char *path;
    fluid_settings_t *settings;

    path = vmfs_wchar_to_utf8(sf2name);
    if (path == NULL) return NULL;

    settings = vmfs_create_settings(samprate);
    if (settings == NULL) {
        _MFREE(path);
        return NULL;
    }

    m = (VMFS_MODULE *)_MALLOC(sizeof(VMFS_MODULE), "vmfs_module");
    if (m == NULL) {
        delete_fluid_settings(settings);
        _MFREE(path);
        return NULL;
    }
    ZeroMemory(m, sizeof(VMFS_MODULE));
    m->samprate = samprate ? samprate : 44100;
    m->sf2path_utf8 = path;
    m->settings = settings;
    return (MIDIMOD)(void *)m;
}

void midimod_destroy(MIDIMOD mod)
{
    VMFS_MODULE *m = (VMFS_MODULE *)(void *)mod;
    if (m) {
        if (m->settings) delete_fluid_settings(m->settings);
        if (m->sf2path_utf8) _MFREE(m->sf2path_utf8);
        _MFREE(m);
    }
}

void midimod_loadprogram(MIDIMOD mod, UINT num) { (void)mod; (void)num; }
void midimod_loadrhythm(MIDIMOD mod, UINT num)  { (void)mod; (void)num; }
void midimod_loadgm(MIDIMOD mod)                { (void)mod; }
void midimod_loadall(MIDIMOD mod)               { (void)mod; }

/* Create a playback handle and load the SoundFont into its FluidSynth instance. */
MIDIHDL midiout_create(MIDIMOD mod, UINT worksize)
{
    VMFS_MODULE *m = (VMFS_MODULE *)(void *)mod;
    VMFS_HANDLE *h;
    fluid_settings_t *settings;
    fluid_synth_t *synth;
    int sfont_id;

    (void)worksize;
    if (m == NULL || m->sf2path_utf8 == NULL) return NULL;

    settings = vmfs_create_settings(m->samprate);
    if (settings == NULL) return NULL;

    synth = new_fluid_synth(settings);
    if (synth == NULL) {
        delete_fluid_settings(settings);
        return NULL;
    }

    sfont_id = fluid_synth_sfload(synth, m->sf2path_utf8, 1); /* reset_presets=1 */
    if (sfont_id == FLUID_FAILED) {
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return NULL;
    }

    h = (VMFS_HANDLE *)_MALLOC(sizeof(VMFS_HANDLE), "vmfs_handle");
    if (h == NULL) {
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return NULL;
    }
    ZeroMemory(h, sizeof(VMFS_HANDLE));

    h->samprate = m->samprate;
    h->worksize = VMFS_MAX_BLOCK;
    h->settings = settings;
    h->synth = synth;
    h->sfont_id = sfont_id;
    h->out_scale = VMFS_OUTPUT_SCALE;
    vmfs_reset_master_controls(h);
    h->out = (SINT32 *)_MALLOC(sizeof(SINT32) * VMFS_MAX_BLOCK * 2, "vmfs_out");
    h->fbuf = (float  *)_MALLOC(sizeof(float)  * VMFS_MAX_BLOCK * 2, "vmfs_float");
    if (h->out == NULL || h->fbuf == NULL) {
        if (h->out) _MFREE(h->out);
        if (h->fbuf) _MFREE(h->fbuf);
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        _MFREE(h);
        return NULL;
    }

    vmfs_initialize_synth(h->synth);
    return (MIDIHDL)(void *)h;
}

void midiout_destroy(MIDIHDL hdl)
{
    VMFS_HANDLE *h = (VMFS_HANDLE *)(void *)hdl;
    if (h) {
        if (h->synth) delete_fluid_synth(h->synth);
        if (h->settings) delete_fluid_settings(h->settings);
        if (h->out) _MFREE(h->out);
        if (h->fbuf) _MFREE(h->fbuf);
        _MFREE(h);
    }
}

/* Dispatch packed WinMM-style MIDI short messages to FluidSynth. */
void midiout_shortmsg(MIDIHDL hdl, UINT32 msg)
{
    VMFS_HANDLE *h = (VMFS_HANDLE *)(void *)hdl;
    UINT8 status, d1, d2;
    int ch;

    if (h == NULL || h->synth == NULL) return;

    status = (UINT8)(msg & 0xff);
    d1 = (UINT8)((msg >> 8) & 0x7f);
    d2 = (UINT8)((msg >> 16) & 0x7f);
    ch = status & 0x0f;

    switch (status & 0xf0) {
    case 0x80:
        fluid_synth_noteoff(h->synth, ch, d1);
        break;
    case 0x90:
        if (d2) fluid_synth_noteon(h->synth, ch, d1, d2);
        else    fluid_synth_noteoff(h->synth, ch, d1);
        break;
    case 0xa0:
        fluid_synth_key_pressure(h->synth, ch, d1, d2);
        break;
    case 0xb0:
        /*
         * CC124..CC127 are channel mode messages. Some MIDI files use them as
         * Mode 3 markers, but passing them to FluidSynth may disable channels
         * other than channel 1, so they are ignored for compatibility.
         */
        if (d1 >= 124 && d1 <= 127) {
            break;
        }
        fluid_synth_cc(h->synth, ch, d1, d2);
        break;
    case 0xc0:
        fluid_synth_program_change(h->synth, ch, d1);
        break;
    case 0xd0:
        fluid_synth_channel_pressure(h->synth, ch, d1);
        break;
    case 0xe0:
        fluid_synth_pitch_bend(h->synth, ch, d1 | (d2 << 7));
        break;
    default:
        break;
    }
}

/* Forward SysEx to FluidSynth. FluidSynth expects the payload without F0/F7. */
void midiout_longmsg(MIDIHDL hdl, const void *msg, UINT size)
{
    VMFS_HANDLE *h = (VMFS_HANDLE *)(void *)hdl;
    const UINT8 *p = (const UINT8 *)msg;
    const UINT8 *orig = (const UINT8 *)msg;
    UINT orig_size = size;
    int handled = 0;

    if (h == NULL || h->synth == NULL || p == NULL || size < 2) return;

    /* FluidSynth expects the SysEx body without the leading F0 and trailing F7. */
    if (p[0] == 0xf0) {
        p++;
        size--;
    }
    if (size > 0 && p[size - 1] == 0xf7) {
        size--;
    }
    if (size == 0) return;

    fluid_synth_sysex(h->synth, (const char *)p, (int)size, NULL, NULL, &handled, 0);

    /*
     * If FluidSynth did not consume the message, handle the compatibility subset
     * required by this backend. Messages already handled by FluidSynth are left
     * untouched to avoid double-applying the same SysEx.
     */
    if (!handled) {
        const UINT8 *q = orig;
        UINT qsize = orig_size;
        if (q && qsize > 0 && q[0] == 0xf0) { q++; qsize--; }
        if (qsize > 0 && q[qsize - 1] == 0xf7) qsize--;

        if (vmfs_handle_master_sysex(h, q, qsize)) {
            return;
        }

        if ((qsize >= 4 && q[0] == 0x7e && q[2] == 0x09) ||
            (qsize >= 9 && q[0] == 0x41 && q[3] == 0x42 && q[4] == 0x12 &&
             q[5] == 0x40 && q[6] == 0x00 && q[7] == 0x7f)) {
            vmfs_initialize_synth(h->synth);
            vmfs_reset_master_controls(h);
        }
    }
}

/* Render up to VMFS_MAX_BLOCK stereo frames and return a temporary SINT32 buffer.
 * The returned pointer remains valid until the next call for the same handle. */
const SINT32 * midiout_get(MIDIHDL hdl, UINT *samples)
{
    VMFS_HANDLE *h = (VMFS_HANDLE *)(void *)hdl;
    UINT n, i, k;
    float gain_l, gain_r;

    if (h == NULL || h->synth == NULL || samples == NULL) return NULL;
    n = *samples;
    if (n == 0) return NULL;
    if (n > VMFS_MAX_BLOCK) n = VMFS_MAX_BLOCK;

    if (fluid_synth_write_float(h->synth, (int)n, h->fbuf, 0, 2, h->fbuf, 1, 2) != FLUID_OK) {
        ZeroMemory(h->out, sizeof(SINT32) * n * 2);
        *samples = n;
        return h->out;
    }

    vmfs_get_master_mix(h, &gain_l, &gain_r);

    k = n * 2;
    for (i = 0; i < k; i += 2) {
        float vl = h->fbuf[i] * h->out_scale * gain_l;
        float vr = h->fbuf[i + 1] * h->out_scale * gain_r;

        if (vl > 8388607.0f) vl = 8388607.0f;
        else if (vl < -8388608.0f) vl = -8388608.0f;
        if (vr > 8388607.0f) vr = 8388607.0f;
        else if (vr < -8388608.0f) vr = -8388608.0f;

        h->out[i] = (SINT32)vl;
        h->out[i + 1] = (SINT32)vr;
    }

    *samples = n;
    return h->out;
}

UINT midiout_get32(MIDIHDL hdl, SINT32 *pcm, UINT size)
{
    UINT n = size;
    const SINT32 *src;
    if (pcm == NULL || size == 0) return 0;
    src = midiout_get(hdl, &n);
    if (src == NULL || n == 0) return 0;
    CopyMemory(pcm, src, sizeof(SINT32) * n * 2);
    return n;
}

/* Runtime gain setter used by the legacy VERMOUTH API. gain=100 means the
 * configured default FluidSynth gain. */
void midiout_setgain(MIDIHDL hdl, int gain)
{
    VMFS_HANDLE *h = (VMFS_HANDLE *)(void *)hdl;
    double fgain;
    if (h == NULL || h->settings == NULL || h->synth == NULL) return;

    /* Treat gain=100 as the configured default because legacy callers do not define a strict unit. */
    if (gain <= 0) gain = 100;
    fgain = VMFS_DEFAULT_SYNTH_GAIN * ((double)gain / 100.0);
    fluid_settings_setnum(h->settings, "synth.gain", fgain);
    fluid_synth_set_gain(h->synth, (float)fgain);
}

