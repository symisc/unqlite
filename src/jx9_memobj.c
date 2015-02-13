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
 /* $SymiscID: memobj.c v2.7 FreeBSD 2012-08-09 03:40 stable <chm@symisc.net> $ */
#ifndef JX9_AMALGAMATION
#include "jx9Int.h"
#endif
/* This file manage low-level stuff related to indexed memory objects [i.e: jx9_value] */
/*
 * Notes on memory objects [i.e: jx9_value].
 * Internally, the JX9 virtual machine manipulates nearly all JX9 values
 * [i.e: string, int, float, resource, object, bool, null..] as jx9_values structures.
 * Each jx9_values struct may cache multiple representations (string, 
 * integer etc.) of the same value.
 */
/*
 * Convert a 64-bit IEEE double into a 64-bit signed integer.
 * If the double is too large, return 0x8000000000000000.
 *
 * Most systems appear to do this simply by assigning ariables and without
 * the extra range tests.
 * But there are reports that windows throws an expection if the floating 
 * point value is out of range.
 */
static sxi64 MemObjRealToInt(jx9_value *pObj)
{
#ifdef JX9_OMIT_FLOATING_POINT
	/* Real and 64bit integer are the same when floating point arithmetic
	 * is omitted from the build.
	 */
	return pObj->x.rVal;
#else
 /*
  ** Many compilers we encounter do not define constants for the
  ** minimum and maximum 64-bit integers, or they define them
  ** inconsistently.  And many do not understand the "LL" notation.
  ** So we define our own static constants here using nothing
  ** larger than a 32-bit integer constant.
  */
  static const sxi64 maxInt = LARGEST_INT64;
  static const sxi64 minInt = SMALLEST_INT64;
  jx9_real r = pObj->x.rVal;
  if( r<(jx9_real)minInt ){
    return minInt;
  }else if( r>(jx9_real)maxInt ){
    /* minInt is correct here - not maxInt.  It turns out that assigning
    ** a very large positive number to an integer results in a very large
    ** negative integer.  This makes no sense, but it is what x86 hardware
    ** does so for compatibility we will do the same in software. */
    return minInt;
  }else{
    return (sxi64)r;
  }
#endif
}
/*
 * Convert a raw token value typically a stream of digit [i.e: hex, octal, binary or decimal] 
 * to a 64-bit integer.
 */
JX9_PRIVATE sxi64 jx9TokenValueToInt64(SyString *pVal)
{
	sxi64 iVal = 0;
	if( pVal->nByte <= 0 ){
		return 0;
	}
	if( pVal->zString[0] == '0' ){
		sxi32 c;
		if( pVal->nByte == sizeof(char) ){
			return 0;
		}
		c = pVal->zString[1];
		if( c  == 'x' || c == 'X' ){
			/* Hex digit stream */
			SyHexStrToInt64(pVal->zString, pVal->nByte, (void *)&iVal, 0);
		}else if( c == 'b' || c == 'B' ){
			/* Binary digit stream */
			SyBinaryStrToInt64(pVal->zString, pVal->nByte, (void *)&iVal, 0);
		}else{
			/* Octal digit stream */
			SyOctalStrToInt64(pVal->zString, pVal->nByte, (void *)&iVal, 0);
		}
	}else{
		/* Decimal digit stream */
		SyStrToInt64(pVal->zString, pVal->nByte, (void *)&iVal, 0);
	}
	return iVal;
}
/*
 * Return some kind of 64-bit integer value which is the best we can
 * do at representing the value that pObj describes as a string
 * representation.
 */
static sxi64 MemObjStringToInt(jx9_value *pObj)
{
	SyString sVal;
	SyStringInitFromBuf(&sVal, SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob));
	return jx9TokenValueToInt64(&sVal);	
}
/*
 * Return some kind of integer value which is the best we can
 * do at representing the value that pObj describes as an integer.
 * If pObj is an integer, then the value is exact. If pObj is
 * a floating-point then  the value returned is the integer part.
 * If pObj is a string, then we make an attempt to convert it into
 * a integer and return that. 
 * If pObj represents a NULL value, return 0.
 */
static sxi64 MemObjIntValue(jx9_value *pObj)
{
	sxi32 iFlags;
	iFlags = pObj->iFlags;
	if (iFlags & MEMOBJ_REAL ){
		return MemObjRealToInt(&(*pObj));
	}else if( iFlags & (MEMOBJ_INT|MEMOBJ_BOOL) ){
		return pObj->x.iVal;
	}else if (iFlags & MEMOBJ_STRING) {
		return MemObjStringToInt(&(*pObj));
	}else if( iFlags & MEMOBJ_NULL ){
		return 0;
	}else if( iFlags & MEMOBJ_HASHMAP ){
		jx9_hashmap *pMap = (jx9_hashmap *)pObj->x.pOther;
		sxu32 n = pMap->nEntry;
		jx9HashmapUnref(pMap);
		/* Return total number of entries in the hashmap */
		return n; 
	}else if(iFlags & MEMOBJ_RES ){
		return pObj->x.pOther != 0;
	}
	/* CANT HAPPEN */
	return 0;
}
/*
 * Return some kind of real value which is the best we can
 * do at representing the value that pObj describes as a real.
 * If pObj is a real, then the value is exact.If pObj is an
 * integer then the integer  is promoted to real and that value
 * is returned.
 * If pObj is a string, then we make an attempt to convert it
 * into a real and return that. 
 * If pObj represents a NULL value, return 0.0
 */
static jx9_real MemObjRealValue(jx9_value *pObj)
{
	sxi32 iFlags;
	iFlags = pObj->iFlags;
	if( iFlags & MEMOBJ_REAL ){
		return pObj->x.rVal;
	}else if (iFlags & (MEMOBJ_INT|MEMOBJ_BOOL) ){
		return (jx9_real)pObj->x.iVal;
	}else if (iFlags & MEMOBJ_STRING){
		SyString sString;
#ifdef JX9_OMIT_FLOATING_POINT
		jx9_real rVal = 0;
#else
		jx9_real rVal = 0.0;
#endif
		SyStringInitFromBuf(&sString, SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob));
		if( SyBlobLength(&pObj->sBlob) > 0 ){
			/* Convert as much as we can */
#ifdef JX9_OMIT_FLOATING_POINT
			rVal = MemObjStringToInt(&(*pObj));
#else
			SyStrToReal(sString.zString, sString.nByte, (void *)&rVal, 0);
#endif
		}
		return rVal;
	}else if( iFlags & MEMOBJ_NULL ){
#ifdef JX9_OMIT_FLOATING_POINT
		return 0;
#else
		return 0.0;
#endif
	}else if( iFlags & MEMOBJ_HASHMAP ){
		/* Return the total number of entries in the hashmap */
		jx9_hashmap *pMap = (jx9_hashmap *)pObj->x.pOther;
		jx9_real n = (jx9_real)pMap->nEntry;
		jx9HashmapUnref(pMap);
		return n;
	}else if(iFlags & MEMOBJ_RES ){
		return (jx9_real)(pObj->x.pOther != 0);
	}
	/* NOT REACHED  */
	return 0;
}
/* 
 * Return the string representation of a given jx9_value.
 * This function never fail and always return SXRET_OK.
 */
static sxi32 MemObjStringValue(SyBlob *pOut,jx9_value *pObj)
{
	if( pObj->iFlags & MEMOBJ_REAL ){
		SyBlobFormat(&(*pOut), "%.15g", pObj->x.rVal);
	}else if( pObj->iFlags & MEMOBJ_INT ){
		SyBlobFormat(&(*pOut), "%qd", pObj->x.iVal);
		/* %qd (BSD quad) is equivalent to %lld in the libc printf */
	}else if( pObj->iFlags & MEMOBJ_BOOL ){
		if( pObj->x.iVal ){
			SyBlobAppend(&(*pOut),"true", sizeof("true")-1);
		}else{
			SyBlobAppend(&(*pOut),"false", sizeof("false")-1);
		}
	}else if( pObj->iFlags & MEMOBJ_HASHMAP ){
		/* Serialize JSON object or array */
		jx9JsonSerialize(pObj,pOut);
		jx9HashmapUnref((jx9_hashmap *)pObj->x.pOther);
	}else if(pObj->iFlags & MEMOBJ_RES ){
		SyBlobFormat(&(*pOut), "ResourceID_%#x", pObj->x.pOther);
	}
	return SXRET_OK;
}
/*
 * Return some kind of boolean value which is the best we can do
 * at representing the value that pObj describes as a boolean.
 * When converting to boolean, the following values are considered FALSE:
 * NULL
 * the boolean FALSE itself.
 * the integer 0 (zero).
 * the real 0.0 (zero).
 * the empty string, a stream of zero [i.e: "0", "00", "000", ...] and the string
 * "false".
 * an array with zero elements. 
 */
static sxi32 MemObjBooleanValue(jx9_value *pObj)
{
	sxi32 iFlags;	
	iFlags = pObj->iFlags;
	if (iFlags & MEMOBJ_REAL ){
#ifdef JX9_OMIT_FLOATING_POINT
		return pObj->x.rVal ? 1 : 0;
#else
		return pObj->x.rVal != 0.0 ? 1 : 0;
#endif
	}else if( iFlags & MEMOBJ_INT ){
		return pObj->x.iVal ? 1 : 0;
	}else if (iFlags & MEMOBJ_STRING) {
		SyString sString;
		SyStringInitFromBuf(&sString, SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob));
		if( sString.nByte == 0 ){
			/* Empty string */
			return 0;
		}else if( (sString.nByte == sizeof("true") - 1 && SyStrnicmp(sString.zString, "true", sizeof("true")-1) == 0) ||
			(sString.nByte == sizeof("on") - 1 && SyStrnicmp(sString.zString, "on", sizeof("on")-1) == 0) ||
			(sString.nByte == sizeof("yes") - 1 && SyStrnicmp(sString.zString, "yes", sizeof("yes")-1) == 0) ){
				return 1;
		}else if( sString.nByte == sizeof("false") - 1 && SyStrnicmp(sString.zString, "false", sizeof("false")-1) == 0 ){
			return 0;
		}else{
			const char *zIn, *zEnd;
			zIn = sString.zString;
			zEnd = &zIn[sString.nByte];
			while( zIn < zEnd && zIn[0] == '0' ){
				zIn++;
			}
			return zIn >= zEnd ? 0 : 1;
		}
	}else if( iFlags & MEMOBJ_NULL ){
		return 0;
	}else if( iFlags & MEMOBJ_HASHMAP ){
		jx9_hashmap *pMap = (jx9_hashmap *)pObj->x.pOther;
		sxu32 n = pMap->nEntry;
		jx9HashmapUnref(pMap);
		return n > 0 ? TRUE : FALSE;
	}else if(iFlags & MEMOBJ_RES ){
		return pObj->x.pOther != 0;
	}
	/* NOT REACHED */
	return 0;
}
/*
 * If the jx9_value is of type real, try to make it an integer also.
 */
static sxi32 MemObjTryIntger(jx9_value *pObj)
{
	sxi64 iVal = MemObjRealToInt(&(*pObj));
  /* Only mark the value as an integer if
  **
  **    (1) the round-trip conversion real->int->real is a no-op, and
  **    (2) The integer is neither the largest nor the smallest
  **        possible integer
  **
  ** The second and third terms in the following conditional enforces
  ** the second condition under the assumption that addition overflow causes
  ** values to wrap around.  On x86 hardware, the third term is always
  ** true and could be omitted.  But we leave it in because other
  ** architectures might behave differently.
  */
	if( pObj->x.rVal ==(jx9_real)iVal && iVal>SMALLEST_INT64 && iVal<LARGEST_INT64 ){
		pObj->x.iVal = iVal; 
		pObj->iFlags = MEMOBJ_INT;
	}
	return SXRET_OK;
}
/*
 * Convert a jx9_value to type integer.Invalidate any prior representations.
 */
JX9_PRIVATE sxi32 jx9MemObjToInteger(jx9_value *pObj)
{
	if( (pObj->iFlags & MEMOBJ_INT) == 0 ){
		/* Preform the conversion */
		pObj->x.iVal = MemObjIntValue(&(*pObj));
		/* Invalidate any prior representations */
		SyBlobRelease(&pObj->sBlob);
		MemObjSetType(pObj, MEMOBJ_INT);
	}
	return SXRET_OK;
}
/*
 * Convert a jx9_value to type real (Try to get an integer representation also).
 * Invalidate any prior representations
 */
JX9_PRIVATE sxi32 jx9MemObjToReal(jx9_value *pObj)
{
	if((pObj->iFlags & MEMOBJ_REAL) == 0 ){
		/* Preform the conversion */
		pObj->x.rVal = MemObjRealValue(&(*pObj));
		/* Invalidate any prior representations */
		SyBlobRelease(&pObj->sBlob);
		MemObjSetType(pObj, MEMOBJ_REAL);
	}
	return SXRET_OK;
}
/*
 * Convert a jx9_value to type boolean.Invalidate any prior representations.
 */
JX9_PRIVATE sxi32 jx9MemObjToBool(jx9_value *pObj)
{
	if( (pObj->iFlags & MEMOBJ_BOOL) == 0 ){
		/* Preform the conversion */
		pObj->x.iVal = MemObjBooleanValue(&(*pObj));
		/* Invalidate any prior representations */
		SyBlobRelease(&pObj->sBlob);
		MemObjSetType(pObj, MEMOBJ_BOOL);
	}
	return SXRET_OK;
}
/*
 * Convert a jx9_value to type string.Prior representations are NOT invalidated.
 */
JX9_PRIVATE sxi32 jx9MemObjToString(jx9_value *pObj)
{
	sxi32 rc = SXRET_OK;
	if( (pObj->iFlags & MEMOBJ_STRING) == 0 ){
		/* Perform the conversion */
		SyBlobReset(&pObj->sBlob); /* Reset the internal buffer */
		rc = MemObjStringValue(&pObj->sBlob, &(*pObj));
		MemObjSetType(pObj, MEMOBJ_STRING);
	}
	return rc;
}
/*
 * Nullify a jx9_value.In other words invalidate any prior
 * representation.
 */
JX9_PRIVATE sxi32 jx9MemObjToNull(jx9_value *pObj)
{
	return jx9MemObjRelease(pObj);
}
/*
 * Convert a jx9_value to type array.Invalidate any prior representations.
  * According to the JX9 language reference manual.
  *   For any of the types: integer, float, string, boolean converting a value
  *   to an array results in an array with a single element with index zero 
  *   and the value of the scalar which was converted.
  */
JX9_PRIVATE sxi32 jx9MemObjToHashmap(jx9_value *pObj)
{
	if( (pObj->iFlags & MEMOBJ_HASHMAP) == 0 ){
		jx9_hashmap *pMap;
		/* Allocate a new hashmap instance */
		pMap = jx9NewHashmap(pObj->pVm, 0, 0);
		if( pMap == 0 ){
			return SXERR_MEM;
		}
		if( (pObj->iFlags & (MEMOBJ_NULL|MEMOBJ_RES)) == 0 ){
			/* 
			 * According to the JX9 language reference manual.
			 *   For any of the types: integer, float, string, boolean converting a value
			 *   to an array results in an array with a single element with index zero 
			 *   and the value of the scalar which was converted.
			 */
			/* Insert a single element */
			jx9HashmapInsert(pMap, 0/* Automatic index assign */, &(*pObj));
			SyBlobRelease(&pObj->sBlob);
		}
		/* Invalidate any prior representation */
		MemObjSetType(pObj, MEMOBJ_HASHMAP);
		pObj->x.pOther = pMap;
	}
	return SXRET_OK;
}
/*
 * Return a pointer to the appropriate convertion method associated 
 * with the given type. 
 * Note on type juggling.
 * Accoding to the JX9 language reference manual
 *  JX9 does not require (or support) explicit type definition in variable
 *  declaration; a variable's type is determined by the context in which
 *  the variable is used. That is to say, if a string value is assigned 
 *  to variable $var, $var becomes a string. If an integer value is then
 *  assigned to $var, it becomes an integer. 
 */
JX9_PRIVATE ProcMemObjCast jx9MemObjCastMethod(sxi32 iFlags)
{
	if( iFlags & MEMOBJ_STRING ){
		return jx9MemObjToString;
	}else if( iFlags & MEMOBJ_INT ){
		return jx9MemObjToInteger;
	}else if( iFlags & MEMOBJ_REAL ){
		return jx9MemObjToReal;
	}else if( iFlags & MEMOBJ_BOOL ){
		return jx9MemObjToBool;
	}else if( iFlags & MEMOBJ_HASHMAP ){
		return jx9MemObjToHashmap;
	}
	/* NULL cast */
	return jx9MemObjToNull;
}
/*
 * Check whether the jx9_value is numeric [i.e: int/float/bool] or looks
 * like a numeric number [i.e: if the jx9_value is of type string.].
 * Return TRUE if numeric.FALSE otherwise.
 */
JX9_PRIVATE sxi32 jx9MemObjIsNumeric(jx9_value *pObj)
{
	if( pObj->iFlags & ( MEMOBJ_BOOL|MEMOBJ_INT|MEMOBJ_REAL) ){
		return TRUE;
	}else if( pObj->iFlags & (MEMOBJ_NULL|MEMOBJ_HASHMAP|MEMOBJ_RES) ){
		return FALSE;
	}else if( pObj->iFlags & MEMOBJ_STRING ){
		SyString sStr;
		sxi32 rc;
		SyStringInitFromBuf(&sStr, SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob));
		if( sStr.nByte <= 0 ){
			/* Empty string */
			return FALSE;
		}
		/* Check if the string representation looks like a numeric number */
		rc = SyStrIsNumeric(sStr.zString, sStr.nByte, 0, 0);
		return rc == SXRET_OK ? TRUE : FALSE;
	}
	/* NOT REACHED */
	return FALSE;
}
/*
 * Check whether the jx9_value is empty.Return TRUE if empty.
 * FALSE otherwise.
 * An jx9_value is considered empty if the following are true:
 * NULL value.
 * Boolean FALSE.
 * Integer/Float with a 0 (zero) value.
 * An empty string or a stream of 0 (zero) [i.e: "0", "00", "000", ...].
 * An empty array.
 * NOTE
 *  OBJECT VALUE MUST NOT BE MODIFIED.
 */
JX9_PRIVATE sxi32 jx9MemObjIsEmpty(jx9_value *pObj)
{
	if( pObj->iFlags & MEMOBJ_NULL ){
		return TRUE;
	}else if( pObj->iFlags & MEMOBJ_INT ){
		return pObj->x.iVal == 0 ? TRUE : FALSE;
	}else if( pObj->iFlags & MEMOBJ_REAL ){
		return pObj->x.rVal == (jx9_real)0 ? TRUE : FALSE;
	}else if( pObj->iFlags & MEMOBJ_BOOL ){
		return !pObj->x.iVal;
	}else if( pObj->iFlags & MEMOBJ_STRING ){
		if( SyBlobLength(&pObj->sBlob) <= 0 ){
			return TRUE;
		}else{
			const char *zIn, *zEnd;
			zIn = (const char *)SyBlobData(&pObj->sBlob);
			zEnd = &zIn[SyBlobLength(&pObj->sBlob)];
			while( zIn < zEnd ){
				if( zIn[0] != '0' ){
					break;
				}
				zIn++;
			}
			return zIn >= zEnd ? TRUE : FALSE;
		}
	}else if( pObj->iFlags & MEMOBJ_HASHMAP ){
		jx9_hashmap *pMap = (jx9_hashmap *)pObj->x.pOther;
		return pMap->nEntry == 0 ? TRUE : FALSE;
	}else if ( pObj->iFlags & (MEMOBJ_RES) ){
		return FALSE;
	}
	/* Assume empty by default */
	return TRUE;
}
/*
 * Convert a jx9_value so that it has types MEMOBJ_REAL or MEMOBJ_INT
 * or both.
 * Invalidate any prior representations. Every effort is made to force
 * the conversion, even if the input is a string that does not look 
 * completely like a number.Convert as much of the string as we can
 * and ignore the rest.
 */
JX9_PRIVATE sxi32 jx9MemObjToNumeric(jx9_value *pObj)
{
	if( pObj->iFlags & (MEMOBJ_INT|MEMOBJ_REAL|MEMOBJ_BOOL|MEMOBJ_NULL) ){
		if( pObj->iFlags & (MEMOBJ_BOOL|MEMOBJ_NULL) ){
			if( pObj->iFlags & MEMOBJ_NULL ){
				pObj->x.iVal = 0;
			}
			MemObjSetType(pObj, MEMOBJ_INT);
		}
		/* Already numeric */
		return  SXRET_OK;
	}
	if( pObj->iFlags & MEMOBJ_STRING ){
		sxi32 rc = SXERR_INVALID;
		sxu8 bReal = FALSE;
		SyString sString;
		SyStringInitFromBuf(&sString, SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob));
		/* Check if the given string looks like a numeric number */
		if( sString.nByte > 0 ){
			rc = SyStrIsNumeric(sString.zString, sString.nByte, &bReal, 0);
		}
		if( bReal ){
			jx9MemObjToReal(&(*pObj));
		}else{
			if( rc != SXRET_OK ){
				/* The input does not look at all like a number, set the value to 0 */
				pObj->x.iVal = 0;
			}else{
				/* Convert as much as we can */
				pObj->x.iVal = MemObjStringToInt(&(*pObj));
			}
			MemObjSetType(pObj, MEMOBJ_INT);
			SyBlobRelease(&pObj->sBlob);
		}
	}else if(pObj->iFlags & (MEMOBJ_HASHMAP|MEMOBJ_RES)){
		jx9MemObjToInteger(pObj);
	}else{
		/* Perform a blind cast */
		jx9MemObjToReal(&(*pObj));
	}
	return SXRET_OK;
}
/*
 * Try a get an integer representation of the given jx9_value.
 * If the jx9_value is not of type real, this function is a no-op.
 */
JX9_PRIVATE sxi32 jx9MemObjTryInteger(jx9_value *pObj)
{
	if( pObj->iFlags & MEMOBJ_REAL ){
		/* Work only with reals */
		MemObjTryIntger(&(*pObj));
	}
	return SXRET_OK;
}
/*
 * Initialize a jx9_value to the null type.
 */
JX9_PRIVATE sxi32 jx9MemObjInit(jx9_vm *pVm, jx9_value *pObj)
{
	/* Zero the structure */
	SyZero(pObj, sizeof(jx9_value));
	/* Initialize fields */
	pObj->pVm = pVm;
	SyBlobInit(&pObj->sBlob, &pVm->sAllocator);
	/* Set the NULL type */
	pObj->iFlags = MEMOBJ_NULL;
	return SXRET_OK;
}
/*
 * Initialize a jx9_value to the integer type.
 */
JX9_PRIVATE sxi32 jx9MemObjInitFromInt(jx9_vm *pVm, jx9_value *pObj, sxi64 iVal)
{
	/* Zero the structure */
	SyZero(pObj, sizeof(jx9_value));
	/* Initialize fields */
	pObj->pVm = pVm;
	SyBlobInit(&pObj->sBlob, &pVm->sAllocator);
	/* Set the desired type */
	pObj->x.iVal = iVal;
	pObj->iFlags = MEMOBJ_INT;
	return SXRET_OK;
}
/*
 * Initialize a jx9_value to the boolean type.
 */
JX9_PRIVATE sxi32 jx9MemObjInitFromBool(jx9_vm *pVm, jx9_value *pObj, sxi32 iVal)
{
	/* Zero the structure */
	SyZero(pObj, sizeof(jx9_value));
	/* Initialize fields */
	pObj->pVm = pVm;
	SyBlobInit(&pObj->sBlob, &pVm->sAllocator);
	/* Set the desired type */
	pObj->x.iVal = iVal ? 1 : 0;
	pObj->iFlags = MEMOBJ_BOOL;
	return SXRET_OK;
}
#if 0
/*
 * Initialize a jx9_value to the real type.
 */
JX9_PRIVATE sxi32 jx9MemObjInitFromReal(jx9_vm *pVm, jx9_value *pObj, jx9_real rVal)
{
	/* Zero the structure */
	SyZero(pObj, sizeof(jx9_value));
	/* Initialize fields */
	pObj->pVm = pVm;
	SyBlobInit(&pObj->sBlob, &pVm->sAllocator);
	/* Set the desired type */
	pObj->x.rVal = rVal;
	pObj->iFlags = MEMOBJ_REAL;
	return SXRET_OK;
}
#endif
/*
 * Initialize a jx9_value to the array type.
 */
JX9_PRIVATE sxi32 jx9MemObjInitFromArray(jx9_vm *pVm, jx9_value *pObj, jx9_hashmap *pArray)
{
	/* Zero the structure */
	SyZero(pObj, sizeof(jx9_value));
	/* Initialize fields */
	pObj->pVm = pVm;
	SyBlobInit(&pObj->sBlob, &pVm->sAllocator);
	/* Set the desired type */
	pObj->iFlags = MEMOBJ_HASHMAP;
	pObj->x.pOther = pArray;
	return SXRET_OK;
}
/*
 * Initialize a jx9_value to the string type.
 */
JX9_PRIVATE sxi32 jx9MemObjInitFromString(jx9_vm *pVm, jx9_value *pObj, const SyString *pVal)
{
	/* Zero the structure */
	SyZero(pObj, sizeof(jx9_value));
	/* Initialize fields */
	pObj->pVm = pVm;
	SyBlobInit(&pObj->sBlob, &pVm->sAllocator);
	if( pVal ){
		/* Append contents */
		SyBlobAppend(&pObj->sBlob, (const void *)pVal->zString, pVal->nByte);
	}
	/* Set the desired type */
	pObj->iFlags = MEMOBJ_STRING;
	return SXRET_OK;
}
/*
 * Append some contents to the internal buffer of a given jx9_value.
 * If the given jx9_value is not of type string, this function
 * invalidate any prior representation and set the string type.
 * Then a simple append operation is performed.
 */
JX9_PRIVATE sxi32 jx9MemObjStringAppend(jx9_value *pObj, const char *zData, sxu32 nLen)
{
	sxi32 rc;
	if( (pObj->iFlags & MEMOBJ_STRING) == 0 ){
		/* Invalidate any prior representation */
		jx9MemObjRelease(pObj);
		MemObjSetType(pObj, MEMOBJ_STRING);
	}
	/* Append contents */
	rc = SyBlobAppend(&pObj->sBlob, zData, nLen);
	return rc;
}
#if 0
/*
 * Format and append some contents to the internal buffer of a given jx9_value.
 * If the given jx9_value is not of type string, this function invalidate
 * any prior representation and set the string type.
 * Then a simple format and append operation is performed.
 */
JX9_PRIVATE sxi32 jx9MemObjStringFormat(jx9_value *pObj, const char *zFormat, va_list ap)
{
	sxi32 rc;
	if( (pObj->iFlags & MEMOBJ_STRING) == 0 ){
		/* Invalidate any prior representation */
		jx9MemObjRelease(pObj);
		MemObjSetType(pObj, MEMOBJ_STRING);
	}
	/* Format and append contents */
	rc = SyBlobFormatAp(&pObj->sBlob, zFormat, ap);
	return rc;
}
#endif
/*
 * Duplicate the contents of a jx9_value.
 */
JX9_PRIVATE sxi32 jx9MemObjStore(jx9_value *pSrc, jx9_value *pDest)
{
	jx9_hashmap *pMap = 0;
	sxi32 rc;
	if( pSrc->iFlags & MEMOBJ_HASHMAP ){
		/* Increment reference count */
		((jx9_hashmap *)pSrc->x.pOther)->iRef++;
	}
	if( pDest->iFlags & MEMOBJ_HASHMAP ){
		pMap = (jx9_hashmap *)pDest->x.pOther;
	}
	SyMemcpy((const void *)&(*pSrc), &(*pDest), sizeof(jx9_value)-(sizeof(jx9_vm *)+sizeof(SyBlob)+sizeof(sxu32)));
	rc = SXRET_OK;
	if( SyBlobLength(&pSrc->sBlob) > 0 ){
		SyBlobReset(&pDest->sBlob);
		rc = SyBlobDup(&pSrc->sBlob, &pDest->sBlob);
	}else{
		if( SyBlobLength(&pDest->sBlob) > 0 ){
			SyBlobRelease(&pDest->sBlob);
		}
	}
	if( pMap ){
		jx9HashmapUnref(pMap);
	}
	return rc;
}
/*
 * Duplicate the contents of a jx9_value but do not copy internal
 * buffer contents, simply point to it.
 */
JX9_PRIVATE sxi32 jx9MemObjLoad(jx9_value *pSrc, jx9_value *pDest)
{
	SyMemcpy((const void *)&(*pSrc), &(*pDest), 
		sizeof(jx9_value)-(sizeof(jx9_vm *)+sizeof(SyBlob)+sizeof(sxu32)));
	if( pSrc->iFlags & MEMOBJ_HASHMAP ){
		/* Increment reference count */
		((jx9_hashmap *)pSrc->x.pOther)->iRef++;
	}
	if( SyBlobLength(&pDest->sBlob) > 0 ){
		SyBlobRelease(&pDest->sBlob);
	}
	if( SyBlobLength(&pSrc->sBlob) > 0 ){
		SyBlobReadOnly(&pDest->sBlob, SyBlobData(&pSrc->sBlob), SyBlobLength(&pSrc->sBlob));
	}
	return SXRET_OK;
}
/*
 * Invalidate any prior representation of a given jx9_value.
 */
JX9_PRIVATE sxi32 jx9MemObjRelease(jx9_value *pObj)
{
	if( (pObj->iFlags & MEMOBJ_NULL) == 0 ){
		if( pObj->iFlags & MEMOBJ_HASHMAP ){
			jx9HashmapUnref((jx9_hashmap *)pObj->x.pOther);
		}
		/* Release the internal buffer */
		SyBlobRelease(&pObj->sBlob);
		/* Invalidate any prior representation */
		pObj->iFlags = MEMOBJ_NULL;
	}
	return SXRET_OK;
}
/*
 * Compare two jx9_values.
 * Return 0 if the values are equals, > 0 if pObj1 is greater than pObj2
 * or < 0 if pObj2 is greater than pObj1.
 * Type comparison table taken from the JX9 language reference manual.
 * Comparisons of $x with JX9 functions Expression
 *              gettype() 	empty() 	is_null() 	isset() 	boolean : if($x)
 * $x = ""; 	string 	    TRUE 	FALSE 	TRUE 	FALSE
 * $x = null 	NULL 	    TRUE 	TRUE 	FALSE 	FALSE
 * var $x; 	    NULL 	TRUE 	TRUE 	FALSE 	FALSE
 * $x is undefined 	NULL 	TRUE 	TRUE 	FALSE 	FALSE
 *  $x = array(); 	array 	TRUE 	FALSE 	TRUE 	FALSE
 * $x = false; 	boolean 	TRUE 	FALSE 	TRUE 	FALSE
 * $x = true; 	boolean 	FALSE 	FALSE 	TRUE 	TRUE
 * $x = 1; 	    integer 	FALSE 	FALSE 	TRUE 	TRUE
 * $x = 42; 	integer 	FALSE 	FALSE 	TRUE 	TRUE
 * $x = 0; 	    integer 	TRUE 	FALSE 	TRUE 	FALSE
 * $x = -1; 	integer 	FALSE 	FALSE 	TRUE 	TRUE
 * $x = "1"; 	string 	FALSE 	FALSE 	TRUE 	TRUE
 * $x = "0"; 	string 	TRUE 	FALSE 	TRUE 	FALSE
 * $x = "-1"; 	string 	FALSE 	FALSE 	TRUE 	TRUE
 * $x = "jx9"; 	string 	FALSE 	FALSE 	TRUE 	TRUE
 * $x = "true"; string 	FALSE 	FALSE 	TRUE 	TRUE
 * $x = "false"; string 	FALSE 	FALSE 	TRUE 	TRUE
 *      Loose comparisons with == 
 * TRUE 	FALSE 	1 	0 	-1 	"1" 	"0" 	"-1" 	NULL 	array() 	"jx9" 	""
 * TRUE 	TRUE 	FALSE 	TRUE 	FALSE 	TRUE 	TRUE 	FALSE 	TRUE 	FALSE 	FALSE 	TRUE 	FALSE
 * FALSE 	FALSE 	TRUE 	FALSE 	TRUE 	FALSE 	FALSE 	TRUE 	FALSE 	TRUE 	TRUE 	FALSE 	TRUE
 * 1 	TRUE 	FALSE 	TRUE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE
 * 0 	FALSE 	TRUE 	FALSE 	TRUE 	FALSE 	FALSE 	TRUE 	FALSE 	TRUE 	FALSE 	TRUE 	TRUE
 * -1 	TRUE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE
 * "1" 	TRUE 	FALSE 	TRUE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE
 * "0" 	FALSE 	TRUE 	FALSE 	TRUE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE
 * "-1" 	TRUE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE
 * NULL 	FALSE 	TRUE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	TRUE 	FALSE 	TRUE
 * array() 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	TRUE 	FALSE 	FALSE
 * "jx9" 	TRUE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE
 * "" 	FALSE 	TRUE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	TRUE
 *    Strict comparisons with === 
 * TRUE 	FALSE 	1 	0 	-1 	"1" 	"0" 	"-1" 	NULL 	array() 	"jx9" 	""
 * TRUE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE
 * FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE
 * 1 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE
 * 0 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE
 * -1 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE
 * "1" 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE
 * "0" 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE
 * "-1" 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE 	FALSE 
 * NULL 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE 	FALSE
 * array() 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE 	FALSE
 * "jx9" 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE 	FALSE
 * "" 	    FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	FALSE 	TRUE
 */
JX9_PRIVATE sxi32 jx9MemObjCmp(jx9_value *pObj1, jx9_value *pObj2, int bStrict, int iNest)
{
	sxi32 iComb; 
	sxi32 rc;
	if( bStrict ){
		sxi32 iF1, iF2;
		/* Strict comparisons with === */
		iF1 = pObj1->iFlags;
		iF2 = pObj2->iFlags;
		if( iF1 != iF2 ){
			/* Not of the same type */
			return 1;
		}
	}
	/* Combine flag together */
	iComb = pObj1->iFlags|pObj2->iFlags;
	if( iComb & (MEMOBJ_RES|MEMOBJ_BOOL) ){
		/* Convert to boolean: Keep in mind FALSE < TRUE */
		if( (pObj1->iFlags & MEMOBJ_BOOL) == 0 ){
			jx9MemObjToBool(pObj1);
		}
		if( (pObj2->iFlags & MEMOBJ_BOOL) == 0 ){
			jx9MemObjToBool(pObj2);
		}
		return (sxi32)((pObj1->x.iVal != 0) - (pObj2->x.iVal != 0));
	}else if( iComb & MEMOBJ_NULL ){
        if( (pObj1->iFlags & MEMOBJ_NULL) == 0 ){
            return 1;
        }
        if( (pObj2->iFlags & MEMOBJ_NULL) == 0 ){
            return -1;
        }
    }else if ( iComb & MEMOBJ_HASHMAP ){
		/* Hashmap aka 'array' comparison */
		if( (pObj1->iFlags & MEMOBJ_HASHMAP) == 0 ){
			/* Array is always greater */
			return -1;
		}
		if( (pObj2->iFlags & MEMOBJ_HASHMAP) == 0 ){
			/* Array is always greater */
			return 1;
		}
		/* Perform the comparison */
		rc = jx9HashmapCmp((jx9_hashmap *)pObj1->x.pOther, (jx9_hashmap *)pObj2->x.pOther, bStrict);
		return rc;
	}else if ( iComb & MEMOBJ_STRING ){
		SyString s1, s2;
		/* Perform a strict string comparison.*/
		if( (pObj1->iFlags&MEMOBJ_STRING) == 0 ){
			jx9MemObjToString(pObj1);
		}
		if( (pObj2->iFlags&MEMOBJ_STRING) == 0 ){
			jx9MemObjToString(pObj2);
		}
		SyStringInitFromBuf(&s1, SyBlobData(&pObj1->sBlob), SyBlobLength(&pObj1->sBlob));
		SyStringInitFromBuf(&s2, SyBlobData(&pObj2->sBlob), SyBlobLength(&pObj2->sBlob));
		/*
		 * Strings are compared using memcmp(). If one value is an exact prefix of the
		 * other, then the shorter value is less than the longer value.
		 */
		rc = SyMemcmp((const void *)s1.zString, (const void *)s2.zString, SXMIN(s1.nByte, s2.nByte));
		if( rc == 0 ){
			if( s1.nByte != s2.nByte ){
				rc = s1.nByte < s2.nByte ? -1 : 1;
			}
		}
		return rc;
	}else if( iComb & (MEMOBJ_INT|MEMOBJ_REAL) ){
		/* Perform a numeric comparison if one of the operand is numeric(integer or real) */
		if( (pObj1->iFlags & (MEMOBJ_INT|MEMOBJ_REAL)) == 0 ){
			jx9MemObjToNumeric(pObj1);
		}
		if( (pObj2->iFlags & (MEMOBJ_INT|MEMOBJ_REAL)) == 0 ){
			jx9MemObjToNumeric(pObj2);
		}
		if( (pObj1->iFlags & pObj2->iFlags & MEMOBJ_INT) == 0) {
			jx9_real r1, r2;
			/* Compare as reals */
			if( (pObj1->iFlags & MEMOBJ_REAL) == 0 ){
				jx9MemObjToReal(pObj1);
			}
			r1 = pObj1->x.rVal;	
			if( (pObj2->iFlags & MEMOBJ_REAL) == 0 ){
				jx9MemObjToReal(pObj2);
			}
			r2 = pObj2->x.rVal;
			if( r1 > r2 ){
				return 1;
			}else if( r1 < r2 ){
				return -1;
			}
			return 0;
		}else{
			/* Integer comparison */
			if( pObj1->x.iVal > pObj2->x.iVal ){
				return 1;
			}else if( pObj1->x.iVal < pObj2->x.iVal ){
				return -1;
			}
			return 0;
		}
	}
	/* NOT REACHED */
	SXUNUSED(iNest);
	return 0;
}
/*
 * Perform an addition operation of two jx9_values.
 * The reason this function is implemented here rather than 'vm.c'
 * is that the '+' operator is overloaded.
 * That is, the '+' operator is used for arithmetic operation and also 
 * used for operation on arrays [i.e: union]. When used with an array 
 * The + operator returns the right-hand array appended to the left-hand array.
 * For keys that exist in both arrays, the elements from the left-hand array
 * will be used, and the matching elements from the right-hand array will
 * be ignored.
 * This function take care of handling all the scenarios.
 */
JX9_PRIVATE sxi32 jx9MemObjAdd(jx9_value *pObj1, jx9_value *pObj2, int bAddStore)
{
	if( ((pObj1->iFlags|pObj2->iFlags) & MEMOBJ_HASHMAP) == 0 ){
			/* Arithemtic operation */
			jx9MemObjToNumeric(pObj1);
			jx9MemObjToNumeric(pObj2);
			if( (pObj1->iFlags|pObj2->iFlags) & MEMOBJ_REAL ){
				/* Floating point arithmetic */
				jx9_real a, b;
				if( (pObj1->iFlags & MEMOBJ_REAL) == 0 ){
					jx9MemObjToReal(pObj1);
				}
				if( (pObj2->iFlags & MEMOBJ_REAL) == 0 ){
					jx9MemObjToReal(pObj2);
				}
				a = pObj1->x.rVal;
				b = pObj2->x.rVal;
				pObj1->x.rVal = a+b;
				MemObjSetType(pObj1, MEMOBJ_REAL);
				/* Try to get an integer representation also */
				MemObjTryIntger(&(*pObj1));
			}else{
				/* Integer arithmetic */
				sxi64 a, b;
				a = pObj1->x.iVal;
				b = pObj2->x.iVal;
				pObj1->x.iVal = a+b;
				MemObjSetType(pObj1, MEMOBJ_INT);
			}
	}else{
		if( (pObj1->iFlags|pObj2->iFlags) & MEMOBJ_HASHMAP ){
			jx9_hashmap *pMap;
			sxi32 rc;
			if( bAddStore ){
				/* Do not duplicate the hashmap, use the left one since its an add&store operation.
				 */
				if( (pObj1->iFlags & MEMOBJ_HASHMAP) == 0 ){				
					/* Force a hashmap cast */
					rc = jx9MemObjToHashmap(pObj1);
					if( rc != SXRET_OK ){
						jx9VmThrowError(pObj1->pVm, 0, JX9_CTX_ERR, "JX9 is running out of memory while creating array");
						return rc;
					}
				}
				/* Point to the structure that describe the hashmap */
				pMap = (jx9_hashmap *)pObj1->x.pOther;
			}else{
				/* Create a new hashmap */
				pMap = jx9NewHashmap(pObj1->pVm, 0, 0);
				if( pMap == 0){
					jx9VmThrowError(pObj1->pVm, 0, JX9_CTX_ERR, "JX9 is running out of memory while creating array");
					return SXERR_MEM;
				}
			}
			if( !bAddStore ){
				if(pObj1->iFlags & MEMOBJ_HASHMAP ){
					/* Perform a hashmap duplication */
					jx9HashmapDup((jx9_hashmap *)pObj1->x.pOther, pMap);
				}else{
					if((pObj1->iFlags & MEMOBJ_NULL) == 0 ){
						/* Simple insertion */
						jx9HashmapInsert(pMap, 0, pObj1);
					}
				}
			}
			/* Perform the union */
			if(pObj2->iFlags & MEMOBJ_HASHMAP ){
				jx9HashmapUnion(pMap, (jx9_hashmap *)pObj2->x.pOther);
			}else{
				if((pObj2->iFlags & MEMOBJ_NULL) == 0 ){
					/* Simple insertion */
					jx9HashmapInsert(pMap, 0, pObj2);
				}
			}
			/* Reflect the change */
			if( pObj1->iFlags & MEMOBJ_STRING ){
				SyBlobRelease(&pObj1->sBlob);
			}
			pObj1->x.pOther = pMap;
			MemObjSetType(pObj1, MEMOBJ_HASHMAP);
		}
	}
	return SXRET_OK;
}
/*
 * Return a printable representation of the type of a given 
 * jx9_value.
 */
JX9_PRIVATE const char * jx9MemObjTypeDump(jx9_value *pVal)
{
	const char *zType = "";
	if( pVal->iFlags & MEMOBJ_NULL ){
		zType = "null";
	}else if( pVal->iFlags & MEMOBJ_INT ){
		zType = "int";
	}else if( pVal->iFlags & MEMOBJ_REAL ){
		zType = "float";
	}else if( pVal->iFlags & MEMOBJ_STRING ){
		zType = "string";
	}else if( pVal->iFlags & MEMOBJ_BOOL ){
		zType = "bool";
	}else if( pVal->iFlags & MEMOBJ_HASHMAP ){
		jx9_hashmap *pMap = (jx9_hashmap *)pVal->x.pOther;
		if( pMap->iFlags & HASHMAP_JSON_OBJECT ){
			zType = "JSON Object";
		}else{
			zType = "JSON Array";
		}
	}else if( pVal->iFlags & MEMOBJ_RES ){
		zType = "resource";
	}
	return zType;
}
/*
 * Dump a jx9_value [i.e: get a printable representation of it's type and contents.].
 * Store the dump in the given blob.
 */
JX9_PRIVATE sxi32 jx9MemObjDump(
	SyBlob *pOut,      /* Store the dump here */
	jx9_value *pObj   /* Dump this */
	)
{
	sxi32 rc = SXRET_OK;
	const char *zType;
	/* Get value type first */
	zType = jx9MemObjTypeDump(pObj);
	SyBlobAppend(&(*pOut), zType, SyStrlen(zType));
	if((pObj->iFlags & MEMOBJ_NULL) == 0 ){
		SyBlobAppend(&(*pOut), "(", sizeof(char));
		if( pObj->iFlags & MEMOBJ_HASHMAP ){
			jx9_hashmap *pMap = (jx9_hashmap *)pObj->x.pOther;
			SyBlobFormat(pOut,"%u ",pMap->nEntry);
			/* Dump hashmap entries */
			rc = jx9JsonSerialize(pObj,pOut);
		}else{
			SyBlob *pContents = &pObj->sBlob;
			/* Get a printable representation of the contents */
			if((pObj->iFlags & MEMOBJ_STRING) == 0 ){
				MemObjStringValue(&(*pOut), &(*pObj));
			}else{
				/* Append length first */
				SyBlobFormat(&(*pOut), "%u '", SyBlobLength(&pObj->sBlob));
				if( SyBlobLength(pContents) > 0 ){
					SyBlobAppend(&(*pOut), SyBlobData(pContents), SyBlobLength(pContents));
				}
				SyBlobAppend(&(*pOut), "'", sizeof(char));
			}
		}
		SyBlobAppend(&(*pOut), ")", sizeof(char));	
	}
#ifdef __WINNT__
	SyBlobAppend(&(*pOut), "\r\n", sizeof("\r\n")-1);
#else
	SyBlobAppend(&(*pOut), "\n", sizeof(char));
#endif
	return rc;
}
