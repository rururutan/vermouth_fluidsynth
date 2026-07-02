/**
 *	@file	ini.cpp
 *	@brief	設定ファイル アクセスの動作の定義を行います
 */

#include "compiler.h"
#include "strres.h"
#include "profile.h"
#include "dosio.h"
#include "ini.h"
#include "dllmain.h"

// ---- user type

/**
 * 16ビット配列を読み込む
 * @param[in] lpString 文字列
 * @param[out] ini 設定テーブル
 */
static void inirdargs16(LPCTSTR lpString, const PFTBL* ini)
{
	SINT16* lpDst = static_cast<SINT16*>(ini->value);
	const int nCount = ini->arg;

	for (int i = 0; i < nCount; i++)
	{
		while (*lpString == ' ')
		{
			lpString++;
		}

		if (*lpString == '\0')
		{
			break;
		}

		lpDst[i] = static_cast<SINT16>(milstr_solveINT(lpString));
		while (*lpString != '\0')
		{
			const TCHAR c = *lpString++;
			if (c == ',')
			{
				break;
			}
		}
	}
}

/**
 * 3バイトを読み込む
 * @param[in] lpString 文字列
 * @param[out] ini 設定テーブル
 */
static void inirdbyte3(LPCTSTR lpString, const PFTBL* ini)
{
	for (int i = 0; i < 3; i++)
	{
		const TCHAR c = lpString[i];
		if (c == '\0')
		{
			break;
		}
		if ((((c - '0') & 0xff) < 9) || (((c - 'A') & 0xdf) < 26))
		{
			(static_cast<UINT8*>(ini->value))[i] = static_cast<UINT8>(c);
		}
	}
}

// ---- Use WinAPI

#if !defined(_UNICODE)
/**
 * ビットを設定
 * @param[in,out] lpBuffer バッファ
 * @param[in] nPos 位置
 * @param[in] set セット or クリア
 */
static void bitmapset(void* lpBuffer, UINT nPos, BOOL set)
{
	const int nIndex = (nPos >> 3);
	const UINT8 cBit = 1 << (nPos & 7);
	if (set)
	{
		(static_cast<UINT8*>(lpBuffer))[nIndex] |= cBit;
	}
	else
	{
		(static_cast<UINT8*>(lpBuffer))[nIndex] &= ~cBit;
	}
}

/**
 * ビットを得る
 * @param[in] lpBuffer バッファ
 * @param[in] nPos 位置
 * @return ビット
 */
static BOOL bitmapget(const void* lpBuffer, UINT nPos)
{
	const int nIndex = (nPos >> 3);
	const UINT8 cBit = 1 << (nPos & 7);
	return (((static_cast<const UINT8*>(lpBuffer))[nIndex]) & cBit) ? TRUE : FALSE;
}

/**
 * バイナリをアンシリアライズ
 * @param[out] lpBin バイナリ
 * @param[in] cbBin バイナリのサイズ
 * @param[in] lpString 文字列バッファ
 */
static void binset(void* lpBin, UINT cbBin, LPCTSTR lpString)
{
	for (UINT i = 0; i < cbBin; i++)
	{
		while (*lpString == ' ')
		{
			lpString++;
		}

		LPTSTR lpStringEnd;
		const long lVal = _tcstol(lpString, &lpStringEnd, 16);
		if (lpString == lpStringEnd)
		{
			break;
		}

		(static_cast<UINT8*>(lpBin))[i] = static_cast<UINT8>(lVal);
		lpString = lpStringEnd;
	}
}

/**
 * バイナリをシリアライズ
 * @param[out] lpString 文字列バッファ
 * @param[in] cchString 文字列バッファ長
 * @param[in] lpBin バイナリ
 * @param[in] cbBin バイナリのサイズ
 */
static void binget(LPTSTR lpString, int cchString, const void* lpBin, UINT cbBin)
{

	if (cbBin)
	{
		TCHAR tmp[8];
		wsprintf(tmp, TEXT("%.2x"), (static_cast<const UINT8*>(lpBin))[0]);
		milstr_ncpy(lpString, tmp, cchString);
	}
	for (UINT i = 1; i < cbBin; i++)
	{
		TCHAR tmp[8];
		wsprintf(tmp, TEXT(" %.2x"), (static_cast<const UINT8*>(lpBin))[i]);
		milstr_ncat(lpString, tmp, cchString);
	}
}

/**
 * 設定読み出し
 * @param[in] lpPath パス
 * @param[in] lpTitle タイトル
 * @param[in] lpTable 設定テーブル
 * @param[in] nCount 設定テーブル アイテム数
 */
void ini_read(LPCTSTR lpPath, LPCTSTR lpTitle, const PFTBL* lpTable, UINT nCount)
{
	const PFTBL* p = lpTable;
	const PFTBL* pTerminate = p + nCount;
	while (p < pTerminate)
	{
		TCHAR szWork[512];
		UINT32 val;
		switch (p->itemtype & PFTYPE_MASK)
		{
			case PFTYPE_STR:
				GetPrivateProfileString(lpTitle, p->item, static_cast<LPCTSTR>(p->value), static_cast<LPTSTR>(p->value), p->arg, lpPath);
				break;

			case PFTYPE_BOOL:
				GetPrivateProfileString(lpTitle, p->item,
									(*(static_cast<const UINT8*>(p->value))) ? str_true : str_false,
									szWork, NELEMENTS(szWork), lpPath);
				*(static_cast<UINT8*>(p->value)) = (!milstr_cmp(szWork, str_true)) ? 1 : 0;
				break;

			case PFTYPE_BITMAP:
				GetPrivateProfileString(lpTitle, p->item,
									(bitmapget(p->value, p->arg)) ? str_true : str_false,
									szWork, _countof(szWork), lpPath);
				bitmapset(p->value, p->arg, (milstr_cmp(szWork, str_true) == 0));
				break;

			case PFTYPE_BIN:
				GetPrivateProfileString(lpTitle, p->item, str_null, szWork, _countof(szWork), lpPath);
				binset(p->value, p->arg, szWork);
				break;

			case PFTYPE_SINT8:
			case PFTYPE_UINT8:
				val = GetPrivateProfileInt(lpTitle, p->item, *(static_cast<const UINT8*>(p->value)), lpPath);
				*(static_cast<UINT8*>(p->value)) = static_cast<UINT8>(val);
				break;

			case PFTYPE_SINT16:
			case PFTYPE_UINT16:
				val = GetPrivateProfileInt(lpTitle, p->item, *(static_cast<const UINT16*>(p->value)), lpPath);
				*(static_cast<UINT16*>(p->value)) = static_cast<UINT16>(val);
				break;

			case PFTYPE_SINT32:
			case PFTYPE_UINT32:
				val = GetPrivateProfileInt(lpTitle, p->item, *(static_cast<const UINT32*>(p->value)), lpPath);
				*(static_cast<UINT32*>(p->value)) = static_cast<UINT32>(val);
				break;

			case PFTYPE_HEX8:
				wsprintf(szWork, str_x, *(static_cast<const UINT8*>(p->value)));
				GetPrivateProfileString(lpTitle, p->item, szWork, szWork, _countof(szWork), lpPath);
				*(static_cast<UINT8*>(p->value)) = static_cast<UINT8>(milstr_solveHEX(szWork));
				break;

			case PFTYPE_HEX16:
				wsprintf(szWork, str_x, *(static_cast<const UINT16*>(p->value)));
				GetPrivateProfileString(lpTitle, p->item, szWork, szWork, _countof(szWork), lpPath);
				*(static_cast<UINT16*>(p->value)) = static_cast<UINT16>(milstr_solveHEX(szWork));
				break;

			case PFTYPE_HEX32:
				wsprintf(szWork, str_x, *(static_cast<const UINT32*>(p->value)));
				GetPrivateProfileString(lpTitle, p->item, szWork, szWork, _countof(szWork), lpPath);
				*(static_cast<UINT32*>(p->value)) = static_cast<UINT32>(milstr_solveHEX(szWork));
				break;

			case PFTYPE_ARGS16:
				GetPrivateProfileString(lpTitle, p->item, str_null, szWork, _countof(szWork), lpPath);
				inirdargs16(szWork, p);
				break;

			case PFTYPE_BYTE3:
				GetPrivateProfileString(lpTitle, p->item, str_null, szWork, _countof(szWork), lpPath);
				inirdbyte3(szWork, p);
				break;

			case PFTYPE_KB:
				GetPrivateProfileString(lpTitle, p->item, str_null, szWork, _countof(szWork), lpPath);
				inirdkb(szWork, p);
				break;
		}
		p++;
	}
}

/**
 * 設定書き込み
 * @param[in] lpPath パス
 * @param[in] lpTitle タイトル
 * @param[in] lpTable 設定テーブル
 * @param[in] nCount 設定テーブル アイテム数
 */
void ini_write(LPCTSTR lpPath, LPCTSTR lpTitle, const PFTBL* lpTable, UINT nCount)
{
	const PFTBL* p = lpTable;
	const PFTBL* pTerminate = p + nCount;
	while (p < pTerminate)
	{
		if (!(p->itemtype & PFFLAG_RO))
		{
			TCHAR szWork[512];
			szWork[0] = '\0';

			LPCTSTR lpSet = szWork;
			switch(p->itemtype & PFTYPE_MASK) {
				case PFTYPE_STR:
					lpSet = static_cast<LPCTSTR>(p->value);
					break;

				case PFTYPE_BOOL:
					lpSet = (*(static_cast<const UINT8*>(p->value))) ? str_true : str_false;
					break;

				case PFTYPE_BITMAP:
					lpSet = (bitmapget(p->value, p->arg)) ? str_true : str_false;
					break;

				case PFTYPE_BIN:
					binget(szWork, _countof(szWork), p->value, p->arg);
					break;

				case PFTYPE_SINT8:
					wsprintf(szWork, str_d, *(static_cast<const SINT8*>(p->value)));
					break;

				case PFTYPE_SINT16:
					wsprintf(szWork, str_d, *(static_cast<const SINT16*>(p->value)));
					break;

				case PFTYPE_SINT32:
					wsprintf(szWork, str_d, *(static_cast<const SINT32*>(p->value)));
					break;

				case PFTYPE_UINT8:
					wsprintf(szWork, str_u, *(static_cast<const UINT8*>(p->value)));
					break;

				case PFTYPE_UINT16:
					wsprintf(szWork, str_u, *(static_cast<const UINT16*>(p->value)));
					break;

				case PFTYPE_UINT32:
					wsprintf(szWork, str_u, *(static_cast<const UINT32*>(p->value)));
					break;

				case PFTYPE_HEX8:
					wsprintf(szWork, str_x, *(static_cast<const UINT8*>(p->value)));
					break;

				case PFTYPE_HEX16:
					wsprintf(szWork, str_x, *(static_cast<const UINT16*>(p->value)));
					break;

				case PFTYPE_HEX32:
					wsprintf(szWork, str_x, *(static_cast<const UINT32*>(p->value)));
					break;

				default:
					lpSet = NULL;
					break;
			}
			if (lpSet)
			{
				::WritePrivateProfileString(lpTitle, p->item, lpSet, lpPath);
			}
		}
		p++;
	}
}

#else	// !defined(_UNICODE)

// ---- Use profile.c

/**
 * コールバック
 * @param[in] item アイテム
 * @param[in] lpString 文字列
 */
static void UserReadItem(const PFTBL* item, LPCTSTR lpString)
{
	switch (item->itemtype & PFTYPE_MASK)
	{
		case PFTYPE_ARGS16:
			inirdargs16(lpString, item);
			break;

		case PFTYPE_BYTE3:
			inirdbyte3(lpString, item);
			break;

		case PFTYPE_KB:
			//inirdkb(lpString, item);
			break;
	}
}

/**
 * 設定読み取り
 * @param[in] lpPath パス
 * @param[in] lpTitle タイトル
 * @param[in] lpTable 設定テーブル
 * @param[in] nCount 設定テーブル アイテム数
 */
void ini_read(LPCTSTR lpPath, LPCTSTR lpTitle, const PFTBL* lpTable, UINT nCount)
{
	profile_iniread(lpPath, lpTitle, lpTable, nCount, UserReadItem);
}

/**
 * 設定書き込み
 * @param[in] lpPath パス
 * @param[in] lpTitle タイトル
 * @param[in] lpTable 設定テーブル
 * @param[in] nCount 設定テーブル アイテム数
 */
void ini_write(LPCTSTR lpPath, LPCTSTR lpTitle, const PFTBL* lpTable, UINT nCount)
{
	profile_iniwrite(lpPath, lpTitle, lpTable, nCount, NULL);
}

#endif	// !defined(_UNICODE)


// ----

#if !defined(SUPPORT_PC9821)
static const TCHAR s_szIniTitle[] = TEXT("NekoProjectII");		//!< アプリ名
#else
static const TCHAR s_szIniTitle[] = TEXT("NekoProject21");		//!< アプリ名
#endif

/**
 * 追加設定
 */
enum
{
	PFRO_STR		= PFFLAG_RO + PFTYPE_STR,
	PFRO_BOOL		= PFFLAG_RO + PFTYPE_BOOL,
	PFRO_BITMAP		= PFFLAG_RO + PFTYPE_BITMAP,
	PFRO_UINT8		= PFFLAG_RO + PFTYPE_UINT8,
	PFRO_SINT32		= PFFLAG_RO + PFTYPE_SINT32,
	PFRO_HEX8		= PFFLAG_RO + PFTYPE_HEX8,
	PFRO_HEX32		= PFFLAG_RO + PFTYPE_HEX32,
	PFRO_BYTE3		= PFFLAG_RO + PFTYPE_BYTE3,
	PFRO_KB			= PFFLAG_RO + PFTYPE_KB
};

//! .ini 拡張子
static const TCHAR s_szExt[] = TEXT(".ini");

/**
 * 設定ファイルのパスを得る
 * @param[out] lpPath パス
 * @param[in] cchPath パス バッファの長さ
 */
void initgetfile(LPTSTR lpPath, UINT cchPath)
{
	{
		file_cpyname(lpPath, modulefile, cchPath);
		file_cutext(lpPath);
		file_catname(lpPath, s_szExt, cchPath);
	}
}
