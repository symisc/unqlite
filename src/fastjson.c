/*
 * Symisc unQLite: An Embeddable NoSQL (Post Modern) Database Engine.
 * Copyright (C) 2012-2026, Symisc Systems https://unqlite.symisc.net/
 * Version 1.2.1
 * For information on licensing, redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES
 * please contact Symisc Systems via:
 *       legal@symisc.net
 *       licensing@symisc.net
 *       contact@symisc.net
 * or visit:
 *      https://unqlite.symisc.net/licensing.html
 */
 /* $SymiscID: fastjson.c v1.1 FreeBSD 2012-12-05 22:52 stable <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif
/* JSON binary encoding, decoding and stuff like that */
#ifndef UNQLITE_FAST_JSON_NEST_LIMIT
#if defined(__WINNT__) || defined(__UNIXES__)
#define UNQLITE_FAST_JSON_NEST_LIMIT 64 /* Nesting limit */
#else
#define UNQLITE_FAST_JSON_NEST_LIMIT 32 /* Nesting limit */
#endif
#endif /* UNQLITE_FAST_JSON_NEST_LIMIT */
/* 
 * JSON to Binary using the FastJSON implementation (BigEndian).
 */
/*
 * FastJSON implemented binary token.
 */
#define FJSON_DOC_START    1 /* { */
#define FJSON_DOC_END      2 /* } */
#define FJSON_ARRAY_START  3 /* [ */
#define FJSON_ARRAY_END    4 /* ] */
#define FJSON_COLON        5 /* : */
#define FJSON_COMMA        6 /* , */
#define FJSON_ID           7 /* ID + 4 Bytes length */
#define FJSON_STRING       8 /* String + 4 bytes length */
#define FJSON_BYTE         9 /* Byte */
#define FJSON_INT64       10 /* Integer 64 + 8 bytes */
#define FJSON_REAL        18 /* Floating point value + 2 bytes */
#define FJSON_NULL        23 /* NULL */
#define FJSON_TRUE        24 /* TRUE */
#define FJSON_FALSE       25 /* FALSE */
/*
 * Encode a Jx9 value to binary JSON.
 */
UNQLITE_PRIVATE sxi32 FastJsonEncode(
	jx9_value *pValue, /* Value to encode */
	SyBlob *pOut,      /* Store encoded value here */
	int iNest          /* Nesting limit */ 
	)
{
	sxi32 iType = pValue ? pValue->iFlags : MEMOBJ_NULL;
	sxi32 rc = SXRET_OK;
	int c;
	if( iNest >= UNQLITE_FAST_JSON_NEST_LIMIT ){
		/* Nesting limit reached */
		return SXERR_LIMIT;
	}
	if( iType & (MEMOBJ_NULL|MEMOBJ_RES) ){
		/*
		 * Resources are encoded as null also.
		 */
		c = FJSON_NULL;
		rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
	}else if( iType & MEMOBJ_BOOL ){
		c = pValue->x.iVal ? FJSON_TRUE : FJSON_FALSE;
		rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
	}else if( iType & MEMOBJ_STRING ){
		unsigned char zBuf[sizeof(sxu32)]; /* String length */
		c = FJSON_STRING;
		SyBigEndianPack32(zBuf,SyBlobLength(&pValue->sBlob));
		rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
		if( rc == SXRET_OK ){
			rc = SyBlobAppend(pOut,(const void *)zBuf,sizeof(zBuf));
			if( rc == SXRET_OK ){
				rc = SyBlobAppend(pOut,SyBlobData(&pValue->sBlob),SyBlobLength(&pValue->sBlob));
			}
		}
	}else if( iType & MEMOBJ_INT ){
		unsigned char zBuf[8];
		/* 64bit big endian integer */
		c = FJSON_INT64;
		rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
		if( rc == SXRET_OK ){
			SyBigEndianPack64(zBuf,(sxu64)pValue->x.iVal);
			rc = SyBlobAppend(pOut,(const void *)zBuf,sizeof(zBuf));
		}
	}else if( iType & MEMOBJ_REAL ){
		/* Real number */
		c = FJSON_REAL;
		rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
		if( rc == SXRET_OK ){
			sxu32 iOfft = SyBlobLength(pOut);
			rc = SyBlobAppendBig16(pOut,0);
			if( rc == SXRET_OK ){
				unsigned char *zBlob;
				SyBlobFormat(pOut,"%.15g",pValue->x.rVal);
				zBlob = (unsigned char *)SyBlobDataAt(pOut,iOfft);
				SyBigEndianPack16(zBlob,(sxu16)(SyBlobLength(pOut) - ( 2 + iOfft)));
			}
		}
	}else if( iType & MEMOBJ_HASHMAP ){
		/* A JSON object or array */
		jx9_hashmap *pMap = (jx9_hashmap *)pValue->x.pOther;
		jx9_hashmap_node *pNode;
		jx9_value *pEntry;
		/* Reset the hashmap loop cursor */
		jx9HashmapResetLoopCursor(pMap);
		if( pMap->iFlags & HASHMAP_JSON_OBJECT ){
			jx9_value sKey;
			/* A JSON object */
			c = FJSON_DOC_START; /* { */
			rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
			if( rc == SXRET_OK ){
				jx9MemObjInit(pMap->pVm,&sKey);
				/* Encode object entries */
				while((pNode = jx9HashmapGetNextEntry(pMap)) != 0 ){
					/* Extract the key */
					jx9HashmapExtractNodeKey(pNode,&sKey);
					/* Encode it */
					rc = FastJsonEncode(&sKey,pOut,iNest+1);
					if( rc != SXRET_OK ){
						break;
					}
					c = FJSON_COLON;
					rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
					if( rc != SXRET_OK ){
						break;
					}
					/* Extract the value */
					pEntry = jx9HashmapGetNodeValue(pNode);
					/* Encode it */
					rc = FastJsonEncode(pEntry,pOut,iNest+1);
					if( rc != SXRET_OK ){
						break;
					}
					/* Delimit the entry */
					c = FJSON_COMMA;
					rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
					if( rc != SXRET_OK ){
						break;
					}
				}
				jx9MemObjRelease(&sKey);
				if( rc == SXRET_OK ){
					c = FJSON_DOC_END; /* } */
					rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
				}
			}
		}else{
			/* A JSON array */
			c = FJSON_ARRAY_START; /* [ */
			rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
			if( rc == SXRET_OK ){
				/* Encode array entries */
				while( (pNode = jx9HashmapGetNextEntry(pMap)) != 0 ){
					/* Extract the value */
					pEntry = jx9HashmapGetNodeValue(pNode);
					/* Encode it */
					rc = FastJsonEncode(pEntry,pOut,iNest+1);
					if( rc != SXRET_OK ){
						break;
					}
					/* Delimit the entry */
					c = FJSON_COMMA;
					rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
					if( rc != SXRET_OK ){
						break;
					}
				}
				if( rc == SXRET_OK ){
					c = FJSON_ARRAY_END; /* ] */
					rc = SyBlobAppend(pOut,(const void *)&c,sizeof(char));
				}
			}
		}
	}
	return rc;
}
/*
 * Decode a FastJSON binary blob.
 */
UNQLITE_PRIVATE sxi32 FastJsonDecode(
	const void *pIn,  /* Binary JSON  */
	sxu32 nByte,      /* Chunk delimiter */
	jx9_value *pOut,  /* Decoded value */
	const unsigned char **pzPtr,
	int iNest /* Nesting limit */
	)
{
	const unsigned char *zIn = (const unsigned char *)pIn;
	const unsigned char *zEnd = &zIn[nByte];
	sxi32 rc = SXRET_OK;
	int c;
	if( iNest >= UNQLITE_FAST_JSON_NEST_LIMIT ){
		/* Nesting limit reached */
		return SXERR_LIMIT;
	}
	c = zIn[0];
	/* Advance the stream cursor */
	zIn++;
	/* Process the binary token */
	switch(c){
	case FJSON_NULL:
		/* null */
		jx9_value_null(pOut);
		break;
	case FJSON_FALSE:
		/* Boolean FALSE */
		jx9_value_bool(pOut,0);
		break;
	case FJSON_TRUE:
		/* Boolean TRUE */
		jx9_value_bool(pOut,1);
		break;
	case FJSON_INT64: {
		/* 64Bit integer */
		sxu64 iVal;
		/* Sanity check */
		if( &zIn[8] >= zEnd ){
			/* Corrupt chunk */
			rc = SXERR_CORRUPT;
			break;
		}
		SyBigEndianUnpack64(zIn,&iVal);
		/* Advance the pointer */
		zIn += 8;
		jx9_value_int64(pOut,(jx9_int64)iVal);
		break;
					  }
	case FJSON_REAL: {
		/* Real number */
		double iVal = 0; /* cc warning */
		sxu16 iLen;
		/* Sanity check */
		if( &zIn[2] >= zEnd ){
			/* Corrupt chunk */
			rc = SXERR_CORRUPT;
			break;
		}
		SyBigEndianUnpack16(zIn,&iLen);
		if( &zIn[iLen] >= zEnd ){
			/* Corrupt chunk */
			rc = SXERR_CORRUPT;
			break;
		}
		zIn += 2;
		SyStrToReal((const char *)zIn,(sxu32)iLen,&iVal,0);
		/* Advance the pointer */
		zIn += iLen;
		jx9_value_double(pOut,iVal);
		break;
					 }
	case FJSON_STRING: {
		/* UTF-8/Binary chunk */
		sxu32 iLength;
		/* Sanity check */
		if( &zIn[4] >= zEnd ){
			/* Corrupt chunk */
			rc = SXERR_CORRUPT;
			break;
		}
		SyBigEndianUnpack32(zIn,&iLength);
		if( &zIn[iLength] >= zEnd ){
			/* Corrupt chunk */
			rc = SXERR_CORRUPT;
			break;
		}
		zIn += 4;
		/* Invalidate any prior representation */
		if( pOut->iFlags & MEMOBJ_STRING ){
			/* Reset the string cursor */
			SyBlobReset(&pOut->sBlob);
		}
		rc = jx9MemObjStringAppend(pOut,(const char *)zIn,iLength);
		/* Update pointer */
		zIn += iLength;
		break;
					   }
	case FJSON_ARRAY_START: {
		/* Binary JSON array */
		jx9_hashmap *pMap;
		jx9_value sVal;
		/* Allocate a new hashmap */
		pMap = (jx9_hashmap *)jx9NewHashmap(pOut->pVm,0,0);
		if( pMap == 0 ){
			rc = SXERR_MEM;
			break;
		}
		jx9MemObjInit(pOut->pVm,&sVal);
		jx9MemObjRelease(pOut);
		MemObjSetType(pOut,MEMOBJ_HASHMAP);
		pOut->x.pOther = pMap;
		rc = SXRET_OK;
		for(;;){
			/* Jump leading binary commas */
			while (zIn < zEnd && zIn[0] == FJSON_COMMA ){
				zIn++;
			}
			if( zIn >= zEnd || zIn[0] == FJSON_ARRAY_END ){
				if( zIn < zEnd ){
					zIn++; /* Jump the trailing binary ] */
				}
				break;
			}
			/* Decode the value */
			rc = FastJsonDecode((const void *)zIn,(sxu32)(zEnd-zIn),&sVal,&zIn,iNest+1);
			if( rc != SXRET_OK ){
				break;
			}
			/* Insert the decoded value */
			rc = jx9HashmapInsert(pMap,0,&sVal);
			if( rc != UNQLITE_OK ){
				break;
			}
		}
		if( rc != SXRET_OK ){
			jx9MemObjRelease(pOut);
		}
		jx9MemObjRelease(&sVal);
		break;
							}
	case FJSON_DOC_START: {
		/* Binary JSON object */
		jx9_value sVal,sKey;
		jx9_hashmap *pMap;
		/* Allocate a new hashmap */
		pMap = (jx9_hashmap *)jx9NewHashmap(pOut->pVm,0,0);
		if( pMap == 0 ){
			rc = SXERR_MEM;
			break;
		}
		jx9MemObjInit(pOut->pVm,&sVal);
		jx9MemObjInit(pOut->pVm,&sKey);
		jx9MemObjRelease(pOut);
		MemObjSetType(pOut,MEMOBJ_HASHMAP);
		pOut->x.pOther = pMap;
		rc = SXRET_OK;
		for(;;){
			/* Jump leading binary commas */
			while (zIn < zEnd && zIn[0] == FJSON_COMMA ){
				zIn++;
			}
			if( zIn >= zEnd || zIn[0] == FJSON_DOC_END ){
				if( zIn < zEnd ){
					zIn++; /* Jump the trailing binary } */
				}
				break;
			}
			/* Extract the key */
			rc = FastJsonDecode((const void *)zIn,(sxu32)(zEnd-zIn),&sKey,&zIn,iNest+1);
			if( rc != UNQLITE_OK ){
				break;
			}
			if( zIn >= zEnd || zIn[0] != FJSON_COLON ){
				rc = UNQLITE_CORRUPT;
				break;
			}
			zIn++; /* Jump the binary colon ':' */
			if( zIn >= zEnd ){
				rc = UNQLITE_CORRUPT;
				break;
			}
			/* Decode the value */
			rc = FastJsonDecode((const void *)zIn,(sxu32)(zEnd-zIn),&sVal,&zIn,iNest+1);
			if( rc != SXRET_OK ){
				break;
			}
			/* Insert the key and its associated value */
			rc = jx9HashmapInsert(pMap,&sKey,&sVal);
			if( rc != UNQLITE_OK ){
				break;
			}
		}
		if( rc != SXRET_OK ){
			jx9MemObjRelease(pOut);
		}
		jx9MemObjRelease(&sVal);
		jx9MemObjRelease(&sKey);
		break;
						  }
	default:
		/* Corrupt data */
		rc = SXERR_CORRUPT;
		break;
	}
	if( pzPtr ){
		*pzPtr = zIn;
	}
	return rc;
}
