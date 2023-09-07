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
 /* $SymiscID: hashmap.c v2.6 Win7 2012-12-11 00:50 stable <chm@symisc.net> $ */
#ifndef JX9_AMALGAMATION
#include "jx9Int.h"
#endif
/* This file implement generic hashmaps used to represent JSON arrays and objects */
/* Allowed node types */
#define HASHMAP_INT_NODE   1  /* Node with an int [i.e: 64-bit integer] key */
#define HASHMAP_BLOB_NODE  2  /* Node with a string/BLOB key */
/*
 * Default hash function for int [i.e; 64-bit integer] keys.
 */
static sxu32 IntHash(sxi64 iKey)
{
	return (sxu32)(iKey ^ (iKey << 8) ^ (iKey >> 8));
}
/*
 * Default hash function for string/BLOB keys.
 */
static sxu32 BinHash(const void *pSrc, sxu32 nLen)
{
	register unsigned char *zIn = (unsigned char *)pSrc;
	unsigned char *zEnd;
	sxu32 nH = 5381;
	zEnd = &zIn[nLen];
	for(;;){
		if( zIn >= zEnd ){ break; } nH = nH * 33 + zIn[0] ; zIn++;
		if( zIn >= zEnd ){ break; } nH = nH * 33 + zIn[0] ; zIn++;
		if( zIn >= zEnd ){ break; } nH = nH * 33 + zIn[0] ; zIn++;
		if( zIn >= zEnd ){ break; } nH = nH * 33 + zIn[0] ; zIn++;
	}	
	return nH;
}
/*
 * Return the total number of entries in a given hashmap.
 * If bRecurisve is set to TRUE then recurse on hashmap entries.
 * If the nesting limit is reached, this function abort immediately. 
 */
static sxi64 HashmapCount(jx9_hashmap *pMap, int bRecursive, int iRecCount)
{
	sxi64 iCount = 0;
	if( !bRecursive ){
		iCount = pMap->nEntry;
	}else{
		/* Recursive hashmap walk */
		jx9_hashmap_node *pEntry = pMap->pLast;
		jx9_value *pElem;
		sxu32 n = 0;
		for(;;){
			if( n >= pMap->nEntry ){
				break;
			}
			/* Point to the element value */
			pElem = (jx9_value *)SySetAt(&pMap->pVm->aMemObj, pEntry->nValIdx);
			if( pElem ){
				if( pElem->iFlags & MEMOBJ_HASHMAP ){
					if( iRecCount > 31 ){
						/* Nesting limit reached */
						return iCount;
					}
					/* Recurse */
					iRecCount++;
					iCount += HashmapCount((jx9_hashmap *)pElem->x.pOther, TRUE, iRecCount);
					iRecCount--;
				}
			}
			/* Point to the next entry */
			pEntry = pEntry->pNext;
			++n;
		}
		/* Update count */
		iCount += pMap->nEntry;
	}
	return iCount;
}
/*
 * Allocate a new hashmap node with a 64-bit integer key.
 * If something goes wrong [i.e: out of memory], this function return NULL.
 * Otherwise a fresh [jx9_hashmap_node] instance is returned.
 */
static jx9_hashmap_node * HashmapNewIntNode(jx9_hashmap *pMap, sxi64 iKey, sxu32 nHash, sxu32 nValIdx)
{
	jx9_hashmap_node *pNode;
	/* Allocate a new node */
	pNode = (jx9_hashmap_node *)SyMemBackendPoolAlloc(&pMap->pVm->sAllocator, sizeof(jx9_hashmap_node));
	if( pNode == 0 ){
		return 0;
	}
	/* Zero the stucture */
	SyZero(pNode, sizeof(jx9_hashmap_node));
	/* Fill in the structure */
	pNode->pMap  = &(*pMap);
	pNode->iType = HASHMAP_INT_NODE;
	pNode->nHash = nHash;
	pNode->xKey.iKey = iKey;
	pNode->nValIdx  = nValIdx;
	return pNode;
}
/*
 * Allocate a new hashmap node with a BLOB key.
 * If something goes wrong [i.e: out of memory], this function return NULL.
 * Otherwise a fresh [jx9_hashmap_node] instance is returned.
 */
static jx9_hashmap_node * HashmapNewBlobNode(jx9_hashmap *pMap, const void *pKey, sxu32 nKeyLen, sxu32 nHash, sxu32 nValIdx)
{
	jx9_hashmap_node *pNode;
	/* Allocate a new node */
	pNode = (jx9_hashmap_node *)SyMemBackendPoolAlloc(&pMap->pVm->sAllocator, sizeof(jx9_hashmap_node));
	if( pNode == 0 ){
		return 0;
	}
	/* Zero the stucture */
	SyZero(pNode, sizeof(jx9_hashmap_node));
	/* Fill in the structure */
	pNode->pMap  = &(*pMap);
	pNode->iType = HASHMAP_BLOB_NODE;
	pNode->nHash = nHash;
	SyBlobInit(&pNode->xKey.sKey, &pMap->pVm->sAllocator);
	SyBlobAppend(&pNode->xKey.sKey, pKey, nKeyLen);
	pNode->nValIdx = nValIdx;
	return pNode;
}
/*
 * link a hashmap node to the given bucket index (last argument to this function).
 */
static void HashmapNodeLink(jx9_hashmap *pMap, jx9_hashmap_node *pNode, sxu32 nBucketIdx)
{
	/* Link */
	if( pMap->apBucket[nBucketIdx] != 0 ){
		pNode->pNextCollide = pMap->apBucket[nBucketIdx];
		pMap->apBucket[nBucketIdx]->pPrevCollide = pNode;
	}
	pMap->apBucket[nBucketIdx] = pNode;
	/* Link to the map list */
	if( pMap->pFirst == 0 ){
		pMap->pFirst = pMap->pLast = pNode;
		/* Point to the first inserted node */
		pMap->pCur = pNode;
	}else{
		MACRO_LD_PUSH(pMap->pLast, pNode);
	}
	++pMap->nEntry;
}
/*
 * Unlink a node from the hashmap.
 * If the node count reaches zero then release the whole hash-bucket.
 */
static void jx9HashmapUnlinkNode(jx9_hashmap_node *pNode)
{
	jx9_hashmap *pMap = pNode->pMap;
	jx9_vm *pVm = pMap->pVm;
	/* Unlink from the corresponding bucket */
	if( pNode->pPrevCollide == 0 ){
		pMap->apBucket[pNode->nHash & (pMap->nSize - 1)] = pNode->pNextCollide;
	}else{
		pNode->pPrevCollide->pNextCollide = pNode->pNextCollide;
	}
	if( pNode->pNextCollide ){
		pNode->pNextCollide->pPrevCollide = pNode->pPrevCollide;
	}
	if( pMap->pFirst == pNode ){
		pMap->pFirst = pNode->pPrev;
	}
	if( pMap->pCur == pNode ){
		/* Advance the node cursor */
		pMap->pCur = pMap->pCur->pPrev; /* Reverse link */
	}
	/* Unlink from the map list */
	MACRO_LD_REMOVE(pMap->pLast, pNode);
	/* Restore to the free list */
	jx9VmUnsetMemObj(pVm, pNode->nValIdx);	
	if( pNode->iType == HASHMAP_BLOB_NODE ){
		SyBlobRelease(&pNode->xKey.sKey);
	}
	SyMemBackendPoolFree(&pVm->sAllocator, pNode);
	pMap->nEntry--;
	if( pMap->nEntry < 1 ){
		/* Free the hash-bucket */
		SyMemBackendFree(&pVm->sAllocator, pMap->apBucket);
		pMap->apBucket = 0;
		pMap->nSize = 0;
		pMap->pFirst = pMap->pLast = pMap->pCur = 0;
	}
}
#define HASHMAP_FILL_FACTOR 3
/*
 * Grow the hash-table and rehash all entries.
 */
static sxi32 HashmapGrowBucket(jx9_hashmap *pMap)
{
	if( pMap->nEntry >= pMap->nSize * HASHMAP_FILL_FACTOR ){
		jx9_hashmap_node **apOld = pMap->apBucket;
		jx9_hashmap_node *pEntry, **apNew;
		sxu32 nNew = pMap->nSize << 1;
		sxu32 nBucket;
		sxu32 n;
		if( nNew < 1 ){
			nNew = 16;
		}
		/* Allocate a new bucket */
		apNew = (jx9_hashmap_node **)SyMemBackendAlloc(&pMap->pVm->sAllocator, nNew * sizeof(jx9_hashmap_node *));
		if( apNew == 0 ){
			if( pMap->nSize < 1 ){
				return SXERR_MEM; /* Fatal */
			}
			/* Not so fatal here, simply a performance hit */
			return SXRET_OK;
		}
		/* Zero the table */
		SyZero((void *)apNew, nNew * sizeof(jx9_hashmap_node *));
		/* Reflect the change */
		pMap->apBucket = apNew;
		pMap->nSize = nNew;
		if( apOld == 0 ){
			/* First allocated table [i.e: no entry], return immediately */
			return SXRET_OK;
		}
		/* Rehash old entries */
		pEntry = pMap->pFirst;
		n = 0;
		for( ;; ){
			if( n >= pMap->nEntry ){
				break;
			}
			/* Clear the old collision link */
			pEntry->pNextCollide = pEntry->pPrevCollide = 0;
			/* Link to the new bucket */
			nBucket = pEntry->nHash & (nNew - 1);
			if( pMap->apBucket[nBucket] != 0 ){
				pEntry->pNextCollide = pMap->apBucket[nBucket];
				pMap->apBucket[nBucket]->pPrevCollide = pEntry;
			}
			pMap->apBucket[nBucket] = pEntry;
			/* Point to the next entry */
			pEntry = pEntry->pPrev; /* Reverse link */
			n++;
		}
		/* Free the old table */
		SyMemBackendFree(&pMap->pVm->sAllocator, (void *)apOld);
	}
	return SXRET_OK;
}
/*
 * Insert a 64-bit integer key and it's associated value (if any) in the given
 * hashmap.
 */
static sxi32 HashmapInsertIntKey(jx9_hashmap *pMap,sxi64 iKey,jx9_value *pValue)
{
	jx9_hashmap_node *pNode;
	jx9_value *pObj;
	sxu32 nIdx;
	sxu32 nHash;
	sxi32 rc;
	/* Reserve a jx9_value for the value */
	pObj = jx9VmReserveMemObj(pMap->pVm,&nIdx);
	if( pObj == 0 ){
		return SXERR_MEM;
	}
	if( pValue ){
		/* Duplicate the value */
		jx9MemObjStore(pValue, pObj);
	}	
	/* Hash the key */
	nHash = pMap->xIntHash(iKey);
	/* Allocate a new int node */
	pNode = HashmapNewIntNode(&(*pMap), iKey, nHash, nIdx);
	if( pNode == 0 ){
		return SXERR_MEM;
	}
	/* Make sure the bucket is big enough to hold the new entry */
	rc = HashmapGrowBucket(&(*pMap));
	if( rc != SXRET_OK ){
		SyMemBackendPoolFree(&pMap->pVm->sAllocator, pNode);
		return rc;
	}
	/* Perform the insertion */
	HashmapNodeLink(&(*pMap), pNode, nHash & (pMap->nSize - 1));
	/* All done */
	return SXRET_OK;
}
/*
 * Insert a BLOB key and it's associated value (if any) in the given
 * hashmap.
 */
static sxi32 HashmapInsertBlobKey(jx9_hashmap *pMap,const void *pKey,sxu32 nKeyLen,jx9_value *pValue)
{
	jx9_hashmap_node *pNode;
	jx9_value *pObj;
	sxu32 nHash;
	sxu32 nIdx;
	sxi32 rc;
	/* Reserve a jx9_value for the value */
	pObj = jx9VmReserveMemObj(pMap->pVm,&nIdx);
	if( pObj == 0 ){
		return SXERR_MEM;
	}
	if( pValue ){
		/* Duplicate the value */
		jx9MemObjStore(pValue, pObj);
	}
	/* Hash the key */
	nHash = pMap->xBlobHash(pKey, nKeyLen);
	/* Allocate a new blob node */
	pNode = HashmapNewBlobNode(&(*pMap), pKey, nKeyLen, nHash, nIdx);
	if( pNode == 0 ){
		return SXERR_MEM;
	}
	/* Make sure the bucket is big enough to hold the new entry */
	rc = HashmapGrowBucket(&(*pMap));
	if( rc != SXRET_OK ){
		SyMemBackendPoolFree(&pMap->pVm->sAllocator, pNode);
		return rc;
	}
	/* Perform the insertion */
	HashmapNodeLink(&(*pMap), pNode, nHash & (pMap->nSize - 1));
	/* All done */
	return SXRET_OK;
}
/*
 * Check if a given 64-bit integer key exists in the given hashmap.
 * Write a pointer to the target node on success. Otherwise
 * SXERR_NOTFOUND is returned on failure.
 */
static sxi32 HashmapLookupIntKey(
	jx9_hashmap *pMap,         /* Target hashmap */
	sxi64 iKey,                /* lookup key */
	jx9_hashmap_node **ppNode  /* OUT: target node on success */
	)
{
	jx9_hashmap_node *pNode;
	sxu32 nHash;
	if( pMap->nEntry < 1 ){
		/* Don't bother hashing, there is no entry anyway */
		return SXERR_NOTFOUND;
	}
	/* Hash the key first */
	nHash = pMap->xIntHash(iKey);
	/* Point to the appropriate bucket */
	pNode = pMap->apBucket[nHash & (pMap->nSize - 1)];
	/* Perform the lookup */
	for(;;){
		if( pNode == 0 ){
			break;
		}
		if( pNode->iType == HASHMAP_INT_NODE
			&& pNode->nHash == nHash
			&& pNode->xKey.iKey == iKey ){
				/* Node found */
				if( ppNode ){
					*ppNode = pNode;
				}
				return SXRET_OK;
		}
		/* Follow the collision link */
		pNode = pNode->pNextCollide;
	}
	/* No such entry */
	return SXERR_NOTFOUND;
}
/*
 * Check if a given BLOB key exists in the given hashmap.
 * Write a pointer to the target node on success. Otherwise
 * SXERR_NOTFOUND is returned on failure.
 */
static sxi32 HashmapLookupBlobKey(
	jx9_hashmap *pMap,          /* Target hashmap */
	const void *pKey,           /* Lookup key */
	sxu32 nKeyLen,              /* Key length in bytes */
	jx9_hashmap_node **ppNode   /* OUT: target node on success */
	)
{
	jx9_hashmap_node *pNode;
	sxu32 nHash;
	if( pMap->nEntry < 1 ){
		/* Don't bother hashing, there is no entry anyway */
		return SXERR_NOTFOUND;
	}
	/* Hash the key first */
	nHash = pMap->xBlobHash(pKey, nKeyLen);
	/* Point to the appropriate bucket */
	pNode = pMap->apBucket[nHash & (pMap->nSize - 1)];
	/* Perform the lookup */
	for(;;){
		if( pNode == 0 ){
			break;
		}
		if( pNode->iType == HASHMAP_BLOB_NODE 
			&& pNode->nHash == nHash
			&& SyBlobLength(&pNode->xKey.sKey) == nKeyLen 
			&& SyMemcmp(SyBlobData(&pNode->xKey.sKey), pKey, nKeyLen) == 0 ){
				/* Node found */
				if( ppNode ){
					*ppNode = pNode;
				}
				return SXRET_OK;
		}
		/* Follow the collision link */
		pNode = pNode->pNextCollide;
	}
	/* No such entry */
	return SXERR_NOTFOUND;
}
/*
 * Check if the given BLOB key looks like a decimal number. 
 * Retrurn TRUE on success.FALSE otherwise.
 */
static int HashmapIsIntKey(SyBlob *pKey)
{
	const char *zIn  = (const char *)SyBlobData(pKey);
	const char *zEnd = &zIn[SyBlobLength(pKey)];
	if( (int)(zEnd-zIn) > 1 && zIn[0] == '0' ){
		/* Octal not decimal number */
		return FALSE;
	}
	if( (zIn[0] == '-' || zIn[0] == '+') && &zIn[1] < zEnd ){
		zIn++;
	}
	for(;;){
		if( zIn >= zEnd ){
			return TRUE;
		}
		if( (unsigned char)zIn[0] >= 0xc0 /* UTF-8 stream */  || !SyisDigit(zIn[0]) ){
			break;
		}
		zIn++;
	}
	/* Key does not look like a decimal number */
	return FALSE;
}
/*
 * Check if a given key exists in the given hashmap.
 * Write a pointer to the target node on success.
 * Otherwise SXERR_NOTFOUND is returned on failure.
 */
static sxi32 HashmapLookup(
	jx9_hashmap *pMap,          /* Target hashmap */
	jx9_value *pKey,            /* Lookup key */
	jx9_hashmap_node **ppNode   /* OUT: target node on success */
	)
{
	jx9_hashmap_node *pNode = 0; /* cc -O6 warning */
	sxi32 rc;
	if( pKey->iFlags & (MEMOBJ_STRING|MEMOBJ_HASHMAP|MEMOBJ_RES) ){
		if( (pKey->iFlags & MEMOBJ_STRING) == 0 ){
			/* Force a string cast */
			jx9MemObjToString(&(*pKey));
		}
		if( SyBlobLength(&pKey->sBlob) > 0 ){
			/* Perform a blob lookup */
			rc = HashmapLookupBlobKey(&(*pMap), SyBlobData(&pKey->sBlob), SyBlobLength(&pKey->sBlob), &pNode);
			goto result;
		}
	}
	/* Perform an int lookup */
	if((pKey->iFlags & MEMOBJ_INT) == 0 ){
		/* Force an integer cast */
		jx9MemObjToInteger(pKey);
	}
	/* Perform an int lookup */
	rc = HashmapLookupIntKey(&(*pMap), pKey->x.iVal, &pNode);
result:
	if( rc == SXRET_OK ){
		/* Node found */
		if( ppNode ){
			*ppNode = pNode;
		}
		return SXRET_OK;
	}
	/* No such entry */
	return SXERR_NOTFOUND;
}
/*
 * Insert a given key and it's associated value (if any) in the given
 * hashmap.
 * If a node with the given key already exists in the database
 * then this function overwrite the old value.
 */
static sxi32 HashmapInsert(
	jx9_hashmap *pMap, /* Target hashmap */
	jx9_value *pKey,   /* Lookup key  */
	jx9_value *pVal    /* Node value */
	)
{
	jx9_hashmap_node *pNode = 0;
	sxi32 rc = SXRET_OK;
	if( pMap->nEntry < 1 && pKey && (pKey->iFlags & MEMOBJ_STRING) ){
		pMap->iFlags |= HASHMAP_JSON_OBJECT;
	}
	if( pKey && (pKey->iFlags & (MEMOBJ_STRING|MEMOBJ_HASHMAP|MEMOBJ_RES)) ){
		if( (pKey->iFlags & MEMOBJ_STRING) == 0 ){
			/* Force a string cast */
			jx9MemObjToString(&(*pKey));
		}
		if( SyBlobLength(&pKey->sBlob) < 1 || HashmapIsIntKey(&pKey->sBlob) ){
			if(SyBlobLength(&pKey->sBlob) < 1){
				/* Automatic index assign */
				pKey = 0;
			}
			goto IntKey;
		}
		if( SXRET_OK == HashmapLookupBlobKey(&(*pMap), SyBlobData(&pKey->sBlob), 
			SyBlobLength(&pKey->sBlob), &pNode) ){
				/* Overwrite the old value */
				jx9_value *pElem;
				pElem = (jx9_value *)SySetAt(&pMap->pVm->aMemObj, pNode->nValIdx);
				if( pElem ){
					if( pVal ){
						jx9MemObjStore(pVal, pElem);
					}else{
						/* Nullify the entry */
						jx9MemObjToNull(pElem);
					}
				}
				return SXRET_OK;
		}
		/* Perform a blob-key insertion */
		rc = HashmapInsertBlobKey(&(*pMap),SyBlobData(&pKey->sBlob),SyBlobLength(&pKey->sBlob),&(*pVal));
		return rc;
	}
IntKey:
	if( pKey ){
		if((pKey->iFlags & MEMOBJ_INT) == 0 ){
			/* Force an integer cast */
			jx9MemObjToInteger(pKey);
		}
		if( SXRET_OK == HashmapLookupIntKey(&(*pMap), pKey->x.iVal, &pNode) ){
			/* Overwrite the old value */
			jx9_value *pElem;
			pElem = (jx9_value *)SySetAt(&pMap->pVm->aMemObj, pNode->nValIdx);
			if( pElem ){
				if( pVal ){
					jx9MemObjStore(pVal, pElem);
				}else{
					/* Nullify the entry */
					jx9MemObjToNull(pElem);
				}
			}
			return SXRET_OK;
		}
		/* Perform a 64-bit-int-key insertion */
		rc = HashmapInsertIntKey(&(*pMap), pKey->x.iVal, &(*pVal));
		if( rc == SXRET_OK ){
			if( pKey->x.iVal >= pMap->iNextIdx ){
				/* Increment the automatic index */ 
				pMap->iNextIdx = pKey->x.iVal + 1;
				/* Make sure the automatic index is not reserved */
				while( SXRET_OK == HashmapLookupIntKey(&(*pMap), pMap->iNextIdx, 0) ){
					pMap->iNextIdx++;
				}
			}
		}
	}else{
		/* Assign an automatic index */
		rc = HashmapInsertIntKey(&(*pMap),pMap->iNextIdx,&(*pVal));
		if( rc == SXRET_OK ){
			++pMap->iNextIdx;
		}
	}
	/* Insertion result */
	return rc;
}
/*
 * Extract node value.
 */
static jx9_value * HashmapExtractNodeValue(jx9_hashmap_node *pNode)
{
	/* Point to the desired object */
	jx9_value *pObj;
	pObj = (jx9_value *)SySetAt(&pNode->pMap->pVm->aMemObj, pNode->nValIdx);
	return pObj;
}
/*
 * Insert a node in the given hashmap.
 * If a node with the given key already exists in the database
 * then this function overwrite the old value.
 */
static sxi32 HashmapInsertNode(jx9_hashmap *pMap, jx9_hashmap_node *pNode, int bPreserve)
{
	jx9_value *pObj;
	sxi32 rc;
	/* Extract the node value */
	pObj = HashmapExtractNodeValue(&(*pNode));
	if( pObj == 0 ){
		return SXERR_EMPTY;
	}
	/* Preserve key */
	if( pNode->iType == HASHMAP_INT_NODE){
		/* Int64 key */
		if( !bPreserve ){
			/* Assign an automatic index */
			rc = HashmapInsert(&(*pMap), 0, pObj);
		}else{
			rc = HashmapInsertIntKey(&(*pMap), pNode->xKey.iKey, pObj);
		}
	}else{
		/* Blob key */
		rc = HashmapInsertBlobKey(&(*pMap), SyBlobData(&pNode->xKey.sKey), 
			SyBlobLength(&pNode->xKey.sKey), pObj);
	}
	return rc;
}
/*
 * Compare two node values.
 * Return 0 if the node values are equals, > 0 if pLeft is greater than pRight
 * or < 0 if pRight is greater than pLeft.
 * For a full description on jx9_values comparison, refer to the implementation
 * of the [jx9MemObjCmp()] function defined in memobj.c or the official
 * documenation.
 */
static sxi32 HashmapNodeCmp(jx9_hashmap_node *pLeft, jx9_hashmap_node *pRight, int bStrict)
{
	jx9_value sObj1, sObj2;
	sxi32 rc;
	if( pLeft == pRight ){
		/*
		 * Same node.Refer to the sort() implementation defined
		 * below for more information on this sceanario.
		 */
		return 0;
	}
	/* Do the comparison */
	jx9MemObjInit(pLeft->pMap->pVm, &sObj1);
	jx9MemObjInit(pLeft->pMap->pVm, &sObj2);
	jx9HashmapExtractNodeValue(pLeft, &sObj1, FALSE);
	jx9HashmapExtractNodeValue(pRight, &sObj2, FALSE);
	rc = jx9MemObjCmp(&sObj1, &sObj2, bStrict, 0);
	jx9MemObjRelease(&sObj1);
	jx9MemObjRelease(&sObj2);
	return rc;
}
/*
 * Rehash a node with a 64-bit integer key.
 * Refer to [merge_sort(), array_shift()] implementations for more information.
 */
static void HashmapRehashIntNode(jx9_hashmap_node *pEntry)
{
	jx9_hashmap *pMap = pEntry->pMap;
	sxu32 nBucket;
	/* Remove old collision links */
	if( pEntry->pPrevCollide ){
		pEntry->pPrevCollide->pNextCollide = pEntry->pNextCollide;
	}else{
		pMap->apBucket[pEntry->nHash & (pMap->nSize - 1)] = pEntry->pNextCollide;
	}
	if( pEntry->pNextCollide ){
		pEntry->pNextCollide->pPrevCollide = pEntry->pPrevCollide;
	}
	pEntry->pNextCollide = pEntry->pPrevCollide = 0;
	/* Compute the new hash */
	pEntry->nHash = pMap->xIntHash(pMap->iNextIdx);
	pEntry->xKey.iKey = pMap->iNextIdx;
	nBucket = pEntry->nHash & (pMap->nSize - 1);
	/* Link to the new bucket */
	pEntry->pNextCollide = pMap->apBucket[nBucket];
	if( pMap->apBucket[nBucket] ){
		pMap->apBucket[nBucket]->pPrevCollide = pEntry;
	}
	pEntry->pNextCollide = pMap->apBucket[nBucket];
	pMap->apBucket[nBucket] = pEntry;
	/* Increment the automatic index */
	pMap->iNextIdx++;
}
/*
 * Perform a linear search on a given hashmap.
 * Write a pointer to the target node on success.
 * Otherwise SXERR_NOTFOUND is returned on failure.
 * Refer to [array_intersect(), array_diff(), in_array(), ...] implementations 
 * for more information.
 */
static int HashmapFindValue(
	jx9_hashmap *pMap,   /* Target hashmap */
	jx9_value *pNeedle,  /* Lookup key */
	jx9_hashmap_node **ppNode, /* OUT: target node on success  */
	int bStrict      /* TRUE for strict comparison */
	)
{
	jx9_hashmap_node *pEntry;
	jx9_value sVal, *pVal;
	jx9_value sNeedle;
	sxi32 rc;
	sxu32 n;
	/* Perform a linear search since we cannot sort the hashmap based on values */
	pEntry = pMap->pFirst;
	n = pMap->nEntry;
	jx9MemObjInit(pMap->pVm, &sVal);
	jx9MemObjInit(pMap->pVm, &sNeedle);
	for(;;){
		if( n < 1 ){
			break;
		}
		/* Extract node value */
		pVal = HashmapExtractNodeValue(pEntry);
		if( pVal ){
			if( (pVal->iFlags|pNeedle->iFlags) & MEMOBJ_NULL ){
				sxi32 iF1 = pVal->iFlags;
				sxi32 iF2 = pNeedle->iFlags;
				if( iF1 == iF2 ){
					/* NULL values are equals */
					if( ppNode ){
						*ppNode = pEntry;
					}
					return SXRET_OK;
				}
			}else{
				/* Duplicate value */
				jx9MemObjLoad(pVal, &sVal);
				jx9MemObjLoad(pNeedle, &sNeedle);
				rc = jx9MemObjCmp(&sNeedle, &sVal, bStrict, 0);
				jx9MemObjRelease(&sVal);
				jx9MemObjRelease(&sNeedle);
				if( rc == 0 ){
					if( ppNode ){
						*ppNode = pEntry;
					}
					/* Match found*/
					return SXRET_OK;
				}
			}
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
		n--;
	}
	/* No such entry */
	return SXERR_NOTFOUND;
}
/*
 * Compare two hashmaps.
 * Return 0 if the hashmaps are equals.Any other value indicates inequality.
 * Note on array comparison operators.
 *  According to the JX9 language reference manual.
 *  Array Operators Example 	Name 	Result
 *  $a + $b 	Union 	Union of $a and $b.
 *  $a == $b 	Equality 	TRUE if $a and $b have the same key/value pairs.
 *  $a === $b 	Identity 	TRUE if $a and $b have the same key/value pairs in the same 
 *                          order and of the same types.
 *  $a != $b 	Inequality 	TRUE if $a is not equal to $b.
 *  $a <> $b 	Inequality 	TRUE if $a is not equal to $b.
 *  $a !== $b 	Non-identity 	TRUE if $a is not identical to $b.
 * The + operator returns the right-hand array appended to the left-hand array;
 * For keys that exist in both arrays, the elements from the left-hand array will be used
 * and the matching elements from the right-hand array will be ignored.
 * <?jx9
 * $a = array("a" => "apple", "b" => "banana");
 * $b = array("a" => "pear", "b" => "strawberry", "c" => "cherry");
 * $c = $a + $b; // Union of $a and $b
 * print "Union of \$a and \$b: \n";
 * dump($c);
 * $c = $b + $a; // Union of $b and $a
 * print "Union of \$b and \$a: \n";
 * dump($c);
 * ?>
 * When executed, this script will print the following:
 * Union of $a and $b:
 * array(3) {
 *  ["a"]=>
 *  string(5) "apple"
 *  ["b"]=>
 * string(6) "banana"
 *  ["c"]=>
 * string(6) "cherry"
 * }
 * Union of $b and $a:
 * array(3) {
 * ["a"]=>
 * string(4) "pear"
 * ["b"]=>
 * string(10) "strawberry"
 * ["c"]=>
 * string(6) "cherry"
 * }
 * Elements of arrays are equal for the comparison if they have the same key and value.
 */
JX9_PRIVATE sxi32 jx9HashmapCmp(
	jx9_hashmap *pLeft,  /* Left hashmap */
	jx9_hashmap *pRight, /* Right hashmap */
	int bStrict          /* TRUE for strict comparison */
	)
{
	jx9_hashmap_node *pLe, *pRe;
	sxi32 rc;
	sxu32 n;
	if( pLeft == pRight ){
		/* Same hashmap instance. This can easily happen since hashmaps are passed by reference.
		 * Unlike the  engine.
		 */
		return 0;
	}
	if( pLeft->nEntry != pRight->nEntry ){
		/* Must have the same number of entries */
		return pLeft->nEntry > pRight->nEntry ? 1 : -1;
	}
	/* Point to the first inserted entry of the left hashmap */
	pLe = pLeft->pFirst;
	pRe = 0; /* cc warning */
	/* Perform the comparison */
	n = pLeft->nEntry;
	for(;;){
		if( n < 1 ){
			break;
		}
		if( pLe->iType == HASHMAP_INT_NODE){
			/* Int key */
			rc = HashmapLookupIntKey(&(*pRight), pLe->xKey.iKey, &pRe);
		}else{
			SyBlob *pKey = &pLe->xKey.sKey;
			/* Blob key */
			rc = HashmapLookupBlobKey(&(*pRight), SyBlobData(pKey), SyBlobLength(pKey), &pRe);
		}
		if( rc != SXRET_OK ){
			/* No such entry in the right side */
			return 1;
		}
		rc = 0;
		if( bStrict ){
			/* Make sure, the keys are of the same type */
			if( pLe->iType != pRe->iType ){
				rc = 1;
			}
		}
		if( !rc ){
			/* Compare nodes */
			rc = HashmapNodeCmp(pLe, pRe, bStrict);
		}
		if( rc != 0 ){
			/* Nodes key/value differ */
			return rc;
		}
		/* Point to the next entry */
		pLe = pLe->pPrev; /* Reverse link */
		n--;
	}
	return 0; /* Hashmaps are equals */
}
/*
 * Merge two hashmaps.
 * Note on the merge process
 * According to the JX9 language reference manual.
 *  Merges the elements of two arrays together so that the values of one are appended
 *  to the end of the previous one. It returns the resulting array (pDest).
 *  If the input arrays have the same string keys, then the later value for that key
 *  will overwrite the previous one. If, however, the arrays contain numeric keys
 *  the later value will not overwrite the original value, but will be appended.
 *  Values in the input array with numeric keys will be renumbered with incrementing
 *  keys starting from zero in the result array. 
 */
static sxi32 HashmapMerge(jx9_hashmap *pSrc, jx9_hashmap *pDest)
{
	jx9_hashmap_node *pEntry;
	jx9_value sKey, *pVal;
	sxi32 rc;
	sxu32 n;
	if( pSrc == pDest ){
		/* Same map. This can easily happen since hashmaps are passed by reference.
		 * Unlike the  engine.
		 */
		return SXRET_OK;
	}
	/* Point to the first inserted entry in the source */
	pEntry = pSrc->pFirst;
	/* Perform the merge */
	for( n = 0 ; n < pSrc->nEntry ; ++n ){
		/* Extract the node value */
		pVal = HashmapExtractNodeValue(pEntry);
		if( pEntry->iType == HASHMAP_BLOB_NODE ){
			/* Blob key insertion */
			jx9MemObjInitFromString(pDest->pVm, &sKey, 0);
			jx9MemObjStringAppend(&sKey, (const char *)SyBlobData(&pEntry->xKey.sKey), SyBlobLength(&pEntry->xKey.sKey));
			rc = jx9HashmapInsert(&(*pDest), &sKey, pVal);
			jx9MemObjRelease(&sKey);
		}else{
			rc = HashmapInsert(&(*pDest), 0/* Automatic index assign */, pVal);
		}
		if( rc != SXRET_OK ){
			return rc;
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
	}
	return SXRET_OK;
}
/*
 * Duplicate the contents of a hashmap. Store the copy in pDest.
 * Refer to the [array_pad(), array_copy(), ...] implementation for more information.
 */
JX9_PRIVATE sxi32 jx9HashmapDup(jx9_hashmap *pSrc, jx9_hashmap *pDest)
{
	jx9_hashmap_node *pEntry;
	jx9_value sKey, *pVal;
	sxi32 rc;
	sxu32 n;
	if( pSrc == pDest ){
		/* Same map. This can easily happen since hashmaps are passed by reference.
		 * Unlike the  engine.
		 */
		return SXRET_OK;
	}
	/* Point to the first inserted entry in the source */
	pEntry = pSrc->pFirst;
	/* Perform the duplication */
	for( n = 0 ; n < pSrc->nEntry ; ++n ){
		/* Extract the node value */
		pVal = HashmapExtractNodeValue(pEntry);
		if( pEntry->iType == HASHMAP_BLOB_NODE ){
			/* Blob key insertion */
			jx9MemObjInitFromString(pDest->pVm, &sKey, 0);
			jx9MemObjStringAppend(&sKey, (const char *)SyBlobData(&pEntry->xKey.sKey), SyBlobLength(&pEntry->xKey.sKey));
			rc = jx9HashmapInsert(&(*pDest), &sKey, pVal);
			jx9MemObjRelease(&sKey);
		}else{
			/* Int key insertion */
			rc = HashmapInsertIntKey(&(*pDest), pEntry->xKey.iKey, pVal);
		}
		if( rc != SXRET_OK ){
			return rc;
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
	}
	return SXRET_OK;
}
/*
 * Perform the union of two hashmaps.
 * This operation is performed only if the user uses the '+' operator
 * with a variable holding an array as follows:
 * <?jx9
 * $a = array("a" => "apple", "b" => "banana");
 * $b = array("a" => "pear", "b" => "strawberry", "c" => "cherry");
 * $c = $a + $b; // Union of $a and $b
 * print "Union of \$a and \$b: \n";
 * dump($c);
 * $c = $b + $a; // Union of $b and $a
 * print "Union of \$b and \$a: \n";
 * dump($c);
 * ?>
 * When executed, this script will print the following:
 * Union of $a and $b:
 * array(3) {
 *  ["a"]=>
 *  string(5) "apple"
 *  ["b"]=>
 * string(6) "banana"
 *  ["c"]=>
 * string(6) "cherry"
 * }
 * Union of $b and $a:
 * array(3) {
 * ["a"]=>
 * string(4) "pear"
 * ["b"]=>
 * string(10) "strawberry"
 * ["c"]=>
 * string(6) "cherry"
 * }
 * The + operator returns the right-hand array appended to the left-hand array;
 * For keys that exist in both arrays, the elements from the left-hand array will be used
 * and the matching elements from the right-hand array will be ignored.
 */
JX9_PRIVATE sxi32 jx9HashmapUnion(jx9_hashmap *pLeft, jx9_hashmap *pRight)
{
	jx9_hashmap_node *pEntry;
	sxi32 rc = SXRET_OK;
	jx9_value *pObj;
	sxu32 n;
	if( pLeft == pRight ){
		/* Same map. This can easily happen since hashmaps are passed by reference.
		 * Unlike the  engine.
		 */
		return SXRET_OK;
	}
	/* Perform the union */
	pEntry = pRight->pFirst;
	for(n = 0 ; n < pRight->nEntry ; ++n ){
		/* Make sure the given key does not exists in the left array */
		if( pEntry->iType == HASHMAP_BLOB_NODE ){
			/* BLOB key */
			if( SXRET_OK != 
				HashmapLookupBlobKey(&(*pLeft), SyBlobData(&pEntry->xKey.sKey), SyBlobLength(&pEntry->xKey.sKey), 0) ){
					pObj = HashmapExtractNodeValue(pEntry);
					if( pObj ){
						/* Perform the insertion */
						rc = HashmapInsertBlobKey(&(*pLeft), SyBlobData(&pEntry->xKey.sKey),
							SyBlobLength(&pEntry->xKey.sKey),pObj);
						if( rc != SXRET_OK ){
							return rc;
						}
					}
			}
		}else{
			/* INT key */
			if( SXRET_OK != HashmapLookupIntKey(&(*pLeft), pEntry->xKey.iKey, 0) ){
				pObj = HashmapExtractNodeValue(pEntry);
				if( pObj ){
					/* Perform the insertion */
					rc = HashmapInsertIntKey(&(*pLeft), pEntry->xKey.iKey, pObj);
					if( rc != SXRET_OK ){
						return rc;
					}
				}
			}
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
	}
	return SXRET_OK;
}
/*
 * Allocate a new hashmap.
 * Return a pointer to the freshly allocated hashmap on success.NULL otherwise.
 */
JX9_PRIVATE jx9_hashmap * jx9NewHashmap(
	jx9_vm *pVm,              /* VM that trigger the hashmap creation */
	sxu32 (*xIntHash)(sxi64), /* Hash function for int keys.NULL otherwise*/
	sxu32 (*xBlobHash)(const void *, sxu32) /* Hash function for BLOB keys.NULL otherwise */
	)
{
	jx9_hashmap *pMap;
	/* Allocate a new instance */
	pMap = (jx9_hashmap *)SyMemBackendPoolAlloc(&pVm->sAllocator, sizeof(jx9_hashmap));
	if( pMap == 0 ){
		return 0;
	}
	/* Zero the structure */
	SyZero(pMap, sizeof(jx9_hashmap));
	/* Fill in the structure */
	pMap->pVm = &(*pVm);
	pMap->iRef = 1;
	/* pMap->iFlags = 0; */
	/* Default hash functions */
	pMap->xIntHash  = xIntHash ? xIntHash : IntHash;
	pMap->xBlobHash = xBlobHash ? xBlobHash : BinHash;
	return pMap;
}
/*
 * Install superglobals in the given virtual machine.
 * Note on superglobals.
 *  According to the JX9 language reference manual.
 *  Superglobals are built-in variables that are always available in all scopes.
*   Description
*   All predefined variables in JX9 are "superglobals", which means they
*   are available in all scopes throughout a script.
*   These variables are:
*    $_SERVER
*    $_GET
*    $_POST
*    $_FILES
*    $_REQUEST
*    $_ENV
*/
JX9_PRIVATE sxi32 jx9HashmapLoadBuiltin(jx9_vm *pVm)
{
	static const char * azSuper[] = {
		"_SERVER",   /* $_SERVER */
		"_GET",      /* $_GET */
		"_POST",     /* $_POST */
		"_FILES",    /* $_FILES */
		"_REQUEST",  /* $_REQUEST */
		"_COOKIE",   /* $_COOKIE */
		"_ENV",      /* $_ENV */
		"_HEADER",   /* $_HEADER */
		"argv"       /* $argv */
	};
	SyString *pFile;
	sxi32 rc;
	sxu32 n;
	/* Install globals variable now */
	for( n =  0 ; n < SX_ARRAYSIZE(azSuper)  ; n++ ){
		jx9_value *pSuper;
		/* Request an empty array */
		pSuper = jx9_new_array(&(*pVm));
		if( pSuper == 0 ){
			return SXERR_MEM;
		}
		/* Install */
		rc = jx9_vm_config(&(*pVm),JX9_VM_CONFIG_CREATE_VAR, azSuper[n]/* Super-global name*/, pSuper/* Super-global value */);
		if( rc != SXRET_OK ){
			return rc;
		}
		/* Release the value now it have been installed */
		jx9_release_value(&(*pVm), pSuper);
	}
	/* Set some $_SERVER entries */
	pFile = (SyString *)SySetPeek(&pVm->aFiles);
	/*
	 * 'SCRIPT_FILENAME'
	 * The absolute pathname of the currently executing script.
	 */
	jx9_vm_config(pVm, JX9_VM_CONFIG_SERVER_ATTR, 
		"SCRIPT_FILENAME", 
		pFile ? pFile->zString : ":Memory:", 
		pFile ? pFile->nByte : sizeof(":Memory:") - 1
		);
	/* All done, all global variables are installed now */
	return SXRET_OK;
}
/*
 * Release a hashmap.
 */
JX9_PRIVATE sxi32 jx9HashmapRelease(jx9_hashmap *pMap, int FreeDS)
{
	jx9_hashmap_node *pEntry, *pNext;
	jx9_vm *pVm = pMap->pVm;
	sxu32 n;
	/* Start the release process */
	n = 0;
	pEntry = pMap->pFirst;
	for(;;){
		if( n >= pMap->nEntry ){
			break;
		}
		pNext = pEntry->pPrev; /* Reverse link */
		/* Restore the jx9_value to the free list */
		jx9VmUnsetMemObj(pVm, pEntry->nValIdx);
		/* Release the node */
		if( pEntry->iType == HASHMAP_BLOB_NODE ){
			SyBlobRelease(&pEntry->xKey.sKey);
		}
		SyMemBackendPoolFree(&pVm->sAllocator, pEntry);
		/* Point to the next entry */
		pEntry = pNext;
		n++;
	}
	if( pMap->nEntry > 0 ){
		/* Release the hash bucket */
		SyMemBackendFree(&pVm->sAllocator, pMap->apBucket);
	}
	if( FreeDS ){
		/* Free the whole instance */
		SyMemBackendPoolFree(&pVm->sAllocator, pMap);
	}else{
		/* Keep the instance but reset it's fields */
		pMap->apBucket = 0;
		pMap->iNextIdx = 0;
		pMap->nEntry = pMap->nSize = 0;
		pMap->pFirst = pMap->pLast = pMap->pCur = 0;
	}
	return SXRET_OK;
}
/*
 * Decrement the reference count of a given hashmap.
 * If the count reaches zero which mean no more variables
 * are pointing to this hashmap, then release the whole instance.
 */
JX9_PRIVATE void  jx9HashmapUnref(jx9_hashmap *pMap)
{
	pMap->iRef--;
	if( pMap->iRef < 1 ){
		jx9HashmapRelease(pMap, TRUE);
	}
}
/*
 * Check if a given key exists in the given hashmap.
 * Write a pointer to the target node on success.
 * Otherwise SXERR_NOTFOUND is returned on failure.
 */
JX9_PRIVATE sxi32 jx9HashmapLookup(
	jx9_hashmap *pMap,        /* Target hashmap */
	jx9_value *pKey,          /* Lookup key */
	jx9_hashmap_node **ppNode /* OUT: Target node on success */
	)
{
	sxi32 rc;
	if( pMap->nEntry < 1 ){
		/* TICKET 1433-25: Don't bother hashing, the hashmap is empty anyway.
		 */
		return SXERR_NOTFOUND;
	}
	rc = HashmapLookup(&(*pMap), &(*pKey), ppNode);
	return rc;
}
/*
 * Check if a given key exists in the given hashmap.
 * Write a pointer to the target node on success.
 * Otherwise SXERR_NOTFOUND is returned on failure.
 */
JX9_PRIVATE sxi32 jx9HashmapLookupIntKey(
	jx9_hashmap *pMap,        /* Target hashmap */
	sxi64 iKey,               /* Lookup key */
	jx9_hashmap_node **ppNode /* OUT: Target node on success */
	)
{
	sxi32 rc;
	if( pMap->nEntry < 1 ){
		/* TICKET 1433-25: Don't bother hashing, the hashmap is empty anyway.
		 */
		return SXERR_NOTFOUND;
	}
	rc = HashmapLookupIntKey(&(*pMap), iKey, ppNode);
	return rc;
}
/*
 * Insert a given key and it's associated value (if any) in the given
 * hashmap.
 * If a node with the given key already exists in the database
 * then this function overwrite the old value.
 */
JX9_PRIVATE sxi32 jx9HashmapInsert(
	jx9_hashmap *pMap, /* Target hashmap */
	jx9_value *pKey,   /* Lookup key */
	jx9_value *pVal    /* Node value.NULL otherwise */
	)
{
	sxi32 rc;
	rc = HashmapInsert(&(*pMap), &(*pKey), &(*pVal));
	return rc;
}
/*
 * Reset the node cursor of a given hashmap.
 */
JX9_PRIVATE void jx9HashmapResetLoopCursor(jx9_hashmap *pMap)
{
	/* Reset the loop cursor */
	pMap->pCur = pMap->pFirst;
}
/*
 * Return a pointer to the node currently pointed by the node cursor.
 * If the cursor reaches the end of the list, then this function
 * return NULL.
 * Note that the node cursor is automatically advanced by this function.
 */
JX9_PRIVATE jx9_hashmap_node * jx9HashmapGetNextEntry(jx9_hashmap *pMap)
{
	jx9_hashmap_node *pCur = pMap->pCur;
	if( pCur == 0 ){
		/* End of the list, return null */
		return 0;
	}
	/* Advance the node cursor */
	pMap->pCur = pCur->pPrev; /* Reverse link */
	return pCur;
}
/*
 * Extract a node value.
 */
JX9_PRIVATE jx9_value * jx9HashmapGetNodeValue(jx9_hashmap_node *pNode)
{
	jx9_value *pValue;
	pValue = HashmapExtractNodeValue(pNode);
	return pValue;
}
/*
 * Extract a node value (Second).
 */
JX9_PRIVATE void jx9HashmapExtractNodeValue(jx9_hashmap_node *pNode, jx9_value *pValue, int bStore)
{
	jx9_value *pEntry = HashmapExtractNodeValue(pNode);
	if( pEntry ){
		if( bStore ){
			jx9MemObjStore(pEntry, pValue);
		}else{
			jx9MemObjLoad(pEntry, pValue);
		}
	}else{
		jx9MemObjRelease(pValue);
	}
}
/*
 * Extract a node key.
 */
JX9_PRIVATE void jx9HashmapExtractNodeKey(jx9_hashmap_node *pNode,jx9_value *pKey)
{
	/* Fill with the current key */
	if( pNode->iType == HASHMAP_INT_NODE ){
		if( SyBlobLength(&pKey->sBlob) > 0 ){
			SyBlobRelease(&pKey->sBlob);
		}
		pKey->x.iVal = pNode->xKey.iKey;
		MemObjSetType(pKey, MEMOBJ_INT);
	}else{
		SyBlobReset(&pKey->sBlob);
		SyBlobAppend(&pKey->sBlob, SyBlobData(&pNode->xKey.sKey), SyBlobLength(&pNode->xKey.sKey));
		MemObjSetType(pKey, MEMOBJ_STRING);
	}
}
#ifndef JX9_DISABLE_BUILTIN_FUNC
/*
 * Store the address of nodes value in the given container.
 * Refer to the [vfprintf(), vprintf(), vsprintf()] implementations
 * defined in 'builtin.c' for more information.
 */
JX9_PRIVATE int jx9HashmapValuesToSet(jx9_hashmap *pMap, SySet *pOut)
{
	jx9_hashmap_node *pEntry = pMap->pFirst;
	jx9_value *pValue;
	sxu32 n;
	/* Initialize the container */
	SySetInit(pOut, &pMap->pVm->sAllocator, sizeof(jx9_value *));
	for(n = 0 ; n < pMap->nEntry ; n++ ){
		/* Extract node value */
		pValue = HashmapExtractNodeValue(pEntry);
		if( pValue ){
			SySetPut(pOut, (const void *)&pValue);
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
	}
	/* Total inserted entries */
	return (int)SySetUsed(pOut);
}
#endif /* JX9_DISABLE_BUILTIN_FUNC */
/*
 * Merge sort.
 * The merge sort implementation is based on the one found in the SQLite3 source tree.
 * Status: Public domain
 */
/* Node comparison callback signature */
typedef sxi32 (*ProcNodeCmp)(jx9_hashmap_node *, jx9_hashmap_node *, void *);
/*
** Inputs:
**   a:       A sorted, null-terminated linked list.  (May be null).
**   b:       A sorted, null-terminated linked list.  (May be null).
**   cmp:     A pointer to the comparison function.
**
** Return Value:
**   A pointer to the head of a sorted list containing the elements
**   of both a and b.
**
** Side effects:
**   The "next", "prev" pointers for elements in the lists a and b are
**   changed.
*/
static jx9_hashmap_node * HashmapNodeMerge(jx9_hashmap_node *pA, jx9_hashmap_node *pB, ProcNodeCmp xCmp, void *pCmpData)
{
	jx9_hashmap_node result, *pTail;
    /* Prevent compiler warning */
	result.pNext = result.pPrev = 0;
	pTail = &result;
	while( pA && pB ){
		if( xCmp(pA, pB, pCmpData) < 0 ){
			pTail->pPrev = pA;
			pA->pNext = pTail;
			pTail = pA;
			pA = pA->pPrev;
		}else{
			pTail->pPrev = pB;
			pB->pNext = pTail;
			pTail = pB;
			pB = pB->pPrev;
		}
	}
	if( pA ){
		pTail->pPrev = pA;
		pA->pNext = pTail;
	}else if( pB ){
		pTail->pPrev = pB;
		pB->pNext = pTail;
	}else{
		pTail->pPrev = pTail->pNext = 0;
	}
	return result.pPrev;
}
/*
** Inputs:
**   Map:       Input hashmap
**   cmp:       A comparison function.
**
** Return Value:
**   Sorted hashmap.
**
** Side effects:
**   The "next" pointers for elements in list are changed.
*/
#define N_SORT_BUCKET  32
static sxi32 HashmapMergeSort(jx9_hashmap *pMap, ProcNodeCmp xCmp, void *pCmpData)
{
	jx9_hashmap_node *a[N_SORT_BUCKET], *p, *pIn;
	sxu32 i;
	SyZero(a, sizeof(a));
	/* Point to the first inserted entry */
	pIn = pMap->pFirst;
	while( pIn ){
		p = pIn;
		pIn = p->pPrev;
		p->pPrev = 0;
		for(i=0; i<N_SORT_BUCKET-1; i++){
			if( a[i]==0 ){
				a[i] = p;
				break;
			}else{
				p = HashmapNodeMerge(a[i], p, xCmp, pCmpData);
				a[i] = 0;
			}
		}
		if( i==N_SORT_BUCKET-1 ){
			/* To get here, there need to be 2^(N_SORT_BUCKET) elements in he input list.
			 * But that is impossible.
			 */
			a[i] = HashmapNodeMerge(a[i], p, xCmp, pCmpData);
		}
	}
	p = a[0];
	for(i=1; i<N_SORT_BUCKET; i++){
		p = HashmapNodeMerge(p, a[i], xCmp, pCmpData);
	}
	p->pNext = 0;
	/* Reflect the change */
	pMap->pFirst = p;
	/* Reset the loop cursor */
	pMap->pCur = pMap->pFirst;
	return SXRET_OK;
}
/* 
 * Node comparison callback.
 * used-by: [sort(), asort(), ...]
 */
static sxi32 HashmapCmpCallback1(jx9_hashmap_node *pA, jx9_hashmap_node *pB, void *pCmpData)
{
	jx9_value sA, sB;
	sxi32 iFlags;
	int rc;
	if( pCmpData == 0 ){
		/* Perform a standard comparison */
		rc = HashmapNodeCmp(pA, pB, FALSE);
		return rc;
	}
	iFlags = SX_PTR_TO_INT(pCmpData);
	/* Duplicate node values */
	jx9MemObjInit(pA->pMap->pVm, &sA);
	jx9MemObjInit(pA->pMap->pVm, &sB);
	jx9HashmapExtractNodeValue(pA, &sA, FALSE);
	jx9HashmapExtractNodeValue(pB, &sB, FALSE);
	if( iFlags == 5 ){
		/* String cast */
		if( (sA.iFlags & MEMOBJ_STRING) == 0 ){
			jx9MemObjToString(&sA);
		}
		if( (sB.iFlags & MEMOBJ_STRING) == 0 ){
			jx9MemObjToString(&sB);
		}
	}else{
		/* Numeric cast */
		jx9MemObjToNumeric(&sA);
		jx9MemObjToNumeric(&sB);
	}
	/* Perform the comparison */
	rc = jx9MemObjCmp(&sA, &sB, FALSE, 0);
	jx9MemObjRelease(&sA);
	jx9MemObjRelease(&sB);
	return rc;
}
/*
 * Node comparison callback.
 * Used by: [rsort(), arsort()];
 */
static sxi32 HashmapCmpCallback3(jx9_hashmap_node *pA, jx9_hashmap_node *pB, void *pCmpData)
{
	jx9_value sA, sB;
	sxi32 iFlags;
	int rc;
	if( pCmpData == 0 ){
		/* Perform a standard comparison */
		rc = HashmapNodeCmp(pA, pB, FALSE);
		return -rc;
	}
	iFlags = SX_PTR_TO_INT(pCmpData);
	/* Duplicate node values */
	jx9MemObjInit(pA->pMap->pVm, &sA);
	jx9MemObjInit(pA->pMap->pVm, &sB);
	jx9HashmapExtractNodeValue(pA, &sA, FALSE);
	jx9HashmapExtractNodeValue(pB, &sB, FALSE);
	if( iFlags == 5 ){
		/* String cast */
		if( (sA.iFlags & MEMOBJ_STRING) == 0 ){
			jx9MemObjToString(&sA);
		}
		if( (sB.iFlags & MEMOBJ_STRING) == 0 ){
			jx9MemObjToString(&sB);
		}
	}else{
		/* Numeric cast */
		jx9MemObjToNumeric(&sA);
		jx9MemObjToNumeric(&sB);
	}
	/* Perform the comparison */
	rc = jx9MemObjCmp(&sA, &sB, FALSE, 0);
	jx9MemObjRelease(&sA);
	jx9MemObjRelease(&sB);
	return -rc;
}
/*
 * Node comparison callback: Invoke an user-defined callback for the purpose of node comparison.
 * used-by: [usort(), uasort()]
 */
static sxi32 HashmapCmpCallback4(jx9_hashmap_node *pA, jx9_hashmap_node *pB, void *pCmpData)
{
	jx9_value sResult, *pCallback;
	jx9_value *pV1, *pV2;
	jx9_value *apArg[2];  /* Callback arguments */
	sxi32 rc;
	/* Point to the desired callback */
	pCallback = (jx9_value *)pCmpData;
	/* initialize the result value */
	jx9MemObjInit(pA->pMap->pVm, &sResult);
	/* Extract nodes values */
	pV1 = HashmapExtractNodeValue(pA);
	pV2 = HashmapExtractNodeValue(pB);
	apArg[0] = pV1;
	apArg[1] = pV2;
	/* Invoke the callback */
	rc = jx9VmCallUserFunction(pA->pMap->pVm, pCallback, 2, apArg, &sResult);
	if( rc != SXRET_OK ){
		/* An error occured while calling user defined function [i.e: not defined] */
		rc = -1; /* Set a dummy result */
	}else{
		/* Extract callback result */
		if((sResult.iFlags & MEMOBJ_INT) == 0 ){
			/* Perform an int cast */
			jx9MemObjToInteger(&sResult);
		}
		rc = (sxi32)sResult.x.iVal;
	}
	jx9MemObjRelease(&sResult);
	/* Callback result */
	return rc;
}
/*
 * Rehash all nodes keys after a merge-sort have been applied.
 * Used by [sort(), usort() and rsort()].
 */
static void HashmapSortRehash(jx9_hashmap *pMap)
{
	jx9_hashmap_node *p, *pLast;
	sxu32 i;
	/* Rehash all entries */
	pLast = p = pMap->pFirst;
	pMap->iNextIdx = 0; /* Reset the automatic index */
	i = 0;
	for( ;; ){
		if( i >= pMap->nEntry ){
			pMap->pLast = pLast; /* Fix the last link broken by the merge-sort */
			break;
		}
		if( p->iType == HASHMAP_BLOB_NODE ){
			/* Do not maintain index association as requested by the JX9 specification */
			SyBlobRelease(&p->xKey.sKey);
			/* Change key type */
			p->iType = HASHMAP_INT_NODE;
		}
		HashmapRehashIntNode(p);
		/* Point to the next entry */
		i++;
		pLast = p;
		p = p->pPrev; /* Reverse link */
	}
}
/*
 * Array functions implementation.
 * Authors:
 *  Symisc Systems, devel@symisc.net.
 *  Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *  Stable.
 */
/*
 * bool sort(array &$array[, int $sort_flags = SORT_REGULAR ] )
 * Sort an array.
 * Parameters
 *  $array
 *   The input array.
 * $sort_flags
 *  The optional second parameter sort_flags may be used to modify the sorting behavior using these values:
 *  Sorting type flags:
 *   SORT_REGULAR - compare items normally (don't change types)
 *   SORT_NUMERIC - compare items numerically
 *   SORT_STRING - compare items as strings
 * Return
 *  TRUE on success or FALSE on failure.
 * 
 */
static int jx9_hashmap_sort(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	/* Make sure we are dealing with a valid hashmap */
	if( nArg < 1 || !jx9_value_is_json_array(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the internal representation of the input hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	if( pMap->nEntry > 1 ){
		sxi32 iCmpFlags = 0;
		if( nArg > 1 ){
			/* Extract comparison flags */
			iCmpFlags = jx9_value_to_int(apArg[1]);
			if( iCmpFlags == 3 /* SORT_REGULAR */ ){
				iCmpFlags = 0; /* Standard comparison */
			}
		}
		/* Do the merge sort */
		HashmapMergeSort(pMap, HashmapCmpCallback1, SX_INT_TO_PTR(iCmpFlags));
		/* Rehash [Do not maintain index association as requested by the JX9 specification] */
		HashmapSortRehash(pMap);
	}
	/* All done, return TRUE */
	jx9_result_bool(pCtx, 1);
	return JX9_OK;
}
/*
 * bool rsort(array &$array[, int $sort_flags = SORT_REGULAR ] )
 * Sort an array in reverse order.
 * Parameters
 *  $array
 *   The input array.
 * $sort_flags
 *  The optional second parameter sort_flags may be used to modify the sorting behavior using these values:
 *  Sorting type flags:
 *   SORT_REGULAR - compare items normally (don't change types)
 *   SORT_NUMERIC - compare items numerically
 *   SORT_STRING - compare items as strings
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9_hashmap_rsort(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	/* Make sure we are dealing with a valid hashmap */
	if( nArg < 1 || !jx9_value_is_json_array(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the internal representation of the input hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	if( pMap->nEntry > 1 ){
		sxi32 iCmpFlags = 0;
		if( nArg > 1 ){
			/* Extract comparison flags */
			iCmpFlags = jx9_value_to_int(apArg[1]);
			if( iCmpFlags == 3 /* SORT_REGULAR */ ){
				iCmpFlags = 0; /* Standard comparison */
			}
		}
		/* Do the merge sort */
		HashmapMergeSort(pMap, HashmapCmpCallback3, SX_INT_TO_PTR(iCmpFlags));
		/* Rehash [Do not maintain index association as requested by the JX9 specification] */
		HashmapSortRehash(pMap);
	}
	/* All done, return TRUE */
	jx9_result_bool(pCtx, 1);
	return JX9_OK;
}
/*
 * bool usort(array &$array, callable $cmp_function)
 *  Sort an array by values using a user-defined comparison function.
 * Parameters
 *  $array
 *   The input array.
 * $cmp_function
 *  The comparison function must return an integer less than, equal to, or greater
 *  than zero if the first argument is considered to be respectively less than, equal
 *  to, or greater than the second.
 *    int callback ( mixed $a, mixed $b )
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9_hashmap_usort(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	/* Make sure we are dealing with a valid hashmap */
	if( nArg < 1 || !jx9_value_is_json_array(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the internal representation of the input hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	if( pMap->nEntry > 1 ){
		jx9_value *pCallback = 0;
		ProcNodeCmp xCmp;
		xCmp = HashmapCmpCallback4; /* User-defined function as the comparison callback */
		if( nArg > 1 && jx9_value_is_callable(apArg[1]) ){
			/* Point to the desired callback */
			pCallback = apArg[1];
		}else{
			/* Use the default comparison function */
			xCmp = HashmapCmpCallback1;
		}
		/* Do the merge sort */
		HashmapMergeSort(pMap, xCmp, pCallback);
		/* Rehash [Do not maintain index association as requested by the JX9 specification] */
		HashmapSortRehash(pMap);
	}
	/* All done, return TRUE */
	jx9_result_bool(pCtx, 1);
	return JX9_OK;
}
/*
 * int count(array $var [, int $mode = COUNT_NORMAL ])
 *   Count all elements in an array, or something in an object.
 * Parameters
 *  $var
 *   The array or the object.
 * $mode
 *  If the optional mode parameter is set to COUNT_RECURSIVE (or 1), count()
 *  will recursively count the array. This is particularly useful for counting 
 *  all the elements of a multidimensional array. count() does not detect infinite
 *  recursion.
 * Return
 *  Returns the number of elements in the array.
 */
static int jx9_hashmap_count(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	int bRecursive = FALSE;
	sxi64 iCount;
	if( nArg < 1 ){
		/* Missing arguments, return 0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* TICKET 1433-19: Handle objects */
		int res = !jx9_value_is_null(apArg[0]);
		jx9_result_int(pCtx, res);
		return JX9_OK;
	}
	if( nArg > 1 ){
		/* Recursive count? */
		bRecursive = jx9_value_to_int(apArg[1]) == 1 /* COUNT_RECURSIVE */;
	}
	/* Count */
	iCount = HashmapCount((jx9_hashmap *)apArg[0]->x.pOther, bRecursive, 0);
	jx9_result_int64(pCtx, iCount);
	return JX9_OK;
}
/*
 * bool array_key_exists(value $key, array $search)
 *  Checks if the given key or index exists in the array.
 * Parameters
 * $key
 *   Value to check.
 * $search
 *  An array with keys to check.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9_hashmap_key_exists(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	sxi32 rc;
	if( nArg < 2 ){
		/* Missing arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[1]) ){
		/* Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the lookup */
	rc = jx9HashmapLookup((jx9_hashmap *)apArg[1]->x.pOther, apArg[0], 0);
	/* lookup result */
	jx9_result_bool(pCtx, rc == SXRET_OK ? 1 : 0);
	return JX9_OK;
}
/*
 * value array_pop(array $array)
 *   POP the last inserted element from the array.
 * Parameter
 *  The array to get the value from.
 * Return
 *  Poped value or NULL on failure.
 */
static int jx9_hashmap_pop(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	if( nArg < 1 ){
		/* Missing arguments, return null */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return null */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	if( pMap->nEntry < 1 ){
		/* Noting to pop, return NULL */
		jx9_result_null(pCtx);
	}else{
		jx9_hashmap_node *pLast = pMap->pLast;
		jx9_value *pObj;
		pObj = HashmapExtractNodeValue(pLast);
		if( pObj ){
			/* Node value */
			jx9_result_value(pCtx, pObj);
			/* Unlink the node */
			jx9HashmapUnlinkNode(pLast);
		}else{
			jx9_result_null(pCtx);
		}
		/* Reset the cursor */
		pMap->pCur = pMap->pFirst;
	}
	return JX9_OK;
}
/*
 * int array_push($array, $var, ...)
 *   Push one or more elements onto the end of array. (Stack insertion)
 * Parameters
 *  array
 *    The input array.
 *  var
 *   On or more value to push.
 * Return
 *  New array count (including old items).
 */
static int jx9_hashmap_push(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	sxi32 rc;
	int i;
	if( nArg < 1 ){
		/* Missing arguments, return 0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return 0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the internal representation of the input hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	/* Start pushing given values */
	for( i = 1 ; i < nArg ; ++i ){
		rc = jx9HashmapInsert(pMap, 0, apArg[i]);
		if( rc != SXRET_OK ){
			break;
		}
	}
	/* Return the new count */
	jx9_result_int64(pCtx, (sxi64)pMap->nEntry);
	return JX9_OK;
}
/*
 * value array_shift(array $array)
 *   Shift an element off the beginning of array.
 * Parameter
 *  The array to get the value from.
 * Return
 *  Shifted value or NULL on failure.
 */
static int jx9_hashmap_shift(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	if( nArg < 1 ){
		/* Missing arguments, return null */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return null */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Point to the internal representation of the hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	if( pMap->nEntry < 1 ){
		/* Empty hashmap, return NULL */
		jx9_result_null(pCtx);
	}else{
		jx9_hashmap_node *pEntry = pMap->pFirst;
		jx9_value *pObj;
		sxu32 n;
		pObj = HashmapExtractNodeValue(pEntry);
		if( pObj ){
			/* Node value */
			jx9_result_value(pCtx, pObj);
			/* Unlink the first node */
			jx9HashmapUnlinkNode(pEntry);
		}else{
			jx9_result_null(pCtx);
		}
		/* Rehash all int keys */
		n = pMap->nEntry;
		pEntry = pMap->pFirst;
		pMap->iNextIdx = 0; /* Reset the automatic index */
		for(;;){
			if( n < 1 ){
				break;
			}
			if( pEntry->iType == HASHMAP_INT_NODE ){
				HashmapRehashIntNode(pEntry);
			}
			/* Point to the next entry */
			pEntry = pEntry->pPrev; /* Reverse link */
			n--;
		}
		/* Reset the cursor */
		pMap->pCur = pMap->pFirst;
	}
	return JX9_OK;
}
/*
 * Extract the node cursor value.
 */
static sxi32 HashmapCurrentValue(jx9_context *pCtx, jx9_hashmap *pMap, int iDirection)
{
	jx9_hashmap_node *pCur = pMap->pCur;
	jx9_value *pVal;
	if( pCur == 0 ){
		/* Cursor does not point to anything, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	if( iDirection != 0 ){
		if( iDirection > 0 ){
			/* Point to the next entry */
			pMap->pCur = pCur->pPrev; /* Reverse link */
			pCur = pMap->pCur;
		}else{
			/* Point to the previous entry */
			pMap->pCur = pCur->pNext; /* Reverse link */
			pCur = pMap->pCur;
		}
		if( pCur == 0 ){
			/* End of input reached, return FALSE */
			jx9_result_bool(pCtx, 0);
			return JX9_OK;
		}
	}		
	/* Point to the desired element */
	pVal = HashmapExtractNodeValue(pCur);
	if( pVal ){
		jx9_result_value(pCtx, pVal);
	}else{
		jx9_result_bool(pCtx, 0);
	}
	return JX9_OK;
}
/*
 * value current(array $array)
 *  Return the current element in an array.
 * Parameter
 *  $input: The input array.
 * Return
 *  The current() function simply returns the value of the array element that's currently
 *  being pointed to by the internal pointer. It does not move the pointer in any way.
 *  If the internal pointer points beyond the end of the elements list or the array 
 *  is empty, current() returns FALSE. 
 */
static int jx9_hashmap_current(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	if( nArg < 1 ){
		/* Missing arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	HashmapCurrentValue(&(*pCtx), (jx9_hashmap *)apArg[0]->x.pOther, 0);
	return JX9_OK;
}
/*
 * value next(array $input)
 *  Advance the internal array pointer of an array.
 * Parameter
 *  $input: The input array.
 * Return
 *  next() behaves like current(), with one difference. It advances the internal array 
 *  pointer one place forward before returning the element value. That means it returns 
 *  the next array value and advances the internal array pointer by one. 
 */
static int jx9_hashmap_next(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	if( nArg < 1 ){
		/* Missing arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	HashmapCurrentValue(&(*pCtx), (jx9_hashmap *)apArg[0]->x.pOther, 1);
	return JX9_OK;
}
/* 
 * value prev(array $input)
 *  Rewind the internal array pointer.
 * Parameter
 *  $input: The input array.
 * Return
 *  Returns the array value in the previous place that's pointed
 *  to by the internal array pointer, or FALSE if there are no more
 *  elements. 
 */
static int jx9_hashmap_prev(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	if( nArg < 1 ){
		/* Missing arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	HashmapCurrentValue(&(*pCtx), (jx9_hashmap *)apArg[0]->x.pOther, -1);
	return JX9_OK;
}
/* 
 * value end(array $input)
 *  Set the internal pointer of an array to its last element.
 * Parameter
 *  $input: The input array.
 * Return
 *  Returns the value of the last element or FALSE for empty array. 
 */
static int jx9_hashmap_end(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	if( nArg < 1 ){
		/* Missing arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the internal representation of the input hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	/* Point to the last node */
	pMap->pCur = pMap->pLast;
	/* Return the last node value */
	HashmapCurrentValue(&(*pCtx), pMap, 0);
	return JX9_OK;
}
/* 
 * value reset(array $array )
 *  Set the internal pointer of an array to its first element.
 * Parameter
 *  $input: The input array.
 * Return
 *  Returns the value of the first array element, or FALSE if the array is empty. 
 */
static int jx9_hashmap_reset(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	if( nArg < 1 ){
		/* Missing arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the internal representation of the input hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	/* Point to the first node */
	pMap->pCur = pMap->pFirst;
	/* Return the last node value if available */
	HashmapCurrentValue(&(*pCtx), pMap, 0);
	return JX9_OK;
}
/*
 * value key(array $array)
 *   Fetch a key from an array
 * Parameter
 *  $input
 *   The input array.
 * Return
 *  The key() function simply returns the key of the array element that's currently
 *  being pointed to by the internal pointer. It does not move the pointer in any way.
 *  If the internal pointer points beyond the end of the elements list or the array 
 *  is empty, key() returns NULL. 
 */
static int jx9_hashmap_simple_key(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap_node *pCur;
	jx9_hashmap *pMap;
	if( nArg < 1 ){
		/* Missing arguments, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	pCur = pMap->pCur;
	if( pCur == 0 ){
		/* Cursor does not point to anything, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	if( pCur->iType == HASHMAP_INT_NODE){
		/* Key is integer */
		jx9_result_int64(pCtx, pCur->xKey.iKey);
	}else{
		/* Key is blob */
		jx9_result_string(pCtx, 
			(const char *)SyBlobData(&pCur->xKey.sKey), (int)SyBlobLength(&pCur->xKey.sKey));
	}
	return JX9_OK;
}
/*
 * array each(array $input)
 *  Return the current key and value pair from an array and advance the array cursor.
 * Parameter
 *  $input
 *    The input array.
 * Return
 *  Returns the current key and value pair from the array array. This pair is returned 
 *  in a four-element array, with the keys 0, 1, key, and value. Elements 0 and key 
 *  contain the key name of the array element, and 1 and value contain the data.
 *  If the internal pointer for the array points past the end of the array contents
 *  each() returns FALSE. 
 */
static int jx9_hashmap_each(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap_node *pCur;
	jx9_hashmap *pMap;
	jx9_value *pArray;
	jx9_value *pVal;
	jx9_value sKey;
	if( nArg < 1 ){
		/* Missing arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the internal representation that describe the input hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	if( pMap->pCur == 0 ){
		/* Cursor does not point to anything, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	pCur = pMap->pCur;
	/* Create a new array */
	pArray = jx9_context_new_array(pCtx);
	if( pArray == 0 ){
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	pVal = HashmapExtractNodeValue(pCur);
	/* Insert the current value */
	jx9_array_add_strkey_elem(pArray, "1", pVal);
	jx9_array_add_strkey_elem(pArray, "value", pVal);
	/* Make the key */
	if( pCur->iType == HASHMAP_INT_NODE ){
		jx9MemObjInitFromInt(pMap->pVm, &sKey, pCur->xKey.iKey);
	}else{
		jx9MemObjInitFromString(pMap->pVm, &sKey, 0);
		jx9MemObjStringAppend(&sKey, (const char *)SyBlobData(&pCur->xKey.sKey), SyBlobLength(&pCur->xKey.sKey));
	}
	/* Insert the current key */
	jx9_array_add_elem(pArray, 0, &sKey);
	jx9_array_add_strkey_elem(pArray, "key", &sKey);
	jx9MemObjRelease(&sKey);
	/* Advance the cursor */
	pMap->pCur = pCur->pPrev; /* Reverse link */
	/* Return the current entry */
	jx9_result_value(pCtx, pArray);
	return JX9_OK;
}
/*
 * array array_values(array $input)
 *   Returns all the values from the input array and indexes numerically the array.
 * Parameters
 *   input: The input array.
 * Return
 *  An indexed array of values or NULL on failure.
 */
static int jx9_hashmap_values(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap_node *pNode;
	jx9_hashmap *pMap;
	jx9_value *pArray;
	jx9_value *pObj;
	sxu32 n;
	if( nArg < 1 ){
		/* Missing arguments, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Point to the internal representation that describe the input hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	/* Create a new array */
	pArray = jx9_context_new_array(pCtx);
	if( pArray == 0 ){
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Perform the requested operation */
	pNode = pMap->pFirst;
	for( n = 0 ; n < pMap->nEntry ; ++n ){
		pObj = HashmapExtractNodeValue(pNode);
		if( pObj ){
			/* perform the insertion */
			jx9_array_add_elem(pArray, 0/* Automatic index assign */, pObj);
		}
		/* Point to the next entry */
		pNode = pNode->pPrev; /* Reverse link */
	}
	/* return the new array */
	jx9_result_value(pCtx, pArray);
	return JX9_OK;
}
/*
 * bool array_same(array $arr1, array $arr2)
 *  Return TRUE if the given arrays are the same instance.
 *  This function is useful under JX9 since arrays and objects
 *  are passed by reference.
 * Parameters
 *  $arr1
 *   First array
 *  $arr2
 *   Second array
 * Return
 *  TRUE if the arrays are the same instance. FALSE otherwise.
 * Note
 *  This function is a symisc eXtension.
 */
static int jx9_hashmap_same(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *p1, *p2;
	int rc;
	if( nArg < 2 || !jx9_value_is_json_array(apArg[0]) || !jx9_value_is_json_array(apArg[1]) ){
		/* Missing or invalid arguments, return FALSE*/
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the hashmaps */
	p1 = (jx9_hashmap *)apArg[0]->x.pOther;
	p2 = (jx9_hashmap *)apArg[1]->x.pOther;
	rc = (p1 == p2);
	/* Same instance? */
	jx9_result_bool(pCtx, rc);
	return JX9_OK;
}
/*
 * array array_merge(array $array1, ...)
 *  Merge one or more arrays.
 * Parameters
 *  $array1
 *    Initial array to merge.
 *  ...
 *   More array to merge.
 * Return
 *  The resulting array.
 */
static int jx9_hashmap_merge(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap, *pSrc;
	jx9_value *pArray;
	int i;
	if( nArg < 1 ){
		/* Missing arguments, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Create a new array */
	pArray = jx9_context_new_array(pCtx);
	if( pArray == 0 ){
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Point to the internal representation of the hashmap */
	pMap = (jx9_hashmap *)pArray->x.pOther;
	/* Start merging */
	for( i = 0 ; i < nArg ; i++ ){
		/* Make sure we are dealing with a valid hashmap */
		if( !jx9_value_is_json_array(apArg[i]) ){
			/* Insert scalar value */
			jx9_array_add_elem(pArray, 0, apArg[i]);
		}else{
			pSrc = (jx9_hashmap *)apArg[i]->x.pOther;
			/* Merge the two hashmaps */
			HashmapMerge(pSrc, pMap);
		}
	}
	/* Return the freshly created array */
	jx9_result_value(pCtx, pArray);
	return JX9_OK;
}
/*
 * bool in_array(value $needle, array $haystack[, bool $strict = FALSE ])
 *  Checks if a value exists in an array.
 * Parameters
 *  $needle
 *   The searched value.
 *   Note:
 *    If needle is a string, the comparison is done in a case-sensitive manner.
 * $haystack
 *  The target array.
 * $strict
 *  If the third parameter strict is set to TRUE then the in_array() function
 *  will also check the types of the needle in the haystack.
 */
static int jx9_hashmap_in_array(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_value *pNeedle;
	int bStrict;
	int rc;
	if( nArg < 2 ){
		/* Missing argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	pNeedle = apArg[0];
	bStrict = 0;
	if( nArg > 2 ){
		bStrict = jx9_value_to_bool(apArg[2]);
	}
	if( !jx9_value_is_json_array(apArg[1]) ){
		/* haystack must be an array, perform a standard comparison */ 
		rc = jx9_value_compare(pNeedle, apArg[1], bStrict);
		/* Set the comparison result */
		jx9_result_bool(pCtx, rc == 0);
		return JX9_OK;
	}
	/* Perform the lookup */
	rc = HashmapFindValue((jx9_hashmap *)apArg[1]->x.pOther, pNeedle, 0, bStrict);
	/* Lookup result */
	jx9_result_bool(pCtx, rc == SXRET_OK);
	return JX9_OK;
}
/*
 * array array_copy(array $source)
 *  Make a blind copy of the target array.
 * Parameters
 *  $source
 *   Target array
 * Return
 *  Copy of the target array on success. NULL otherwise.
 * Note
 *  This function is a symisc eXtension.
 */
static int jx9_hashmap_copy(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	jx9_value *pArray;
	if( nArg < 1 ){
		/* Missing arguments, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Create a new array */
	pArray = jx9_context_new_array(pCtx);
	if( pArray == 0 ){
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Point to the internal representation of the hashmap */
	pMap = (jx9_hashmap *)pArray->x.pOther;
	if( jx9_value_is_json_array(apArg[0])){
		/* Point to the internal representation of the source */
		jx9_hashmap *pSrc = (jx9_hashmap *)apArg[0]->x.pOther;
		/* Perform the copy */
		jx9HashmapDup(pSrc, pMap);
	}else{
		/* Simple insertion */
		jx9HashmapInsert(pMap, 0/* Automatic index assign*/, apArg[0]); 
	}
	/* Return the duplicated array */
	jx9_result_value(pCtx, pArray);
	return JX9_OK;
}
/*
 * bool array_erase(array $source)
 *  Remove all elements from a given array.
 * Parameters
 *  $source
 *   Target array
 * Return
 *  TRUE on success. FALSE otherwise.
 * Note
 *  This function is a symisc eXtension.
 */
static int jx9_hashmap_erase(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	if( nArg < 1 ){
		/* Missing arguments */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	/* Erase */
	jx9HashmapRelease(pMap, FALSE);
	return JX9_OK;
}
/*
 * array array_diff(array $array1, array $array2, ...)
 *  Computes the difference of arrays.
 * Parameters
 *  $array1
 *    The array to compare from
 *  $array2
 *    An array to compare against 
 *  $...
 *   More arrays to compare against
 * Return
 *  Returns an array containing all the entries from array1 that
 *  are not present in any of the other arrays.
 */
static int jx9_hashmap_diff(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap_node *pEntry;
	jx9_hashmap *pSrc, *pMap;
	jx9_value *pArray;
	jx9_value *pVal;
	sxi32 rc;
	sxu32 n;
	int i;
	if( nArg < 1 || !jx9_value_is_json_array(apArg[0]) ){
		/* Missing arguments, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	if( nArg == 1 ){
		/* Return the first array since we cannot perform a diff */
		jx9_result_value(pCtx, apArg[0]);
		return JX9_OK;
	}
	/* Create a new array */
	pArray = jx9_context_new_array(pCtx);
	if( pArray == 0 ){
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Point to the internal representation of the source hashmap */
	pSrc = (jx9_hashmap *)apArg[0]->x.pOther;
	/* Perform the diff */
	pEntry = pSrc->pFirst;
	n = pSrc->nEntry;
	for(;;){
		if( n < 1 ){
			break;
		}
		/* Extract the node value */
		pVal = HashmapExtractNodeValue(pEntry);
		if( pVal ){
			for( i = 1 ; i < nArg ; i++ ){
				if( !jx9_value_is_json_array(apArg[i])) {
					/* ignore */
					continue;
				}
				/* Point to the internal representation of the hashmap */
				pMap = (jx9_hashmap *)apArg[i]->x.pOther;
				/* Perform the lookup */
				rc = HashmapFindValue(pMap, pVal, 0, TRUE);
				if( rc == SXRET_OK ){
					/* Value exist */
					break;
				}
			}
			if( i >= nArg ){
				/* Perform the insertion */
				HashmapInsertNode((jx9_hashmap *)pArray->x.pOther, pEntry, TRUE);
			}
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
		n--;
	}
	/* Return the freshly created array */
	jx9_result_value(pCtx, pArray);
	return JX9_OK;
}
/*
 * array array_intersect(array $array1 , array $array2, ...)
 *  Computes the intersection of arrays.
 * Parameters
 *  $array1
 *    The array to compare from
 *  $array2
 *    An array to compare against 
 *  $...
 *   More arrays to compare against
 * Return
 *  Returns an array containing all of the values in array1 whose values exist
 *  in all of the parameters. .
 * Note that NULL is returned on failure.
 */
static int jx9_hashmap_intersect(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap_node *pEntry;
	jx9_hashmap *pSrc, *pMap;
	jx9_value *pArray;
	jx9_value *pVal;
	sxi32 rc;
	sxu32 n;
	int i;
	if( nArg < 1 || !jx9_value_is_json_array(apArg[0]) ){
		/* Missing arguments, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	if( nArg == 1 ){
		/* Return the first array since we cannot perform a diff */
		jx9_result_value(pCtx, apArg[0]);
		return JX9_OK;
	}
	/* Create a new array */
	pArray = jx9_context_new_array(pCtx);
	if( pArray == 0 ){
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Point to the internal representation of the source hashmap */
	pSrc = (jx9_hashmap *)apArg[0]->x.pOther;
	/* Perform the intersection */
	pEntry = pSrc->pFirst;
	n = pSrc->nEntry;
	for(;;){
		if( n < 1 ){
			break;
		}
		/* Extract the node value */
		pVal = HashmapExtractNodeValue(pEntry);
		if( pVal ){
			for( i = 1 ; i < nArg ; i++ ){
				if( !jx9_value_is_json_array(apArg[i])) {
					/* ignore */
					continue;
				}
				/* Point to the internal representation of the hashmap */
				pMap = (jx9_hashmap *)apArg[i]->x.pOther;
				/* Perform the lookup */
				rc = HashmapFindValue(pMap, pVal, 0, TRUE);
				if( rc != SXRET_OK ){
					/* Value does not exist */
					break;
				}
			}
			if( i >= nArg ){
				/* Perform the insertion */
				HashmapInsertNode((jx9_hashmap *)pArray->x.pOther, pEntry, TRUE);
			}
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
		n--;
	}
	/* Return the freshly created array */
	jx9_result_value(pCtx, pArray);
	return JX9_OK;
}
/*
 * number array_sum(array $array )
 *  Calculate the sum of values in an array.
 * Parameters
 *  $array: The input array.
 * Return
 *  Returns the sum of values as an integer or float.
 */
static void DoubleSum(jx9_context *pCtx, jx9_hashmap *pMap)
{
	jx9_hashmap_node *pEntry;
	jx9_value *pObj;
	double dSum = 0;
	sxu32 n;
	pEntry = pMap->pFirst;
	for( n = 0 ; n < pMap->nEntry ; n++ ){
		pObj = HashmapExtractNodeValue(pEntry);
		if( pObj && (pObj->iFlags & (MEMOBJ_NULL|MEMOBJ_HASHMAP|MEMOBJ_RES)) == 0){
			if( pObj->iFlags & MEMOBJ_REAL ){
				dSum += pObj->x.rVal;
			}else if( pObj->iFlags & (MEMOBJ_INT|MEMOBJ_BOOL) ){
				dSum += (double)pObj->x.iVal;
			}else if( pObj->iFlags & MEMOBJ_STRING ){
				if( SyBlobLength(&pObj->sBlob) > 0 ){
					double dv = 0;
					SyStrToReal((const char *)SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob), (void *)&dv, 0);
					dSum += dv;
				}
			}
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
	}
	/* Return sum */
	jx9_result_double(pCtx, dSum);
}
static void Int64Sum(jx9_context *pCtx, jx9_hashmap *pMap)
{
	jx9_hashmap_node *pEntry;
	jx9_value *pObj;
	sxi64 nSum = 0;
	sxu32 n;
	pEntry = pMap->pFirst;
	for( n = 0 ; n < pMap->nEntry ; n++ ){
		pObj = HashmapExtractNodeValue(pEntry);
		if( pObj && (pObj->iFlags & (MEMOBJ_NULL|MEMOBJ_HASHMAP|MEMOBJ_RES)) == 0){
			if( pObj->iFlags & MEMOBJ_REAL ){
				nSum += (sxi64)pObj->x.rVal;
			}else if( pObj->iFlags & (MEMOBJ_INT|MEMOBJ_BOOL) ){
				nSum += pObj->x.iVal;
			}else if( pObj->iFlags & MEMOBJ_STRING ){
				if( SyBlobLength(&pObj->sBlob) > 0 ){
					sxi64 nv = 0;
					SyStrToInt64((const char *)SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob), (void *)&nv, 0);
					nSum += nv;
				}
			}
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
	}
	/* Return sum */
	jx9_result_int64(pCtx, nSum);
}
/* number array_sum(array $array ) 
 * (See block-coment above)
 */
static int jx9_hashmap_sum(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	jx9_value *pObj;
	if( nArg < 1 ){
		/* Missing arguments, return 0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return 0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	if( pMap->nEntry < 1 ){
		/* Nothing to compute, return 0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* If the first element is of type float, then perform floating
	 * point computaion.Otherwise switch to int64 computaion.
	 */
	pObj = HashmapExtractNodeValue(pMap->pFirst);
	if( pObj == 0 ){
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	if( pObj->iFlags & MEMOBJ_REAL ){
		DoubleSum(pCtx, pMap);
	}else{
		Int64Sum(pCtx, pMap);
	}
	return JX9_OK;
}
/*
 * number array_product(array $array )
 *  Calculate the product of values in an array.
 * Parameters
 *  $array: The input array.
 * Return
 *  Returns the product of values as an integer or float.
 */
static void DoubleProd(jx9_context *pCtx, jx9_hashmap *pMap)
{
	jx9_hashmap_node *pEntry;
	jx9_value *pObj;
	double dProd;
	sxu32 n;
	pEntry = pMap->pFirst;
	dProd = 1;
	for( n = 0 ; n < pMap->nEntry ; n++ ){
		pObj = HashmapExtractNodeValue(pEntry);
		if( pObj && (pObj->iFlags & (MEMOBJ_NULL|MEMOBJ_HASHMAP|MEMOBJ_RES)) == 0){
			if( pObj->iFlags & MEMOBJ_REAL ){
				dProd *= pObj->x.rVal;
			}else if( pObj->iFlags & (MEMOBJ_INT|MEMOBJ_BOOL) ){
				dProd *= (double)pObj->x.iVal;
			}else if( pObj->iFlags & MEMOBJ_STRING ){
				if( SyBlobLength(&pObj->sBlob) > 0 ){
					double dv = 0;
					SyStrToReal((const char *)SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob), (void *)&dv, 0);
					dProd *= dv;
				}
			}
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
	}
	/* Return product */
	jx9_result_double(pCtx, dProd);
}
static void Int64Prod(jx9_context *pCtx, jx9_hashmap *pMap)
{
	jx9_hashmap_node *pEntry;
	jx9_value *pObj;
	sxi64 nProd;
	sxu32 n;
	pEntry = pMap->pFirst;
	nProd = 1;
	for( n = 0 ; n < pMap->nEntry ; n++ ){
		pObj = HashmapExtractNodeValue(pEntry);
		if( pObj && (pObj->iFlags & (MEMOBJ_NULL|MEMOBJ_HASHMAP|MEMOBJ_RES)) == 0){
			if( pObj->iFlags & MEMOBJ_REAL ){
				nProd *= (sxi64)pObj->x.rVal;
			}else if( pObj->iFlags & (MEMOBJ_INT|MEMOBJ_BOOL) ){
				nProd *= pObj->x.iVal;
			}else if( pObj->iFlags & MEMOBJ_STRING ){
				if( SyBlobLength(&pObj->sBlob) > 0 ){
					sxi64 nv = 0;
					SyStrToInt64((const char *)SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob), (void *)&nv, 0);
					nProd *= nv;
				}
			}
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
	}
	/* Return product */
	jx9_result_int64(pCtx, nProd);
}
/* number array_product(array $array )
 * (See block-block comment above)
 */
static int jx9_hashmap_product(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_hashmap *pMap;
	jx9_value *pObj;
	if( nArg < 1 ){
		/* Missing arguments, return 0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid argument, return 0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	if( pMap->nEntry < 1 ){
		/* Nothing to compute, return 0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* If the first element is of type float, then perform floating
	 * point computaion.Otherwise switch to int64 computaion.
	 */
	pObj = HashmapExtractNodeValue(pMap->pFirst);
	if( pObj == 0 ){
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	if( pObj->iFlags & MEMOBJ_REAL ){
		DoubleProd(pCtx, pMap);
	}else{
		Int64Prod(pCtx, pMap);
	}
	return JX9_OK;
}
/*
 * array array_map(callback $callback, array $arr1)
 *  Applies the callback to the elements of the given arrays.
 * Parameters
 *  $callback
 *   Callback function to run for each element in each array.
 * $arr1
 *   An array to run through the callback function.
 * Return
 *  Returns an array containing all the elements of arr1 after applying
 *  the callback function to each one. 
 * NOTE:
 *  array_map() passes only a single value to the callback. 
 */
static int jx9_hashmap_map(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_value *pArray, *pValue, sKey, sResult;
	jx9_hashmap_node *pEntry;
	jx9_hashmap *pMap;
	sxu32 n;
	if( nArg < 2 || !jx9_value_is_json_array(apArg[1]) ){
		/* Invalid arguments, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Create a new array */
	pArray = jx9_context_new_array(pCtx);
	if( pArray == 0 ){
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Point to the internal representation of the input hashmap */
	pMap = (jx9_hashmap *)apArg[1]->x.pOther;
	jx9MemObjInit(pMap->pVm, &sResult);
	jx9MemObjInit(pMap->pVm, &sKey);
	/* Perform the requested operation */
	pEntry = pMap->pFirst;
	for( n = 0 ; n < pMap->nEntry ; n++ ){
		/* Extrcat the node value */
		pValue = HashmapExtractNodeValue(pEntry);
		if( pValue ){
			sxi32 rc;
			/* Invoke the supplied callback */
			rc = jx9VmCallUserFunction(pMap->pVm, apArg[0], 1, &pValue, &sResult);
			/* Extract the node key */
			jx9HashmapExtractNodeKey(pEntry, &sKey);
			if( rc != SXRET_OK ){
				/* An error occured while invoking the supplied callback [i.e: not defined] */
				jx9_array_add_elem(pArray, &sKey, pValue); /* Keep the same value */
			}else{
				/* Insert the callback return value */
				jx9_array_add_elem(pArray, &sKey, &sResult);
			}
			jx9MemObjRelease(&sKey);
			jx9MemObjRelease(&sResult);
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
	}
	jx9_result_value(pCtx, pArray);
	return JX9_OK;
}
/*
 * bool array_walk(array &$array, callback $funcname [, value $userdata ] )
 *  Apply a user function to every member of an array.
 * Parameters
 *  $array
 *   The input array.
 * $funcname
 *  Typically, funcname takes on two parameters.The array parameter's value being
 *  the first, and the key/index second.
 * Note:
 *  If funcname needs to be working with the actual values of the array, specify the first
 *  parameter of funcname as a reference. Then, any changes made to those elements will 
 *  be made in the original array itself.
 * $userdata
 *  If the optional userdata parameter is supplied, it will be passed as the third parameter
 *  to the callback funcname.
 * Return
 *  Returns TRUE on success or FALSE on failure.
 */
static int jx9_hashmap_walk(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_value *pValue, *pUserData, sKey;
	jx9_hashmap_node *pEntry;
	jx9_hashmap *pMap;
	sxi32 rc;
	sxu32 n;
	if( nArg < 2 || !jx9_value_is_json_array(apArg[0]) ){
		/* Invalid/Missing arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	pUserData = nArg > 2 ? apArg[2] : 0;
	/* Point to the internal representation of the input hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	jx9MemObjInit(pMap->pVm, &sKey);
	/* Perform the desired operation */
	pEntry = pMap->pFirst;
	for( n = 0 ; n < pMap->nEntry ; n++ ){
		/* Extract the node value */
		pValue = HashmapExtractNodeValue(pEntry);
		if( pValue ){
			/* Extract the entry key */
			jx9HashmapExtractNodeKey(pEntry, &sKey);
			/* Invoke the supplied callback */
			rc = jx9VmCallUserFunctionAp(pMap->pVm, apArg[1], 0, pValue, &sKey, pUserData, 0);
			jx9MemObjRelease(&sKey);
			if( rc != SXRET_OK ){
				/* An error occured while invoking the supplied callback [i.e: not defined] */
				jx9_result_bool(pCtx, 0); /* return FALSE */
				return JX9_OK;
			}
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
	}
	/* All done, return TRUE */
	jx9_result_bool(pCtx, 1);
	return JX9_OK;
}
/*
 * Table of built-in hashmap functions.
 */
static const jx9_builtin_func aHashmapFunc[] = {
	{"count",             jx9_hashmap_count }, 
	{"sizeof",            jx9_hashmap_count }, 
	{"array_key_exists",  jx9_hashmap_key_exists }, 
	{"array_pop",         jx9_hashmap_pop     }, 
	{"array_push",        jx9_hashmap_push    }, 
	{"array_shift",       jx9_hashmap_shift   }, 
	{"array_product",     jx9_hashmap_product }, 
	{"array_sum",         jx9_hashmap_sum     }, 
	{"array_values",      jx9_hashmap_values  }, 
	{"array_same",        jx9_hashmap_same    },
	{"array_merge",       jx9_hashmap_merge   }, 
	{"array_diff",        jx9_hashmap_diff    }, 
	{"array_intersect",   jx9_hashmap_intersect}, 
	{"in_array",          jx9_hashmap_in_array },
	{"array_copy",        jx9_hashmap_copy    }, 
	{"array_erase",       jx9_hashmap_erase   }, 
	{"array_map",         jx9_hashmap_map     }, 
	{"array_walk",        jx9_hashmap_walk    }, 
	{"sort",              jx9_hashmap_sort    }, 
	{"rsort",             jx9_hashmap_rsort   }, 
	{"usort",             jx9_hashmap_usort   }, 
	{"current",           jx9_hashmap_current }, 
	{"each",              jx9_hashmap_each    }, 
	{"pos",               jx9_hashmap_current }, 
	{"next",              jx9_hashmap_next    }, 
	{"prev",              jx9_hashmap_prev    }, 
	{"end",               jx9_hashmap_end     }, 
	{"reset",             jx9_hashmap_reset   }, 
	{"key",               jx9_hashmap_simple_key }
};
/*
 * Register the built-in hashmap functions defined above.
 */
JX9_PRIVATE void jx9RegisterHashmapFunctions(jx9_vm *pVm)
{
	sxu32 n;
	for( n = 0 ; n < SX_ARRAYSIZE(aHashmapFunc) ; n++ ){
		jx9_create_function(&(*pVm), aHashmapFunc[n].zName, aHashmapFunc[n].xFunc, 0);
	}
}
/*
 * Iterate throw hashmap entries and invoke the given callback [i.e: xWalk()] for each 
 * retrieved entry.
 * Note that argument are passed to the callback by copy. That is, any modification to 
 * the entry value in the callback body will not alter the real value.
 * If the callback wishes to abort processing [i.e: it's invocation] it must return
 * a value different from JX9_OK.
 * Refer to [jx9_array_walk()] for more information.
 */
JX9_PRIVATE sxi32 jx9HashmapWalk(
	jx9_hashmap *pMap, /* Target hashmap */
	int (*xWalk)(jx9_value *, jx9_value *, void *), /* Walker callback */
	void *pUserData /* Last argument to xWalk() */
	)
{
	jx9_hashmap_node *pEntry;
	jx9_value sKey, sValue;
	sxi32 rc;
	sxu32 n;
	/* Initialize walker parameter */
	rc = SXRET_OK;
	jx9MemObjInit(pMap->pVm, &sKey);
	jx9MemObjInit(pMap->pVm, &sValue);
	n = pMap->nEntry;
	pEntry = pMap->pFirst;
	/* Start the iteration process */
	for(;;){
		if( n < 1 ){
			break;
		}
		/* Extract a copy of the key and a copy the current value */
		jx9HashmapExtractNodeKey(pEntry, &sKey);
		jx9HashmapExtractNodeValue(pEntry, &sValue, FALSE);
		/* Invoke the user callback */
		rc = xWalk(&sKey, &sValue, pUserData);
		/* Release the copy of the key and the value */
		jx9MemObjRelease(&sKey);
		jx9MemObjRelease(&sValue);
		if( rc != JX9_OK ){
			/* Callback request an operation abort */
			return SXERR_ABORT;
		}
		/* Point to the next entry */
		pEntry = pEntry->pPrev; /* Reverse link */
		n--;
	}
	/* All done */
	return SXRET_OK;
}
