#include	"compiler.h"
#include	"parts.h"
#include	"dllmain.h"
#include	"oemtext.h"
#include	"dosio.h"
#include	"subwind.h"
#include	"vermouth.h"
#include	"keydisp.h"
#include	<shlwapi.h>


	HWND		hWndMain;
	HINSTANCE	hInst;
	OEMCHAR		modulefile[MAX_PATH];
	MIDIMOD		vermouth_module = NULL;
	MIDIHDL		vermouth_handle = NULL;


#define MIDI_BUFF_SIZE (4096)

#define MIDI_STATE_READY		0
#define MIDI_STATE_DATA2		1
#define MIDI_STATE_DATA3		2
#define MIDI_STATE_EXCLUSIVE	3
#define MIDI_STATE_TIMECODE		4
#define MIDI_STATE_SYSTEMDATA1	5
#define MIDI_STATE_SYSTEMDATA2	6
#define MIDI_STATE_SYSTEMDATA3	7

static int midi_state;
static unsigned char midi_buff[MIDI_BUFF_SIZE];
static int midi_buff_ptr;
static unsigned char midi_last_data;

#define MIDIOUTS(a, b, c)	(((c) << 16) + (b << 8) + (a))
#define MIDIOUTS2(a)		(*(UINT16 *)(a))
#define MIDIOUTS3(a)		((*(UINT32 *)(a)) & 0xffffff)

static void GetPath(LPTSTR lpModule, UINT cbModule)
{
	TCHAR szModule[MAX_PATH];
	::GetModuleFileName(hInst, szModule, _countof(szModule));
	::PathRenameExtension(szModule, TEXT(".ini"));

	TCHAR szSF2[MAX_PATH];
	::GetPrivateProfileString(TEXT("SF2"), TEXT("File"), TEXT("soundfont.sf2"), szSF2, _countof(szSF2), szModule);

	::ExpandEnvironmentStrings(szSF2, lpModule, cbModule);
}


DLLAPI int WINAPI vermouth_Open(UINT sample_rate) {

	if (vermouth_handle != NULL) {
		goto open_err1;
	}

	TCHAR szModule[MAX_PATH];
	GetPath(szModule, _countof(szModule));

	midi_state = MIDI_STATE_READY;
	ZeroMemory(midi_buff, sizeof(midi_buff));
	midi_buff_ptr = 0;
	midi_last_data = 0;

	vermouth_module = midimod_create(szModule, sample_rate);
	if (vermouth_module == NULL) {
		goto open_err1;
	}
	midimod_loadall(vermouth_module);

	vermouth_handle = midiout_create(vermouth_module, 512);
	if (vermouth_handle == NULL) {
		goto open_err2;
	}

	kdispwin_create();
	return(0);

open_err2:
	midimod_destroy(vermouth_module);
	vermouth_module = NULL;

open_err1:
	return(-1);
}

DLLAPI int WINAPI vermouth_Close(void) {

	kdispwin_destroy();

	midiout_destroy(vermouth_handle);
	vermouth_handle = NULL;

	midimod_destroy(vermouth_module);
	vermouth_module = NULL;
	return(0);
}

DLLAPI int WINAPI vermouth_Write(UINT8 data) {

	if (vermouth_handle == NULL) {
		return(-1);
	}
	switch(data) {
		case 0xf8: // TIMING
		case 0xfa: // START
		case 0xfb: // CONTINUE
		case 0xfc: // STOP
		case 0xfe: // ACTIVE SENSING
		case 0xff: // SYSTEM RESET
			return(-1);
	}

	if ((midi_state == MIDI_STATE_READY) ||
		((midi_state == MIDI_STATE_DATA2) && (data & 0x80)) ||
		((midi_state == MIDI_STATE_DATA3) && (data & 0x80))) {
		midi_buff_ptr = 0;
		if (data & 0x80) {
			midi_last_data = data;
			switch(data & 0xf0) {
				case 0xc0: // PROGRAM CHANGE
				case 0xd0: // AFTER TOUCH
					midi_state = MIDI_STATE_DATA2;
					break;

				case 0x80: // NOTE OFF
				case 0x90: // NOTE ON
				case 0xa0: // POLYPHONIC KEY PRESSURE
				case 0xb0: // CONTROL CHANGE
				case 0xe0: // PITCH BENDER
					midi_state = MIDI_STATE_DATA3;
					break;

				default:
					switch(data) {
						case 0xf0: // EXCLUSIVE
							midi_state = MIDI_STATE_EXCLUSIVE;
							break;

						case 0xf1: // TIME CODE
							midi_state = MIDI_STATE_TIMECODE;
							break;

						case 0xf2: // SONG POS
							midi_state = MIDI_STATE_SYSTEMDATA3;
							break;

						case 0xf3: // SONG SELECT
							midi_state = MIDI_STATE_SYSTEMDATA2;
							break;

						case 0xf6: // TUNE REQUEST
							midi_state = MIDI_STATE_SYSTEMDATA1;
							break;

						default:
							return(-1);
					}
					break;
			}
		}
		else {
			midi_buff[midi_buff_ptr++] = midi_last_data;
			midi_state = MIDI_STATE_DATA3;
		}
	}

	midi_buff[midi_buff_ptr++] = data;

	switch (midi_state) {
		case MIDI_STATE_DATA2:
			if (midi_buff_ptr >= 2) {
				keydisp_midi(midi_buff);
				midiout_shortmsg(vermouth_handle, MIDIOUTS2(midi_buff));
				midi_state = MIDI_STATE_READY;
			}
			break;

		case MIDI_STATE_DATA3:
			if (midi_buff_ptr >= 3) {
				keydisp_midi(midi_buff);
				midiout_shortmsg(vermouth_handle, MIDIOUTS3(midi_buff));
				midi_state = MIDI_STATE_READY;
			}
			break;

		case MIDI_STATE_EXCLUSIVE:
			if (data == 0xf7) {
				midiout_longmsg(vermouth_handle, midi_buff, midi_buff_ptr);
				midi_state = MIDI_STATE_READY;
			}
			else if (midi_buff_ptr >= MIDI_BUFF_SIZE) {
				midi_state = MIDI_STATE_READY;
			}
			break;

		case MIDI_STATE_SYSTEMDATA1:
			if (midi_buff_ptr >= 1) {
				midi_state = MIDI_STATE_READY;
			}
			break;

		case MIDI_STATE_SYSTEMDATA2:
			if (midi_buff_ptr >= 2) {
				midi_state = MIDI_STATE_READY;
			}
			break;

		case MIDI_STATE_SYSTEMDATA3:
			if (midi_buff_ptr >= 3) {
				midi_state = MIDI_STATE_READY;
			}
			break;
	}
	return(0);
}

DLLAPI int WINAPI vermouth_ShortMsg(UINT32 data) {

	if (vermouth_handle != NULL) {
		keydisp_midi((UINT8 *)&data);
		midiout_shortmsg(vermouth_handle, data);
		return(0);
	}
	else {
		return(-1);
	}
}

DLLAPI int WINAPI vermouth_LongMsg(const UINT8 *sysex, UINT32 len) {

	if (vermouth_handle != NULL) {
		midiout_longmsg(vermouth_handle, sysex, len);
		return(0);
	}
	else {
		return(-1);
	}
}

DLLAPI int WINAPI vermouth_Get16(SINT16 *pcm, UINT32 size) {

	UINT32		count;
const SINT32	*ptr;
	UINT		len;

	if (vermouth_handle != NULL) {
		count = size;
		while(count) {
			len = count;
			ptr = midiout_get(vermouth_handle, &len);
			if (ptr == NULL) {
				break;
			}
			satuation_s16(pcm, ptr, len * 2 * sizeof(SINT16));
			pcm += len * 2;
			count -= len;
		}
		ZeroMemory(pcm, count * 2 * sizeof(SINT16));
		return(0);
	}
	else {
		return(-1);
	}
}

DLLAPI int WINAPI vermouth_Get32(SINT32 *pcm, UINT32 size) {

	UINT32		count;
const SINT32	*ptr;
	UINT		len;

	if (vermouth_handle != NULL) {
		count = size;
		while(count) {
			len = count;
			ptr = midiout_get(vermouth_handle, &len);
			if (ptr == NULL) {
				break;
			}
			CopyMemory(pcm, ptr, len * 2 * sizeof(SINT32));
			pcm += len * 2;
			count -= len;
		}
		ZeroMemory(pcm, count * 2 * sizeof(SINT32));
		return(0);
	}
	else {
		return(-1);
	}
}


// ----

DLLAPI HWND WINAPI vermouth_SetParent(HWND hWnd) {

	HWND ret = hWndMain;
	hWndMain = hWnd;
	return(ret);
}


// ----

static void dllinitialize(HINSTANCE hInstance) {

	hWndMain = NULL;
	hInst = hInstance;

#if defined(UNICODE)
	GetModuleFileName(hInstance, modulefile, NELEMENTS(modulefile));
#else
	TCHAR _modulefile[MAX_PATH];
	GetModuleFileName(hInstance, _modulefile, NELEMENTS(_modulefile));
	oemtext_ucs2tochar(modulefile, NELEMENTS(modulefile), _modulefile, (UINT)-1);
#endif
	dosio_init();
	file_setcd(modulefile);

	kdispwin_readini();
	kdispwin_initialize();
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved) {

	switch(dwReason) {
		case DLL_PROCESS_ATTACH:
			dllinitialize(hInstance);
			TRACEINIT();
			break;

		case DLL_PROCESS_DETACH:
			vermouth_Close();
			kdispwin_writeini();
			kdispwin_deinitialize();
			TRACETERM();
			dosio_term();
			break;
	}
	return(TRUE);
}

