/*
 * Symisc unQLite: An Embeddable NoSQL (Post Modern) Database Engine.
 * Copyright (C) 2012-2013, Symisc Systems http://unqlite.org/
 * Version 1.1.6
 * For information on licensing, redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES
 * please contact Symisc Systems via:
 *       legal@symisc.net
 *       licensing@symisc.net
 *       contact@symisc.net
 * or visit:
 *      http://unqlite.org/licensing.html
 */
 /* $SymiscID: mem_kv.c v1.7 Win7 2012-11-28 01:41 stable <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif
/* 
 * This file implements an in-memory key value storage engine for unQLite.
 * Note that this storage engine does not support transactions.
 *
 * Normaly, I (chm@symisc.net) planned to implement a red-black tree
 * which is suitable for this kind of operation, but due to the lack
 * of time, I decided to implement a tunned hashtable which everybody
 * know works very well for this kind of operation.
 * Again, I insist on a red-black tree implementation for future version
 * of Unqlite.
 */
/* Forward declaration */
typedef struct mem_hash_kv_engine mem_hash_kv_engine;
/*
 * Each record is storead in an instance of the following structure.
 */
typedef struct mem_hash_record mem_hash_record;
struct mem_hash_record
{
	mem_hash_kv_engine *pEngine;    /* Storage engine */
	sxu32 nHash;                    /* Hash of the key */
	const void *pKey;               /* Key */
	sxu32 nKeyLen;                  /* Key size (Max 1GB) */
	const void *pData;              /* Data */
	sxu32 nDataLen;                 /* Data length (Max 4GB) */
	mem_hash_record *pNext,*pPrev;  /* Link to other records */
	mem_hash_record *pNextHash,*pPrevHash; /* Collision link */
};
/*
 * Each in-memory KV engine is represented by an instance
 * of the following structure.
 */
struct mem_hash_kv_engine
{
	const unqlite_kv_io *pIo; /* IO methods: MUST be first */
	/* Private data */
	SyMemBackend sAlloc;        /* Private memory allocator */
	ProcHash    xHash;          /* Default hash function */
	ProcCmp     xCmp;           /* Default comparison function */
	sxu32 nRecord;              /* Total number of records  */
	sxu32 nBucket;              /* Bucket size: Must be a power of two */
	mem_hash_record **apBucket; /* Hash bucket */
	mem_hash_record *pFirst;    /* First inserted entry */
	mem_hash_record *pLast;     /* Last inserted entry */
};
/*
 * Allocate a new hash record.
 */
static mem_hash_record * MemHashNewRecord(
	mem_hash_kv_engine *pEngine,
	const void *pKey,int nKey,
	const void *pData,unqlite_int64 nData,
	sxu32 nHash
	)
{
	SyMemBackend *pAlloc = &pEngine->sAlloc;
	mem_hash_record *pRecord;
	void *pDupData;
	sxu32 nByte;
	char *zPtr;
	
	/* Total number of bytes to alloc */
	nByte = sizeof(mem_hash_record) + nKey;
	/* Allocate a new instance */
	pRecord = (mem_hash_record *)SyMemBackendAlloc(pAlloc,nByte);
	if( pRecord == 0 ){
		return 0;
	}
	pDupData = (void *)SyMemBackendAlloc(pAlloc,(sxu32)nData);
	if( pDupData == 0 ){
		SyMemBackendFree(pAlloc,pRecord);
		return 0;
	}
	zPtr = (char *)pRecord;
	zPtr += sizeof(mem_hash_record);
	/* Zero the structure */
	SyZero(pRecord,sizeof(mem_hash_record));
	/* Fill in the structure */
	pRecord->pEngine = pEngine;
	pRecord->nDataLen = (sxu32)nData;
	pRecord->nKeyLen = (sxu32)nKey;
	pRecord->nHash = nHash;
	SyMemcpy(pKey,zPtr,pRecord->nKeyLen);
	pRecord->pKey = (const void *)zPtr;
	SyMemcpy(pData,pDupData,pRecord->nDataLen);
	pRecord->pData = pDupData;
	/* All done */
	return pRecord;
}
/*
 * Install a given record in the hashtable.
 */
static void MemHashLinkRecord(mem_hash_kv_engine *pEngine,mem_hash_record *pRecord)
{
	sxu32 nBucket = pRecord->nHash & (pEngine->nBucket - 1);
	pRecord->pNextHash = pEngine->apBucket[nBucket];
	if( pEngine->apBucket[nBucket] ){
		pEngine->apBucket[nBucket]->pPrevHash = pRecord;
	}
	pEngine->apBucket[nBucket] = pRecord;
	if( pEngine->pFirst == 0 ){
		pEngine->pFirst = pEngine->pLast = pRecord;
	}else{
		MACRO_LD_PUSH(pEngine->pLast,pRecord);
	}
	pEngine->nRecord++;
}
/*
 * Unlink a given record from the hashtable.
 */
static void MemHashUnlinkRecord(mem_hash_kv_engine *pEngine,mem_hash_record *pEntry)
{
	sxu32 nBucket = pEntry->nHash & (pEngine->nBucket - 1);
	SyMemBackend *pAlloc = &pEngine->sAlloc;
	if( pEntry->pPrevHash == 0 ){
		pEngine->apBucket[nBucket] = pEntry->pNextHash;
	}else{
		pEntry->pPrevHash->pNextHash = pEntry->pNextHash;
	}
	if( pEntry->pNextHash ){
		pEntry->pNextHash->pPrevHash = pEntry->pPrevHash;
	}
	MACRO_LD_REMOVE(pEngine->pLast,pEntry);
	if( pEntry == pEngine->pFirst ){
		pEngine->pFirst = pEntry->pPrev;
	}
	pEngine->nRecord--;
	/* Release the entry */
	SyMemBackendFree(pAlloc,(void *)pEntry->pData);
	SyMemBackendFree(pAlloc,pEntry); /* Key is also stored here */
}
/*
 * Perform a lookup for a given entry.
 */
static mem_hash_record * MemHashGetEntry(
	mem_hash_kv_engine *pEngine,
	const void *pKey,int nKeyLen
	)
{
	mem_hash_record *pEntry;
	sxu32 nHash,nBucket;
	/* Hash the entry */
	nHash = pEngine->xHash(pKey,(sxu32)nKeyLen);
	nBucket = nHash & (pEngine->nBucket - 1);
	pEntry = pEngine->apBucket[nBucket];
	for(;;){
		if( pEntry == 0 ){
			break;
		}
		if( pEntry->nHash == nHash && pEntry->nKeyLen == (sxu32)nKeyLen && 
			pEngine->xCmp(pEntry->pKey,pKey,pEntry->nKeyLen) == 0 ){
				return pEntry;
		}
		pEntry = pEntry->pNextHash;
	}
	/* No such entry */
	return 0;
}
/*
 * Rehash all the entries in the given table.
 */
static int MemHashGrowTable(mem_hash_kv_engine *pEngine)
{
	sxu32 nNewSize = pEngine->nBucket << 1;
	mem_hash_record *pEntry;
	mem_hash_record **apNew;
	sxu32 n,iBucket;
	/* Allocate a new larger table */
	apNew = (mem_hash_record **)SyMemBackendAlloc(&pEngine->sAlloc, nNewSize * sizeof(mem_hash_record *));
	if( apNew == 0 ){
		/* Not so fatal, simply a performance hit */
		return UNQLITE_OK;
	}
	/* Zero the new table */
	SyZero((void *)apNew, nNewSize * sizeof(mem_hash_record *));
	/* Rehash all entries */
	n = 0;
	pEntry = pEngine->pLast;
	for(;;){
		
		/* Loop one */
		if( n >= pEngine->nRecord ){
			break;
		}
		pEntry->pNextHash = pEntry->pPrevHash = 0;
		/* Install in the new bucket */
		iBucket = pEntry->nHash & (nNewSize - 1);
		pEntry->pNextHash = apNew[iBucket];
		if( apNew[iBucket] ){
			apNew[iBucket]->pPrevHash = pEntry;
		}
		apNew[iBucket] = pEntry;
		/* Point to the next entry */
		pEntry = pEntry->pNext;
		n++;

		/* Loop two */
		if( n >= pEngine->nRecord ){
			break;
		}
		pEntry->pNextHash = pEntry->pPrevHash = 0;
		/* Install in the new bucket */
		iBucket = pEntry->nHash & (nNewSize - 1);
		pEntry->pNextHash = apNew[iBucket];
		if( apNew[iBucket] ){
			apNew[iBucket]->pPrevHash = pEntry;
		}
		apNew[iBucket] = pEntry;
		/* Point to the next entry */
		pEntry = pEntry->pNext;
		n++;

		/* Loop three */
		if( n >= pEngine->nRecord ){
			break;
		}
		pEntry->pNextHash = pEntry->pPrevHash = 0;
		/* Install in the new bucket */
		iBucket = pEntry->nHash & (nNewSize - 1);
		pEntry->pNextHash = apNew[iBucket];
		if( apNew[iBucket] ){
			apNew[iBucket]->pPrevHash = pEntry;
		}
		apNew[iBucket] = pEntry;
		/* Point to the next entry */
		pEntry = pEntry->pNext;
		n++;

		/* Loop four */
		if( n >= pEngine->nRecord ){
			break;
		}
		pEntry->pNextHash = pEntry->pPrevHash = 0;
		/* Install in the new bucket */
		iBucket = pEntry->nHash & (nNewSize - 1);
		pEntry->pNextHash = apNew[iBucket];
		if( apNew[iBucket] ){
			apNew[iBucket]->pPrevHash = pEntry;
		}
		apNew[iBucket] = pEntry;
		/* Point to the next entry */
		pEntry = pEntry->pNext;
		n++;
	}
	/* Release the old table and reflect the change */
	SyMemBackendFree(&pEngine->sAlloc,(void *)pEngine->apBucket);
	pEngine->apBucket = apNew;
	pEngine->nBucket  = nNewSize;
	return UNQLITE_OK;
}
/*
 * Exported Interfaces.
 */
/*
 * Each public cursor is identified by an instance of this structure.
 */
typedef struct mem_hash_cursor mem_hash_cursor;
struct mem_hash_cursor
{
	unqlite_kv_engine *pStore; /* Must be first */
	/* Private fields */
	mem_hash_record *pCur;     /* Current hash record */
};
/*
 * Initialize the cursor.
 */
static void MemHashInitCursor(unqlite_kv_cursor *pCursor)
{
	 mem_hash_kv_engine *pEngine = (mem_hash_kv_engine *)pCursor->pStore;
	 mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	 /* Point to the first inserted entry */
	 pMem->pCur = pEngine->pFirst;
}
/*
 * Point to the first entry.
 */
static int MemHashCursorFirst(unqlite_kv_cursor *pCursor)
{
	 mem_hash_kv_engine *pEngine = (mem_hash_kv_engine *)pCursor->pStore;
	 mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	 pMem->pCur = pEngine->pFirst;
	 return UNQLITE_OK;
}
/*
 * Point to the last entry.
 */
static int MemHashCursorLast(unqlite_kv_cursor *pCursor)
{
	 mem_hash_kv_engine *pEngine = (mem_hash_kv_engine *)pCursor->pStore;
	 mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	 pMem->pCur = pEngine->pLast;
	 return UNQLITE_OK;
}
/*
 * is a Valid Cursor.
 */
static int MemHashCursorValid(unqlite_kv_cursor *pCursor)
{
	 mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	 return pMem->pCur != 0 ? 1 : 0;
}
/*
 * Point to the next entry.
 */
static int MemHashCursorNext(unqlite_kv_cursor *pCursor)
{
	 mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	 if( pMem->pCur == 0){
		 return UNQLITE_EOF;
	 }
	 pMem->pCur = pMem->pCur->pPrev; /* Reverse link: Not a Bug */
	 return UNQLITE_OK;
}
/*
 * Point to the previous entry.
 */
static int MemHashCursorPrev(unqlite_kv_cursor *pCursor)
{
	 mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	 if( pMem->pCur == 0){
		 return UNQLITE_EOF;
	 }
	 pMem->pCur = pMem->pCur->pNext; /* Reverse link: Not a Bug */
	 return UNQLITE_OK;
}
/*
 * Return key length.
 */
static int MemHashCursorKeyLength(unqlite_kv_cursor *pCursor,int *pLen)
{
	mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	if( pMem->pCur == 0){
		 return UNQLITE_EOF;
	}
	*pLen = (int)pMem->pCur->nKeyLen;
	return UNQLITE_OK;
}
/*
 * Return data length.
 */
static int MemHashCursorDataLength(unqlite_kv_cursor *pCursor,unqlite_int64 *pLen)
{
	mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	if( pMem->pCur == 0 ){
		 return UNQLITE_EOF;
	}
	*pLen = pMem->pCur->nDataLen;
	return UNQLITE_OK;
}
/*
 * Consume the key.
 */
static int MemHashCursorKey(unqlite_kv_cursor *pCursor,int (*xConsumer)(const void *,unsigned int,void *),void *pUserData)
{
	mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	int rc;
	if( pMem->pCur == 0){
		 return UNQLITE_EOF;
	}
	/* Invoke the callback */
	rc = xConsumer(pMem->pCur->pKey,pMem->pCur->nKeyLen,pUserData);
	/* Callback result */
	return rc;
}
/*
 * Consume the data.
 */
static int MemHashCursorData(unqlite_kv_cursor *pCursor,int (*xConsumer)(const void *,unsigned int,void *),void *pUserData)
{
	mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	int rc;
	if( pMem->pCur == 0){
		 return UNQLITE_EOF;
	}
	/* Invoke the callback */
	rc = xConsumer(pMem->pCur->pData,pMem->pCur->nDataLen,pUserData);
	/* Callback result */
	return rc;
}
/*
 * Reset the cursor.
 */
static void MemHashCursorReset(unqlite_kv_cursor *pCursor)
{
	mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	pMem->pCur = ((mem_hash_kv_engine *)pCursor->pStore)->pFirst;
}
/*
 * Remove a particular record.
 */
static int MemHashCursorDelete(unqlite_kv_cursor *pCursor)
{
	mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	mem_hash_record *pNext;
	if( pMem->pCur == 0 ){
		/* Cursor does not point to anything */
		return UNQLITE_NOTFOUND;
	}
	pNext = pMem->pCur->pPrev;
	/* Perform the deletion */
	MemHashUnlinkRecord(pMem->pCur->pEngine,pMem->pCur);
	/* Point to the next entry */
	pMem->pCur = pNext;
	return UNQLITE_OK;
}
/*
 * Find a particular record.
 */
static int MemHashCursorSeek(unqlite_kv_cursor *pCursor,const void *pKey,int nByte,int iPos)
{
	mem_hash_kv_engine *pEngine = (mem_hash_kv_engine *)pCursor->pStore;
	mem_hash_cursor *pMem = (mem_hash_cursor *)pCursor;
	/* Perform the lookup */
	pMem->pCur = MemHashGetEntry(pEngine,pKey,nByte);
	if( pMem->pCur == 0 ){
		if( iPos != UNQLITE_CURSOR_MATCH_EXACT ){
			/* noop; */
		}
		/* No such record */
		return UNQLITE_NOTFOUND;
	}
	return UNQLITE_OK;
}
/*
 * Builtin hash function.
 */
static sxu32 MemHashFunc(const void *pSrc,sxu32 nLen)
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
/* Default bucket size */
#define MEM_HASH_BUCKET_SIZE 64
/* Default fill factor */
#define MEM_HASH_FILL_FACTOR 4 /* or 3 */
/*
 * Initialize the in-memory storage engine.
 */
static int MemHashInit(unqlite_kv_engine *pKvEngine,int iPageSize)
{
	mem_hash_kv_engine *pEngine = (mem_hash_kv_engine *)pKvEngine;
	/* Note that this instance is already zeroed */	
	/* Memory backend */
	SyMemBackendInitFromParent(&pEngine->sAlloc,unqliteExportMemBackend());
//#if defined(UNQLITE_ENABLE_THREADS)
//	/* Already protected by the upper layers */
//	SyMemBackendDisbaleMutexing(&pEngine->sAlloc);
//#endif
	/* Default hash & comparison function */
	pEngine->xHash = MemHashFunc;
	pEngine->xCmp = SyMemcmp;
	/* Allocate a new bucket */
	pEngine->apBucket = (mem_hash_record **)SyMemBackendAlloc(&pEngine->sAlloc,MEM_HASH_BUCKET_SIZE * sizeof(mem_hash_record *));
	if( pEngine->apBucket == 0 ){
		SXUNUSED(iPageSize); /* cc warning */
		return UNQLITE_NOMEM;
	}
	/* Zero the bucket */
	SyZero(pEngine->apBucket,MEM_HASH_BUCKET_SIZE * sizeof(mem_hash_record *));
	pEngine->nRecord = 0;
	pEngine->nBucket = MEM_HASH_BUCKET_SIZE;
	return UNQLITE_OK;
}
/*
 * Release the in-memory storage engine.
 */
static void MemHashRelease(unqlite_kv_engine *pKvEngine)
{
	mem_hash_kv_engine *pEngine = (mem_hash_kv_engine *)pKvEngine;
	/* Release the private memory backend */
	SyMemBackendRelease(&pEngine->sAlloc);
}
/*
 * Configure the in-memory storage engine.
 */
static int MemHashConfigure(unqlite_kv_engine *pKvEngine,int iOp,va_list ap)
{
	mem_hash_kv_engine *pEngine = (mem_hash_kv_engine *)pKvEngine;
	int rc = UNQLITE_OK;
	switch(iOp){
	case UNQLITE_KV_CONFIG_HASH_FUNC:{
		/* Use a default hash function */
		if( pEngine->nRecord > 0 ){
			rc = UNQLITE_LOCKED;
		}else{
			ProcHash xHash = va_arg(ap,ProcHash);
			if( xHash ){
				pEngine->xHash = xHash;
			}
		}
		break;
									 }
	case UNQLITE_KV_CONFIG_CMP_FUNC: {
		/* Default comparison function */
		ProcCmp xCmp = va_arg(ap,ProcCmp);
		if( xCmp ){
			pEngine->xCmp = xCmp;
		}
		break;
									 }
	default:
		/* Unknown configuration option */
		rc = UNQLITE_UNKNOWN;
	}
	return rc;
}
/*
 * Replace method.
 */
static int MemHashReplace(
	  unqlite_kv_engine *pKv,
	  const void *pKey,int nKeyLen,
	  const void *pData,unqlite_int64 nDataLen
	  )
{
	mem_hash_kv_engine *pEngine = (mem_hash_kv_engine *)pKv;
	mem_hash_record *pRecord;
	if( nDataLen > SXU32_HIGH ){
		/* Database limit */
		pEngine->pIo->xErr(pEngine->pIo->pHandle,"Record size limit reached");
		return UNQLITE_LIMIT;
	}
	/* Fetch the record first */
	pRecord = MemHashGetEntry(pEngine,pKey,nKeyLen);
	if( pRecord == 0 ){
		/* Allocate a new record */
		pRecord = MemHashNewRecord(pEngine,
			pKey,nKeyLen,
			pData,nDataLen,
			pEngine->xHash(pKey,nKeyLen)
			);
		if( pRecord == 0 ){
			return UNQLITE_NOMEM;
		}
		/* Link the entry */
		MemHashLinkRecord(pEngine,pRecord);
		if( (pEngine->nRecord >= pEngine->nBucket * MEM_HASH_FILL_FACTOR) && pEngine->nRecord < 100000 ){
			/* Rehash the table */
			MemHashGrowTable(pEngine);
		}
	}else{
		sxu32 nData = (sxu32)nDataLen;
		void *pNew;
		/* Replace an existing record */
		if( nData == pRecord->nDataLen ){
			/* No need to free the old chunk */
			pNew = (void *)pRecord->pData;
		}else{
			pNew = SyMemBackendAlloc(&pEngine->sAlloc,nData);
			if( pNew == 0 ){
				return UNQLITE_NOMEM;
			}
			/* Release the old data */
			SyMemBackendFree(&pEngine->sAlloc,(void *)pRecord->pData);
		}
		/* Reflect the change */
		pRecord->nDataLen = nData;
		SyMemcpy(pData,pNew,nData);
		pRecord->pData = pNew;
	}
	return UNQLITE_OK;
}
/*
 * Append method.
 */
static int MemHashAppend(
	  unqlite_kv_engine *pKv,
	  const void *pKey,int nKeyLen,
	  const void *pData,unqlite_int64 nDataLen
	  )
{
	mem_hash_kv_engine *pEngine = (mem_hash_kv_engine *)pKv;
	mem_hash_record *pRecord;
	if( nDataLen > SXU32_HIGH ){
		/* Database limit */
		pEngine->pIo->xErr(pEngine->pIo->pHandle,"Record size limit reached");
		return UNQLITE_LIMIT;
	}
	/* Fetch the record first */
	pRecord = MemHashGetEntry(pEngine,pKey,nKeyLen);
	if( pRecord == 0 ){
		/* Allocate a new record */
		pRecord = MemHashNewRecord(pEngine,
			pKey,nKeyLen,
			pData,nDataLen,
			pEngine->xHash(pKey,nKeyLen)
			);
		if( pRecord == 0 ){
			return UNQLITE_NOMEM;
		}
		/* Link the entry */
		MemHashLinkRecord(pEngine,pRecord);
		if( pEngine->nRecord * MEM_HASH_FILL_FACTOR >= pEngine->nBucket && pEngine->nRecord < 100000 ){
			/* Rehash the table */
			MemHashGrowTable(pEngine);
		}
	}else{
		unqlite_int64 nNew = pRecord->nDataLen + nDataLen;
		void *pOld = (void *)pRecord->pData;
		sxu32 nData;
		char *zNew;
		/* Append data to the existing record */
		if( nNew > SXU32_HIGH ){
			/* Overflow */
			pEngine->pIo->xErr(pEngine->pIo->pHandle,"Append operation will cause data overflow");	
			return UNQLITE_LIMIT;
		}
		nData = (sxu32)nNew;
		/* Allocate bigger chunk */
		zNew = (char *)SyMemBackendRealloc(&pEngine->sAlloc,pOld,nData);
		if( zNew == 0 ){
			return UNQLITE_NOMEM;
		}
		/* Reflect the change */
		SyMemcpy(pData,&zNew[pRecord->nDataLen],(sxu32)nDataLen);
		pRecord->pData = (const void *)zNew;
		pRecord->nDataLen = nData;
	}
	return UNQLITE_OK;
}
/*
 * Export the in-memory storage engine.
 */
UNQLITE_PRIVATE const unqlite_kv_methods * unqliteExportMemKvStorage(void)
{
	static const unqlite_kv_methods sMemStore = {
		"mem",                      /* zName */
		sizeof(mem_hash_kv_engine), /* szKv */
		sizeof(mem_hash_cursor),    /* szCursor */
		1,                          /* iVersion */
		MemHashInit,                /* xInit */
		MemHashRelease,             /* xRelease */
		MemHashConfigure,           /* xConfig */
		0,                          /* xOpen */
		MemHashReplace,             /* xReplace */
		MemHashAppend,              /* xAppend */
		MemHashInitCursor,          /* xCursorInit */
		MemHashCursorSeek,          /* xSeek */
		MemHashCursorFirst,         /* xFirst */
		MemHashCursorLast,          /* xLast */
		MemHashCursorValid,         /* xValid */
		MemHashCursorNext,          /* xNext */
		MemHashCursorPrev,          /* xPrev */
		MemHashCursorDelete,        /* xDelete */
		MemHashCursorKeyLength,     /* xKeyLength */
		MemHashCursorKey,           /* xKey */
		MemHashCursorDataLength,    /* xDataLength */
		MemHashCursorData,          /* xData */
		MemHashCursorReset,         /* xReset */
		0        /* xRelease */                        
	};
	return &sMemStore;
}
