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
 /* $SymiscID: unqlite_vm.c v1.0 Win7 2013-01-29 23:37 stable <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif
/* This file deals with low level stuff related to the unQLite Virtual Machine */

/* Record ID as a hash value */
#define COL_RECORD_HASH(RID) (RID)
/*
 * Fetch a record from a given collection.
 */
static unqlite_col_record * CollectionCacheFetchRecord(
	unqlite_col *pCol, /* Target collection */
	jx9_int64 nId      /* Unique record ID */
	)
{
	unqlite_col_record *pEntry;
	if( pCol->nRec < 1 ){
		/* Don't bother hashing */
		return 0;
	}
	pEntry = pCol->apRecord[COL_RECORD_HASH(nId) & (pCol->nRecSize - 1)];
	for(;;){
		if( pEntry == 0 ){
			break;
		}
		if( pEntry->nId == nId ){
			/* Record found */
			return pEntry;
		}
		/* Point to the next entry */
		pEntry = pEntry->pNextCol;

	}
	/* No such record */
	return 0;
}
/*
 * Install a freshly created record in a given collection. 
 */
static int CollectionCacheInstallRecord(
	unqlite_col *pCol, /* Target collection */
	jx9_int64 nId,     /* Unique record ID */
	jx9_value *pValue  /* JSON value */
	)
{
	unqlite_col_record *pRecord;
	sxu32 iBucket;
	/* Fetch the record first */
	pRecord = CollectionCacheFetchRecord(pCol,nId);
	if( pRecord ){
		/* Record already installed, overwrite its old value  */
		jx9MemObjStore(pValue,&pRecord->sValue);
		return UNQLITE_OK;
	}
	/* Allocate a new instance */
	pRecord = (unqlite_col_record *)SyMemBackendPoolAlloc(&pCol->pVm->sAlloc,sizeof(unqlite_col_record));
	if( pRecord == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Zero the structure */
	SyZero(pRecord,sizeof(unqlite_col_record));
	/* Fill in the structure */
	jx9MemObjInit(pCol->pVm->pJx9Vm,&pRecord->sValue);
	jx9MemObjStore(pValue,&pRecord->sValue);
	pRecord->nId = nId;
	pRecord->pCol = pCol;
	/* Install in the corresponding bucket */
	iBucket = COL_RECORD_HASH(nId) & (pCol->nRecSize - 1);
	pRecord->pNextCol = pCol->apRecord[iBucket];
	if( pCol->apRecord[iBucket] ){
		pCol->apRecord[iBucket]->pPrevCol = pRecord;
	}
	pCol->apRecord[iBucket] = pRecord;
	/* Link */
	MACRO_LD_PUSH(pCol->pList,pRecord);
	pCol->nRec++;
	if( (pCol->nRec >= pCol->nRecSize * 3) && pCol->nRec < 100000 ){
		/* Allocate a new larger table */
		sxu32 nNewSize = pCol->nRecSize << 1;
		unqlite_col_record *pEntry;
		unqlite_col_record **apNew;
		sxu32 n;
		
		apNew = (unqlite_col_record **)SyMemBackendAlloc(&pCol->pVm->sAlloc, nNewSize * sizeof(unqlite_col_record *));
		if( apNew ){
			/* Zero the new table */
			SyZero((void *)apNew, nNewSize * sizeof(unqlite_col_record *));
			/* Rehash all entries */
			n = 0;
			pEntry = pCol->pList;
			for(;;){
				/* Loop one */
				if( n >= pCol->nRec ){
					break;
				}
				pEntry->pNextCol = pEntry->pPrevCol = 0;
				/* Install in the new bucket */
				iBucket = COL_RECORD_HASH(pEntry->nId) & (nNewSize - 1);
				pEntry->pNextCol = apNew[iBucket];
				if( apNew[iBucket]  ){
					apNew[iBucket]->pPrevCol = pEntry;
				}
				apNew[iBucket] = pEntry;
				/* Point to the next entry */
				pEntry = pEntry->pNext;
				n++;
			}
			/* Release the old table and reflect the change */
			SyMemBackendFree(&pCol->pVm->sAlloc,(void *)pCol->apRecord);
			pCol->apRecord = apNew;
			pCol->nRecSize = nNewSize;
		}
	}
	/* All done */
	return UNQLITE_OK;
}
/*
 * Remove a record from the collection table.
 */
UNQLITE_PRIVATE int unqliteCollectionCacheRemoveRecord(
	unqlite_col *pCol, /* Target collection */
	jx9_int64 nId      /* Unique record ID */
	)
{
	unqlite_col_record *pRecord;
	/* Fetch the record first */
	pRecord = CollectionCacheFetchRecord(pCol,nId);
	if( pRecord == 0 ){
		/* No such record */
		return UNQLITE_NOTFOUND;
	}
	if( pRecord->pPrevCol ){
		pRecord->pPrevCol->pNextCol = pRecord->pNextCol;
	}else{
		sxu32 iBucket = COL_RECORD_HASH(nId) & (pCol->nRecSize - 1);
		pCol->apRecord[iBucket] = pRecord->pNextCol;
	}
	if( pRecord->pNextCol ){
		pRecord->pNextCol->pPrevCol = pRecord->pPrevCol;
	}
	/* Unlink */
	MACRO_LD_REMOVE(pCol->pList,pRecord);
	pCol->nRec--;
	return UNQLITE_OK;
}
/*
 * Discard a collection and its records.
 */
static int CollectionCacheRelease(unqlite_col *pCol)
{
	unqlite_col_record *pNext,*pRec = pCol->pList;
	unqlite_vm *pVm = pCol->pVm;
	sxu32 n;
	/* Discard all records */
	for( n = 0 ; n < pCol->nRec ; ++n ){
		pNext = pRec->pNext;
		jx9MemObjRelease(&pRec->sValue);
		SyMemBackendPoolFree(&pVm->sAlloc,(void *)pRec);
		/* Point to the next record */
		pRec = pNext;
	}
	SyMemBackendFree(&pVm->sAlloc,(void *)pCol->apRecord);
	pCol->nRec = pCol->nRecSize = 0;
	pCol->pList = 0;
	return UNQLITE_OK;
}
/*
 * Install a freshly created collection in the unqlite VM.
 */
static int unqliteVmInstallCollection(
	unqlite_vm *pVm,  /* Target VM */
	unqlite_col *pCol /* Collection to install */
	)
{
	SyString *pName = &pCol->sName;
	sxu32 iBucket;
	/* Hash the collection name */
	pCol->nHash = SyBinHash((const void *)pName->zString,pName->nByte);
	/* Install it in the corresponding bucket */
	iBucket = pCol->nHash & (pVm->iColSize - 1);
	pCol->pNextCol = pVm->apCol[iBucket];
	if( pVm->apCol[iBucket] ){
		pVm->apCol[iBucket]->pPrevCol = pCol;
	}
	pVm->apCol[iBucket] = pCol;
	/* Link to the list of active collections */
	MACRO_LD_PUSH(pVm->pCol,pCol);
	pVm->iCol++;
	if( (pVm->iCol >= pVm->iColSize * 4) && pVm->iCol < 10000 ){
		/* Grow the hashtable */
		sxu32 nNewSize = pVm->iColSize << 1;
		unqlite_col *pEntry;
		unqlite_col **apNew;
		sxu32 n;
		
		apNew = (unqlite_col **)SyMemBackendAlloc(&pVm->sAlloc, nNewSize * sizeof(unqlite_col *));
		if( apNew ){
			/* Zero the new table */
			SyZero((void *)apNew, nNewSize * sizeof(unqlite_col *));
			/* Rehash all entries */
			n = 0;
			pEntry = pVm->pCol;
			for(;;){
				/* Loop one */
				if( n >= pVm->iCol ){
					break;
				}
				pEntry->pNextCol = pEntry->pPrevCol = 0;
				/* Install in the new bucket */
				iBucket = pEntry->nHash & (nNewSize - 1);
				pEntry->pNextCol = apNew[iBucket];
				if( apNew[iBucket]  ){
					apNew[iBucket]->pPrevCol = pEntry;
				}
				apNew[iBucket] = pEntry;
				/* Point to the next entry */
				pEntry = pEntry->pNext;
				n++;
			}
			/* Release the old table and reflect the change */
			SyMemBackendFree(&pVm->sAlloc,(void *)pVm->apCol);
			pVm->apCol = apNew;
			pVm->iColSize  = nNewSize;
		}
	}
	return UNQLITE_OK;
}
/*
 * Fetch a collection from the target VM.
 */
static unqlite_col * unqliteVmFetchCollection(
	unqlite_vm *pVm, /* Target VM */
	SyString *pName  /* Lookup name */
	)
{
	unqlite_col *pCol;
	sxu32 nHash;
	if( pVm->iCol < 1 ){
		/* Don't bother hashing */
		return 0;
	}
	nHash = SyBinHash((const void *)pName->zString,pName->nByte);
	/* Perform the lookup */
	pCol = pVm->apCol[nHash & ( pVm->iColSize - 1)];
	for(;;){
		if( pCol == 0 ){
			break;
		}
		if( nHash == pCol->nHash && SyStringCmp(pName,&pCol->sName,SyMemcmp) == 0 ){
			/* Collection found */
			return pCol;
		}
		/* Point to the next entry */
		pCol = pCol->pNextCol;
	}
	/* No such collection */
	return 0;
}
/*
 * Write and/or alter collection binary header.
 */
static int CollectionSetHeader(
	unqlite_kv_engine *pEngine, /* Underlying KV storage engine */
	unqlite_col *pCol,          /* Target collection */
	jx9_int64 iRec,             /* Last record ID */
	jx9_int64 iTotal,           /* Total number of records in this collection */
	jx9_value *pSchema          /* Collection schema */
	)
{
	SyBlob *pHeader = &pCol->sHeader;
	unqlite_kv_methods *pMethods;
	int iWrite = 0;
	int rc;
	if( pEngine == 0 ){
		/* Default storage engine */
		pEngine = unqlitePagerGetKvEngine(pCol->pVm->pDb);
	}
	pMethods = pEngine->pIo->pMethods;
	if( SyBlobLength(pHeader) < 1 ){
		Sytm *pCreate = &pCol->sCreation; /* Creation time */
		unqlite_vfs *pVfs;
		sxu32 iDos;
		/* Magic number */
		rc = SyBlobAppendBig16(pHeader,UNQLITE_COLLECTION_MAGIC);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		/* Initial record ID */
		rc = SyBlobAppendBig64(pHeader,0);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		/* Total records in the collection */
		rc = SyBlobAppendBig64(pHeader,0);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		pVfs = (unqlite_vfs *)unqliteExportBuiltinVfs();
		/* Creation time of the collection */
		if( pVfs->xCurrentTime ){
			/* Get the creation time */
			pVfs->xCurrentTime(pVfs,pCreate);
		}else{
			/* Zero the structure */
			SyZero(pCreate,sizeof(Sytm));
		}
		/* Convert to DOS time */
		SyTimeFormatToDos(pCreate,&iDos);
		rc = SyBlobAppendBig32(pHeader,iDos);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		/* Offset to start writing collection schema */
		pCol->nSchemaOfft = SyBlobLength(pHeader);
		iWrite = 1;
	}else{
		unsigned char *zBinary = (unsigned char *)SyBlobData(pHeader);
		/* Header update */
		if( iRec >= 0 ){
			/* Update record ID */
			SyBigEndianPack64(&zBinary[2/* Magic number*/],(sxu64)iRec);
			iWrite = 1;
		}
		if( iTotal >= 0 ){
			/* Total records */
			SyBigEndianPack64(&zBinary[2/* Magic number*/+8/* Record ID*/],(sxu64)iTotal);
			iWrite = 1;
		}
		if( pSchema ){
			/* Collection Schema */
			SyBlobTruncate(pHeader,pCol->nSchemaOfft);
			/* Encode the schema to FastJson */
			rc = FastJsonEncode(pSchema,pHeader,0);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			/* Copy the collection schema */
			jx9MemObjStore(pSchema,&pCol->sSchema);
			iWrite = 1;
		}
	}
	if( iWrite ){
		SyString *pId = &pCol->sName;
		/* Reflect the disk and/or in-memory image */
		rc = pMethods->xReplace(pEngine,
			(const void *)pId->zString,pId->nByte,
			SyBlobData(pHeader),SyBlobLength(pHeader)
			);
		if( rc != UNQLITE_OK ){
			unqliteGenErrorFormat(pCol->pVm->pDb,
				"Cannot save collection '%z' header in the underlying storage engine",
				pId
				);
			return rc;
		}
	}
	return UNQLITE_OK;
}
/*
 * Load a binary collection from disk.
 */
static int CollectionLoadHeader(unqlite_col *pCol)
{
	SyBlob *pHeader = &pCol->sHeader;
	unsigned char *zRaw,*zEnd;
	sxu16 nMagic;
	sxu32 iDos;
	int rc;
	SyBlobReset(pHeader);
	/* Read the binary header */
	rc = unqlite_kv_cursor_data_callback(pCol->pCursor,unqliteDataConsumer,pHeader);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Perform a sanity check */
	if( SyBlobLength(pHeader) < (2 /* magic */ + 8 /* record_id */ + 8 /* total_records */+ 4 /* DOS creation time*/) ){
		return UNQLITE_CORRUPT;
	}
	zRaw = (unsigned char *)SyBlobData(pHeader);
	zEnd = &zRaw[SyBlobLength(pHeader)];
	/* Extract the magic number */
	SyBigEndianUnpack16(zRaw,&nMagic);
	if( nMagic != UNQLITE_COLLECTION_MAGIC ){
		return UNQLITE_CORRUPT;
	}
	zRaw += 2; /* sizeof(sxu16) */
	/* Extract the record ID */
	SyBigEndianUnpack64(zRaw,(sxu64 *)&pCol->nLastid);
	zRaw += 8; /* sizeof(sxu64) */
	/* Total records in the collection */
	SyBigEndianUnpack64(zRaw,(sxu64 *)&pCol->nTotRec);
	/* Extract the collection creation date (DOS) */
	zRaw += 8; /* sizeof(sxu64) */
	SyBigEndianUnpack32(zRaw,&iDos);
	SyDosTimeFormat(iDos,&pCol->sCreation);
	zRaw += 4;
	/* Check for a collection schema */
	pCol->nSchemaOfft = (sxu32)(zRaw - (unsigned char *)SyBlobData(pHeader));
	if( zRaw < zEnd ){
		/* Decode the FastJson value */
		FastJsonDecode((const void *)zRaw,(sxu32)(zEnd-zRaw),&pCol->sSchema,0,0);
	}
	return UNQLITE_OK;
}
/*
 * Load or create a binary collection.
 */
static int unqliteVmLoadCollection(
	unqlite_vm *pVm,    /* Target VM */
	const char *zName,  /* Collection name */
	sxu32 nByte,        /* zName length */
	int iFlag,          /* Control flag */
	unqlite_col **ppOut /* OUT: in-memory collection */
	)
{
	unqlite_kv_methods *pMethods;
	unqlite_kv_engine *pEngine;
	unqlite_kv_cursor *pCursor;
	unqlite *pDb = pVm->pDb;
	unqlite_col *pCol = 0; /* cc warning */
	int rc = SXERR_MEM;
	char *zDup = 0;
	/* Point to the underlying KV store */
	pEngine = unqlitePagerGetKvEngine(pVm->pDb);
	pMethods = pEngine->pIo->pMethods;
	/* Allocate a new cursor */
	rc = unqliteInitCursor(pDb,&pCursor);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	if( (iFlag & UNQLITE_VM_COLLECTION_CREATE) == 0 ){
		/* Seek to the desired location */
		rc = pMethods->xSeek(pCursor,(const void *)zName,(int)nByte,UNQLITE_CURSOR_MATCH_EXACT);
		if( rc != UNQLITE_OK && (iFlag & UNQLITE_VM_COLLECTION_EXISTS) == 0){
            unqliteGenErrorFormat(pDb,"Collection '%.*s' not defined in the underlying database",nByte,zName);
			unqliteReleaseCursor(pDb,pCursor);
			return rc;
		}
        else if((iFlag & UNQLITE_VM_COLLECTION_EXISTS)){
            unqliteReleaseCursor(pDb,pCursor);
            return rc;
        }
	}
	/* Allocate a new instance */
	pCol = (unqlite_col *)SyMemBackendPoolAlloc(&pVm->sAlloc,sizeof(unqlite_col));
	if( pCol == 0 ){
		unqliteGenOutofMem(pDb);
		rc = UNQLITE_NOMEM;
		goto fail;
	}
	SyZero(pCol,sizeof(unqlite_col));
	/* Fill in the structure */
	SyBlobInit(&pCol->sWorker,&pVm->sAlloc);
	SyBlobInit(&pCol->sHeader,&pVm->sAlloc);
	pCol->pVm = pVm;
	pCol->pCursor = pCursor;
	/* Duplicate collection name */
	zDup = SyMemBackendStrDup(&pVm->sAlloc,zName,nByte);
	if( zDup == 0 ){
		unqliteGenOutofMem(pDb);
		rc = UNQLITE_NOMEM;
		goto fail;
	}
	pCol->nRecSize = 64; /* Must be a power of two */
	pCol->apRecord = (unqlite_col_record **)SyMemBackendAlloc(&pVm->sAlloc,pCol->nRecSize * sizeof(unqlite_col_record *));
	if( pCol->apRecord == 0 ){
		unqliteGenOutofMem(pDb);
		rc = UNQLITE_NOMEM;
		goto fail;
	}
	/* Zero the table */
	SyZero((void *)pCol->apRecord,pCol->nRecSize * sizeof(unqlite_col_record *));
	SyStringInitFromBuf(&pCol->sName,zDup,nByte);
	jx9MemObjInit(pVm->pJx9Vm,&pCol->sSchema);
	if( iFlag & UNQLITE_VM_COLLECTION_CREATE ){
		/* Create a new collection */
		if( pMethods->xReplace == 0 ){
			/* Read-only KV engine: Generate an error message and return */
			unqliteGenErrorFormat(pDb,
				"Cannot create new collection '%z' due to a read-only Key/Value storage engine",
				&pCol->sName
			);
			rc = UNQLITE_ABORT; /* Abort VM execution */
			goto fail;
		}
		/* Write the collection header */
		rc = CollectionSetHeader(pEngine,pCol,0,0,0);
		if( rc != UNQLITE_OK ){
			rc = UNQLITE_ABORT; /* Abort VM execution */
			goto fail;
		}
	}else{
		/* Read the collection header */
		rc = CollectionLoadHeader(pCol);
		if( rc != UNQLITE_OK ){
			unqliteGenErrorFormat(pDb,"Corrupt collection '%z' header",&pCol->sName);
			goto fail;
		}
	}
	/* Finally install the collection */
	unqliteVmInstallCollection(pVm,pCol);
	/* All done */
	if( ppOut ){
		*ppOut = pCol;
	}
	return UNQLITE_OK;
fail:
	unqliteReleaseCursor(pDb,pCursor);
	if( zDup ){
		SyMemBackendFree(&pVm->sAlloc,zDup);
	}
	if( pCol ){
		if( pCol->apRecord ){
			SyMemBackendFree(&pVm->sAlloc,(void *)pCol->apRecord);
		}
		SyBlobRelease(&pCol->sHeader);
		SyBlobRelease(&pCol->sWorker);
		jx9MemObjRelease(&pCol->sSchema);
		SyMemBackendPoolFree(&pVm->sAlloc,pCol);
	}
	return rc;
}
/*
 * Fetch a collection.
 */
UNQLITE_PRIVATE unqlite_col * unqliteCollectionFetch(
	unqlite_vm *pVm, /* Target VM */
	SyString *pName, /* Lookup key */
	int iFlag        /* Control flag */
	)
{
	unqlite_col *pCol = 0; /* cc warning */
	int rc;
	/* Check if the collection is already loaded in memory */
	pCol = unqliteVmFetchCollection(pVm,pName);
	if( pCol ){
		/* Already loaded in memory*/
		return pCol;
	}
	if( (iFlag & UNQLITE_VM_AUTO_LOAD) == 0 ){
		return 0;
	}
	/* Ask the storage engine for the collection */
	rc = unqliteVmLoadCollection(pVm,pName->zString,pName->nByte,0,&pCol);
	/* Return to the caller */
	return rc == UNQLITE_OK ? pCol : 0;
}
/*
 * Return the unique ID of the last inserted record.
 */
UNQLITE_PRIVATE jx9_int64 unqliteCollectionLastRecordId(unqlite_col *pCol)
{
	return pCol->nLastid == 0 ? 0 : (pCol->nLastid - 1);
}
/*
 * Return the current record ID.
 */
UNQLITE_PRIVATE jx9_int64 unqliteCollectionCurrentRecordId(unqlite_col *pCol)
{
	return pCol->nCurid;
}
/*
 * Return the total number of records in a given collection. 
 */
UNQLITE_PRIVATE jx9_int64 unqliteCollectionTotalRecords(unqlite_col *pCol)
{
	return pCol->nTotRec;
}
/*
 * Reset the record cursor.
 */
UNQLITE_PRIVATE void unqliteCollectionResetRecordCursor(unqlite_col *pCol)
{
	pCol->nCurid = 0;
}
/*
 * Fetch a record by its unique ID.
 */
UNQLITE_PRIVATE int unqliteCollectionFetchRecordById(
	unqlite_col *pCol, /* Target collection */
	jx9_int64 nId,     /* Unique record ID */
	jx9_value *pValue  /* OUT: record value */
	)
{
	SyBlob *pWorker = &pCol->sWorker;
	unqlite_col_record *pRec;
	int rc;
	jx9_value_null(pValue);
	/* Perform a cache lookup first */
	pRec = CollectionCacheFetchRecord(pCol,nId);
	if( pRec ){
		/* Copy record value */
		jx9MemObjStore(&pRec->sValue,pValue);
		return UNQLITE_OK;
	}
	/* Reset the working buffer */
	SyBlobReset(pWorker);
	/* Generate the unique ID */
	SyBlobFormat(pWorker,"%z_%qd",&pCol->sName,nId);
	/* Reset the cursor */
	unqlite_kv_cursor_reset(pCol->pCursor);
	/* Seek the cursor to the desired location */
	rc = unqlite_kv_cursor_seek(pCol->pCursor,
		SyBlobData(pWorker),SyBlobLength(pWorker),
		UNQLITE_CURSOR_MATCH_EXACT
		);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Consume the binary JSON */
	SyBlobReset(pWorker);
	unqlite_kv_cursor_data_callback(pCol->pCursor,unqliteDataConsumer,pWorker);
	if( SyBlobLength(pWorker) < 1 ){
		unqliteGenErrorFormat(pCol->pVm->pDb,
			"Empty record '%qd'",nId
			);
		jx9_value_null(pValue);
	}else{
		/* Decode the binary JSON */
		rc = FastJsonDecode(SyBlobData(pWorker),SyBlobLength(pWorker),pValue,0,0);
		if( rc == UNQLITE_OK ){
			/* Install the record in the cache */
			CollectionCacheInstallRecord(pCol,nId,pValue);
		}
	}
	return rc;
}
/*
 * Fetch the next record from a given collection.
 */ 
UNQLITE_PRIVATE int unqliteCollectionFetchNextRecord(unqlite_col *pCol,jx9_value *pValue)
{
	int rc;
	for(;;){
		if( pCol->nCurid >= pCol->nLastid ){
			/* No more records, reset the record cursor ID */
			pCol->nCurid = 0;
			/* Return to the caller */
			return SXERR_EOF;
		}
		rc = unqliteCollectionFetchRecordById(pCol,pCol->nCurid,pValue);
		/* Increment the record ID */
		pCol->nCurid++;
		/* Lookup result */
		if( rc == UNQLITE_OK || rc != UNQLITE_NOTFOUND ){
			break;
		}
	}
	return rc;
}
/*
 * Judge a collection whether exists
 */
UNQLITE_PRIVATE int unqliteExistsCollection(
    unqlite_vm *pVm, /* Target VM */
    SyString *pName  /* Collection name */
    )
{
    unqlite_col *pCol;
    int rc;
    /* Perform a lookup first */
    pCol = unqliteVmFetchCollection(pVm,pName);
    if( pCol ){
        /* Already loaded in memory*/
        return UNQLITE_OK;
    }
    rc = unqliteVmLoadCollection(pVm,pName->zString,pName->nByte,UNQLITE_VM_COLLECTION_EXISTS,0);
    return rc;
}
/*
 * Create a new collection.
 */
UNQLITE_PRIVATE int unqliteCreateCollection(
	unqlite_vm *pVm, /* Target VM */
	SyString *pName  /* Collection name */
	)
{
	unqlite_col *pCol;
	int rc;
	/* Perform a lookup first */
	pCol = unqliteCollectionFetch(pVm,pName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		return UNQLITE_EXISTS;
	}
	/* Now, safely create the collection */
	rc = unqliteVmLoadCollection(pVm,pName->zString,pName->nByte,UNQLITE_VM_COLLECTION_CREATE,0);
	return rc;
}
/*
 * Set a schema (JSON object) for a given collection.
 */
UNQLITE_PRIVATE int unqliteCollectionSetSchema(unqlite_col *pCol,jx9_value *pValue)
{
	int rc;
	if( !jx9_value_is_json_object(pValue) ){
		/* Must be a JSON object */
		return SXERR_INVALID;
	}
	rc = CollectionSetHeader(0,pCol,-1,-1,pValue);
	return rc;
}
/*
 * Perform a store operation on a given collection.
 */
static int CollectionStore(
	unqlite_col *pCol, /* Target collection */
	jx9_value *pValue  /* JSON value to be stored */
	)
{
	SyBlob *pWorker = &pCol->sWorker;
	unqlite_kv_methods *pMethods;
	unqlite_kv_engine *pEngine;
	sxu32 nKeyLen;
	int rc;	
	/* Point to the underlying KV store */
	pEngine = unqlitePagerGetKvEngine(pCol->pVm->pDb);
	pMethods = pEngine->pIo->pMethods;
	if( pCol->nTotRec >= SXI64_HIGH ){
		/* Collection limit reached. No more records */
		unqliteGenErrorFormat(pCol->pVm->pDb,
				"Collection '%z': Records limit reached",
				&pCol->sName
			);
		return UNQLITE_LIMIT;
	}
	if( pMethods->xReplace == 0 ){
		unqliteGenErrorFormat(pCol->pVm->pDb,
				"Cannot store record into collection '%z' due to a read-only Key/Value storage engine",
				&pCol->sName
			);
		return UNQLITE_READ_ONLY;
	}
	/* Reset the working buffer */
	SyBlobReset(pWorker);
	if( jx9_value_is_json_object(pValue) ){
		jx9_value sId;
		/* If the given type is a JSON object, then add the special __id field */
		jx9MemObjInitFromInt(pCol->pVm->pJx9Vm,&sId,pCol->nLastid);
		jx9_array_add_strkey_elem(pValue,"__id",&sId);
		jx9MemObjRelease(&sId);
	}
	/* Prepare the unique ID for this record */
	SyBlobFormat(pWorker,"%z_%qd",&pCol->sName,pCol->nLastid);
	nKeyLen = SyBlobLength(pWorker);
	if( nKeyLen < 1 ){
		unqliteGenOutofMem(pCol->pVm->pDb);
		return UNQLITE_NOMEM;
	}
	/* Turn to FastJson */
	rc = FastJsonEncode(pValue,pWorker,0);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Finally perform the insertion */
	rc = pMethods->xReplace(
		pEngine,
		SyBlobData(pWorker),nKeyLen,
		SyBlobDataAt(pWorker,nKeyLen),SyBlobLength(pWorker)-nKeyLen
		);
	if( rc == UNQLITE_OK ){
		/* Save the value in the cache */
		CollectionCacheInstallRecord(pCol,pCol->nLastid,pValue);
		/* Increment the unique __id */
		pCol->nLastid++;
		pCol->nTotRec++;
		/* Reflect the change */
		rc = CollectionSetHeader(0,pCol,pCol->nLastid,pCol->nTotRec,0);
	}
	if( rc != UNQLITE_OK ){
		unqliteGenErrorFormat(pCol->pVm->pDb,
				"IO error while storing record into collection '%z'",
				&pCol->sName
			);
		return rc;
	}
	return UNQLITE_OK;
}
/*
 * Perform a update operation on a given collection.
 */
 static int CollectionUpdate(
                           unqlite_col *pCol, /* Target collection */
                           jx9_int64 nId,     /* Record ID */
                           jx9_value *pValue  /* JSON value to be stored */
)
{
    SyBlob *pWorker = &pCol->sWorker;
    unqlite_kv_methods *pMethods;
    unqlite_kv_engine *pEngine;
    sxu32 nKeyLen;
    int rc;
    /* Point to the underlying KV store */
    pEngine = unqlitePagerGetKvEngine(pCol->pVm->pDb);
    pMethods = pEngine->pIo->pMethods;
    if( pCol->nTotRec >= SXI64_HIGH ){
        /* Collection limit reached. No more records */
        unqliteGenErrorFormat(pCol->pVm->pDb,
                              "Collection '%z': Records limit reached",
                              &pCol->sName
                              );
        return UNQLITE_LIMIT;
    }
    if( pMethods->xReplace == 0 ){
        unqliteGenErrorFormat(pCol->pVm->pDb,
                              "Cannot store record into collection '%z' due to a read-only Key/Value storage engine",
                              &pCol->sName
                              );
        return UNQLITE_READ_ONLY;
    }
    /* Reset the working buffer */
    SyBlobReset(pWorker);
    
    /* Prepare the unique ID for this record */
    SyBlobFormat(pWorker,"%z_%qd",&pCol->sName, nId);
    
    /* Reset the cursor */
    unqlite_kv_cursor_reset(pCol->pCursor);
    /* Seek the cursor to the desired location */
    rc = unqlite_kv_cursor_seek(pCol->pCursor,
                                SyBlobData(pWorker),SyBlobLength(pWorker),
                                UNQLITE_CURSOR_MATCH_EXACT
                                );
    if( rc != UNQLITE_OK ){
        unqliteGenErrorFormat(pCol->pVm->pDb,
                              "No record to update in collection '%z'",
                              &pCol->sName
                              );
        return rc;
    }
    
    if( jx9_value_is_json_object(pValue) ){
        jx9_value sId;
        /* If the given type is a JSON object, then add the special __id field */
        jx9MemObjInitFromInt(pCol->pVm->pJx9Vm,&sId,nId);
        jx9_array_add_strkey_elem(pValue,"__id",&sId);
        jx9MemObjRelease(&sId);
    }
    
    nKeyLen = SyBlobLength(pWorker);
    if( nKeyLen < 1 ){
        unqliteGenOutofMem(pCol->pVm->pDb);
        return UNQLITE_NOMEM;
    }
    /* Turn to FastJson */
    rc = FastJsonEncode(pValue,pWorker,0);
    if( rc != UNQLITE_OK ){
        return rc;
    }
    /* Finally perform the insertion */
    rc = pMethods->xReplace(
                            pEngine,
                            SyBlobData(pWorker),nKeyLen,
                            SyBlobDataAt(pWorker,nKeyLen),SyBlobLength(pWorker)-nKeyLen
                            );
    if( rc == UNQLITE_OK ){
        /* Save the value in the cache */
        CollectionCacheInstallRecord(pCol,nId,pValue);
    }
    if( rc != UNQLITE_OK ){
        unqliteGenErrorFormat(pCol->pVm->pDb,
                              "IO error while storing record into collection '%z'",
                              &pCol->sName
                              );
        return rc;
    }
    return UNQLITE_OK;
}
/*
 * Array walker callback (Refer to jx9_array_walk()).
 */
static int CollectionRecordArrayWalker(jx9_value *pKey,jx9_value *pData,void *pUserData)
{
	unqlite_col *pCol = (unqlite_col *)pUserData;
	int rc;
	/* Perform the insertion */
	rc = CollectionStore(pCol,pData);
	if( rc != UNQLITE_OK ){
		SXUNUSED(pKey); /* cc warning */
	}
	return rc;
}
/*
 * Perform a store operation on a given collection.
 */
UNQLITE_PRIVATE int unqliteCollectionPut(unqlite_col *pCol,jx9_value *pValue,int iFlag)
{
	int rc;
	if( !jx9_value_is_json_object(pValue) && jx9_value_is_json_array(pValue) ){
		/* Iterate over the array and store its members in the collection */
		rc = jx9_array_walk(pValue,CollectionRecordArrayWalker,pCol);
		SXUNUSED(iFlag); /* cc warning */
	}else{
		rc = CollectionStore(pCol,pValue);
	}
	return rc;
}
/*
 * Drop a record from a given collection.
 */
UNQLITE_PRIVATE int unqliteCollectionDropRecord(
	unqlite_col *pCol,  /* Target collection */
	jx9_int64 nId,      /* Unique ID of the record to be droped */
	int wr_header,      /* True to alter collection header */
	int log_err         /* True to log error */
	)
{
	SyBlob *pWorker = &pCol->sWorker;
	int rc;		
	/* Reset the working buffer */
	SyBlobReset(pWorker);
	/* Prepare the unique ID for this record */
	SyBlobFormat(pWorker,"%z_%qd",&pCol->sName,nId);
	/* Reset the cursor */
	unqlite_kv_cursor_reset(pCol->pCursor);
	/* Seek the cursor to the desired location */
	rc = unqlite_kv_cursor_seek(pCol->pCursor,
		SyBlobData(pWorker),SyBlobLength(pWorker),
		UNQLITE_CURSOR_MATCH_EXACT
		);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Remove the record from the storage engine */
	rc = unqlite_kv_cursor_delete_entry(pCol->pCursor);
	/* Finally, Remove the record from the cache */
	unqliteCollectionCacheRemoveRecord(pCol,nId);
	if( rc == UNQLITE_OK ){
		pCol->nTotRec--;
		if( wr_header ){
			/* Relect in the collection header */
			rc = CollectionSetHeader(0,pCol,-1,pCol->nTotRec,0);
		}
	}else if( rc == UNQLITE_NOTIMPLEMENTED ){
		if( log_err ){
			unqliteGenErrorFormat(pCol->pVm->pDb,
				"Cannot delete record from collection '%z' due to a read-only Key/Value storage engine",
				&pCol->sName
				);
		}
	}
	return rc;
}
/*
 * Update a given record with new data
 */
UNQLITE_PRIVATE int unqliteCollectionUpdateRecord(unqlite_col *pCol,jx9_int64 nId, jx9_value *pValue,int iFlag)
{
    int rc;
    if( !jx9_value_is_json_object(pValue) && jx9_value_is_json_array(pValue) ){
        /* Iterate over the array and store its members in the collection */
        rc = jx9_array_walk(pValue,CollectionRecordArrayWalker,pCol);
        SXUNUSED(iFlag); /* cc warning */
    }else{
        rc = CollectionUpdate(pCol,nId,pValue);
    }
    return rc;
}
/*
 * Drop a collection from the KV storage engine and the underlying
 * unqlite VM.
 */
UNQLITE_PRIVATE int unqliteDropCollection(unqlite_col *pCol)
{
	unqlite_vm *pVm = pCol->pVm;
	jx9_int64 nId;
	int rc;
	/* Reset the cursor */
	unqlite_kv_cursor_reset(pCol->pCursor);
	/* Seek the cursor to the desired location */
	rc = unqlite_kv_cursor_seek(pCol->pCursor,
		SyStringData(&pCol->sName),SyStringLength(&pCol->sName),
		UNQLITE_CURSOR_MATCH_EXACT
		);
	if( rc == UNQLITE_OK ){
		/* Remove the record from the storage engine */
		rc = unqlite_kv_cursor_delete_entry(pCol->pCursor);
	}
	if( rc != UNQLITE_OK ){
		unqliteGenErrorFormat(pCol->pVm->pDb,
				"Cannot remove collection '%z' due to a read-only Key/Value storage engine",
				&pCol->sName
			);
		return rc;
	}
	/* Drop collection records */
	for( nId = 0 ; nId < pCol->nLastid ; ++nId ){
		unqliteCollectionDropRecord(pCol,nId,0,0);
	}
	/* Cleanup */
	CollectionCacheRelease(pCol);
	SyBlobRelease(&pCol->sHeader);
	SyBlobRelease(&pCol->sWorker);
	SyMemBackendFree(&pVm->sAlloc,(void *)SyStringData(&pCol->sName));
	unqliteReleaseCursor(pVm->pDb,pCol->pCursor);
	/* Unlink */
	if( pCol->pPrevCol ){
		pCol->pPrevCol->pNextCol = pCol->pNextCol;
	}else{
		sxu32 iBucket = pCol->nHash & (pVm->iColSize - 1);
		pVm->apCol[iBucket] = pCol->pNextCol;
	}
	if( pCol->pNextCol ){
		pCol->pNextCol->pPrevCol = pCol->pPrevCol;
	}
	MACRO_LD_REMOVE(pVm->pCol,pCol);
	pVm->iCol--;
	SyMemBackendPoolFree(&pVm->sAlloc,pCol);
	return UNQLITE_OK;
}
