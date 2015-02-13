/*
 * Symisc JX9: A Highly Efficient Embeddable Scripting Engine Based on JSON.
 * Copyright (C) 2012-2013, Symisc Systems http://jx9.symisc.net/
 * Version 1.7.2
 * For information on licensing, redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES
 * please contact Symisc Systems via:
 *       legal@symisc.net
 *       licensing@symisc.net
 *       contact@symisc.net
 * or visit:
 *      http://jx9.symisc.net/
 */
 /* $SymiscID: jx9Int.h v1.9 FreeBSD 2012-08-13 23:25 devel <chm@symisc.net> $ */
#ifndef __JX9INT_H__
#define __JX9INT_H__
/* Internal interface definitions for JX9. */
#ifdef JX9_AMALGAMATION
#ifndef JX9_PRIVATE
/* Marker for routines not intended for external use */
#define JX9_PRIVATE static
#endif /* JX9_PRIVATE */
#else
#define JX9_PRIVATE
#include "jx9.h"
#endif 
#ifndef JX9_PI
/* Value of PI */
#define JX9_PI 3.1415926535898
#endif
/*
 * Constants for the largest and smallest possible 64-bit signed integers.
 * These macros are designed to work correctly on both 32-bit and 64-bit
 * compilers.
 */
#ifndef LARGEST_INT64
#define LARGEST_INT64  (0xffffffff|(((sxi64)0x7fffffff)<<32))
#endif
#ifndef SMALLEST_INT64
#define SMALLEST_INT64 (((sxi64)-1) - LARGEST_INT64)
#endif
/* Forward declaration of private structures */
typedef struct jx9_foreach_info   jx9_foreach_info;
typedef struct jx9_foreach_step   jx9_foreach_step;
typedef struct jx9_hashmap_node   jx9_hashmap_node;
typedef struct jx9_hashmap        jx9_hashmap;
/* Symisc Standard types */
#if !defined(SYMISC_STD_TYPES)
#define SYMISC_STD_TYPES
#ifdef __WINNT__
/* Disable nuisance warnings on Borland compilers */
#if defined(__BORLANDC__)
#pragma warn -rch /* unreachable code */
#pragma warn -ccc /* Condition is always true or false */
#pragma warn -aus /* Assigned value is never used */
#pragma warn -csu /* Comparing signed and unsigned */
#pragma warn -spa /* Suspicious pointer arithmetic */
#endif
#endif
typedef signed char        sxi8; /* signed char */
typedef unsigned char      sxu8; /* unsigned char */
typedef signed short int   sxi16; /* 16 bits(2 bytes) signed integer */
typedef unsigned short int sxu16; /* 16 bits(2 bytes) unsigned integer */
typedef int                sxi32; /* 32 bits(4 bytes) integer */
typedef unsigned int       sxu32; /* 32 bits(4 bytes) unsigned integer */
typedef long               sxptr;
typedef unsigned long      sxuptr;
typedef long               sxlong;
typedef unsigned long      sxulong;
typedef sxi32              sxofft;
typedef sxi64              sxofft64;
typedef long double	       sxlongreal;
typedef double             sxreal;
#define SXI8_HIGH       0x7F
#define SXU8_HIGH       0xFF
#define SXI16_HIGH      0x7FFF
#define SXU16_HIGH      0xFFFF
#define SXI32_HIGH      0x7FFFFFFF
#define SXU32_HIGH      0xFFFFFFFF
#define SXI64_HIGH      0x7FFFFFFFFFFFFFFF
#define SXU64_HIGH      0xFFFFFFFFFFFFFFFF 
#if !defined(TRUE)
#define TRUE 1
#endif
#if !defined(FALSE)
#define FALSE 0
#endif
/*
 * The following macros are used to cast pointers to integers and
 * integers to pointers.
 */
#if defined(__PTRDIFF_TYPE__)  
# define SX_INT_TO_PTR(X)  ((void*)(__PTRDIFF_TYPE__)(X))
# define SX_PTR_TO_INT(X)  ((int)(__PTRDIFF_TYPE__)(X))
#elif !defined(__GNUC__)    
# define SX_INT_TO_PTR(X)  ((void*)&((char*)0)[X])
# define SX_PTR_TO_INT(X)  ((int)(((char*)X)-(char*)0))
#else                       
# define SX_INT_TO_PTR(X)  ((void*)(X))
# define SX_PTR_TO_INT(X)  ((int)(X))
#endif
#define SXMIN(a, b)  ((a < b) ? (a) : (b))
#define SXMAX(a, b)  ((a < b) ? (b) : (a))
#endif /* SYMISC_STD_TYPES */
/* Symisc Run-time API private definitions */
#if !defined(SYMISC_PRIVATE_DEFS)
#define SYMISC_PRIVATE_DEFS

typedef sxi32 (*ProcRawStrCmp)(const SyString *, const SyString *);
#define SyStringData(RAW)	((RAW)->zString)
#define SyStringLength(RAW)	((RAW)->nByte)
#define SyStringInitFromBuf(RAW, ZBUF, NLEN){\
	(RAW)->zString 	= (const char *)ZBUF;\
	(RAW)->nByte	= (sxu32)(NLEN);\
}
#define SyStringUpdatePtr(RAW, NBYTES){\
	if( NBYTES > (RAW)->nByte ){\
		(RAW)->nByte = 0;\
	}else{\
		(RAW)->zString += NBYTES;\
		(RAW)->nByte -= NBYTES;\
	}\
}
#define SyStringDupPtr(RAW1, RAW2)\
	(RAW1)->zString = (RAW2)->zString;\
	(RAW1)->nByte = (RAW2)->nByte;

#define SyStringTrimLeadingChar(RAW, CHAR)\
	while((RAW)->nByte > 0 && (RAW)->zString[0] == CHAR ){\
			(RAW)->zString++;\
			(RAW)->nByte--;\
	}
#define SyStringTrimTrailingChar(RAW, CHAR)\
	while((RAW)->nByte > 0 && (RAW)->zString[(RAW)->nByte - 1] == CHAR){\
		(RAW)->nByte--;\
	}
#define SyStringCmp(RAW1, RAW2, xCMP)\
	(((RAW1)->nByte == (RAW2)->nByte) ? xCMP((RAW1)->zString, (RAW2)->zString, (RAW2)->nByte) : (sxi32)((RAW1)->nByte - (RAW2)->nByte))

#define SyStringCmp2(RAW1, RAW2, xCMP)\
	(((RAW1)->nByte >= (RAW2)->nByte) ? xCMP((RAW1)->zString, (RAW2)->zString, (RAW2)->nByte) : (sxi32)((RAW2)->nByte - (RAW1)->nByte))

#define SyStringCharCmp(RAW, CHAR) \
	(((RAW)->nByte == sizeof(char)) ? ((RAW)->zString[0] == CHAR ? 0 : CHAR - (RAW)->zString[0]) : ((RAW)->zString[0] == CHAR ? 0 : (RAW)->nByte - sizeof(char)))

#define SX_ADDR(PTR)    ((sxptr)PTR)
#define SX_ARRAYSIZE(X) (sizeof(X)/sizeof(X[0]))
#define SXUNUSED(P)	(P = 0)
#define	SX_EMPTY(PTR)   (PTR == 0)
#define SX_EMPTY_STR(STR) (STR == 0 || STR[0] == 0 )
typedef struct SyMemBackend SyMemBackend;
typedef struct SyBlob SyBlob;
typedef struct SySet SySet;
/* Standard function signatures */
typedef sxi32 (*ProcCmp)(const void *, const void *, sxu32);
typedef sxi32 (*ProcPatternMatch)(const char *, sxu32, const char *, sxu32, sxu32 *);
typedef sxi32 (*ProcSearch)(const void *, sxu32, const void *, sxu32, ProcCmp, sxu32 *);
typedef sxu32 (*ProcHash)(const void *, sxu32);
typedef sxi32 (*ProcHashSum)(const void *, sxu32, unsigned char *, sxu32);
typedef sxi32 (*ProcSort)(void *, sxu32, sxu32, ProcCmp);
#define MACRO_LIST_PUSH(Head, Item)\
	Item->pNext = Head;\
	Head = Item; 
#define MACRO_LD_PUSH(Head, Item)\
	if( Head == 0 ){\
		Head = Item;\
	}else{\
		Item->pNext = Head;\
		Head->pPrev = Item;\
		Head = Item;\
	}
#define MACRO_LD_REMOVE(Head, Item)\
	if( Head == Item ){\
		Head = Head->pNext;\
	}\
	if( Item->pPrev ){ Item->pPrev->pNext = Item->pNext;}\
	if( Item->pNext ){ Item->pNext->pPrev = Item->pPrev;}
/*
 * A generic dynamic set.
 */
struct SySet
{
	SyMemBackend *pAllocator; /* Memory backend */
	void *pBase;              /* Base pointer */	
	sxu32 nUsed;              /* Total number of used slots  */
	sxu32 nSize;              /* Total number of available slots */
	sxu32 eSize;              /* Size of a single slot */
	sxu32 nCursor;	          /* Loop cursor */	
	void *pUserData;          /* User private data associated with this container */
};
#define SySetBasePtr(S)           ((S)->pBase)
#define SySetBasePtrJump(S, OFFT)  (&((char *)(S)->pBase)[OFFT*(S)->eSize])
#define SySetUsed(S)              ((S)->nUsed)
#define SySetSize(S)              ((S)->nSize)
#define SySetElemSize(S)          ((S)->eSize) 
#define SySetCursor(S)            ((S)->nCursor)
#define SySetGetAllocator(S)      ((S)->pAllocator)
#define SySetSetUserData(S, DATA)  ((S)->pUserData = DATA)
#define SySetGetUserData(S)       ((S)->pUserData)
/*
 * A variable length containers for generic data.
 */
struct SyBlob
{
	SyMemBackend *pAllocator; /* Memory backend */
	void   *pBlob;	          /* Base pointer */
	sxu32  nByte;	          /* Total number of used bytes */
	sxu32  mByte;	          /* Total number of available bytes */
	sxu32  nFlags;	          /* Blob internal flags, see below */
};
#define SXBLOB_LOCKED	0x01	/* Blob is locked [i.e: Cannot auto grow] */
#define SXBLOB_STATIC	0x02	/* Not allocated from heap   */
#define SXBLOB_RDONLY   0x04    /* Read-Only data */

#define SyBlobFreeSpace(BLOB)	 ((BLOB)->mByte - (BLOB)->nByte)
#define SyBlobLength(BLOB)	     ((BLOB)->nByte)
#define SyBlobData(BLOB)	     ((BLOB)->pBlob)
#define SyBlobCurData(BLOB)	     ((void*)(&((char*)(BLOB)->pBlob)[(BLOB)->nByte]))
#define SyBlobDataAt(BLOB, OFFT)	 ((void *)(&((char *)(BLOB)->pBlob)[OFFT]))
#define SyBlobGetAllocator(BLOB) ((BLOB)->pAllocator)

#define SXMEM_POOL_INCR			3
#define SXMEM_POOL_NBUCKETS		12
#define SXMEM_BACKEND_MAGIC	0xBAC3E67D
#define SXMEM_BACKEND_CORRUPT(BACKEND)	(BACKEND == 0 || BACKEND->nMagic != SXMEM_BACKEND_MAGIC)

#define SXMEM_BACKEND_RETRY	3
/* A memory backend subsystem is defined by an instance of the following structures */
typedef union SyMemHeader SyMemHeader;
typedef struct SyMemBlock SyMemBlock;
struct SyMemBlock
{
	SyMemBlock *pNext, *pPrev; /* Chain of allocated memory blocks */
#ifdef UNTRUST
	sxu32 nGuard;             /* magic number associated with each valid block, so we
							   * can detect misuse.
							   */
#endif
};
/*
 * Header associated with each valid memory pool block.
 */
union SyMemHeader
{
	SyMemHeader *pNext; /* Next chunk of size 1 << (nBucket + SXMEM_POOL_INCR) in the list */
	sxu32 nBucket;      /* Bucket index in aPool[] */
};
struct SyMemBackend
{
	const SyMutexMethods *pMutexMethods; /* Mutex methods */
	const SyMemMethods *pMethods;  /* Memory allocation methods */
	SyMemBlock *pBlocks;           /* List of valid memory blocks */
	sxu32 nBlock;                  /* Total number of memory blocks allocated so far */
	ProcMemError xMemError;        /* Out-of memory callback */
	void *pUserData;               /* First arg to xMemError() */
	SyMutex *pMutex;               /* Per instance mutex */
	sxu32 nMagic;                  /* Sanity check against misuse */
	SyMemHeader *apPool[SXMEM_POOL_NBUCKETS+SXMEM_POOL_INCR]; /* Pool of memory chunks */
};
/* Mutex types */
#define SXMUTEX_TYPE_FAST	1
#define SXMUTEX_TYPE_RECURSIVE	2
#define SXMUTEX_TYPE_STATIC_1	3
#define SXMUTEX_TYPE_STATIC_2	4
#define SXMUTEX_TYPE_STATIC_3	5
#define SXMUTEX_TYPE_STATIC_4	6
#define SXMUTEX_TYPE_STATIC_5	7
#define SXMUTEX_TYPE_STATIC_6	8

#define SyMutexGlobalInit(METHOD){\
	if( (METHOD)->xGlobalInit ){\
	(METHOD)->xGlobalInit();\
	}\
}
#define SyMutexGlobalRelease(METHOD){\
	if( (METHOD)->xGlobalRelease ){\
	(METHOD)->xGlobalRelease();\
	}\
}
#define SyMutexNew(METHOD, TYPE)			(METHOD)->xNew(TYPE)
#define SyMutexRelease(METHOD, MUTEX){\
	if( MUTEX && (METHOD)->xRelease ){\
		(METHOD)->xRelease(MUTEX);\
	}\
}
#define SyMutexEnter(METHOD, MUTEX){\
	if( MUTEX ){\
	(METHOD)->xEnter(MUTEX);\
	}\
}
#define SyMutexTryEnter(METHOD, MUTEX){\
	if( MUTEX && (METHOD)->xTryEnter ){\
	(METHOD)->xTryEnter(MUTEX);\
	}\
}
#define SyMutexLeave(METHOD, MUTEX){\
	if( MUTEX ){\
	(METHOD)->xLeave(MUTEX);\
	}\
}
/* Comparison, byte swap, byte copy macros */
#define SX_MACRO_FAST_CMP(X1, X2, SIZE, RC){\
	register unsigned char *r1 = (unsigned char *)X1;\
	register unsigned char *r2 = (unsigned char *)X2;\
	register sxu32 LEN = SIZE;\
	for(;;){\
	  if( !LEN ){ break; }if( r1[0] != r2[0] ){ break; } r1++; r2++; LEN--;\
	  if( !LEN ){ break; }if( r1[0] != r2[0] ){ break; } r1++; r2++; LEN--;\
	  if( !LEN ){ break; }if( r1[0] != r2[0] ){ break; } r1++; r2++; LEN--;\
	  if( !LEN ){ break; }if( r1[0] != r2[0] ){ break; } r1++; r2++; LEN--;\
	}\
	RC = !LEN ? 0 : r1[0] - r2[0];\
}
#define	SX_MACRO_FAST_MEMCPY(SRC, DST, SIZ){\
	register unsigned char *xSrc = (unsigned char *)SRC;\
	register unsigned char *xDst = (unsigned char *)DST;\
	register sxu32 xLen = SIZ;\
	for(;;){\
	    if( !xLen ){ break; }xDst[0] = xSrc[0]; xDst++; xSrc++; --xLen;\
		if( !xLen ){ break; }xDst[0] = xSrc[0]; xDst++; xSrc++; --xLen;\
		if( !xLen ){ break; }xDst[0] = xSrc[0]; xDst++; xSrc++; --xLen;\
		if( !xLen ){ break; }xDst[0] = xSrc[0]; xDst++; xSrc++; --xLen;\
	}\
}
#define SX_MACRO_BYTE_SWAP(X, Y, Z){\
	register unsigned char *s = (unsigned char *)X;\
	register unsigned char *d = (unsigned char *)Y;\
	sxu32	ZLong = Z;  \
	sxi32 c; \
	for(;;){\
	  if(!ZLong){ break; } c = s[0] ; s[0] = d[0]; d[0] = (unsigned char)c; s++; d++; --ZLong;\
	  if(!ZLong){ break; } c = s[0] ; s[0] = d[0]; d[0] = (unsigned char)c; s++; d++; --ZLong;\
	  if(!ZLong){ break; } c = s[0] ; s[0] = d[0]; d[0] = (unsigned char)c; s++; d++; --ZLong;\
	  if(!ZLong){ break; } c = s[0] ; s[0] = d[0]; d[0] = (unsigned char)c; s++; d++; --ZLong;\
	}\
}
#define SX_MSEC_PER_SEC	(1000)			/* Millisec per seconds */
#define SX_USEC_PER_SEC	(1000000)		/* Microsec per seconds */
#define SX_NSEC_PER_SEC	(1000000000)	/* Nanosec per seconds */
#endif /* SYMISC_PRIVATE_DEFS */
/* Symisc Run-time API auxiliary definitions */
#if !defined(SYMISC_PRIVATE_AUX_DEFS)
#define SYMISC_PRIVATE_AUX_DEFS

typedef struct SyHashEntry_Pr SyHashEntry_Pr;
typedef struct SyHashEntry SyHashEntry;
typedef struct SyHash SyHash;
/*
 * Each public hashtable entry is represented by an instance
 * of the following structure.
 */
struct SyHashEntry
{
	const void *pKey; /* Hash key */
	sxu32 nKeyLen;    /* Key length */
	void *pUserData;  /* User private data */
};
#define SyHashEntryGetUserData(ENTRY) ((ENTRY)->pUserData)
#define SyHashEntryGetKey(ENTRY)      ((ENTRY)->pKey)
/* Each active hashtable is identified by an instance of the following structure */
struct SyHash
{
	SyMemBackend *pAllocator;         /* Memory backend */
	ProcHash xHash;                   /* Hash function */
	ProcCmp xCmp;                     /* Comparison function */
	SyHashEntry_Pr *pList, *pCurrent;  /* Linked list of hash entries user for linear traversal */
	sxu32 nEntry;                     /* Total number of entries */
	SyHashEntry_Pr **apBucket;        /* Hash buckets */
	sxu32 nBucketSize;                /* Current bucket size */
};
#define SXHASH_BUCKET_SIZE 16 /* Initial bucket size: must be a power of two */
#define SXHASH_FILL_FACTOR 3
/* Hash access macro */
#define SyHashFunc(HASH)		((HASH)->xHash)
#define SyHashCmpFunc(HASH)		((HASH)->xCmp)
#define SyHashTotalEntry(HASH)	((HASH)->nEntry)
#define SyHashGetPool(HASH)		((HASH)->pAllocator)
/*
 * An instance of the following structure define a single context
 * for an Pseudo Random Number Generator.
 *
 * Nothing in this file or anywhere else in the library does any kind of
 * encryption.  The RC4 algorithm is being used as a PRNG (pseudo-random
 * number generator) not as an encryption device.
 * This implementation is taken from the SQLite3 source tree.
 */
typedef struct SyPRNGCtx SyPRNGCtx;
struct SyPRNGCtx
{
    sxu8 i, j;				/* State variables */
    unsigned char s[256];   /* State variables */
	sxu16 nMagic;			/* Sanity check */
 };
typedef sxi32 (*ProcRandomSeed)(void *, unsigned int, void *);
/* High resolution timer.*/
typedef struct sytime sytime;
struct sytime
{
	long tm_sec;	/* seconds */
	long tm_usec;	/* microseconds */
};
/* Forward declaration */
typedef struct SyStream SyStream;
typedef struct SyToken  SyToken;
typedef struct SyLex    SyLex;
/*
 * Tokenizer callback signature.
 */
typedef sxi32 (*ProcTokenizer)(SyStream *, SyToken *, void *, void *);
/*
 * Each token in the input is represented by an instance
 * of the following structure.
 */
struct SyToken
{
	SyString sData;  /* Token text and length */
	sxu32 nType;     /* Token type */
	sxu32 nLine;     /* Token line number */
	void *pUserData; /* User private data associated with this token */
};
/*
 * During tokenization, information about the state of the input
 * stream is held in an instance of the following structure.
 */
struct SyStream
{
	const unsigned char *zInput; /* Complete text of the input */
	const unsigned char *zText; /* Current input we are processing */	
	const unsigned char *zEnd; /* End of input marker */
	sxu32  nLine; /* Total number of processed lines */
	sxu32  nIgn; /* Total number of ignored tokens */
	SySet *pSet; /* Token containers */
};
/*
 * Each lexer is represented by an instance of the following structure.
 */
struct SyLex
{
	SyStream sStream;         /* Input stream */
	ProcTokenizer xTokenizer; /* Tokenizer callback */
	void * pUserData;         /* Third argument to xTokenizer() */
	SySet *pTokenSet;         /* Token set */
};
#define SyLexTotalToken(LEX)    SySetTotalEntry(&(LEX)->aTokenSet)
#define SyLexTotalLines(LEX)    ((LEX)->sStream.nLine)
#define SyLexTotalIgnored(LEX)  ((LEX)->sStream.nIgn)
#define XLEX_IN_LEN(STREAM)     (sxu32)(STREAM->zEnd - STREAM->zText)
#endif /* SYMISC_PRIVATE_AUX_DEFS */
/*
** Notes on UTF-8 (According to SQLite3 authors):
**
**   Byte-0    Byte-1    Byte-2    Byte-3    Value
**  0xxxxxxx                                 00000000 00000000 0xxxxxxx
**  110yyyyy  10xxxxxx                       00000000 00000yyy yyxxxxxx
**  1110zzzz  10yyyyyy  10xxxxxx             00000000 zzzzyyyy yyxxxxxx
**  11110uuu  10uuzzzz  10yyyyyy  10xxxxxx   000uuuuu zzzzyyyy yyxxxxxx
**
*/
/*
** Assuming zIn points to the first byte of a UTF-8 character, 
** advance zIn to point to the first byte of the next UTF-8 character.
*/
#define SX_JMP_UTF8(zIn, zEnd)\
	while(zIn < zEnd && (((unsigned char)zIn[0] & 0xc0) == 0x80) ){ zIn++; }
#define SX_WRITE_UTF8(zOut, c) {                       \
  if( c<0x00080 ){                                     \
    *zOut++ = (sxu8)(c&0xFF);                          \
  }else if( c<0x00800 ){                               \
    *zOut++ = 0xC0 + (sxu8)((c>>6)&0x1F);              \
    *zOut++ = 0x80 + (sxu8)(c & 0x3F);                 \
  }else if( c<0x10000 ){                               \
    *zOut++ = 0xE0 + (sxu8)((c>>12)&0x0F);             \
    *zOut++ = 0x80 + (sxu8)((c>>6) & 0x3F);            \
    *zOut++ = 0x80 + (sxu8)(c & 0x3F);                 \
  }else{                                               \
    *zOut++ = 0xF0 + (sxu8)((c>>18) & 0x07);           \
    *zOut++ = 0x80 + (sxu8)((c>>12) & 0x3F);           \
    *zOut++ = 0x80 + (sxu8)((c>>6) & 0x3F);            \
    *zOut++ = 0x80 + (sxu8)(c & 0x3F);                 \
  }                                                    \
}
/* Rely on the standard ctype */
#include <ctype.h>
#define SyToUpper(c) toupper(c) 
#define SyToLower(c) tolower(c) 
#define SyisUpper(c) isupper(c)
#define SyisLower(c) islower(c)
#define SyisSpace(c) isspace(c)
#define SyisBlank(c) isspace(c)
#define SyisAlpha(c) isalpha(c)
#define SyisDigit(c) isdigit(c)
#define SyisHex(c)	 isxdigit(c)
#define SyisPrint(c) isprint(c)
#define SyisPunct(c) ispunct(c)
#define SyisSpec(c)	 iscntrl(c)
#define SyisCtrl(c)	 iscntrl(c)
#define SyisAscii(c) isascii(c)
#define SyisAlphaNum(c) isalnum(c)
#define SyisGraph(c)     isgraph(c)
#define SyDigToHex(c)    "0123456789ABCDEF"[c & 0x0F] 		
#define SyDigToInt(c)     ((c < 0xc0 && SyisDigit(c))? (c - '0') : 0 )
#define SyCharToUpper(c)  ((c < 0xc0 && SyisLower(c))? SyToUpper(c) : c)
#define SyCharToLower(c)  ((c < 0xc0 && SyisUpper(c))? SyToLower(c) : c)
/* Remove white space/NUL byte from a raw string */
#define SyStringLeftTrim(RAW)\
	while((RAW)->nByte > 0 && (unsigned char)(RAW)->zString[0] < 0xc0 && SyisSpace((RAW)->zString[0])){\
		(RAW)->nByte--;\
		(RAW)->zString++;\
	}
#define SyStringLeftTrimSafe(RAW)\
	while((RAW)->nByte > 0 && (unsigned char)(RAW)->zString[0] < 0xc0 && ((RAW)->zString[0] == 0 || SyisSpace((RAW)->zString[0]))){\
		(RAW)->nByte--;\
		(RAW)->zString++;\
	}
#define SyStringRightTrim(RAW)\
	while((RAW)->nByte > 0 && (unsigned char)(RAW)->zString[(RAW)->nByte - 1] < 0xc0  && SyisSpace((RAW)->zString[(RAW)->nByte - 1])){\
		(RAW)->nByte--;\
	}
#define SyStringRightTrimSafe(RAW)\
	while((RAW)->nByte > 0 && (unsigned char)(RAW)->zString[(RAW)->nByte - 1] < 0xc0  && \
	(( RAW)->zString[(RAW)->nByte - 1] == 0 || SyisSpace((RAW)->zString[(RAW)->nByte - 1]))){\
		(RAW)->nByte--;\
	}

#define SyStringFullTrim(RAW)\
	while((RAW)->nByte > 0 && (unsigned char)(RAW)->zString[0] < 0xc0  && SyisSpace((RAW)->zString[0])){\
		(RAW)->nByte--;\
		(RAW)->zString++;\
	}\
	while((RAW)->nByte > 0 && (unsigned char)(RAW)->zString[(RAW)->nByte - 1] < 0xc0  && SyisSpace((RAW)->zString[(RAW)->nByte - 1])){\
		(RAW)->nByte--;\
	}
#define SyStringFullTrimSafe(RAW)\
	while((RAW)->nByte > 0 && (unsigned char)(RAW)->zString[0] < 0xc0  && \
          ( (RAW)->zString[0] == 0 || SyisSpace((RAW)->zString[0]))){\
		(RAW)->nByte--;\
		(RAW)->zString++;\
	}\
	while((RAW)->nByte > 0 && (unsigned char)(RAW)->zString[(RAW)->nByte - 1] < 0xc0  && \
                   ( (RAW)->zString[(RAW)->nByte - 1] == 0 || SyisSpace((RAW)->zString[(RAW)->nByte - 1]))){\
		(RAW)->nByte--;\
	}
#ifndef JX9_DISABLE_BUILTIN_FUNC
/* 
 * An XML raw text, CDATA, tag name and son is parsed out and stored
 * in an instance of the following structure.
 */
typedef struct SyXMLRawStr SyXMLRawStr;
struct SyXMLRawStr
{
	const char *zString; /* Raw text [UTF-8 ENCODED EXCEPT CDATA] [NOT NULL TERMINATED] */
	sxu32 nByte; /* Text length */
	sxu32 nLine; /* Line number this text occurs */
};
/*
 * Event callback signatures.
 */
typedef sxi32 (*ProcXMLStartTagHandler)(SyXMLRawStr *, SyXMLRawStr *, sxu32, SyXMLRawStr *, void *);
typedef sxi32 (*ProcXMLTextHandler)(SyXMLRawStr *, void *);
typedef sxi32 (*ProcXMLEndTagHandler)(SyXMLRawStr *, SyXMLRawStr *, void *);
typedef sxi32 (*ProcXMLPIHandler)(SyXMLRawStr *, SyXMLRawStr *, void *);
typedef sxi32 (*ProcXMLDoctypeHandler)(SyXMLRawStr *, void *);
typedef sxi32 (*ProcXMLSyntaxErrorHandler)(const char *, int, SyToken *, void *);
typedef sxi32 (*ProcXMLStartDocument)(void *);
typedef sxi32 (*ProcXMLNameSpaceStart)(SyXMLRawStr *, SyXMLRawStr *, void *);
typedef sxi32 (*ProcXMLNameSpaceEnd)(SyXMLRawStr *, void *);
typedef sxi32 (*ProcXMLEndDocument)(void *);
/* XML processing control flags */
#define SXML_ENABLE_NAMESPACE	    0x01 /* Parse XML with namespace support enbaled */
#define SXML_ENABLE_QUERY		    0x02 /* Not used */	
#define SXML_OPTION_CASE_FOLDING    0x04 /* Controls whether case-folding is enabled for this XML parser */
#define SXML_OPTION_SKIP_TAGSTART   0x08 /* Specify how many characters should be skipped in the beginning of a tag name.*/
#define SXML_OPTION_SKIP_WHITE      0x10 /* Whether to skip values consisting of whitespace characters. */
#define SXML_OPTION_TARGET_ENCODING 0x20 /* Default encoding: UTF-8 */
/* XML error codes */
enum xml_err_code{
    SXML_ERROR_NONE = 1, 
    SXML_ERROR_NO_MEMORY, 
    SXML_ERROR_SYNTAX, 
    SXML_ERROR_NO_ELEMENTS, 
    SXML_ERROR_INVALID_TOKEN, 
    SXML_ERROR_UNCLOSED_TOKEN, 
    SXML_ERROR_PARTIAL_CHAR, 
    SXML_ERROR_TAG_MISMATCH, 
    SXML_ERROR_DUPLICATE_ATTRIBUTE, 
    SXML_ERROR_JUNK_AFTER_DOC_ELEMENT, 
    SXML_ERROR_PARAM_ENTITY_REF, 
    SXML_ERROR_UNDEFINED_ENTITY, 
    SXML_ERROR_RECURSIVE_ENTITY_REF, 
    SXML_ERROR_ASYNC_ENTITY, 
    SXML_ERROR_BAD_CHAR_REF, 
    SXML_ERROR_BINARY_ENTITY_REF, 
    SXML_ERROR_ATTRIBUTE_EXTERNAL_ENTITY_REF, 
    SXML_ERROR_MISPLACED_XML_PI, 
    SXML_ERROR_UNKNOWN_ENCODING, 
    SXML_ERROR_INCORRECT_ENCODING, 
    SXML_ERROR_UNCLOSED_CDATA_SECTION, 
    SXML_ERROR_EXTERNAL_ENTITY_HANDLING
};
/* Each active XML SAX parser is represented by an instance 
 * of the following structure.
 */
typedef struct SyXMLParser SyXMLParser;
struct SyXMLParser
{
	SyMemBackend *pAllocator; /* Memory backend */
	void *pUserData;          /* User private data forwarded varbatim by the XML parser
					           * as the last argument to the users callbacks.
						       */
	SyHash hns;               /* Namespace hashtable */
	SySet sToken;             /* XML tokens */
	SyLex sLex;               /* Lexical analyzer */
	sxi32 nFlags;             /* Control flags */
	/* User callbacks */
	ProcXMLStartTagHandler    xStartTag;     /* Start element handler */
	ProcXMLEndTagHandler      xEndTag;       /* End element handler */
	ProcXMLTextHandler        xRaw;          /* Raw text/CDATA handler   */
	ProcXMLDoctypeHandler     xDoctype;      /* DOCTYPE handler */
	ProcXMLPIHandler          xPi;           /* Processing instruction (PI) handler*/
	ProcXMLSyntaxErrorHandler xError;        /* Error handler */
	ProcXMLStartDocument      xStartDoc;     /* StartDoc handler */
	ProcXMLEndDocument        xEndDoc;       /* EndDoc handler */
	ProcXMLNameSpaceStart   xNameSpace;    /* Namespace declaration handler  */
	ProcXMLNameSpaceEnd       xNameSpaceEnd; /* End namespace declaration handler */
};
/*
 * --------------
 * Archive extractor:
 * --------------
 * Each open ZIP/TAR archive is identified by an instance of the following structure.
 * That is, a process can open one or more archives and manipulates them in thread safe
 * way by simply working with pointers to the following structure.
 * Each entry in the archive is remembered in a hashtable.
 * Lookup is very fast and entry with the same name are chained together.
 */
 typedef struct SyArchiveEntry SyArchiveEntry;
 typedef struct SyArchive SyArchive;
 struct SyArchive
 {
 	SyMemBackend	*pAllocator; /* Memory backend */
	SyArchiveEntry *pCursor;     /* Cursor for linear traversal of archive entries */
	SyArchiveEntry *pList;       /* Pointer to the List of the loaded archive */
	SyArchiveEntry **apHash;     /* Hashtable for archive entry */
	ProcRawStrCmp xCmp;          /* Hash comparison function */
	ProcHash xHash;              /* Hash Function */
	sxu32 nSize;        /* Hashtable size */
 	sxu32 nEntry;       /* Total number of entries in the zip/tar archive */
 	sxu32 nLoaded;      /* Total number of entries loaded in memory */
 	sxu32 nCentralOfft;	/* Central directory offset(ZIP only. Otherwise Zero) */
 	sxu32 nCentralSize;	/* Central directory size(ZIP only. Otherwise Zero) */
	void *pUserData;    /* Upper layer private data */
	sxu32 nMagic;       /* Sanity check */
	
 };
#define SXARCH_MAGIC	0xDEAD635A
#define SXARCH_INVALID(ARCH)            (ARCH == 0  || ARCH->nMagic != SXARCH_MAGIC)
#define SXARCH_ENTRY_INVALID(ENTRY)	    (ENTRY == 0 || ENTRY->nMagic != SXARCH_MAGIC)
#define SyArchiveHashFunc(ARCH)	        (ARCH)->xHash
#define SyArchiveCmpFunc(ARCH)	        (ARCH)->xCmp
#define SyArchiveUserData(ARCH)         (ARCH)->pUserData
#define SyArchiveSetUserData(ARCH, DATA) (ARCH)->pUserData = DATA
/*
 * Each loaded archive record is identified by an instance
 * of the following structure.
 */
 struct SyArchiveEntry
 { 	
 	sxu32 nByte;         /* Contents size before compression */
 	sxu32 nByteCompr;    /* Contents size after compression */
	sxu32 nReadCount;    /* Read counter */
 	sxu32 nCrc;          /* Contents CRC32  */
 	Sytm  sFmt;	         /* Last-modification time */
 	sxu32 nOfft;         /* Data offset. */
 	sxu16 nComprMeth;	 /* Compression method 0 == stored/8 == deflated and so on (see appnote.txt)*/
 	sxu16 nExtra;        /* Extra size if any */
 	SyString sFileName;  /* entry name & length */
 	sxu32 nDup;	/* Total number of entries with the same name */
	SyArchiveEntry *pNextHash, *pPrevHash; /* Hash collision chains */
 	SyArchiveEntry *pNextName;    /* Next entry with the same name */
	SyArchiveEntry *pNext, *pPrev; /* Next and previous entry in the list */
	sxu32 nHash;     /* Hash of the entry name */
 	void *pUserData; /* User data */ 
	sxu32 nMagic;    /* Sanity check */
 };
 /*
 * Extra flags for extending the file local header
 */ 
#define SXZIP_EXTRA_TIMESTAMP	0x001	/* Extended UNIX timestamp */
#endif /* JX9_DISABLE_BUILTIN_FUNC */
#ifndef JX9_DISABLE_HASH_FUNC
/* MD5 context */
typedef struct MD5Context MD5Context;
struct MD5Context {
 sxu32 buf[4];
 sxu32 bits[2];
 unsigned char in[64];
};
/* SHA1 context */
typedef struct SHA1Context SHA1Context;
struct SHA1Context {
  unsigned int state[5];
  unsigned int count[2];
  unsigned char buffer[64];
};
#endif /* JX9_DISABLE_HASH_FUNC */
/* JX9 private declaration */
/*
 * Memory Objects.
 * Internally, the JX9 virtual machine manipulates nearly all JX9 values
 * [i.e: string, int, float, resource, object, bool, null] as jx9_values structures.
 * Each jx9_values struct may cache multiple representations (string, integer etc.)
 * of the same value.
 */
struct jx9_value
{
	union{
		jx9_real rVal;  /* Real value */
		sxi64 iVal;     /* Integer value */
		void *pOther;   /* Other values (Object, Array, Resource, Namespace, etc.) */
	}x;
	sxi32 iFlags;       /* Control flags (see below) */
	jx9_vm *pVm;        /* VM this instance belong */
	SyBlob sBlob;       /* String values */
	sxu32 nIdx;         /* Object index in the global pool */
};
/* Allowed value types.
 */
#define MEMOBJ_STRING    0x001  /* Memory value is a UTF-8 string */
#define MEMOBJ_INT       0x002  /* Memory value is an integer */
#define MEMOBJ_REAL      0x004  /* Memory value is a real number */
#define MEMOBJ_BOOL      0x008  /* Memory value is a boolean */
#define MEMOBJ_NULL      0x020  /* Memory value is NULL */
#define MEMOBJ_HASHMAP   0x040  /* Memory value is a hashmap (JSON representation of Array and Objects)  */
#define MEMOBJ_RES       0x100  /* Memory value is a resource [User private data] */
/* Mask of all known types */
#define MEMOBJ_ALL (MEMOBJ_STRING|MEMOBJ_INT|MEMOBJ_REAL|MEMOBJ_BOOL|MEMOBJ_NULL|MEMOBJ_HASHMAP|MEMOBJ_RES) 
/* Scalar variables
 * According to the JX9 language reference manual
 *  Scalar variables are those containing an integer, float, string or boolean.
 *  Types array, object and resource are not scalar. 
 */
#define MEMOBJ_SCALAR (MEMOBJ_STRING|MEMOBJ_INT|MEMOBJ_REAL|MEMOBJ_BOOL|MEMOBJ_NULL)
/*
 * The following macro clear the current jx9_value type and replace
 * it with the given one.
 */
#define MemObjSetType(OBJ, TYPE) ((OBJ)->iFlags = ((OBJ)->iFlags&~MEMOBJ_ALL)|TYPE)
/* jx9_value cast method signature */
typedef sxi32 (*ProcMemObjCast)(jx9_value *);
/* Forward reference */
typedef struct jx9_output_consumer jx9_output_consumer;
typedef struct jx9_user_func jx9_user_func;
typedef struct jx9_conf jx9_conf;
/*
 * An instance of the following structure store the default VM output 
 * consumer and it's private data.
 * Client-programs can register their own output consumer callback
 * via the [JX9_VM_CONFIG_OUTPUT] configuration directive.
 * Please refer to the official documentation for more information
 * on how to register an output consumer callback.
 */
struct jx9_output_consumer
{
	ProcConsumer xConsumer; /* VM output consumer routine */
	void *pUserData;        /* Third argument to xConsumer() */
	ProcConsumer xDef;      /* Default output consumer routine */
	void *pDefData;         /* Third argument to xDef() */
};
/*
 * JX9 engine [i.e: jx9 instance] configuration is stored in
 * an instance of the following structure.
 * Please refer to the official documentation for more information
 * on how to configure your jx9 engine instance.
 */
struct jx9_conf
{
	ProcConsumer xErr;   /* Compile-time error consumer callback */
	void *pErrData;      /* Third argument to xErr() */
	SyBlob sErrConsumer; /* Default error consumer */
};
/*
 * Signature of the C function responsible of expanding constant values.
 */
typedef void (*ProcConstant)(jx9_value *, void *);
/*
 * Each registered constant [i.e: __TIME__, __DATE__, JX9_OS, INT_MAX, etc.] is stored
 * in an instance of the following structure.
 * Please refer to the official documentation for more information
 * on how to create/install foreign constants.
 */
typedef struct jx9_constant jx9_constant;
struct jx9_constant
{
	SyString sName;        /* Constant name */
	ProcConstant xExpand;  /* Function responsible of expanding constant value */
	void *pUserData;       /* Last argument to xExpand() */
};
typedef struct jx9_aux_data jx9_aux_data;
/*
 * Auxiliary data associated with each foreign function is stored
 * in a stack of the following structure.
 * Note that automatic tracked chunks are also stored in an instance
 * of this structure.
 */
struct jx9_aux_data
{
	void *pAuxData; /* Aux data */
};
/* Foreign functions signature */
typedef int (*ProcHostFunction)(jx9_context *, int, jx9_value **);
/*
 * Each installed foreign function is recored in an instance of the following
 * structure.
 * Please refer to the official documentation for more information on how 
 * to create/install foreign functions.
 */
struct jx9_user_func
{
	jx9_vm *pVm;              /* VM that own this instance */
	SyString sName;           /* Foreign function name */
	ProcHostFunction xFunc;  /* Implementation of the foreign function */
	void *pUserData;          /* User private data [Refer to the official documentation for more information]*/
	SySet aAux;               /* Stack of auxiliary data [Refer to the official documentation for more information]*/
};
/*
 * The 'context' argument for an installable function. A pointer to an
 * instance of this structure is the first argument to the routines used
 * implement the foreign functions.
 */
struct jx9_context
{
	jx9_user_func *pFunc;   /* Function information. */
	jx9_value *pRet;        /* Return value is stored here. */
	SySet sVar;             /* Container of dynamically allocated jx9_values
							 * [i.e: Garbage collection purposes.]
							 */
	SySet sChunk;           /* Track dynamically allocated chunks [jx9_aux_data instance]. 
							 * [i.e: Garbage collection purposes.]
							 */
	jx9_vm *pVm;            /* Virtual machine that own this context */
	sxi32 iFlags;           /* Call flags */
};
/* Hashmap control flags */
#define HASHMAP_JSON_OBJECT 0x001 /* Hashmap represent JSON Object*/
/*
 * Each hashmap entry [i.e: array(4, 5, 6)] is recorded in an instance
 * of the following structure.
 */
struct jx9_hashmap_node
{
	jx9_hashmap *pMap;     /* Hashmap that own this instance */
	sxi32 iType;           /* Node type */
	union{
		sxi64 iKey;        /* Int key */
		SyBlob sKey;       /* Blob key */
	}xKey;
	sxi32 iFlags;          /* Control flags */
	sxu32 nHash;           /* Key hash value */
	sxu32 nValIdx;         /* Value stored in this node */
	jx9_hashmap_node *pNext, *pPrev;               /* Link to other entries [i.e: linear traversal] */
	jx9_hashmap_node *pNextCollide, *pPrevCollide; /* Collision chain */
};
/* 
 * Each active hashmap aka array in the JX9 jargon is represented
 * by an instance of the following structure.
 */
struct jx9_hashmap
{
	jx9_vm *pVm;                  /* VM that own this instance */
	jx9_hashmap_node **apBucket;  /* Hash bucket */
	jx9_hashmap_node *pFirst;     /* First inserted entry */
	jx9_hashmap_node *pLast;      /* Last inserted entry */
	jx9_hashmap_node *pCur;       /* Current entry */
	sxu32 nSize;                  /* Bucket size */
	sxu32 nEntry;                 /* Total number of inserted entries */
	sxu32 (*xIntHash)(sxi64);     /* Hash function for int_keys */
	sxu32 (*xBlobHash)(const void *, sxu32); /* Hash function for blob_keys */
	sxi32 iFlags;                 /* Hashmap control flags */
	sxi64 iNextIdx;               /* Next available automatically assigned index */
	sxi32 iRef;                   /* Reference count */
};
/* An instance of the following structure is the context
 * for the FOREACH_STEP/FOREACH_INIT VM instructions.
 * Those instructions are used to implement the 'foreach'
 * statement.
 * This structure is made available to these instructions
 * as the P3 operand. 
 */
struct jx9_foreach_info
{
	SyString sKey;      /* Key name. Empty otherwise*/
	SyString sValue;    /* Value name */
	sxi32 iFlags;       /* Control flags */
	SySet aStep;        /* Stack of steps [i.e: jx9_foreach_step instance] */
};
struct jx9_foreach_step
{
	sxi32 iFlags;                   /* Control flags (see below) */
	/* Iterate on this map*/
	jx9_hashmap *pMap;          /* Hashmap [i.e: array in the JX9 jargon] iteration
									 * Ex: foreach(array(1, 2, 3) as $key=>$value){} 
									 */
	
};
/* Foreach step control flags */
#define JX9_4EACH_STEP_KEY     0x001 /* Make Key available */
/*
 * Each JX9 engine is identified by an instance of the following structure.
 * Please refer to the official documentation for more information
 * on how to configure your JX9 engine instance.
 */
struct jx9
{
	SyMemBackend sAllocator;     /* Low level memory allocation subsystem */
	const jx9_vfs *pVfs;         /* Underlying Virtual File System */
	jx9_conf xConf;              /* Configuration */
#if defined(JX9_ENABLE_THREADS)
	SyMutex *pMutex;                 /* Per-engine mutex */
#endif
	jx9_vm *pVms;      /* List of active VM */
	sxi32 iVm;         /* Total number of active VM */
	jx9 *pNext, *pPrev; /* List of active engines */
	sxu32 nMagic;      /* Sanity check against misuse */
};
/* Code generation data structures */
typedef sxi32 (*ProcErrorGen)(void *, sxi32, sxu32, const char *, ...);
typedef struct jx9_expr_node   jx9_expr_node;
typedef struct jx9_expr_op     jx9_expr_op;
typedef struct jx9_gen_state   jx9_gen_state;
typedef struct GenBlock        GenBlock;
typedef sxi32 (*ProcLangConstruct)(jx9_gen_state *);
typedef sxi32 (*ProcNodeConstruct)(jx9_gen_state *, sxi32);
/*
 * Each supported operator [i.e: +, -, ==, *, %, >>, >=, new, etc.] is represented
 * by an instance of the following structure.
 * The JX9 parser does not use any external tools and is 100% handcoded.
 * That is, the JX9 parser is thread-safe , full reentrant, produce consistant 
 * compile-time errrors and at least 7 times faster than the standard JX9 parser.
 */
struct jx9_expr_op
{
	SyString sOp;   /* String representation of the operator [i.e: "+", "*", "=="...] */
	sxi32 iOp;      /* Operator ID */
	sxi32 iPrec;    /* Operator precedence: 1 == Highest */ 
	sxi32 iAssoc;   /* Operator associativity (either left, right or non-associative) */ 
	sxi32 iVmOp;    /* VM OP code for this operator [i.e: JX9_OP_EQ, JX9_OP_LT, JX9_OP_MUL...]*/
};
/*
 * Each expression node is parsed out and recorded
 * in an instance of the following structure.
 */
struct jx9_expr_node
{
	const jx9_expr_op *pOp;  /* Operator ID or NULL if literal, constant, variable, function or object method call */
	jx9_expr_node *pLeft;    /* Left expression tree */
	jx9_expr_node *pRight;   /* Right expression tree */
	SyToken *pStart;         /* Stream of tokens that belong to this node */
	SyToken *pEnd;           /* End of token stream */
	sxi32 iFlags;            /* Node construct flags */
	ProcNodeConstruct xCode; /* C routine responsible of compiling this node */
	SySet aNodeArgs;         /* Node arguments. Only used by postfix operators [i.e: function call]*/
	jx9_expr_node *pCond;    /* Condition: Only used by the ternary operator '?:' */
};
/* Node Construct flags */
#define EXPR_NODE_PRE_INCR 0x01 /* Pre-icrement/decrement [i.e: ++$i, --$j] node */
/*
 * A block of instructions is recorded in an instance of the following structure.
 * This structure is used only during compile-time and have no meaning
 * during bytecode execution.
 */
struct GenBlock
{
	jx9_gen_state *pGen;  /* State of the code generator */
	GenBlock *pParent;    /* Upper block or NULL if global */
	sxu32 nFirstInstr;    /* First instruction to execute  */
	sxi32 iFlags;         /* Block control flags (see below) */
	SySet aJumpFix;       /* Jump fixup (JumpFixup instance) */
	void *pUserData;      /* Upper layer private data */
	/* The following two fields are used only when compiling 
	 * the 'do..while()' language construct.
	 */
	sxu8 bPostContinue;    /* TRUE when compiling the do..while() statement */
	SySet aPostContFix;    /* Post-continue jump fix */
};
/*
 * Code generator state is remembered in an instance of the following
 * structure. We put the information in this structure and pass around
 * a pointer to this structure, rather than pass around  all of the 
 * information separately. This helps reduce the number of  arguments
 * to generator functions.
 * This structure is used only during compile-time and have no meaning
 * during bytecode execution.
 */
struct jx9_gen_state
{
	jx9_vm *pVm;         /* VM that own this instance */
	SyHash hLiteral;     /* Constant string Literals table */
	SyHash hNumLiteral;  /* Numeric literals table */
	SyHash hVar;         /* Collected variable hashtable */
	GenBlock *pCurrent;  /* Current processed block */
	GenBlock sGlobal;    /* Global block */
	ProcConsumer xErr;   /* Error consumer callback */
	void *pErrData;      /* Third argument to xErr() */
	SyToken *pIn;        /* Current processed token */
	SyToken *pEnd;       /* Last token in the stream */
	sxu32 nErr;          /* Total number of compilation error */
};
/* Forward references */
typedef struct jx9_vm_func_static_var  jx9_vm_func_static_var;
typedef struct jx9_vm_func_arg jx9_vm_func_arg;
typedef struct jx9_vm_func jx9_vm_func;
typedef struct VmFrame VmFrame;
/*
 * Each collected function argument is recorded in an instance
 * of the following structure.
 * Note that as an extension, JX9 implements full type hinting
 * which mean that any function can have it's own signature.
 * Example:
 *      function foo(int $a, string $b, float $c, ClassInstance $d){}
 * This is how the powerful function overloading mechanism is
 * implemented.
 * Note that as an extension, JX9 allow function arguments to have
 * any complex default value associated with them unlike the standard
 * JX9 engine.
 * Example:
 *    function foo(int $a = rand() & 1023){}
 *    now, when foo is called without arguments [i.e: foo()] the
 *    $a variable (first parameter) will be set to a random number
 *    between 0 and 1023 inclusive.
 * Refer to the official documentation for more information on this
 * mechanism and other extension introduced by the JX9 engine.
 */
struct jx9_vm_func_arg
{
	SyString sName;      /* Argument name */
	SySet aByteCode;     /* Compiled default value associated with this argument */
	sxu32 nType;         /* Type of this argument [i.e: array, int, string, float, object, etc.] */
	sxi32 iFlags;        /* Configuration flags */
};
/*
 * Each static variable is parsed out and remembered in an instance
 * of the following structure.
 * Note that as an extension, JX9 allow static variable have
 * any complex default value associated with them unlike the standard
 * JX9 engine.
 * Example:
 *   static $rand_str = 'JX9'.rand_str(3); // Concatenate 'JX9' with 
 *                                         // a random three characters(English alphabet)
 *   dump($rand_str);
 *   //You should see something like this
 *   string(6 'JX9awt');   
 */
struct jx9_vm_func_static_var
{
	SyString sName;   /* Static variable name */
	SySet aByteCode;  /* Compiled initialization expression  */
	sxu32 nIdx;       /* Object index in the global memory object container */
};
/* Function configuration flags */
#define VM_FUNC_ARG_HAS_DEF  0x001 /* Argument has default value associated with it */
#define VM_FUNC_ARG_IGNORE   0x002 /* Do not install argument in the current frame */
/*
 * Each user defined function is parsed out and stored in an instance
 * of the following structure.
 * JX9 introduced some powerfull extensions to the JX9 5 programming
 * language like function overloading, type hinting, complex default
 * arguments values and many more.
 * Please refer to the official documentation for more information.
 */
struct jx9_vm_func
{
	SySet aArgs;         /* Expected arguments (jx9_vm_func_arg instance) */
	SySet aStatic;       /* Static variable (jx9_vm_func_static_var instance) */
	SyString sName;      /* Function name */
	SySet aByteCode;     /* Compiled function body */
	sxi32 iFlags;        /* VM function configuration */
	SyString sSignature; /* Function signature used to implement function overloading
						  * (Refer to the official docuemntation for more information
						  *  on this powerfull feature)
						  */
	void *pUserData;     /* Upper layer private data associated with this instance */
	jx9_vm_func *pNextName; /* Next VM function with the same name as this one */
};
/* Forward reference */
typedef struct jx9_builtin_constant jx9_builtin_constant;
typedef struct jx9_builtin_func jx9_builtin_func;
/*
 * Each built-in foreign function (C function) is stored in an
 * instance of the following structure.
 * Please refer to the official documentation for more information
 * on how to create/install foreign functions.
 */
struct jx9_builtin_func
{
	const char *zName;        /* Function name [i.e: strlen(), rand(), array_merge(), etc.]*/
	ProcHostFunction xFunc;  /* C routine performing the computation */
};
/*
 * Each built-in foreign constant is stored in an instance
 * of the following structure.
 * Please refer to the official documentation for more information
 * on how to create/install foreign constants.
 */
struct jx9_builtin_constant
{
	const char *zName;     /* Constant name */
	ProcConstant xExpand;  /* C routine responsible of expanding constant value*/
};
/*
 * A single instruction of the virtual machine has an opcode
 * and as many as three operands.
 * Each VM instruction resulting from compiling a JX9 script
 * is stored in an instance of the following structure.
 */
typedef struct VmInstr VmInstr;
struct VmInstr
{
	sxu8  iOp; /* Operation to preform */
	sxi32 iP1; /* First operand */
	sxu32 iP2; /* Second operand (Often the jump destination) */
	void *p3;  /* Third operand (Often Upper layer private data) */
};
/* Forward reference */
typedef struct jx9_case_expr jx9_case_expr;
typedef struct jx9_switch jx9_switch;
/*
 * Each compiled case block in a swicth statement is compiled
 * and stored in an instance of the following structure.
 */
struct jx9_case_expr
{
	SySet aByteCode;   /* Compiled body of the case block */
	sxu32 nStart;      /* First instruction to execute */
};
/*
 * Each compiled switch statement is parsed out and stored
 * in an instance of the following structure.
 */
struct jx9_switch
{
	SySet aCaseExpr;  /* Compile case block */
	sxu32 nOut;       /* First instruction to execute after this statement */
	sxu32 nDefault;   /* First instruction to execute in the default block */
};
/* Assertion flags */
#define JX9_ASSERT_DISABLE    0x01  /* Disable assertion */
#define JX9_ASSERT_WARNING    0x02  /* Issue a warning for each failed assertion */
#define JX9_ASSERT_BAIL       0x04  /* Terminate execution on failed assertions */
#define JX9_ASSERT_QUIET_EVAL 0x08  /* Not used */
#define JX9_ASSERT_CALLBACK   0x10  /* Callback to call on failed assertions */
/* 
 * An instance of the following structure hold the bytecode instructions
 * resulting from compiling a JX9 script.
 * This structure contains the complete state of the virtual machine.
 */
struct jx9_vm
{
	SyMemBackend sAllocator;	/* Memory backend */
#if defined(JX9_ENABLE_THREADS)
	SyMutex *pMutex;           /* Recursive mutex associated with this VM. */
#endif
	jx9 *pEngine;               /* Interpreter that own this VM */
	SySet aByteCode;            /* Default bytecode container */
	SySet *pByteContainer;      /* Current bytecode container */
	VmFrame *pFrame;            /* Stack of active frames */
	SyPRNGCtx sPrng;            /* PRNG context */
	SySet aMemObj;              /* Object allocation table */
	SySet aLitObj;              /* Literals allocation table */
	jx9_value *aOps;            /* Operand stack */
	SySet aFreeObj;             /* Stack of free memory objects */
	SyHash hConstant;           /* Host-application and user defined constants container */
	SyHash hHostFunction;       /* Host-application installable functions */
	SyHash hFunction;           /* Compiled functions */
	SyHash hSuper;              /* Global variable */
	SyBlob sConsumer;           /* Default VM consumer [i.e Redirect all VM output to this blob] */
	SyBlob sWorker;             /* General purpose working buffer */
	SyBlob sArgv;               /* $argv[] collector [refer to the [getopt()] implementation for more information] */
	SySet aFiles;               /* Stack of processed files */
	SySet aPaths;               /* Set of import paths */
	SySet aIncluded;            /* Set of included files */
	SySet aIOstream;            /* Installed IO stream container */
	const jx9_io_stream *pDefStream; /* Default IO stream [i.e: typically this is the 'file://' stream] */
	jx9_value sExec;           /* Compiled script return value [Can be extracted via the JX9_VM_CONFIG_EXEC_VALUE directive]*/
	void *pStdin;              /* STDIN IO stream */
	void *pStdout;             /* STDOUT IO stream */
	void *pStderr;             /* STDERR IO stream */
	int bErrReport;            /* TRUE to report all runtime Error/Warning/Notice */
	int nRecursionDepth;       /* Current recursion depth */
	int nMaxDepth;             /* Maximum allowed recusion depth */
	sxu32 nOutputLen;          /* Total number of generated output */
	jx9_output_consumer sVmConsumer; /* Registered output consumer callback */
	int iAssertFlags;          /* Assertion flags */
	jx9_value sAssertCallback; /* Callback to call on failed assertions */
	sxi32 iExitStatus;         /* Script exit status */
	jx9_gen_state sCodeGen;    /* Code generator module */
	jx9_vm *pNext, *pPrev;      /* List of active VM's */
	sxu32 nMagic;              /* Sanity check against misuse */
};
/*
 * Allowed value for jx9_vm.nMagic
 */
#define JX9_VM_INIT   0xEA12CD72  /* VM correctly initialized */
#define JX9_VM_RUN    0xBA851227  /* VM ready to execute JX9 bytecode */
#define JX9_VM_EXEC   0xCDFE1DAD  /* VM executing JX9 bytecode */
#define JX9_VM_STALE  0xDEAD2BAD  /* Stale VM */
/*
 * Error codes according to the JX9 language reference manual.
 */
enum iErrCode
{
    E_ABORT             = -1,  /* deadliness error， should halt script execution. */
	E_ERROR             = 1,   /* Fatal run-time errors. These indicate errors that can not be recovered 
							    * from, such as a memory allocation problem. Execution of the script is
							    * halted.
								* The only fatal error under JX9 is an out-of-memory. All others erros
								* even a call to undefined function will not halt script execution.
							    */
	E_WARNING           ,   /* Run-time warnings (non-fatal errors). Execution of the script is not halted.  */
	E_PARSE             ,   /* Compile-time parse errors. Parse errors should only be generated by the parser.*/
	E_NOTICE            ,   /* Run-time notices. Indicate that the script encountered something that could 
							    * indicate an error, but could also happen in the normal course of running a script. 
							    */
};
/*
 * Each VM instruction resulting from compiling a JX9 script is represented
 * by one of the following OP codes.
 * The program consists of a linear sequence of operations. Each operation
 * has an opcode and 3 operands.Operands P1 is an integer.
 * Operand P2 is an unsigned integer and operand P3 is a memory address.
 * Few opcodes use all 3 operands.
 */
enum jx9_vm_op {
  JX9_OP_DONE =   1,   /* Done */
  JX9_OP_HALT,         /* Halt */
  JX9_OP_LOAD,         /* Load memory object */
  JX9_OP_LOADC,        /* Load constant */
  JX9_OP_LOAD_IDX,     /* Load array entry */   
  JX9_OP_LOAD_MAP,     /* Load hashmap('array') */
  JX9_OP_NOOP,         /* NOOP */
  JX9_OP_JMP,          /* Unconditional jump */
  JX9_OP_JZ,           /* Jump on zero (FALSE jump) */
  JX9_OP_JNZ,          /* Jump on non-zero (TRUE jump) */
  JX9_OP_POP,          /* Stack POP */ 
  JX9_OP_CAT,          /* Concatenation */
  JX9_OP_CVT_INT,      /* Integer cast */
  JX9_OP_CVT_STR,      /* String cast */
  JX9_OP_CVT_REAL,     /* Float cast */
  JX9_OP_CALL,         /* Function call */
  JX9_OP_UMINUS,       /* Unary minus '-'*/
  JX9_OP_UPLUS,        /* Unary plus '+'*/
  JX9_OP_BITNOT,       /* Bitwise not '~' */
  JX9_OP_LNOT,         /* Logical not '!' */
  JX9_OP_MUL,          /* Multiplication '*' */
  JX9_OP_DIV,          /* Division '/' */
  JX9_OP_MOD,          /* Modulus '%' */
  JX9_OP_ADD,          /* Add '+' */
  JX9_OP_SUB,          /* Sub '-' */
  JX9_OP_SHL,          /* Left shift '<<' */
  JX9_OP_SHR,          /* Right shift '>>' */
  JX9_OP_LT,           /* Less than '<' */
  JX9_OP_LE,           /* Less or equal '<=' */
  JX9_OP_GT,           /* Greater than '>' */
  JX9_OP_GE,           /* Greater or equal '>=' */
  JX9_OP_EQ,           /* Equal '==' */
  JX9_OP_NEQ,          /* Not equal '!=' */
  JX9_OP_TEQ,          /* Type equal '===' */
  JX9_OP_TNE,          /* Type not equal '!==' */
  JX9_OP_BAND,         /* Bitwise and '&' */
  JX9_OP_BXOR,         /* Bitwise xor '^' */
  JX9_OP_BOR,          /* Bitwise or '|' */
  JX9_OP_LAND,         /* Logical and '&&','and' */
  JX9_OP_LOR,          /* Logical or  '||','or' */
  JX9_OP_LXOR,         /* Logical xor 'xor' */
  JX9_OP_STORE,        /* Store Object */
  JX9_OP_STORE_IDX,    /* Store indexed object */
  JX9_OP_PULL,         /* Stack pull */
  JX9_OP_SWAP,         /* Stack swap */
  JX9_OP_YIELD,        /* Stack yield */
  JX9_OP_CVT_BOOL,     /* Boolean cast */
  JX9_OP_CVT_NUMC,     /* Numeric (integer, real or both) type cast */
  JX9_OP_INCR,         /* Increment ++ */
  JX9_OP_DECR,         /* Decrement -- */
  JX9_OP_ADD_STORE,    /* Add and store '+=' */
  JX9_OP_SUB_STORE,    /* Sub and store '-=' */
  JX9_OP_MUL_STORE,    /* Mul and store '*=' */
  JX9_OP_DIV_STORE,    /* Div and store '/=' */
  JX9_OP_MOD_STORE,    /* Mod and store '%=' */
  JX9_OP_CAT_STORE,    /* Cat and store '.=' */
  JX9_OP_SHL_STORE,    /* Shift left and store '>>=' */
  JX9_OP_SHR_STORE,    /* Shift right and store '<<=' */
  JX9_OP_BAND_STORE,   /* Bitand and store '&=' */
  JX9_OP_BOR_STORE,    /* Bitor and store '|=' */
  JX9_OP_BXOR_STORE,   /* Bitxor and store '^=' */
  JX9_OP_CONSUME,      /* Consume VM output */
  JX9_OP_MEMBER,       /* Object member run-time access */
  JX9_OP_UPLINK,       /* Run-Time frame link */
  JX9_OP_CVT_NULL,     /* NULL cast */
  JX9_OP_CVT_ARRAY,    /* Array cast */
  JX9_OP_FOREACH_INIT, /* For each init */
  JX9_OP_FOREACH_STEP, /* For each step */
  JX9_OP_SWITCH        /* Switch operation */
};
/* -- END-OF INSTRUCTIONS -- */
/*
 * Expression Operators ID.
 */
enum jx9_expr_id {
	EXPR_OP_DOT,      /* Member access */
	EXPR_OP_DC,        /* :: */
	EXPR_OP_SUBSCRIPT, /* []: Subscripting */
	EXPR_OP_FUNC_CALL, /* func_call() */
	EXPR_OP_INCR,      /* ++ */
	EXPR_OP_DECR,      /* -- */ 
	EXPR_OP_BITNOT,    /* ~ */
	EXPR_OP_UMINUS,    /* Unary minus  */
	EXPR_OP_UPLUS,     /* Unary plus */
	EXPR_OP_TYPECAST,  /* Type cast [i.e: (int), (float), (string)...] */
	EXPR_OP_ALT,       /* @ */
	EXPR_OP_INSTOF,    /* instanceof */
	EXPR_OP_LOGNOT,    /* logical not ! */
	EXPR_OP_MUL,       /* Multiplication */
	EXPR_OP_DIV,       /* division */
	EXPR_OP_MOD,       /* Modulus */
	EXPR_OP_ADD,       /* Addition */
	EXPR_OP_SUB,       /* Substraction */
	EXPR_OP_DDOT,      /* Concatenation */
	EXPR_OP_SHL,       /* Left shift */
	EXPR_OP_SHR,       /* Right shift */
	EXPR_OP_LT,        /* Less than */
	EXPR_OP_LE,        /* Less equal */
	EXPR_OP_GT,        /* Greater than */
	EXPR_OP_GE,        /* Greater equal */
	EXPR_OP_EQ,        /* Equal == */
	EXPR_OP_NE,        /* Not equal != <> */
	EXPR_OP_TEQ,       /* Type equal === */
	EXPR_OP_TNE,       /* Type not equal !== */
	EXPR_OP_SEQ,       /* String equal 'eq' */
	EXPR_OP_SNE,       /* String not equal 'ne' */
	EXPR_OP_BAND,      /* Biwise and '&' */
	EXPR_OP_REF,       /* Reference operator '&' */
	EXPR_OP_XOR,       /* bitwise xor '^' */
	EXPR_OP_BOR,       /* bitwise or '|' */
	EXPR_OP_LAND,      /* Logical and '&&','and' */
	EXPR_OP_LOR,       /* Logical or  '||','or'*/
	EXPR_OP_LXOR,      /* Logical xor 'xor' */
	EXPR_OP_QUESTY,    /* Ternary operator '?' */
	EXPR_OP_ASSIGN,    /* Assignment '=' */
	EXPR_OP_ADD_ASSIGN, /* Combined operator: += */
	EXPR_OP_SUB_ASSIGN, /* Combined operator: -= */
	EXPR_OP_MUL_ASSIGN, /* Combined operator: *= */
	EXPR_OP_DIV_ASSIGN, /* Combined operator: /= */
	EXPR_OP_MOD_ASSIGN, /* Combined operator: %= */
	EXPR_OP_DOT_ASSIGN, /* Combined operator: .= */
	EXPR_OP_AND_ASSIGN, /* Combined operator: &= */
	EXPR_OP_OR_ASSIGN,  /* Combined operator: |= */
	EXPR_OP_XOR_ASSIGN, /* Combined operator: ^= */
	EXPR_OP_SHL_ASSIGN, /* Combined operator: <<= */
	EXPR_OP_SHR_ASSIGN, /* Combined operator: >>= */
	EXPR_OP_COMMA       /* Comma expression */
};
/*
 * Lexer token codes
 * The following set of constants are the tokens recognized
 * by the lexer when processing JX9 input.
 * Important: Token values MUST BE A POWER OF TWO.
 */
#define JX9_TK_INTEGER   0x0000001  /* Integer */
#define JX9_TK_REAL      0x0000002  /* Real number */
#define JX9_TK_NUM       (JX9_TK_INTEGER|JX9_TK_REAL) /* Numeric token, either integer or real */
#define JX9_TK_KEYWORD   0x0000004 /* Keyword [i.e: while, for, if, foreach...] */
#define JX9_TK_ID        0x0000008 /* Alphanumeric or UTF-8 stream */
#define JX9_TK_DOLLAR    0x0000010 /* '$' Dollar sign */
#define JX9_TK_OP        0x0000020 /* Operator [i.e: +, *, /...] */
#define JX9_TK_OCB       0x0000040 /* Open curly brace'{' */
#define JX9_TK_CCB       0x0000080 /* Closing curly brace'}' */
#define JX9_TK_DOT       0x0000100 /* Dot . */
#define JX9_TK_LPAREN    0x0000200 /* Left parenthesis '(' */
#define JX9_TK_RPAREN    0x0000400 /* Right parenthesis ')' */
#define JX9_TK_OSB       0x0000800 /* Open square bracket '[' */
#define JX9_TK_CSB       0x0001000 /* Closing square bracket ']' */
#define JX9_TK_DSTR      0x0002000 /* Double quoted string "$str" */
#define JX9_TK_SSTR      0x0004000 /* Single quoted string 'str' */
#define JX9_TK_NOWDOC    0x0010000 /* Nowdoc <<< */
#define JX9_TK_COMMA     0x0020000 /* Comma ',' */
#define JX9_TK_SEMI      0x0040000 /* Semi-colon ";" */
#define JX9_TK_BSTR      0x0080000 /* Backtick quoted string [i.e: Shell command `date`] */
#define JX9_TK_COLON     0x0100000 /* single Colon ':' */
#define JX9_TK_AMPER     0x0200000 /* Ampersand '&' */
#define JX9_TK_EQUAL     0x0400000 /* Equal '=' */
#define JX9_TK_OTHER     0x1000000 /* Other symbols */
/*
 * JX9 keyword.
 * These words have special meaning in JX9. Some of them represent things which look like
 * functions, some look like constants, and so on, but they're not, really: they are language constructs.
 * You cannot use any of the following words as constants, object names, function or method names.
 * Using them as variable names is generally OK, but could lead to confusion. 
 */
#define JX9_TKWRD_SWITCH       1 /* switch */
#define JX9_TKWRD_PRINT        2 /* print */
#define JX9_TKWRD_ELIF         0x4000000 /* elseif: MUST BE A POWER OF TWO */
#define JX9_TKWRD_ELSE         0x8000000 /* else:  MUST BE A POWER OF TWO */
#define JX9_TKWRD_IF           3 /* if */
#define JX9_TKWRD_STATIC       4 /* static */
#define JX9_TKWRD_CASE         5 /* case */
#define JX9_TKWRD_FUNCTION     6 /* function */
#define JX9_TKWRD_CONST        7 /* const */
/* The number '8' is reserved for JX9_TK_ID */
#define JX9_TKWRD_WHILE        9 /* while */
#define JX9_TKWRD_DEFAULT      10 /* default */
#define JX9_TKWRD_AS           11 /* as */
#define JX9_TKWRD_CONTINUE     12 /* continue */
#define JX9_TKWRD_EXIT         13 /* exit */
#define JX9_TKWRD_DIE          14 /* die */
#define JX9_TKWRD_IMPORT       15 /* import */
#define JX9_TKWRD_INCLUDE      16 /* include */
#define JX9_TKWRD_FOR          17 /* for */
#define JX9_TKWRD_FOREACH      18 /* foreach */
#define JX9_TKWRD_RETURN       19 /* return */
#define JX9_TKWRD_BREAK        20 /* break */
#define JX9_TKWRD_UPLINK       21 /* uplink */
#define JX9_TKWRD_BOOL         0x8000   /* bool:  MUST BE A POWER OF TWO */
#define JX9_TKWRD_INT          0x10000  /* int:   MUST BE A POWER OF TWO */
#define JX9_TKWRD_FLOAT        0x20000  /* float:  MUST BE A POWER OF TWO */
#define JX9_TKWRD_STRING       0x40000  /* string: MUST BE A POWER OF TWO */

/* api.c */
JX9_PRIVATE sxi32 jx9EngineConfig(jx9 *pEngine, sxi32 nOp, va_list ap);
JX9_PRIVATE int jx9DeleteFunction(jx9_vm *pVm,const char *zName);
JX9_PRIVATE int Jx9DeleteConstant(jx9_vm *pVm,const char *zName);
/* json.c function prototypes */
JX9_PRIVATE int jx9JsonSerialize(jx9_value *pValue,SyBlob *pOut);
JX9_PRIVATE int jx9JsonDecode(jx9_context *pCtx,const char *zJSON,int nByte);
/* memobj.c function prototypes */
JX9_PRIVATE sxi32 jx9MemObjDump(SyBlob *pOut, jx9_value *pObj);
JX9_PRIVATE const char * jx9MemObjTypeDump(jx9_value *pVal);
JX9_PRIVATE sxi32 jx9MemObjAdd(jx9_value *pObj1, jx9_value *pObj2, int bAddStore);
JX9_PRIVATE sxi32 jx9MemObjCmp(jx9_value *pObj1, jx9_value *pObj2, int bStrict, int iNest);
JX9_PRIVATE sxi32 jx9MemObjInitFromString(jx9_vm *pVm, jx9_value *pObj, const SyString *pVal);
JX9_PRIVATE sxi32 jx9MemObjInitFromArray(jx9_vm *pVm, jx9_value *pObj, jx9_hashmap *pArray);
#if 0
/* Not used in the current release of the JX9 engine */
JX9_PRIVATE sxi32 jx9MemObjInitFromReal(jx9_vm *pVm, jx9_value *pObj, jx9_real rVal);
#endif
JX9_PRIVATE sxi32 jx9MemObjInitFromInt(jx9_vm *pVm, jx9_value *pObj, sxi64 iVal);
JX9_PRIVATE sxi32 jx9MemObjInitFromBool(jx9_vm *pVm, jx9_value *pObj, sxi32 iVal);
JX9_PRIVATE sxi32 jx9MemObjInit(jx9_vm *pVm, jx9_value *pObj);
JX9_PRIVATE sxi32 jx9MemObjStringAppend(jx9_value *pObj, const char *zData, sxu32 nLen);
#if 0
/* Not used in the current release of the JX9 engine */
JX9_PRIVATE sxi32 jx9MemObjStringFormat(jx9_value *pObj, const char *zFormat, va_list ap);
#endif
JX9_PRIVATE sxi32 jx9MemObjStore(jx9_value *pSrc, jx9_value *pDest);
JX9_PRIVATE sxi32 jx9MemObjLoad(jx9_value *pSrc, jx9_value *pDest);
JX9_PRIVATE sxi32 jx9MemObjRelease(jx9_value *pObj);
JX9_PRIVATE sxi32 jx9MemObjToNumeric(jx9_value *pObj);
JX9_PRIVATE sxi32 jx9MemObjTryInteger(jx9_value *pObj);
JX9_PRIVATE ProcMemObjCast jx9MemObjCastMethod(sxi32 iFlags);
JX9_PRIVATE sxi32 jx9MemObjIsNumeric(jx9_value *pObj);
JX9_PRIVATE sxi32 jx9MemObjIsEmpty(jx9_value *pObj);
JX9_PRIVATE sxi32 jx9MemObjToHashmap(jx9_value *pObj);
JX9_PRIVATE sxi32 jx9MemObjToString(jx9_value *pObj);
JX9_PRIVATE sxi32 jx9MemObjToNull(jx9_value *pObj);
JX9_PRIVATE sxi32 jx9MemObjToReal(jx9_value *pObj);
JX9_PRIVATE sxi32 jx9MemObjToInteger(jx9_value *pObj);
JX9_PRIVATE sxi32 jx9MemObjToBool(jx9_value *pObj);
JX9_PRIVATE sxi64 jx9TokenValueToInt64(SyString *pData);
/* lex.c function prototypes */
JX9_PRIVATE sxi32 jx9Tokenize(const char *zInput, sxu32 nLen, SySet *pOut);
/* vm.c function prototypes */
JX9_PRIVATE void jx9VmReleaseContextValue(jx9_context *pCtx, jx9_value *pValue);
JX9_PRIVATE sxi32 jx9VmInitFuncState(jx9_vm *pVm, jx9_vm_func *pFunc, const char *zName, sxu32 nByte, 
	sxi32 iFlags, void *pUserData);
JX9_PRIVATE sxi32 jx9VmInstallUserFunction(jx9_vm *pVm, jx9_vm_func *pFunc, SyString *pName);
JX9_PRIVATE sxi32 jx9VmRegisterConstant(jx9_vm *pVm, const SyString *pName, ProcConstant xExpand, void *pUserData);
JX9_PRIVATE sxi32 jx9VmInstallForeignFunction(jx9_vm *pVm, const SyString *pName, ProcHostFunction xFunc, void *pUserData);
JX9_PRIVATE sxi32 jx9VmBlobConsumer(const void *pSrc, unsigned int nLen, void *pUserData);
JX9_PRIVATE jx9_value * jx9VmReserveMemObj(jx9_vm *pVm,sxu32 *pIndex);
JX9_PRIVATE jx9_value * jx9VmReserveConstObj(jx9_vm *pVm, sxu32 *pIndex);
JX9_PRIVATE sxi32 jx9VmOutputConsume(jx9_vm *pVm, SyString *pString);
JX9_PRIVATE sxi32 jx9VmOutputConsumeAp(jx9_vm *pVm, const char *zFormat, va_list ap);
JX9_PRIVATE sxi32 jx9VmThrowErrorAp(jx9_vm *pVm, SyString *pFuncName, sxi32 iErr, const char *zFormat, va_list ap);
JX9_PRIVATE sxi32 jx9VmThrowError(jx9_vm *pVm, SyString *pFuncName, sxi32 iErr, const char *zMessage);
JX9_PRIVATE void  jx9VmExpandConstantValue(jx9_value *pVal, void *pUserData);
JX9_PRIVATE sxi32 jx9VmDump(jx9_vm *pVm, ProcConsumer xConsumer, void *pUserData);
JX9_PRIVATE sxi32 jx9VmInit(jx9_vm *pVm, jx9 *pEngine);
JX9_PRIVATE sxi32 jx9VmConfigure(jx9_vm *pVm, sxi32 nOp, va_list ap);
JX9_PRIVATE sxi32 jx9VmByteCodeExec(jx9_vm *pVm);
JX9_PRIVATE jx9_value * jx9VmExtractVariable(jx9_vm *pVm,SyString *pVar);
JX9_PRIVATE sxi32 jx9VmRelease(jx9_vm *pVm);
JX9_PRIVATE sxi32 jx9VmReset(jx9_vm *pVm);
JX9_PRIVATE sxi32 jx9VmMakeReady(jx9_vm *pVm);
JX9_PRIVATE sxu32 jx9VmInstrLength(jx9_vm *pVm);
JX9_PRIVATE VmInstr * jx9VmPopInstr(jx9_vm *pVm);
JX9_PRIVATE VmInstr * jx9VmPeekInstr(jx9_vm *pVm);
JX9_PRIVATE VmInstr *jx9VmGetInstr(jx9_vm *pVm, sxu32 nIndex);
JX9_PRIVATE SySet * jx9VmGetByteCodeContainer(jx9_vm *pVm);
JX9_PRIVATE sxi32 jx9VmSetByteCodeContainer(jx9_vm *pVm, SySet *pContainer);
JX9_PRIVATE sxi32 jx9VmEmitInstr(jx9_vm *pVm, sxi32 iOp, sxi32 iP1, sxu32 iP2, void *p3, sxu32 *pIndex);
JX9_PRIVATE sxu32 jx9VmRandomNum(jx9_vm *pVm);
JX9_PRIVATE sxi32 jx9VmCallUserFunction(jx9_vm *pVm, jx9_value *pFunc, int nArg, jx9_value **apArg, jx9_value *pResult);
JX9_PRIVATE sxi32 jx9VmCallUserFunctionAp(jx9_vm *pVm, jx9_value *pFunc, jx9_value *pResult, ...);
JX9_PRIVATE sxi32 jx9VmUnsetMemObj(jx9_vm *pVm, sxu32 nObjIdx);
JX9_PRIVATE void jx9VmRandomString(jx9_vm *pVm, char *zBuf, int nLen);
JX9_PRIVATE int jx9VmIsCallable(jx9_vm *pVm, jx9_value *pValue);
JX9_PRIVATE sxi32 jx9VmPushFilePath(jx9_vm *pVm, const char *zPath, int nLen, sxu8 bMain, sxi32 *pNew);
#ifndef JX9_DISABLE_BUILTIN_FUNC
JX9_PRIVATE const jx9_io_stream * jx9VmGetStreamDevice(jx9_vm *pVm, const char **pzDevice, int nByte);
#endif /* JX9_DISABLE_BUILTIN_FUNC */
JX9_PRIVATE int jx9Utf8Read(
  const unsigned char *z,         /* First byte of UTF-8 character */
  const unsigned char *zTerm,     /* Pretend this byte is 0x00 */
  const unsigned char **pzNext    /* Write first byte past UTF-8 char here */
);
/* parse.c function prototypes */
JX9_PRIVATE int jx9IsLangConstruct(sxu32 nKeyID);
JX9_PRIVATE sxi32 jx9ExprMakeTree(jx9_gen_state *pGen, SySet *pExprNode, jx9_expr_node **ppRoot);
JX9_PRIVATE sxi32 jx9GetNextExpr(SyToken *pStart, SyToken *pEnd, SyToken **ppNext);
JX9_PRIVATE void jx9DelimitNestedTokens(SyToken *pIn, SyToken *pEnd, sxu32 nTokStart, sxu32 nTokEnd, SyToken **ppEnd);
JX9_PRIVATE const jx9_expr_op * jx9ExprExtractOperator(SyString *pStr, SyToken *pLast);
JX9_PRIVATE sxi32 jx9ExprFreeTree(jx9_gen_state *pGen, SySet *pNodeSet);
/* compile.c function prototypes */
JX9_PRIVATE ProcNodeConstruct jx9GetNodeHandler(sxu32 nNodeType);
JX9_PRIVATE sxi32 jx9CompileLangConstruct(jx9_gen_state *pGen, sxi32 iCompileFlag);
JX9_PRIVATE sxi32 jx9CompileJsonArray(jx9_gen_state *pGen, sxi32 iCompileFlag);
JX9_PRIVATE sxi32 jx9CompileJsonObject(jx9_gen_state *pGen, sxi32 iCompileFlag);
JX9_PRIVATE sxi32 jx9CompileVariable(jx9_gen_state *pGen, sxi32 iCompileFlag);
JX9_PRIVATE sxi32 jx9CompileLiteral(jx9_gen_state *pGen, sxi32 iCompileFlag);
JX9_PRIVATE sxi32 jx9CompileSimpleString(jx9_gen_state *pGen, sxi32 iCompileFlag);
JX9_PRIVATE sxi32 jx9CompileString(jx9_gen_state *pGen, sxi32 iCompileFlag);
JX9_PRIVATE sxi32 jx9CompileAnnonFunc(jx9_gen_state *pGen, sxi32 iCompileFlag);
JX9_PRIVATE sxi32 jx9InitCodeGenerator(jx9_vm *pVm, ProcConsumer xErr, void *pErrData);
JX9_PRIVATE sxi32 jx9ResetCodeGenerator(jx9_vm *pVm, ProcConsumer xErr, void *pErrData);
JX9_PRIVATE sxi32 jx9GenCompileError(jx9_gen_state *pGen, sxi32 nErrType, sxu32 nLine, const char *zFormat, ...);
JX9_PRIVATE sxi32 jx9CompileScript(jx9_vm *pVm, SyString *pScript, sxi32 iFlags);
/* constant.c function prototypes */
JX9_PRIVATE void jx9RegisterBuiltInConstant(jx9_vm *pVm);
/* builtin.c function prototypes */
JX9_PRIVATE void jx9RegisterBuiltInFunction(jx9_vm *pVm);
/* hashmap.c function prototypes */
JX9_PRIVATE jx9_hashmap * jx9NewHashmap(jx9_vm *pVm, sxu32 (*xIntHash)(sxi64), sxu32 (*xBlobHash)(const void *, sxu32));
JX9_PRIVATE sxi32 jx9HashmapLoadBuiltin(jx9_vm *pVm);
JX9_PRIVATE sxi32 jx9HashmapRelease(jx9_hashmap *pMap, int FreeDS);
JX9_PRIVATE void  jx9HashmapUnref(jx9_hashmap *pMap);
JX9_PRIVATE sxi32 jx9HashmapLookup(jx9_hashmap *pMap, jx9_value *pKey, jx9_hashmap_node **ppNode);
JX9_PRIVATE sxi32 jx9HashmapInsert(jx9_hashmap *pMap, jx9_value *pKey, jx9_value *pVal);
JX9_PRIVATE sxi32 jx9HashmapUnion(jx9_hashmap *pLeft, jx9_hashmap *pRight);
JX9_PRIVATE sxi32 jx9HashmapDup(jx9_hashmap *pSrc, jx9_hashmap *pDest);
JX9_PRIVATE sxi32 jx9HashmapCmp(jx9_hashmap *pLeft, jx9_hashmap *pRight, int bStrict);
JX9_PRIVATE void jx9HashmapResetLoopCursor(jx9_hashmap *pMap);
JX9_PRIVATE jx9_hashmap_node * jx9HashmapGetNextEntry(jx9_hashmap *pMap);
JX9_PRIVATE jx9_value * jx9HashmapGetNodeValue(jx9_hashmap_node *pNode);
JX9_PRIVATE void jx9HashmapExtractNodeValue(jx9_hashmap_node *pNode, jx9_value *pValue, int bStore);
JX9_PRIVATE void jx9HashmapExtractNodeKey(jx9_hashmap_node *pNode, jx9_value *pKey);
JX9_PRIVATE void jx9RegisterHashmapFunctions(jx9_vm *pVm);
JX9_PRIVATE sxi32 jx9HashmapWalk(jx9_hashmap *pMap, int (*xWalk)(jx9_value *, jx9_value *, void *), void *pUserData);
#ifndef JX9_DISABLE_BUILTIN_FUNC
JX9_PRIVATE int jx9HashmapValuesToSet(jx9_hashmap *pMap, SySet *pOut);
/* builtin.c function prototypes */ 
JX9_PRIVATE sxi32 jx9InputFormat(int (*xConsumer)(jx9_context *, const char *, int, void *), 
	jx9_context *pCtx, const char *zIn, int nByte, int nArg, jx9_value **apArg, void *pUserData, int vf);
JX9_PRIVATE sxi32 jx9ProcessCsv(const char *zInput, int nByte, int delim, int encl, 
	int escape, sxi32 (*xConsumer)(const char *, int, void *), void *pUserData);
JX9_PRIVATE sxi32 jx9CsvConsumer(const char *zToken, int nTokenLen, void *pUserData);
JX9_PRIVATE sxi32 jx9StripTagsFromString(jx9_context *pCtx, const char *zIn, int nByte, const char *zTaglist, int nTaglen);
JX9_PRIVATE sxi32 jx9ParseIniString(jx9_context *pCtx, const char *zIn, sxu32 nByte, int bProcessSection);
#endif
/* vfs.c */
#ifndef JX9_DISABLE_BUILTIN_FUNC
JX9_PRIVATE void * jx9StreamOpenHandle(jx9_vm *pVm, const jx9_io_stream *pStream, const char *zFile, 
	int iFlags, int use_include, jx9_value *pResource, int bPushInclude, int *pNew);
JX9_PRIVATE sxi32 jx9StreamReadWholeFile(void *pHandle, const jx9_io_stream *pStream, SyBlob *pOut);
JX9_PRIVATE void jx9StreamCloseHandle(const jx9_io_stream *pStream, void *pHandle);
#endif /* JX9_DISABLE_BUILTIN_FUNC */
JX9_PRIVATE const char * jx9ExtractDirName(const char *zPath, int nByte, int *pLen);
JX9_PRIVATE sxi32 jx9RegisterIORoutine(jx9_vm *pVm);
JX9_PRIVATE const jx9_vfs * jx9ExportBuiltinVfs(void);
JX9_PRIVATE void * jx9ExportStdin(jx9_vm *pVm);
JX9_PRIVATE void * jx9ExportStdout(jx9_vm *pVm);
JX9_PRIVATE void * jx9ExportStderr(jx9_vm *pVm);
/* lib.c function prototypes */
#ifndef JX9_DISABLE_BUILTIN_FUNC
JX9_PRIVATE sxi32 SyArchiveInit(SyArchive *pArch, SyMemBackend *pAllocator, ProcHash xHash, ProcRawStrCmp xCmp);
JX9_PRIVATE sxi32 SyArchiveRelease(SyArchive *pArch);
JX9_PRIVATE sxi32 SyArchiveResetLoopCursor(SyArchive *pArch);
JX9_PRIVATE sxi32 SyArchiveGetNextEntry(SyArchive *pArch, SyArchiveEntry **ppEntry);
JX9_PRIVATE sxi32 SyZipExtractFromBuf(SyArchive *pArch, const char *zBuf, sxu32 nLen);
#endif /* JX9_DISABLE_BUILTIN_FUNC */
#ifndef JX9_DISABLE_BUILTIN_FUNC
JX9_PRIVATE sxi32 SyBinToHexConsumer(const void *pIn, sxu32 nLen, ProcConsumer xConsumer, void *pConsumerData);
#endif /* JX9_DISABLE_BUILTIN_FUNC */
#ifndef JX9_DISABLE_BUILTIN_FUNC
#ifndef JX9_DISABLE_HASH_FUNC
JX9_PRIVATE sxu32 SyCrc32(const void *pSrc, sxu32 nLen);
JX9_PRIVATE void MD5Update(MD5Context *ctx, const unsigned char *buf, unsigned int len);
JX9_PRIVATE void MD5Final(unsigned char digest[16], MD5Context *ctx);
JX9_PRIVATE sxi32 MD5Init(MD5Context *pCtx);
JX9_PRIVATE sxi32 SyMD5Compute(const void *pIn, sxu32 nLen, unsigned char zDigest[16]);
JX9_PRIVATE void SHA1Init(SHA1Context *context);
JX9_PRIVATE void SHA1Update(SHA1Context *context, const unsigned char *data, unsigned int len);
JX9_PRIVATE void SHA1Final(SHA1Context *context, unsigned char digest[20]);
JX9_PRIVATE sxi32 SySha1Compute(const void *pIn, sxu32 nLen, unsigned char zDigest[20]);
#endif
#endif /* JX9_DISABLE_BUILTIN_FUNC */
JX9_PRIVATE sxi32 SyRandomness(SyPRNGCtx *pCtx, void *pBuf, sxu32 nLen);
JX9_PRIVATE sxi32 SyRandomnessInit(SyPRNGCtx *pCtx, ProcRandomSeed xSeed, void *pUserData);
JX9_PRIVATE sxu32 SyBufferFormat(char *zBuf, sxu32 nLen, const char *zFormat, ...);
JX9_PRIVATE sxu32 SyBlobFormatAp(SyBlob *pBlob, const char *zFormat, va_list ap);
JX9_PRIVATE sxu32 SyBlobFormat(SyBlob *pBlob, const char *zFormat, ...);
JX9_PRIVATE sxi32 SyProcFormat(ProcConsumer xConsumer, void *pData, const char *zFormat, ...);
#ifndef JX9_DISABLE_BUILTIN_FUNC
JX9_PRIVATE const char *SyTimeGetMonth(sxi32 iMonth);
JX9_PRIVATE const char *SyTimeGetDay(sxi32 iDay);
#endif /* JX9_DISABLE_BUILTIN_FUNC */
JX9_PRIVATE sxi32 SyUriDecode(const char *zSrc, sxu32 nLen, ProcConsumer xConsumer, void *pUserData, int bUTF8);
#ifndef JX9_DISABLE_BUILTIN_FUNC
JX9_PRIVATE sxi32 SyUriEncode(const char *zSrc, sxu32 nLen, ProcConsumer xConsumer, void *pUserData);
#endif
JX9_PRIVATE sxi32 SyLexRelease(SyLex *pLex);
JX9_PRIVATE sxi32 SyLexTokenizeInput(SyLex *pLex, const char *zInput, sxu32 nLen, void *pCtxData, ProcSort xSort, ProcCmp xCmp);
JX9_PRIVATE sxi32 SyLexInit(SyLex *pLex, SySet *pSet, ProcTokenizer xTokenizer, void *pUserData);
#ifndef JX9_DISABLE_BUILTIN_FUNC
JX9_PRIVATE sxi32 SyBase64Decode(const char *zB64, sxu32 nLen, ProcConsumer xConsumer, void *pUserData);
JX9_PRIVATE sxi32 SyBase64Encode(const char *zSrc, sxu32 nLen, ProcConsumer xConsumer, void *pUserData);
#endif /* JX9_DISABLE_BUILTIN_FUNC */
JX9_PRIVATE sxu32 SyBinHash(const void *pSrc, sxu32 nLen);
JX9_PRIVATE sxi32 SyStrToReal(const char *zSrc, sxu32 nLen, void *pOutVal, const char **zRest);
JX9_PRIVATE sxi32 SyBinaryStrToInt64(const char *zSrc, sxu32 nLen, void *pOutVal, const char **zRest);
JX9_PRIVATE sxi32 SyOctalStrToInt64(const char *zSrc, sxu32 nLen, void *pOutVal, const char **zRest);
JX9_PRIVATE sxi32 SyHexStrToInt64(const char *zSrc, sxu32 nLen, void *pOutVal, const char **zRest);
JX9_PRIVATE sxi32 SyHexToint(sxi32 c);
JX9_PRIVATE sxi32 SyStrToInt64(const char *zSrc, sxu32 nLen, void *pOutVal, const char **zRest);
JX9_PRIVATE sxi32 SyStrToInt32(const char *zSrc, sxu32 nLen, void *pOutVal, const char **zRest);
JX9_PRIVATE sxi32 SyStrIsNumeric(const char *zSrc, sxu32 nLen, sxu8 *pReal, const char **pzTail);
JX9_PRIVATE sxi32 SyHashInsert(SyHash *pHash, const void *pKey, sxu32 nKeyLen, void *pUserData);
JX9_PRIVATE sxi32 SyHashForEach(SyHash *pHash, sxi32(*xStep)(SyHashEntry *, void *), void *pUserData);
JX9_PRIVATE sxi32 SyHashDeleteEntry(SyHash *pHash, const void *pKey, sxu32 nKeyLen, void **ppUserData);
JX9_PRIVATE SyHashEntry *SyHashGet(SyHash *pHash, const void *pKey, sxu32 nKeyLen);
JX9_PRIVATE sxi32 SyHashRelease(SyHash *pHash);
JX9_PRIVATE sxi32 SyHashInit(SyHash *pHash, SyMemBackend *pAllocator, ProcHash xHash, ProcCmp xCmp);
JX9_PRIVATE void *SySetAt(SySet *pSet, sxu32 nIdx);
JX9_PRIVATE void *SySetPop(SySet *pSet);
JX9_PRIVATE void *SySetPeek(SySet *pSet);
JX9_PRIVATE sxi32 SySetRelease(SySet *pSet);
JX9_PRIVATE sxi32 SySetReset(SySet *pSet);
JX9_PRIVATE sxi32 SySetResetCursor(SySet *pSet);
JX9_PRIVATE sxi32 SySetGetNextEntry(SySet *pSet, void **ppEntry);
JX9_PRIVATE sxi32 SySetAlloc(SySet *pSet, sxi32 nItem);
JX9_PRIVATE sxi32 SySetPut(SySet *pSet, const void *pItem);
JX9_PRIVATE sxi32 SySetInit(SySet *pSet, SyMemBackend *pAllocator, sxu32 ElemSize);
#ifndef JX9_DISABLE_BUILTIN_FUNC
JX9_PRIVATE sxi32 SyBlobSearch(const void *pBlob, sxu32 nLen, const void *pPattern, sxu32 pLen, sxu32 *pOfft);
#endif
JX9_PRIVATE sxi32 SyBlobRelease(SyBlob *pBlob);
JX9_PRIVATE sxi32 SyBlobReset(SyBlob *pBlob);
JX9_PRIVATE sxi32 SyBlobTruncate(SyBlob *pBlob,sxu32 nNewLen);
JX9_PRIVATE sxi32 SyBlobDup(SyBlob *pSrc, SyBlob *pDest);
JX9_PRIVATE sxi32 SyBlobNullAppend(SyBlob *pBlob);
JX9_PRIVATE sxi32 SyBlobAppend(SyBlob *pBlob, const void *pData, sxu32 nSize);
JX9_PRIVATE sxi32 SyBlobReadOnly(SyBlob *pBlob, const void *pData, sxu32 nByte);
JX9_PRIVATE sxi32 SyBlobInit(SyBlob *pBlob, SyMemBackend *pAllocator);
JX9_PRIVATE sxi32 SyBlobInitFromBuf(SyBlob *pBlob, void *pBuffer, sxu32 nSize);
JX9_PRIVATE char *SyMemBackendStrDup(SyMemBackend *pBackend, const char *zSrc, sxu32 nSize);
JX9_PRIVATE void *SyMemBackendDup(SyMemBackend *pBackend, const void *pSrc, sxu32 nSize);
JX9_PRIVATE sxi32 SyMemBackendRelease(SyMemBackend *pBackend);
JX9_PRIVATE sxi32 SyMemBackendInitFromOthers(SyMemBackend *pBackend, const SyMemMethods *pMethods, ProcMemError xMemErr, void *pUserData);
JX9_PRIVATE sxi32 SyMemBackendInit(SyMemBackend *pBackend, ProcMemError xMemErr, void *pUserData);
JX9_PRIVATE sxi32 SyMemBackendInitFromParent(SyMemBackend *pBackend,const SyMemBackend *pParent);
#if 0
/* Not used in the current release of the JX9 engine */
JX9_PRIVATE void *SyMemBackendPoolRealloc(SyMemBackend *pBackend, void *pOld, sxu32 nByte);
#endif
JX9_PRIVATE sxi32 SyMemBackendPoolFree(SyMemBackend *pBackend, void *pChunk);
JX9_PRIVATE void *SyMemBackendPoolAlloc(SyMemBackend *pBackend, sxu32 nByte);
JX9_PRIVATE sxi32 SyMemBackendFree(SyMemBackend *pBackend, void *pChunk);
JX9_PRIVATE void *SyMemBackendRealloc(SyMemBackend *pBackend, void *pOld, sxu32 nByte);
JX9_PRIVATE void *SyMemBackendAlloc(SyMemBackend *pBackend, sxu32 nByte);
JX9_PRIVATE sxu32 SyMemcpy(const void *pSrc, void *pDest, sxu32 nLen);
JX9_PRIVATE sxi32 SyMemcmp(const void *pB1, const void *pB2, sxu32 nSize);
JX9_PRIVATE void SyZero(void *pSrc, sxu32 nSize);
JX9_PRIVATE sxi32 SyStrnicmp(const char *zLeft, const char *zRight, sxu32 SLen);
JX9_PRIVATE sxu32 Systrcpy(char *zDest, sxu32 nDestLen, const char *zSrc, sxu32 nLen);
#if !defined(JX9_DISABLE_BUILTIN_FUNC) || defined(__APPLE__)
JX9_PRIVATE sxi32 SyStrncmp(const char *zLeft, const char *zRight, sxu32 nLen);
#endif
JX9_PRIVATE sxi32 SyByteListFind(const char *zSrc, sxu32 nLen, const char *zList, sxu32 *pFirstPos);
#ifndef JX9_DISABLE_BUILTIN_FUNC
JX9_PRIVATE sxi32 SyByteFind2(const char *zStr, sxu32 nLen, sxi32 c, sxu32 *pPos);
#endif
JX9_PRIVATE sxi32 SyByteFind(const char *zStr, sxu32 nLen, sxi32 c, sxu32 *pPos);
JX9_PRIVATE sxu32 SyStrlen(const char *zSrc);
#if defined(JX9_ENABLE_THREADS)
JX9_PRIVATE const SyMutexMethods *SyMutexExportMethods(void);
JX9_PRIVATE sxi32 SyMemBackendMakeThreadSafe(SyMemBackend *pBackend, const SyMutexMethods *pMethods);
JX9_PRIVATE sxi32 SyMemBackendDisbaleMutexing(SyMemBackend *pBackend);
#endif
JX9_PRIVATE void SyBigEndianPack32(unsigned char *buf,sxu32 nb);
JX9_PRIVATE void SyBigEndianUnpack32(const unsigned char *buf,sxu32 *uNB);
JX9_PRIVATE void SyBigEndianPack16(unsigned char *buf,sxu16 nb);
JX9_PRIVATE void SyBigEndianUnpack16(const unsigned char *buf,sxu16 *uNB);
JX9_PRIVATE void SyBigEndianPack64(unsigned char *buf,sxu64 n64);
JX9_PRIVATE void SyBigEndianUnpack64(const unsigned char *buf,sxu64 *n64);
JX9_PRIVATE sxi32 SyBlobAppendBig64(SyBlob *pBlob,sxu64 n64);
JX9_PRIVATE sxi32 SyBlobAppendBig32(SyBlob *pBlob,sxu32 n32);
JX9_PRIVATE sxi32 SyBlobAppendBig16(SyBlob *pBlob,sxu16 n16);
JX9_PRIVATE void SyTimeFormatToDos(Sytm *pFmt,sxu32 *pOut);
JX9_PRIVATE void SyDosTimeFormat(sxu32 nDosDate, Sytm *pOut);
#endif /* __JX9INT_H__ */
