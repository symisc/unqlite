/*
 * Symisc JX9: A Highly Efficient Embeddable Scripting Engine Based on JSON.
 * Copyright (C) 2012-2026, Symisc Systems https://jx9.symisc.net/
 * Version 1.7.2
 * For information on licensing, redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES
 * please contact Symisc Systems via:
 *       legal@symisc.net
 *       licensing@symisc.net
 *       contact@symisc.net
 * or visit:
 *      https://jx9.symisc.net/
 */
 /* $SymiscID: const.c v1.7 Win7 2012-12-13 00:01 stable <chm@symisc.net> $ */
#ifndef JX9_AMALGAMATION
#include "jx9Int.h"
#endif
/* This file implement built-in constants for the JX9 engine. */
/*
 * JX9_VERSION
 * __JX9__
 *   Expand the current version of the JX9 engine.
 */
static void JX9_VER_Const(jx9_value *pVal, void *pUnused)
{
	SXUNUSED(pUnused);
	jx9_value_string(pVal, jx9_lib_signature(), -1/*Compute length automatically*/);
}
#ifdef __WINNT__
#include <Windows.h>
#elif defined(__UNIXES__)
#include <sys/utsname.h>
#endif
/*
 * JX9_OS
 * __OS__
 *  Expand the name of the host Operating System.
 */
static void JX9_OS_Const(jx9_value *pVal, void *pUnused)
{
#if defined(__WINNT__)
	jx9_value_string(pVal, "WinNT", (int)sizeof("WinNT")-1);
#elif defined(__UNIXES__)
	struct utsname sInfo;
	if( uname(&sInfo) != 0 ){
		jx9_value_string(pVal, "Unix", (int)sizeof("Unix")-1);
	}else{
		jx9_value_string(pVal, sInfo.sysname, -1);
	}
#else
	jx9_value_string(pVal,"Host OS", (int)sizeof("Host OS")-1);
#endif
	SXUNUSED(pUnused);
}
/*
 * JX9_EOL
 *  Expand the correct 'End Of Line' symbol for this platform.
 */
static void JX9_EOL_Const(jx9_value *pVal, void *pUnused)
{
	SXUNUSED(pUnused);
#ifdef __WINNT__
	jx9_value_string(pVal, "\r\n", (int)sizeof("\r\n")-1);
#else
	jx9_value_string(pVal, "\n", (int)sizeof(char));
#endif
}
/*
 * JX9_INT_MAX
 * Expand the largest integer supported.
 * Note that JX9 deals with 64-bit integer for all platforms.
 */
static void JX9_INTMAX_Const(jx9_value *pVal, void *pUnused)
{
	SXUNUSED(pUnused);
	jx9_value_int64(pVal, SXI64_HIGH);
}
/*
 * JX9_INT_SIZE
 * Expand the size in bytes of a 64-bit integer.
 */
static void JX9_INTSIZE_Const(jx9_value *pVal, void *pUnused)
{
	SXUNUSED(pUnused);
	jx9_value_int64(pVal, sizeof(sxi64));
}
/*
 * DIRECTORY_SEPARATOR.
 * Expand the directory separator character.
 */
static void JX9_DIRSEP_Const(jx9_value *pVal, void *pUnused)
{
	SXUNUSED(pUnused);
#ifdef __WINNT__
	jx9_value_string(pVal, "\\", (int)sizeof(char));
#else
	jx9_value_string(pVal, "/", (int)sizeof(char));
#endif
}
/*
 * PATH_SEPARATOR.
 * Expand the path separator character.
 */
static void JX9_PATHSEP_Const(jx9_value *pVal, void *pUnused)
{
	SXUNUSED(pUnused);
#ifdef __WINNT__
	jx9_value_string(pVal, ";", (int)sizeof(char));
#else
	jx9_value_string(pVal, ":", (int)sizeof(char));
#endif
}
#ifndef __WINNT__
#include <time.h>
#endif
/*
 * __TIME__
 *  Expand the current time (GMT).
 */
static void JX9_TIME_Const(jx9_value *pVal, void *pUnused)
{
	Sytm sTm;
#ifdef __WINNT__
	SYSTEMTIME sOS;
	GetSystemTime(&sOS);
	SYSTEMTIME_TO_SYTM(&sOS, &sTm);
#else
	struct tm *pTm;
	time_t t;
	time(&t);
	pTm = gmtime(&t);
	STRUCT_TM_TO_SYTM(pTm, &sTm);
#endif
	SXUNUSED(pUnused); /* cc warning */
	/* Expand */
	jx9_value_string_format(pVal, "%02d:%02d:%02d", sTm.tm_hour, sTm.tm_min, sTm.tm_sec);
}
/*
 * __DATE__
 *  Expand the current date in the ISO-8601 format.
 */
static void JX9_DATE_Const(jx9_value *pVal, void *pUnused)
{
	Sytm sTm;
#ifdef __WINNT__
	SYSTEMTIME sOS;
	GetSystemTime(&sOS);
	SYSTEMTIME_TO_SYTM(&sOS, &sTm);
#else
	struct tm *pTm;
	time_t t;
	time(&t);
	pTm = gmtime(&t);
	STRUCT_TM_TO_SYTM(pTm, &sTm);
#endif
	SXUNUSED(pUnused); /* cc warning */
	/* Expand */
	jx9_value_string_format(pVal, "%04d-%02d-%02d", sTm.tm_year, sTm.tm_mon+1, sTm.tm_mday);
}
/*
 * __FILE__
 *  Path of the processed script.
 */
static void JX9_FILE_Const(jx9_value *pVal, void *pUserData)
{
	jx9_vm *pVm = (jx9_vm *)pUserData;
	SyString *pFile;
	/* Peek the top entry */
	pFile = (SyString *)SySetPeek(&pVm->aFiles);
	if( pFile == 0 ){
		/* Expand the magic word: ":MEMORY:" */
		jx9_value_string(pVal, ":MEMORY:", (int)sizeof(":MEMORY:")-1);
	}else{
		jx9_value_string(pVal, pFile->zString, pFile->nByte);
	}
}
/*
 * __DIR__
 *  Directory holding the processed script.
 */
static void JX9_DIR_Const(jx9_value *pVal, void *pUserData)
{
	jx9_vm *pVm = (jx9_vm *)pUserData;
	SyString *pFile;
	/* Peek the top entry */
	pFile = (SyString *)SySetPeek(&pVm->aFiles);
	if( pFile == 0 ){
		/* Expand the magic word: ":MEMORY:" */
		jx9_value_string(pVal, ":MEMORY:", (int)sizeof(":MEMORY:")-1);
	}else{
		if( pFile->nByte > 0 ){
			const char *zDir;
			int nLen;
			zDir = jx9ExtractDirName(pFile->zString, (int)pFile->nByte, &nLen);
			jx9_value_string(pVal, zDir, nLen);
		}else{
			/* Expand '.' as the current directory*/
			jx9_value_string(pVal, ".", (int)sizeof(char));
		}
	}
}
/*
 * E_ERROR
 *  Expands 1
 */
static void JX9_E_ERROR_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 1);
	SXUNUSED(pUserData);
}
/*
 * E_WARNING
 *  Expands 2
 */
static void JX9_E_WARNING_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 2);
	SXUNUSED(pUserData);
}
/*
 * E_PARSE
 *  Expands 4
 */
static void JX9_E_PARSE_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 4);
	SXUNUSED(pUserData);
}
/*
 * E_NOTICE
 * Expands 8
 */
static void JX9_E_NOTICE_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 8);
	SXUNUSED(pUserData);
}
/*
 * CASE_LOWER
 *  Expands 0.
 */
static void JX9_CASE_LOWER_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 0);
	SXUNUSED(pUserData);
}
/*
 * CASE_UPPER
 *  Expands 1.
 */
static void JX9_CASE_UPPER_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 1);
	SXUNUSED(pUserData);
}
/*
 * STR_PAD_LEFT
 *  Expands 0.
 */
static void JX9_STR_PAD_LEFT_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 0);
	SXUNUSED(pUserData);
}
/*
 * STR_PAD_RIGHT
 *  Expands 1.
 */
static void JX9_STR_PAD_RIGHT_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 1);
	SXUNUSED(pUserData);
}
/*
 * STR_PAD_BOTH
 *  Expands 2.
 */
static void JX9_STR_PAD_BOTH_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 2);
	SXUNUSED(pUserData);
}
/*
 * COUNT_NORMAL
 *  Expands 0
 */
static void JX9_COUNT_NORMAL_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 0);
	SXUNUSED(pUserData);
}
/*
 * COUNT_RECURSIVE
 *  Expands 1.
 */
static void JX9_COUNT_RECURSIVE_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 1);
	SXUNUSED(pUserData);
}
/*
 * SORT_ASC
 *  Expands 1.
 */
static void JX9_SORT_ASC_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 1);
	SXUNUSED(pUserData);
}
/*
 * SORT_DESC
 *  Expands 2.
 */
static void JX9_SORT_DESC_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 2);
	SXUNUSED(pUserData);
}
/*
 * SORT_REGULAR
 *  Expands 3.
 */
static void JX9_SORT_REG_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 3);
	SXUNUSED(pUserData);
}
/*
 * SORT_NUMERIC
 *  Expands 4.
 */
static void JX9_SORT_NUMERIC_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 4);
	SXUNUSED(pUserData);
}
/*
 * SORT_STRING
 *  Expands 5.
 */
static void JX9_SORT_STRING_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 5);
	SXUNUSED(pUserData);
}
/*
 * JX9_ROUND_HALF_UP
 *  Expands 1.
 */
static void JX9_JX9_ROUND_HALF_UP_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 1);
	SXUNUSED(pUserData);
}
/*
 * SJX9_ROUND_HALF_DOWN
 *  Expands 2.
 */
static void JX9_JX9_ROUND_HALF_DOWN_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 2);
	SXUNUSED(pUserData);
}
/*
 * JX9_ROUND_HALF_EVEN
 *  Expands 3.
 */
static void JX9_JX9_ROUND_HALF_EVEN_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 3);
	SXUNUSED(pUserData);
}
/*
 * JX9_ROUND_HALF_ODD
 *  Expands 4.
 */
static void JX9_JX9_ROUND_HALF_ODD_Const(jx9_value *pVal, void *pUserData)
{
	jx9_value_int(pVal, 4);
	SXUNUSED(pUserData);
}
#ifdef JX9_ENABLE_MATH_FUNC
/*
 * PI
 *  Expand the value of pi.
 */
static void JX9_M_PI_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, JX9_PI);
}
/*
 * M_E
 *  Expand 2.7182818284590452354
 */
static void JX9_M_E_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 2.7182818284590452354);
}
/*
 * M_LOG2E
 *  Expand 2.7182818284590452354
 */
static void JX9_M_LOG2E_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 1.4426950408889634074);
}
/*
 * M_LOG10E
 *  Expand 0.4342944819032518276
 */
static void JX9_M_LOG10E_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 0.4342944819032518276);
}
/*
 * M_LN2
 *  Expand 	0.69314718055994530942
 */
static void JX9_M_LN2_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 0.69314718055994530942);
}
/*
 * M_LN10
 *  Expand 	2.30258509299404568402
 */
static void JX9_M_LN10_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 2.30258509299404568402);
}
/*
 * M_PI_2
 *  Expand 	1.57079632679489661923
 */
static void JX9_M_PI_2_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 1.57079632679489661923);
}
/*
 * M_PI_4
 *  Expand 	0.78539816339744830962
 */
static void JX9_M_PI_4_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 0.78539816339744830962);
}
/*
 * M_1_PI
 *  Expand 	0.31830988618379067154
 */
static void JX9_M_1_PI_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 0.31830988618379067154);
}
/*
 * M_2_PI
 *  Expand 0.63661977236758134308
 */
static void JX9_M_2_PI_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 0.63661977236758134308);
}
/*
 * M_SQRTPI
 *  Expand 1.77245385090551602729
 */
static void JX9_M_SQRTPI_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 1.77245385090551602729);
}
/*
 * M_2_SQRTPI
 *  Expand 	1.12837916709551257390
 */
static void JX9_M_2_SQRTPI_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 1.12837916709551257390);
}
/*
 * M_SQRT2
 *  Expand 	1.41421356237309504880
 */
static void JX9_M_SQRT2_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 1.41421356237309504880);
}
/*
 * M_SQRT3
 *  Expand 	1.73205080756887729352
 */
static void JX9_M_SQRT3_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 1.73205080756887729352);
}
/*
 * M_SQRT1_2
 *  Expand 	0.70710678118654752440
 */
static void JX9_M_SQRT1_2_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 0.70710678118654752440);
}
/*
 * M_LNPI
 *  Expand 	1.14472988584940017414
 */
static void JX9_M_LNPI_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 1.14472988584940017414);
}
/*
 * M_EULER
 *  Expand  0.57721566490153286061
 */
static void JX9_M_EULER_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_double(pVal, 0.57721566490153286061);
}
#endif /* JX9_DISABLE_BUILTIN_MATH */
/*
 * DATE_ATOM
 *  Expand Atom (example: 2005-08-15T15:52:01+00:00) 
 */
static void JX9_DATE_ATOM_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_string(pVal, "Y-m-d\\TH:i:sP", -1/*Compute length automatically*/);
}
/*
 * DATE_COOKIE
 *  HTTP Cookies (example: Monday, 15-Aug-05 15:52:01 UTC)  
 */
static void JX9_DATE_COOKIE_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_string(pVal, "l, d-M-y H:i:s T", -1/*Compute length automatically*/);
}
/*
 * DATE_ISO8601
 *  ISO-8601 (example: 2005-08-15T15:52:01+0000) 
 */
static void JX9_DATE_ISO8601_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_string(pVal, "Y-m-d\\TH:i:sO", -1/*Compute length automatically*/);
}
/*
 * DATE_RFC822
 *  RFC 822 (example: Mon, 15 Aug 05 15:52:01 +0000) 
 */
static void JX9_DATE_RFC822_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_string(pVal, "D, d M y H:i:s O", -1/*Compute length automatically*/);
}
/*
 * DATE_RFC850
 *  RFC 850 (example: Monday, 15-Aug-05 15:52:01 UTC) 
 */
static void JX9_DATE_RFC850_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_string(pVal, "l, d-M-y H:i:s T", -1/*Compute length automatically*/);
}
/*
 * DATE_RFC1036
 *  RFC 1123 (example: Mon, 15 Aug 2005 15:52:01 +0000) 
 */
static void JX9_DATE_RFC1036_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_string(pVal, "D, d M y H:i:s O", -1/*Compute length automatically*/);
}
/*
 * DATE_RFC1123
 *  RFC 1123 (example: Mon, 15 Aug 2005 15:52:01 +0000)  
 */
static void JX9_DATE_RFC1123_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_string(pVal, "D, d M Y H:i:s O", -1/*Compute length automatically*/);
}
/*
 * DATE_RFC2822
 *  RFC 2822 (Mon, 15 Aug 2005 15:52:01 +0000)  
 */
static void JX9_DATE_RFC2822_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_string(pVal, "D, d M Y H:i:s O", -1/*Compute length automatically*/);
}
/*
 * DATE_RSS
 *  RSS (Mon, 15 Aug 2005 15:52:01 +0000) 
 */
static void JX9_DATE_RSS_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_string(pVal, "D, d M Y H:i:s O", -1/*Compute length automatically*/);
}
/*
 * DATE_W3C
 *  World Wide Web Consortium (example: 2005-08-15T15:52:01+00:00) 
 */
static void JX9_DATE_W3C_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_string(pVal, "Y-m-d\\TH:i:sP", -1/*Compute length automatically*/);
}
/*
 * ENT_COMPAT
 *  Expand 0x01 (Must be a power of two)
 */
static void JX9_ENT_COMPAT_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x01);
}
/*
 * ENT_QUOTES
 *  Expand 0x02 (Must be a power of two)
 */
static void JX9_ENT_QUOTES_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x02);
}
/*
 * ENT_NOQUOTES
 *  Expand 0x04 (Must be a power of two)
 */
static void JX9_ENT_NOQUOTES_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x04);
}
/*
 * ENT_IGNORE
 *  Expand 0x08 (Must be a power of two)
 */
static void JX9_ENT_IGNORE_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x08);
}
/*
 * ENT_SUBSTITUTE
 *  Expand 0x10 (Must be a power of two)
 */
static void JX9_ENT_SUBSTITUTE_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x10);
}
/*
 * ENT_DISALLOWED
 *  Expand 0x20 (Must be a power of two)
 */
static void JX9_ENT_DISALLOWED_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x20);
}
/*
 * ENT_HTML401
 *  Expand 0x40 (Must be a power of two)
 */
static void JX9_ENT_HTML401_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x40);
}
/*
 * ENT_XML1
 *  Expand 0x80 (Must be a power of two)
 */
static void JX9_ENT_XML1_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x80);
}
/*
 * ENT_XHTML
 *  Expand 0x100 (Must be a power of two)
 */
static void JX9_ENT_XHTML_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x100);
}
/*
 * ENT_HTML5
 *  Expand 0x200 (Must be a power of two)
 */
static void JX9_ENT_HTML5_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x200);
}
/*
 * ISO-8859-1
 * ISO_8859_1
 *   Expand 1
 */
static void JX9_ISO88591_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 1);
}
/*
 * UTF-8
 * UTF8
 *  Expand 2
 */
static void JX9_UTF8_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 1);
}
/*
 * HTML_ENTITIES
 *  Expand 1
 */
static void JX9_HTML_ENTITIES_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 1);
}
/*
 * HTML_SPECIALCHARS
 *  Expand 2
 */
static void JX9_HTML_SPECIALCHARS_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 2);
}
/*
 * JX9_URL_SCHEME.
 * Expand 1
 */
static void JX9_JX9_URL_SCHEME_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 1);
}
/*
 * JX9_URL_HOST.
 * Expand 2
 */
static void JX9_JX9_URL_HOST_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 2);
}
/*
 * JX9_URL_PORT.
 * Expand 3
 */
static void JX9_JX9_URL_PORT_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 3);
}
/*
 * JX9_URL_USER.
 * Expand 4
 */
static void JX9_JX9_URL_USER_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 4);
}
/*
 * JX9_URL_PASS.
 * Expand 5
 */
static void JX9_JX9_URL_PASS_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 5);
}
/*
 * JX9_URL_PATH.
 * Expand 6
 */
static void JX9_JX9_URL_PATH_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 6);
}
/*
 * JX9_URL_QUERY.
 * Expand 7
 */
static void JX9_JX9_URL_QUERY_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 7);
}
/*
 * JX9_URL_FRAGMENT.
 * Expand 8
 */
static void JX9_JX9_URL_FRAGMENT_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 8);
}
/*
 * JX9_QUERY_RFC1738
 * Expand 1
 */
static void JX9_JX9_QUERY_RFC1738_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 1);
}
/*
 * JX9_QUERY_RFC3986
 * Expand 1
 */
static void JX9_JX9_QUERY_RFC3986_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 2);
}
/*
 * FNM_NOESCAPE
 *  Expand 0x01 (Must be a power of two)
 */
static void JX9_FNM_NOESCAPE_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x01);
}
/*
 * FNM_PATHNAME
 *  Expand 0x02 (Must be a power of two)
 */
static void JX9_FNM_PATHNAME_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x02);
}
/*
 * FNM_PERIOD
 *  Expand 0x04 (Must be a power of two)
 */
static void JX9_FNM_PERIOD_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x04);
}
/*
 * FNM_CASEFOLD
 *  Expand 0x08 (Must be a power of two)
 */
static void JX9_FNM_CASEFOLD_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x08);
}
/*
 * PATHINFO_DIRNAME
 *  Expand 1.
 */
static void JX9_PATHINFO_DIRNAME_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 1);
}
/*
 * PATHINFO_BASENAME
 *  Expand 2.
 */
static void JX9_PATHINFO_BASENAME_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 2);
}
/*
 * PATHINFO_EXTENSION
 *  Expand 3.
 */
static void JX9_PATHINFO_EXTENSION_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 3);
}
/*
 * PATHINFO_FILENAME
 *  Expand 4.
 */
static void JX9_PATHINFO_FILENAME_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 4);
}
/*
 * ASSERT_ACTIVE.
 *  Expand the value of JX9_ASSERT_ACTIVE defined in jx9Int.h
 */
static void JX9_ASSERT_ACTIVE_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, JX9_ASSERT_DISABLE);
}
/*
 * ASSERT_WARNING.
 *  Expand the value of JX9_ASSERT_WARNING defined in jx9Int.h
 */
static void JX9_ASSERT_WARNING_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, JX9_ASSERT_WARNING);
}
/*
 * ASSERT_BAIL.
 *  Expand the value of JX9_ASSERT_BAIL defined in jx9Int.h
 */
static void JX9_ASSERT_BAIL_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, JX9_ASSERT_BAIL);
}
/*
 * ASSERT_QUIET_EVAL.
 *  Expand the value of JX9_ASSERT_QUIET_EVAL defined in jx9Int.h
 */
static void JX9_ASSERT_QUIET_EVAL_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, JX9_ASSERT_QUIET_EVAL);
}
/*
 * ASSERT_CALLBACK.
 *  Expand the value of JX9_ASSERT_CALLBACK defined in jx9Int.h
 */
static void JX9_ASSERT_CALLBACK_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, JX9_ASSERT_CALLBACK);
}
/*
 * SEEK_SET.
 *  Expand 0
 */
static void JX9_SEEK_SET_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0);
}
/*
 * SEEK_CUR.
 *  Expand 1
 */
static void JX9_SEEK_CUR_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 1);
}
/*
 * SEEK_END.
 *  Expand 2
 */
static void JX9_SEEK_END_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 2);
}
/*
 * LOCK_SH.
 *  Expand 2
 */
static void JX9_LOCK_SH_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 1);
}
/*
 * LOCK_NB.
 *  Expand 5
 */
static void JX9_LOCK_NB_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 5);
}
/*
 * LOCK_EX.
 *  Expand 0x01 (MUST BE A POWER OF TWO)
 */
static void JX9_LOCK_EX_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x01);
}
/*
 * LOCK_UN.
 *  Expand 0
 */
static void JX9_LOCK_UN_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0);
}
/*
 * FILE_USE_INC_PATH
 *  Expand 0x01 (Must be a power of two)
 */
static void JX9_FILE_USE_INCLUDE_PATH_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x1);
}
/*
 * FILE_IGN_NL
 *  Expand 0x02 (Must be a power of two)
 */
static void JX9_FILE_IGNORE_NEW_LINES_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x2);
}
/*
 * FILE_SKIP_EL
 *  Expand 0x04 (Must be a power of two)
 */
static void JX9_FILE_SKIP_EMPTY_LINES_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x4);
}
/*
 * FILE_APPEND
 *  Expand 0x08 (Must be a power of two)
 */
static void JX9_FILE_APPEND_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x08);
}
/*
 * SCANDIR_SORT_ASCENDING
 *  Expand 0
 */
static void JX9_SCANDIR_SORT_ASCENDING_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0);
}
/*
 * SCANDIR_SORT_DESCENDING
 *  Expand 1
 */
static void JX9_SCANDIR_SORT_DESCENDING_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 1);
}
/*
 * SCANDIR_SORT_NONE
 *  Expand 2
 */
static void JX9_SCANDIR_SORT_NONE_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 2);
}
/*
 * GLOB_MARK
 *  Expand 0x01 (must be a power of two)
 */
static void JX9_GLOB_MARK_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x01);
}
/*
 * GLOB_NOSORT
 *  Expand 0x02 (must be a power of two)
 */
static void JX9_GLOB_NOSORT_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x02);
}
/*
 * GLOB_NOCHECK
 *  Expand 0x04 (must be a power of two)
 */
static void JX9_GLOB_NOCHECK_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x04);
}
/*
 * GLOB_NOESCAPE
 *  Expand 0x08 (must be a power of two)
 */
static void JX9_GLOB_NOESCAPE_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x08);
}
/*
 * GLOB_BRACE
 *  Expand 0x10 (must be a power of two)
 */
static void JX9_GLOB_BRACE_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x10);
}
/*
 * GLOB_ONLYDIR
 *  Expand 0x20 (must be a power of two)
 */
static void JX9_GLOB_ONLYDIR_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x20);
}
/*
 * GLOB_ERR
 *  Expand 0x40 (must be a power of two)
 */
static void JX9_GLOB_ERR_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x40);
}
/*
 * STDIN
 *  Expand the STDIN handle as a resource.
 */
static void JX9_STDIN_Const(jx9_value *pVal, void *pUserData)
{
	jx9_vm *pVm = (jx9_vm *)pUserData;
	void *pResource;
	pResource = jx9ExportStdin(pVm);
	jx9_value_resource(pVal, pResource);
}
/*
 * STDOUT
 *   Expand the STDOUT handle as a resource.
 */
static void JX9_STDOUT_Const(jx9_value *pVal, void *pUserData)
{
	jx9_vm *pVm = (jx9_vm *)pUserData;
	void *pResource;
	pResource = jx9ExportStdout(pVm);
	jx9_value_resource(pVal, pResource);
}
/*
 * STDERR
 *  Expand the STDERR handle as a resource.
 */
static void JX9_STDERR_Const(jx9_value *pVal, void *pUserData)
{
	jx9_vm *pVm = (jx9_vm *)pUserData;
	void *pResource;
	pResource = jx9ExportStderr(pVm);
	jx9_value_resource(pVal, pResource);
}
/*
 * INI_SCANNER_NORMAL
 *   Expand 1
 */
static void JX9_INI_SCANNER_NORMAL_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 1);
}
/*
 * INI_SCANNER_RAW
 *   Expand 2
 */
static void JX9_INI_SCANNER_RAW_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 2);
}
/*
 * EXTR_OVERWRITE
 *   Expand 0x01 (Must be a power of two)
 */
static void JX9_EXTR_OVERWRITE_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x1);
}
/*
 * EXTR_SKIP
 *   Expand 0x02 (Must be a power of two)
 */
static void JX9_EXTR_SKIP_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x2);
}
/*
 * EXTR_PREFIX_SAME
 *   Expand 0x04 (Must be a power of two)
 */
static void JX9_EXTR_PREFIX_SAME_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x4);
}
/*
 * EXTR_PREFIX_ALL
 *   Expand 0x08 (Must be a power of two)
 */
static void JX9_EXTR_PREFIX_ALL_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x8);
}
/*
 * EXTR_PREFIX_INVALID
 *   Expand 0x10 (Must be a power of two)
 */
static void JX9_EXTR_PREFIX_INVALID_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x10);
}
/*
 * EXTR_IF_EXISTS
 *   Expand 0x20 (Must be a power of two)
 */
static void JX9_EXTR_IF_EXISTS_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x20);
}
/*
 * EXTR_PREFIX_IF_EXISTS
 *   Expand 0x40 (Must be a power of two)
 */
static void JX9_EXTR_PREFIX_IF_EXISTS_Const(jx9_value *pVal, void *pUserData)
{
	SXUNUSED(pUserData); /* cc warning */
	jx9_value_int(pVal, 0x40);
}
/*
 * Table of built-in constants.
 */
static const jx9_builtin_constant aBuiltIn[] = {
	{"JX9_VERSION",          JX9_VER_Const      }, 
	{"JX9_ENGINE",           JX9_VER_Const      }, 
	{"__JX9__",              JX9_VER_Const      }, 
	{"JX9_OS",               JX9_OS_Const       }, 
	{"__OS__",               JX9_OS_Const       }, 
	{"JX9_EOL",              JX9_EOL_Const      }, 
	{"JX9_INT_MAX",          JX9_INTMAX_Const   }, 
	{"MAXINT",               JX9_INTMAX_Const   }, 
	{"JX9_INT_SIZE",         JX9_INTSIZE_Const  }, 
	{"PATH_SEPARATOR",       JX9_PATHSEP_Const  }, 
	{"DIRECTORY_SEPARATOR",  JX9_DIRSEP_Const   }, 
	{"DIR_SEP",              JX9_DIRSEP_Const   }, 
	{"__TIME__",             JX9_TIME_Const     }, 
	{"__DATE__",             JX9_DATE_Const     }, 
	{"__FILE__",             JX9_FILE_Const     }, 
	{"__DIR__",              JX9_DIR_Const      }, 
	{"E_ERROR",              JX9_E_ERROR_Const  }, 
	{"E_WARNING",            JX9_E_WARNING_Const}, 
	{"E_PARSE",              JX9_E_PARSE_Const  }, 
	{"E_NOTICE",             JX9_E_NOTICE_Const }, 
	{"CASE_LOWER",           JX9_CASE_LOWER_Const   }, 
	{"CASE_UPPER",           JX9_CASE_UPPER_Const   }, 
	{"STR_PAD_LEFT",         JX9_STR_PAD_LEFT_Const }, 
	{"STR_PAD_RIGHT",        JX9_STR_PAD_RIGHT_Const}, 
	{"STR_PAD_BOTH",         JX9_STR_PAD_BOTH_Const }, 
	{"COUNT_NORMAL",         JX9_COUNT_NORMAL_Const }, 
	{"COUNT_RECURSIVE",      JX9_COUNT_RECURSIVE_Const }, 
	{"SORT_ASC",             JX9_SORT_ASC_Const     }, 
	{"SORT_DESC",            JX9_SORT_DESC_Const    }, 
	{"SORT_REGULAR",         JX9_SORT_REG_Const     }, 
	{"SORT_NUMERIC",         JX9_SORT_NUMERIC_Const }, 
	{"SORT_STRING",          JX9_SORT_STRING_Const  }, 
	{"JX9_ROUND_HALF_DOWN",  JX9_JX9_ROUND_HALF_DOWN_Const }, 
	{"JX9_ROUND_HALF_EVEN",  JX9_JX9_ROUND_HALF_EVEN_Const }, 
	{"JX9_ROUND_HALF_UP",    JX9_JX9_ROUND_HALF_UP_Const   }, 
	{"JX9_ROUND_HALF_ODD",   JX9_JX9_ROUND_HALF_ODD_Const  }, 
#ifdef JX9_ENABLE_MATH_FUNC 
	{"PI",                 JX9_M_PI_Const         }, 
	{"M_E",                  JX9_M_E_Const          }, 
	{"M_LOG2E",              JX9_M_LOG2E_Const      }, 
	{"M_LOG10E",             JX9_M_LOG10E_Const     }, 
	{"M_LN2",                JX9_M_LN2_Const        }, 
	{"M_LN10",               JX9_M_LN10_Const       }, 
	{"M_PI_2",               JX9_M_PI_2_Const       }, 
	{"M_PI_4",               JX9_M_PI_4_Const       }, 
	{"M_1_PI",               JX9_M_1_PI_Const       }, 
	{"M_2_PI",               JX9_M_2_PI_Const       }, 
	{"M_SQRTPI",             JX9_M_SQRTPI_Const     }, 
	{"M_2_SQRTPI",           JX9_M_2_SQRTPI_Const   }, 
	{"M_SQRT2",              JX9_M_SQRT2_Const      }, 
	{"M_SQRT3",              JX9_M_SQRT3_Const      }, 
	{"M_SQRT1_2",            JX9_M_SQRT1_2_Const    }, 
	{"M_LNPI",               JX9_M_LNPI_Const       }, 
	{"M_EULER",              JX9_M_EULER_Const      }, 
#endif /* JX9_ENABLE_MATH_FUNC */
	{"DATE_ATOM",            JX9_DATE_ATOM_Const    }, 
	{"DATE_COOKIE",          JX9_DATE_COOKIE_Const  }, 
	{"DATE_ISO8601",         JX9_DATE_ISO8601_Const }, 
	{"DATE_RFC822",          JX9_DATE_RFC822_Const  }, 
	{"DATE_RFC850",          JX9_DATE_RFC850_Const  }, 
	{"DATE_RFC1036",         JX9_DATE_RFC1036_Const }, 
	{"DATE_RFC1123",         JX9_DATE_RFC1123_Const }, 
	{"DATE_RFC2822",         JX9_DATE_RFC2822_Const }, 
	{"DATE_RFC3339",         JX9_DATE_ATOM_Const    }, 
	{"DATE_RSS",             JX9_DATE_RSS_Const     }, 
	{"DATE_W3C",             JX9_DATE_W3C_Const     }, 
	{"ENT_COMPAT",           JX9_ENT_COMPAT_Const   }, 
	{"ENT_QUOTES",           JX9_ENT_QUOTES_Const   }, 
	{"ENT_NOQUOTES",         JX9_ENT_NOQUOTES_Const }, 
	{"ENT_IGNORE",           JX9_ENT_IGNORE_Const   }, 
	{"ENT_SUBSTITUTE",       JX9_ENT_SUBSTITUTE_Const}, 
	{"ENT_DISALLOWED",       JX9_ENT_DISALLOWED_Const}, 
	{"ENT_HTML401",          JX9_ENT_HTML401_Const  }, 
	{"ENT_XML1",             JX9_ENT_XML1_Const     }, 
	{"ENT_XHTML",            JX9_ENT_XHTML_Const    }, 
	{"ENT_HTML5",            JX9_ENT_HTML5_Const    }, 
	{"ISO-8859-1",           JX9_ISO88591_Const     }, 
	{"ISO_8859_1",           JX9_ISO88591_Const     }, 
	{"UTF-8",                JX9_UTF8_Const         }, 
	{"UTF8",                 JX9_UTF8_Const         }, 
	{"HTML_ENTITIES",        JX9_HTML_ENTITIES_Const}, 
	{"HTML_SPECIALCHARS",    JX9_HTML_SPECIALCHARS_Const }, 
	{"JX9_URL_SCHEME",       JX9_JX9_URL_SCHEME_Const}, 
	{"JX9_URL_HOST",         JX9_JX9_URL_HOST_Const}, 
	{"JX9_URL_PORT",         JX9_JX9_URL_PORT_Const}, 
	{"JX9_URL_USER",         JX9_JX9_URL_USER_Const}, 
	{"JX9_URL_PASS",         JX9_JX9_URL_PASS_Const}, 
	{"JX9_URL_PATH",         JX9_JX9_URL_PATH_Const}, 
	{"JX9_URL_QUERY",        JX9_JX9_URL_QUERY_Const}, 
	{"JX9_URL_FRAGMENT",     JX9_JX9_URL_FRAGMENT_Const}, 
	{"JX9_QUERY_RFC1738",    JX9_JX9_QUERY_RFC1738_Const}, 
	{"JX9_QUERY_RFC3986",    JX9_JX9_QUERY_RFC3986_Const}, 
	{"FNM_NOESCAPE",         JX9_FNM_NOESCAPE_Const }, 
	{"FNM_PATHNAME",         JX9_FNM_PATHNAME_Const }, 
	{"FNM_PERIOD",           JX9_FNM_PERIOD_Const   }, 
	{"FNM_CASEFOLD",         JX9_FNM_CASEFOLD_Const }, 
	{"PATHINFO_DIRNAME",     JX9_PATHINFO_DIRNAME_Const  }, 
	{"PATHINFO_BASENAME",    JX9_PATHINFO_BASENAME_Const }, 
	{"PATHINFO_EXTENSION",   JX9_PATHINFO_EXTENSION_Const}, 
	{"PATHINFO_FILENAME",    JX9_PATHINFO_FILENAME_Const }, 
	{"ASSERT_ACTIVE",        JX9_ASSERT_ACTIVE_Const     }, 
	{"ASSERT_WARNING",       JX9_ASSERT_WARNING_Const    }, 
	{"ASSERT_BAIL",          JX9_ASSERT_BAIL_Const       }, 
	{"ASSERT_QUIET_EVAL",    JX9_ASSERT_QUIET_EVAL_Const }, 
	{"ASSERT_CALLBACK",      JX9_ASSERT_CALLBACK_Const   }, 
	{"SEEK_SET",             JX9_SEEK_SET_Const      }, 
	{"SEEK_CUR",             JX9_SEEK_CUR_Const      }, 
	{"SEEK_END",             JX9_SEEK_END_Const      }, 
	{"LOCK_EX",              JX9_LOCK_EX_Const      }, 
	{"LOCK_SH",              JX9_LOCK_SH_Const      }, 
	{"LOCK_NB",              JX9_LOCK_NB_Const      }, 
	{"LOCK_UN",              JX9_LOCK_UN_Const      }, 
	{"FILE_USE_INC_PATH",    JX9_FILE_USE_INCLUDE_PATH_Const}, 
	{"FILE_IGN_NL",          JX9_FILE_IGNORE_NEW_LINES_Const}, 
	{"FILE_SKIP_EL",         JX9_FILE_SKIP_EMPTY_LINES_Const}, 
	{"FILE_APPEND",          JX9_FILE_APPEND_Const }, 
	{"SCANDIR_SORT_ASC",     JX9_SCANDIR_SORT_ASCENDING_Const  }, 
	{"SCANDIR_SORT_DESC",    JX9_SCANDIR_SORT_DESCENDING_Const }, 
	{"SCANDIR_SORT_NONE",    JX9_SCANDIR_SORT_NONE_Const }, 
	{"GLOB_MARK",            JX9_GLOB_MARK_Const    }, 
	{"GLOB_NOSORT",          JX9_GLOB_NOSORT_Const  }, 
	{"GLOB_NOCHECK",         JX9_GLOB_NOCHECK_Const }, 
	{"GLOB_NOESCAPE",        JX9_GLOB_NOESCAPE_Const}, 
	{"GLOB_BRACE",           JX9_GLOB_BRACE_Const   }, 
	{"GLOB_ONLYDIR",         JX9_GLOB_ONLYDIR_Const }, 
	{"GLOB_ERR",             JX9_GLOB_ERR_Const     }, 
	{"STDIN",                JX9_STDIN_Const        }, 
	{"stdin",                JX9_STDIN_Const        }, 
	{"STDOUT",               JX9_STDOUT_Const       }, 
	{"stdout",               JX9_STDOUT_Const       }, 
	{"STDERR",               JX9_STDERR_Const       }, 
	{"stderr",               JX9_STDERR_Const       }, 
	{"INI_SCANNER_NORMAL",   JX9_INI_SCANNER_NORMAL_Const }, 
	{"INI_SCANNER_RAW",      JX9_INI_SCANNER_RAW_Const    }, 
	{"EXTR_OVERWRITE",       JX9_EXTR_OVERWRITE_Const     }, 
	{"EXTR_SKIP",            JX9_EXTR_SKIP_Const        }, 
	{"EXTR_PREFIX_SAME",     JX9_EXTR_PREFIX_SAME_Const }, 
	{"EXTR_PREFIX_ALL",      JX9_EXTR_PREFIX_ALL_Const  }, 
	{"EXTR_PREFIX_INVALID",  JX9_EXTR_PREFIX_INVALID_Const }, 
	{"EXTR_IF_EXISTS",       JX9_EXTR_IF_EXISTS_Const   }, 
	{"EXTR_PREFIX_IF_EXISTS", JX9_EXTR_PREFIX_IF_EXISTS_Const}
};
/*
 * Register the built-in constants defined above.
 */
JX9_PRIVATE void jx9RegisterBuiltInConstant(jx9_vm *pVm)
{
	sxu32 n;
	/* 
	 * Note that all built-in constants have access to the jx9 virtual machine
	 * that trigger the constant invocation as their private data.
	 */
	for( n = 0 ; n < SX_ARRAYSIZE(aBuiltIn) ; ++n ){
		jx9_create_constant(&(*pVm), aBuiltIn[n].zName, aBuiltIn[n].xExpand, &(*pVm));
	}
}
