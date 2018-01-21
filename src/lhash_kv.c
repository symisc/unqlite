/*
 * Symisc unQLite: An Embeddable NoSQL (Post Modern) Database Engine.
 * Copyright (C) 2012-2018, Symisc Systems http://unqlite.org/
 * Copyright (C) 2014, Yuras Shumovich <shumovichy@gmail.com>
 * Version 1.1.6
 * For information on licensing, redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES
 * please contact Symisc Systems via:
 *       legal@symisc.net
 *       licensing@symisc.net
 *       contact@symisc.net
 * or visit:
 *      http://unqlite.org/licensing.html
 */
 /* $SymiscID: lhash_kv.c v1.7 Solaris 2013-01-14 12:56 stable <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif
/* 
 * This file implements disk based hashtable using the linear hashing algorithm.
 * This implementation is the one decribed in the paper:
 *  LINEAR HASHING : A NEW TOOL FOR FILE AND TABLE ADDRESSING. Witold Litwin. I. N. Ft. I. A.. 78 150 Le Chesnay, France.
 * Plus a smart extension called Virtual Bucket Table. (contact devel@symisc.net for additional information).
 */
/* Magic number identifying a valid storage image */
#define L_HASH_MAGIC 0xFA782DCB
/*
 * Magic word to hash to identify a valid hash function.
 */
#define L_HASH_WORD "chm@symisc"
/*
 * Cell size on disk. 
 */
#define L_HASH_CELL_SZ (4/*Hash*/+4/*Key*/+8/*Data*/+2/* Offset of the next cell */+8/*Overflow*/)
/*
 * Primary page (not overflow pages) header size on disk.
 */
#define L_HASH_PAGE_HDR_SZ (2/* Cell offset*/+2/* Free block offset*/+8/*Slave page number*/)
/*
 * The maximum amount of payload (in bytes) that can be stored locally for
 * a database entry.  If the entry contains more data than this, the
 * extra goes onto overflow pages.
*/
#define L_HASH_MX_PAYLOAD(PageSize)  (PageSize-(L_HASH_PAGE_HDR_SZ+L_HASH_CELL_SZ))
/*
 * Maxium free space on a single page.
 */
#define L_HASH_MX_FREE_SPACE(PageSize) (PageSize - (L_HASH_PAGE_HDR_SZ))
/*
** The maximum number of bytes of payload allowed on a single overflow page.
*/
#define L_HASH_OVERFLOW_SIZE(PageSize) (PageSize-8)
/* Forward declaration */
typedef struct lhash_kv_engine lhash_kv_engine;
typedef struct lhpage lhpage;
/*
 * Each record in the database is identified either in-memory or in
 * disk by an instance of the following structure.
 */
typedef struct lhcell lhcell;
struct lhcell
{
	/* Disk-data (Big-Endian) */
	sxu32 nHash;   /* Hash of the key: 4 bytes */
	sxu32 nKey;    /* Key length: 4 bytes */
	sxu64 nData;   /* Data length: 8 bytes */
	sxu16 iNext;   /* Offset of the next cell: 2 bytes */
	pgno iOvfl;    /* Overflow page number if any: 8 bytes */
	/* In-memory data only */
	lhpage *pPage;     /* Page this cell belongs */
	sxu16 iStart;      /* Offset of this cell */
	pgno iDataPage;    /* Data page number when overflow */
	sxu16 iDataOfft;   /* Offset of the data in iDataPage */
	SyBlob sKey;       /* Record key for fast lookup (Kept in-memory if < 256KB ) */
	lhcell *pNext,*pPrev;         /* Linked list of the loaded memory cells */
	lhcell *pNextCol,*pPrevCol;   /* Collison chain  */
};
/*
** Each database page has a header that is an instance of this
** structure.
*/
typedef struct lhphdr lhphdr;
struct lhphdr 
{
  sxu16 iOfft; /* Offset of the first cell */
  sxu16 iFree; /* Offset of the first free block*/
  pgno iSlave; /* Slave page number */
};
/*
 * Each loaded primary disk page is represented in-memory using
 * an instance of the following structure.
 */
struct lhpage
{
	lhash_kv_engine *pHash;  /* KV Storage engine that own this page */
	unqlite_page *pRaw;      /* Raw page contents */
	lhphdr sHdr;             /* Processed page header */
	lhcell **apCell;         /* Cell buckets */
	lhcell *pList,*pFirst;   /* Linked list of cells */
	sxu32 nCell;             /* Total number of cells */
	sxu32 nCellSize;         /* apCell[] size */
	lhpage *pMaster;         /* Master page in case we are dealing with a slave page */
	lhpage *pSlave;          /* List of slave pages */
	lhpage *pNextSlave;      /* Next slave page on the list */
	sxi32 iSlave;            /* Total number of slave pages */
	sxu16 nFree;             /* Amount of free space available in the page */
};
/*
 * A Bucket map record which is used to map logical bucket number to real
 * bucket number is represented by an instance of the following structure.
 */
typedef struct lhash_bmap_rec lhash_bmap_rec;
struct lhash_bmap_rec
{
	pgno iLogic;                   /* Logical bucket number */
	pgno iReal;                    /* Real bucket number */
	lhash_bmap_rec *pNext,*pPrev;  /* Link to other bucket map */     
	lhash_bmap_rec *pNextCol,*pPrevCol; /* Collision links */
};
typedef struct lhash_bmap_page lhash_bmap_page;
struct lhash_bmap_page
{
	pgno iNum;   /* Page number where this entry is stored */
	sxu16 iPtr;  /* Offset to start reading/writing from */
	sxu32 nRec;  /* Total number of records in this page */
	pgno iNext;  /* Next map page */
};
/*
 * An in memory linear hash implemenation is represented by in an isntance
 * of the following structure.
 */
struct lhash_kv_engine
{
	const unqlite_kv_io *pIo;     /* IO methods: Must be first */
	/* Private fields */
	SyMemBackend sAllocator;      /* Private memory backend */
	ProcHash xHash;               /* Default hash function */
	ProcCmp xCmp;                 /* Default comparison function */
	unqlite_page *pHeader;        /* Page one to identify a valid implementation */
	lhash_bmap_rec **apMap;       /* Buckets map records */
	sxu32 nBuckRec;               /* Total number of bucket map records */
	sxu32 nBuckSize;              /* apMap[] size  */
	lhash_bmap_rec *pList;        /* List of bucket map records */
	lhash_bmap_rec *pFirst;       /* First record*/
	lhash_bmap_page sPageMap;     /* Primary bucket map */
	int iPageSize;                /* Page size */
	pgno nFreeList;               /* List of free pages */
	pgno split_bucket;            /* Current split bucket: MUST BE A POWER OF TWO */
	pgno max_split_bucket;        /* Maximum split bucket: MUST BE A POWER OF TWO */
	pgno nmax_split_nucket;       /* Next maximum split bucket (1 << nMsb): In-memory only */
	sxu32 nMagic;                 /* Magic number to identify a valid linear hash disk database */
};
/*
 * Given a logical bucket number, return the record associated with it.
 */
static lhash_bmap_rec * lhMapFindBucket(lhash_kv_engine *pEngine,pgno iLogic)
{
	lhash_bmap_rec *pRec;
	if( pEngine->nBuckRec < 1 ){
		/* Don't bother */
		return 0;
	}
	pRec = pEngine->apMap[iLogic & (pEngine->nBuckSize - 1)];
	for(;;){
		if( pRec == 0 ){
			break;
		}
		if( pRec->iLogic == iLogic ){
			return pRec;
		}
		/* Point to the next entry */
		pRec = pRec->pNextCol;
	}
	/* No such record */
	return 0;
}
/*
 * Install a new bucket map record.
 */
static int lhMapInstallBucket(lhash_kv_engine *pEngine,pgno iLogic,pgno iReal)
{
	lhash_bmap_rec *pRec;
	sxu32 iBucket;
	/* Allocate a new instance */
	pRec = (lhash_bmap_rec *)SyMemBackendPoolAlloc(&pEngine->sAllocator,sizeof(lhash_bmap_rec));
	if( pRec == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Zero the structure */
	SyZero(pRec,sizeof(lhash_bmap_rec));
	/* Fill in the structure */
	pRec->iLogic = iLogic;
	pRec->iReal = iReal;
	iBucket = iLogic & (pEngine->nBuckSize - 1);
	pRec->pNextCol = pEngine->apMap[iBucket];
	if( pEngine->apMap[iBucket] ){
		pEngine->apMap[iBucket]->pPrevCol = pRec;
	}
	pEngine->apMap[iBucket] = pRec;
	/* Link */
	if( pEngine->pFirst == 0 ){
		pEngine->pFirst = pEngine->pList = pRec;
	}else{
		MACRO_LD_PUSH(pEngine->pList,pRec);
	}
	pEngine->nBuckRec++;
	if( (pEngine->nBuckRec >= pEngine->nBuckSize * 3) && pEngine->nBuckRec < 100000 ){
		/* Allocate a new larger table */
		sxu32 nNewSize = pEngine->nBuckSize << 1;
		lhash_bmap_rec *pEntry;
		lhash_bmap_rec **apNew;
		sxu32 n;
		
		apNew = (lhash_bmap_rec **)SyMemBackendAlloc(&pEngine->sAllocator, nNewSize * sizeof(lhash_bmap_rec *));
		if( apNew ){
			/* Zero the new table */
			SyZero((void *)apNew, nNewSize * sizeof(lhash_bmap_rec *));
			/* Rehash all entries */
			n = 0;
			pEntry = pEngine->pList;
			for(;;){
				/* Loop one */
				if( n >= pEngine->nBuckRec ){
					break;
				}
				pEntry->pNextCol = pEntry->pPrevCol = 0;
				/* Install in the new bucket */
				iBucket = pEntry->iLogic & (nNewSize - 1);
				pEntry->pNextCol = apNew[iBucket];
				if( apNew[iBucket] ){
					apNew[iBucket]->pPrevCol = pEntry;
				}
				apNew[iBucket] = pEntry;
				/* Point to the next entry */
				pEntry = pEntry->pNext;
				n++;
			}
			/* Release the old table and reflect the change */
			SyMemBackendFree(&pEngine->sAllocator,(void *)pEngine->apMap);
			pEngine->apMap = apNew;
			pEngine->nBuckSize  = nNewSize;
		}
	}
	return UNQLITE_OK;
}
/*
 * Process a raw bucket map record.
 */
static int lhMapLoadPage(lhash_kv_engine *pEngine,lhash_bmap_page *pMap,const unsigned char *zRaw)
{
	const unsigned char *zEnd = &zRaw[pEngine->iPageSize];
	const unsigned char *zPtr = zRaw;
	pgno iLogic,iReal;
	sxu32 n;
	int rc;
	if( pMap->iPtr == 0 ){
		/* Read the map header */
		SyBigEndianUnpack64(zRaw,&pMap->iNext);
		zRaw += 8;
		SyBigEndianUnpack32(zRaw,&pMap->nRec);
		zRaw += 4;
	}else{
		/* Mostly page one of the database */
		zRaw += pMap->iPtr;
	}
	/* Start processing */
	for( n = 0; n < pMap->nRec ; ++n ){
		if( zRaw >= zEnd ){
			break;
		}
		/* Extract the logical and real bucket number */
		SyBigEndianUnpack64(zRaw,&iLogic);
		zRaw += 8;
		SyBigEndianUnpack64(zRaw,&iReal);
		zRaw += 8;
		/* Install the record in the map */
		rc = lhMapInstallBucket(pEngine,iLogic,iReal);
		if( rc != UNQLITE_OK ){
			return rc;
		}
	}
	pMap->iPtr = (sxu16)(zRaw-zPtr);
	/* All done */
	return UNQLITE_OK;
}
/* 
 * Allocate a new cell instance.
 */
static lhcell * lhNewCell(lhash_kv_engine *pEngine,lhpage *pPage)
{
	lhcell *pCell;
	pCell = (lhcell *)SyMemBackendPoolAlloc(&pEngine->sAllocator,sizeof(lhcell));
	if( pCell == 0 ){
		return 0;
	}
	/* Zero the structure */
	SyZero(pCell,sizeof(lhcell));
	/* Fill in the structure */
	SyBlobInit(&pCell->sKey,&pEngine->sAllocator);
	pCell->pPage = pPage;
	return pCell;
}
/*
 * Discard a cell from the page table.
 */
static void lhCellDiscard(lhcell *pCell)
{
	lhpage *pPage = pCell->pPage->pMaster;	
	
	if( pCell->pPrevCol ){
		pCell->pPrevCol->pNextCol = pCell->pNextCol;
	}else{
		pPage->apCell[pCell->nHash & (pPage->nCellSize - 1)] = pCell->pNextCol;
	}
	if( pCell->pNextCol ){
		pCell->pNextCol->pPrevCol = pCell->pPrevCol;
	}
	MACRO_LD_REMOVE(pPage->pList,pCell);
	if( pCell == pPage->pFirst ){
		pPage->pFirst = pCell->pPrev;
	}
	pPage->nCell--;
	/* Release the cell */
	SyBlobRelease(&pCell->sKey);
	SyMemBackendPoolFree(&pPage->pHash->sAllocator,pCell);
}
/*
 * Install a cell in the page table.
 */
static int lhInstallCell(lhcell *pCell)
{
	lhpage *pPage = pCell->pPage->pMaster;
	sxu32 iBucket;
	if( pPage->nCell < 1 ){
		sxu32 nTableSize = 32; /* Must be a power of two */
		lhcell **apTable;
		/* Allocate a new cell table */
		apTable = (lhcell **)SyMemBackendAlloc(&pPage->pHash->sAllocator, nTableSize * sizeof(lhcell *));
		if( apTable == 0 ){
			return UNQLITE_NOMEM;
		}
		/* Zero the new table */
		SyZero((void *)apTable, nTableSize * sizeof(lhcell *));
		/* Install it */
		pPage->apCell = apTable;
		pPage->nCellSize = nTableSize;
	}
	iBucket = pCell->nHash & (pPage->nCellSize - 1);
	pCell->pNextCol = pPage->apCell[iBucket];
	if( pPage->apCell[iBucket] ){
		pPage->apCell[iBucket]->pPrevCol = pCell;
	}
	pPage->apCell[iBucket] = pCell;
	if( pPage->pFirst == 0 ){
		pPage->pFirst = pPage->pList = pCell;
	}else{
		MACRO_LD_PUSH(pPage->pList,pCell);
	}
	pPage->nCell++;
	if( (pPage->nCell >= pPage->nCellSize * 3) && pPage->nCell < 100000 ){
		/* Allocate a new larger table */
		sxu32 nNewSize = pPage->nCellSize << 1;
		lhcell *pEntry;
		lhcell **apNew;
		sxu32 n;
		
		apNew = (lhcell **)SyMemBackendAlloc(&pPage->pHash->sAllocator, nNewSize * sizeof(lhcell *));
		if( apNew ){
			/* Zero the new table */
			SyZero((void *)apNew, nNewSize * sizeof(lhcell *));
			/* Rehash all entries */
			n = 0;
			pEntry = pPage->pList;
			for(;;){
				/* Loop one */
				if( n >= pPage->nCell ){
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
			SyMemBackendFree(&pPage->pHash->sAllocator,(void *)pPage->apCell);
			pPage->apCell = apNew;
			pPage->nCellSize  = nNewSize;
		}
	}
	return UNQLITE_OK;
}
/*
 * Private data of lhKeyCmp().
 */
struct lhash_key_cmp
{
	const char *zIn;  /* Start of the stream */
	const char *zEnd; /* End of the stream */
	ProcCmp xCmp;     /* Comparison function */
};
/*
 * Comparsion callback for large key > 256 KB
 */
static int lhKeyCmp(const void *pData,sxu32 nLen,void *pUserData)
{
	struct lhash_key_cmp *pCmp = (struct lhash_key_cmp *)pUserData;
	int rc;
	if( pCmp->zIn >= pCmp->zEnd ){
		if( nLen > 0 ){
			return UNQLITE_ABORT;
		}
		return UNQLITE_OK;
	}
	/* Perform the comparison */
	rc = pCmp->xCmp((const void *)pCmp->zIn,pData,nLen);
	if( rc != 0 ){
		/* Abort comparison */
		return UNQLITE_ABORT;
	}
	/* Advance the cursor */
	pCmp->zIn += nLen;
	return UNQLITE_OK;
}
/* Forward declaration */
static int lhConsumeCellkey(lhcell *pCell,int (*xConsumer)(const void *,unsigned int,void *),void *pUserData,int offt_only);
/*
 * given a key, return the cell associated with it on success. NULL otherwise.
 */
static lhcell * lhFindCell(
	lhpage *pPage,    /* Target page */
	const void *pKey, /* Lookup key */
	sxu32 nByte,      /* Key length */
	sxu32 nHash       /* Hash of the key */
	)
{
	lhcell *pEntry;
	if( pPage->nCell < 1 ){
		/* Don't bother hashing */
		return 0;
	}
	/* Point to the corresponding bucket */
	pEntry = pPage->apCell[nHash & (pPage->nCellSize - 1)];
	for(;;){
		if( pEntry == 0 ){
			break;
		}
		if( pEntry->nHash == nHash && pEntry->nKey == nByte ){
			if( SyBlobLength(&pEntry->sKey) < 1 ){
				/* Large key (> 256 KB) are not kept in-memory */
				struct lhash_key_cmp sCmp;
				int rc;
				/* Fill-in the structure */
				sCmp.zIn = (const char *)pKey;
				sCmp.zEnd = &sCmp.zIn[nByte];
				sCmp.xCmp = pPage->pHash->xCmp;
				/* Fetch the key from disk and perform the comparison */
				rc = lhConsumeCellkey(pEntry,lhKeyCmp,&sCmp,0);
				if( rc == UNQLITE_OK ){
					/* Cell found */
					return pEntry;
				}
			}else if ( pPage->pHash->xCmp(pKey,SyBlobData(&pEntry->sKey),nByte) == 0 ){
				/* Cell found */
				return pEntry;
			}
		}
		/* Point to the next entry */
		pEntry = pEntry->pNextCol;
	}
	/* No such entry */
	return 0;
}
/*
 * Parse a raw cell fetched from disk.
 */
static int lhParseOneCell(lhpage *pPage,const unsigned char *zRaw,const unsigned char *zEnd,lhcell **ppOut)
{
	sxu16 iNext,iOfft;
	sxu32 iHash,nKey;
	lhcell *pCell;
	sxu64 nData;
	int rc;
	/* Offset this cell is stored */
	iOfft = (sxu16)(zRaw - (const unsigned char *)pPage->pRaw->zData);
	/* 4 byte hash number */
	SyBigEndianUnpack32(zRaw,&iHash);
	zRaw += 4;	
	/* 4 byte key length  */
	SyBigEndianUnpack32(zRaw,&nKey);
	zRaw += 4;	
	/* 8 byte data length */
	SyBigEndianUnpack64(zRaw,&nData);
	zRaw += 8;
	/* 2 byte offset of the next cell */
	SyBigEndianUnpack16(zRaw,&iNext);
	/* Perform a sanity check */
	if( iNext > 0 && &pPage->pRaw->zData[iNext] >= zEnd ){
		return UNQLITE_CORRUPT;
	}
	zRaw += 2;
	pCell = lhNewCell(pPage->pHash,pPage);
	if( pCell == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Fill in the structure */
	pCell->iNext = iNext;
	pCell->nKey  = nKey;
	pCell->nData = nData;
	pCell->nHash = iHash;
	/* Overflow page if any */
	SyBigEndianUnpack64(zRaw,&pCell->iOvfl);
	zRaw += 8;
	/* Cell offset */
	pCell->iStart = iOfft;
	/* Consume the key */
	rc = lhConsumeCellkey(pCell,unqliteDataConsumer,&pCell->sKey,pCell->nKey > 262144 /* 256 KB */? 1 : 0);
	if( rc != UNQLITE_OK ){
		/* TICKET: 14-32-chm@symisc.net: Key too large for memory */
		SyBlobRelease(&pCell->sKey);
	}
	/* Finally install the cell */
	rc = lhInstallCell(pCell);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	if( ppOut ){
		*ppOut = pCell;
	}
	return UNQLITE_OK;
}
/*
 * Compute the total number of free space on a given page.
 */
static int lhPageFreeSpace(lhpage *pPage)
{
	const unsigned char *zEnd,*zRaw = pPage->pRaw->zData;
	lhphdr *pHdr = &pPage->sHdr;
	sxu16 iNext,iAmount;
	sxu16 nFree = 0;
	if( pHdr->iFree < 1 ){
		/* Don't bother processing, the page is full */
		pPage->nFree = 0;
		return UNQLITE_OK;
	}
	/* Point to first free block */
	zEnd = &zRaw[pPage->pHash->iPageSize];
	zRaw += pHdr->iFree;
	for(;;){
		/* Offset of the next free block */
		SyBigEndianUnpack16(zRaw,&iNext);
		zRaw += 2;
		/* Available space on this block */
		SyBigEndianUnpack16(zRaw,&iAmount);
		nFree += iAmount;
		if( iNext < 1 ){
			/* No more free blocks */
			break;
		}
		/* Point to the next free block*/
		zRaw = &pPage->pRaw->zData[iNext];
		if( zRaw >= zEnd ){
			/* Corrupt page */
			return UNQLITE_CORRUPT;
		}
	}
	/* Save the amount of free space */
	pPage->nFree = nFree;
	return UNQLITE_OK;
}
/*
 * Given a primary page, load all its cell.
 */
static int lhLoadCells(lhpage *pPage)
{
	const unsigned char *zEnd,*zRaw = pPage->pRaw->zData;
	lhphdr *pHdr = &pPage->sHdr;
	lhcell *pCell = 0; /* cc warning */
	int rc;
	/* Calculate the amount of free space available first */
	rc = lhPageFreeSpace(pPage);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	if( pHdr->iOfft < 1 ){
		/* Don't bother processing, the page is empty */
		return UNQLITE_OK;
	}
	/* Point to first cell */
	zRaw += pHdr->iOfft;
	zEnd = &zRaw[pPage->pHash->iPageSize];
	for(;;){
		/* Parse a single cell */
		rc = lhParseOneCell(pPage,zRaw,zEnd,&pCell);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		if( pCell->iNext < 1 ){
			/* No more cells */
			break;
		}
		/* Point to the next cell */
		zRaw = &pPage->pRaw->zData[pCell->iNext];
		if( zRaw >= zEnd ){
			/* Corrupt page */
			return UNQLITE_CORRUPT;
		}
	}
	/* All done */
	return UNQLITE_OK;
}
/*
 * Given a page, parse its raw headers.
 */
static int lhParsePageHeader(lhpage *pPage)
{
	const unsigned char *zRaw = pPage->pRaw->zData;
	lhphdr *pHdr = &pPage->sHdr;
	/* Offset of the first cell */
	SyBigEndianUnpack16(zRaw,&pHdr->iOfft);
	zRaw += 2;
	/* Offset of the first free block */
	SyBigEndianUnpack16(zRaw,&pHdr->iFree);
	zRaw += 2;
	/* Slave page number */
	SyBigEndianUnpack64(zRaw,&pHdr->iSlave);
	/* All done */
	return UNQLITE_OK;
}
/*
 * Allocate a new page instance.
 */
static lhpage * lhNewPage(
	lhash_kv_engine *pEngine, /* KV store which own this instance */
	unqlite_page *pRaw,       /* Raw page contents */
	lhpage *pMaster           /* Master page in case we are dealing with a slave page */
	)
{
	lhpage *pPage;
	/* Allocate a new instance */
	pPage = (lhpage *)SyMemBackendPoolAlloc(&pEngine->sAllocator,sizeof(lhpage));
	if( pPage == 0 ){
		return 0;
	}
	/* Zero the structure */
	SyZero(pPage,sizeof(lhpage));
	/* Fill-in the structure */
	pPage->pHash = pEngine;
	pPage->pRaw = pRaw;
	pPage->pMaster = pMaster ? pMaster /* Slave page */ : pPage /* Master page */ ;
	if( pPage->pMaster != pPage ){
		/* Slave page, attach it to its master */
		pPage->pNextSlave = pMaster->pSlave;
		pMaster->pSlave = pPage;
		pMaster->iSlave++;
	}
	/* Save this instance for future fast lookup */
	pRaw->pUserData = pPage;
	/* All done */
	return pPage;
}
/*
 * Load a primary and its associated slave pages from disk.
 */
static int lhLoadPage(lhash_kv_engine *pEngine,pgno pnum,lhpage *pMaster,lhpage **ppOut,int iNest)
{
	unqlite_page *pRaw;
	lhpage *pPage = 0; /* cc warning */
	int rc;
	/* Aquire the page from the pager first */
	rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,pnum,&pRaw);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	if( pRaw->pUserData ){
		/* The page is already parsed and loaded in memory. Point to it */
		pPage = (lhpage *)pRaw->pUserData;
	}else{
		/* Allocate a new page */
		pPage = lhNewPage(pEngine,pRaw,pMaster);
		if( pPage == 0 ){
			return UNQLITE_NOMEM;
		}
		/* Process the page */
		rc = lhParsePageHeader(pPage);
		if( rc == UNQLITE_OK ){
			/* Load cells */
			rc = lhLoadCells(pPage);
		}
		if( rc != UNQLITE_OK ){
			pEngine->pIo->xPageUnref(pPage->pRaw); /* pPage will be released inside this call */
			return rc;
		}
		if( pPage->sHdr.iSlave > 0 && iNest < 128 ){
			if( pMaster == 0 ){
				pMaster = pPage;
			}
			/* Slave page. Not a fatal error if something goes wrong here */
			lhLoadPage(pEngine,pPage->sHdr.iSlave,pMaster,0,iNest++);
		}
	}
	if( ppOut ){
		*ppOut = pPage;
	}
	return UNQLITE_OK;
}
/*
 * Given a cell, Consume its key by invoking the given callback for each extracted chunk.
 */
static int lhConsumeCellkey(
	lhcell *pCell, /* Target cell */
	int (*xConsumer)(const void *,unsigned int,void *), /* Consumer callback */
	void *pUserData, /* Last argument to xConsumer() */
	int offt_only
	)
{
	lhpage *pPage = pCell->pPage;
	const unsigned char *zRaw = pPage->pRaw->zData;
	const unsigned char *zPayload;
	int rc;
	/* Point to the payload area */
	zPayload = &zRaw[pCell->iStart];
	if( pCell->iOvfl == 0 ){
		/* Best scenario, consume the key directly without any overflow page */
		zPayload += L_HASH_CELL_SZ;
		rc = xConsumer((const void *)zPayload,pCell->nKey,pUserData);
		if( rc != UNQLITE_OK ){
			rc = UNQLITE_ABORT;
		}
	}else{
		lhash_kv_engine *pEngine = pPage->pHash;
		sxu32 nByte,nData = pCell->nKey;
		unqlite_page *pOvfl;
		int data_offset = 0;
		pgno iOvfl;
		/* Overflow page */
		iOvfl = pCell->iOvfl;
		/* Total usable bytes in an overflow page */
		nByte = L_HASH_OVERFLOW_SIZE(pEngine->iPageSize);
		for(;;){
			if( iOvfl == 0 || nData < 1 ){
				/* no more overflow page */
				break;
			}
			/* Point to the overflow page */
			rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,iOvfl,&pOvfl);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			zPayload = &pOvfl->zData[8];
			/* Point to the raw content */
			if( !data_offset ){
				/* Get the data page and offset */
				SyBigEndianUnpack64(zPayload,&pCell->iDataPage);
				zPayload += 8;
				SyBigEndianUnpack16(zPayload,&pCell->iDataOfft);
				zPayload += 2;
				if( offt_only ){
					/* Key too large, grab the data offset and return */
					pEngine->pIo->xPageUnref(pOvfl);
					return UNQLITE_OK;
				}
				data_offset = 1;
			}
			/* Consume the key */
			if( nData <= nByte ){
				rc = xConsumer((const void *)zPayload,nData,pUserData);
				if( rc != UNQLITE_OK ){
					pEngine->pIo->xPageUnref(pOvfl);
					return UNQLITE_ABORT;
				}
				nData = 0;
			}else{
				rc = xConsumer((const void *)zPayload,nByte,pUserData);
				if( rc != UNQLITE_OK ){
					pEngine->pIo->xPageUnref(pOvfl);
					return UNQLITE_ABORT;
				}
				nData -= nByte;
			}
			/* Next overflow page in the chain */
			SyBigEndianUnpack64(pOvfl->zData,&iOvfl);
			/* Unref the page */
			pEngine->pIo->xPageUnref(pOvfl);
		}
		rc = UNQLITE_OK;
	}
	return rc;
}
/*
 * Given a cell, Consume its data by invoking the given callback for each extracted chunk.
 */
static int lhConsumeCellData(
	lhcell *pCell, /* Target cell */
	int (*xConsumer)(const void *,unsigned int,void *), /* Data consumer callback */
	void *pUserData /* Last argument to xConsumer() */
	)
{
	lhpage *pPage = pCell->pPage;
	const unsigned char *zRaw = pPage->pRaw->zData;
	const unsigned char *zPayload;
	int rc;
	/* Point to the payload area */
	zPayload = &zRaw[pCell->iStart];
	if( pCell->iOvfl == 0 ){
		/* Best scenario, consume the data directly without any overflow page */
		zPayload += L_HASH_CELL_SZ + pCell->nKey;
		rc = xConsumer((const void *)zPayload,(sxu32)pCell->nData,pUserData);
		if( rc != UNQLITE_OK ){
			rc = UNQLITE_ABORT;
		}
	}else{
		lhash_kv_engine *pEngine = pPage->pHash;
		sxu64 nData = pCell->nData;
		unqlite_page *pOvfl;
		int fix_offset = 0;
		sxu32 nByte;
		pgno iOvfl;
		/* Overflow page where data is stored */
		iOvfl = pCell->iDataPage;
		for(;;){
			if( iOvfl == 0 || nData < 1 ){
				/* no more overflow page */
				break;
			}
			/* Point to the overflow page */
			rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,iOvfl,&pOvfl);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			/* Point to the raw content */
			zPayload = pOvfl->zData;
			if( !fix_offset ){
				/* Point to the data */
				zPayload += pCell->iDataOfft;
				nByte = pEngine->iPageSize - pCell->iDataOfft;
				fix_offset = 1;
			}else{
				zPayload += 8;
				/* Total usable bytes in an overflow page */
				nByte = L_HASH_OVERFLOW_SIZE(pEngine->iPageSize);
			}
			/* Consume the data */
			if( nData <= (sxu64)nByte ){
				rc = xConsumer((const void *)zPayload,(unsigned int)nData,pUserData);
				if( rc != UNQLITE_OK ){
					pEngine->pIo->xPageUnref(pOvfl);
					return UNQLITE_ABORT;
				}
				nData = 0;
			}else{
				if( nByte > 0 ){
					rc = xConsumer((const void *)zPayload,nByte,pUserData);
					if( rc != UNQLITE_OK ){
						pEngine->pIo->xPageUnref(pOvfl);
						return UNQLITE_ABORT;
					}
					nData -= nByte;
				}
			}
			/* Next overflow page in the chain */
			SyBigEndianUnpack64(pOvfl->zData,&iOvfl);
			/* Unref the page */
			pEngine->pIo->xPageUnref(pOvfl);
		}
		rc = UNQLITE_OK;
	}
	return rc;
}
/*
 * Read the linear hash header (Page one of the database).
 */
static int lhash_read_header(lhash_kv_engine *pEngine,unqlite_page *pHeader)
{
	const unsigned char *zRaw = pHeader->zData;
	lhash_bmap_page *pMap;
	sxu32 nHash;
	int rc;
	pEngine->pHeader = pHeader;
	/* 4 byte magic number */
	SyBigEndianUnpack32(zRaw,&pEngine->nMagic);
	zRaw += 4;
	if( pEngine->nMagic != L_HASH_MAGIC ){
		/* Corrupt implementation */
		return UNQLITE_CORRUPT;
	}
	/* 4 byte hash value to identify a valid hash function */
	SyBigEndianUnpack32(zRaw,&nHash);
	zRaw += 4;
	/* Sanity check */
	if( pEngine->xHash(L_HASH_WORD,sizeof(L_HASH_WORD)-1) != nHash ){
		/* Different hash function */
		pEngine->pIo->xErr(pEngine->pIo->pHandle,"Invalid hash function");
		return UNQLITE_INVALID;
	}
	/* List of free pages */
	SyBigEndianUnpack64(zRaw,&pEngine->nFreeList);
	zRaw += 8;
	/* Current split bucket */
	SyBigEndianUnpack64(zRaw,&pEngine->split_bucket);
	zRaw += 8;
	/* Maximum split bucket */
	SyBigEndianUnpack64(zRaw,&pEngine->max_split_bucket);
	zRaw += 8;
	/* Next generation */
	pEngine->nmax_split_nucket = pEngine->max_split_bucket << 1;
	/* Initialiaze the bucket map */
	pMap = &pEngine->sPageMap;
	/* Fill in the structure */
	pMap->iNum = pHeader->pgno;
	/* Next page in the bucket map */
	SyBigEndianUnpack64(zRaw,&pMap->iNext);
	zRaw += 8;
	/* Total number of records in the bucket map (This page only) */
	SyBigEndianUnpack32(zRaw,&pMap->nRec);
	zRaw += 4;
	pMap->iPtr = (sxu16)(zRaw - pHeader->zData);
	/* Load the map in memory */
	rc = lhMapLoadPage(pEngine,pMap,pHeader->zData);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Load the bucket map chain if any */
	for(;;){
		pgno iNext = pMap->iNext;
		unqlite_page *pPage;
		if( iNext == 0 ){
			/* No more map pages */
			break;
		}
		/* Point to the target page */
		rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,iNext,&pPage);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		/* Fill in the structure */
		pMap->iNum = iNext;
		pMap->iPtr = 0;
		/* Load the map in memory */
		rc = lhMapLoadPage(pEngine,pMap,pPage->zData);
		if( rc != UNQLITE_OK ){
			return rc;
		}
	}
	/* All done */
	return UNQLITE_OK;
}
/*
 * Perform a record lookup.
 */
static int lhRecordLookup(
	lhash_kv_engine *pEngine, /* KV storage engine */
	const void *pKey,         /* Lookup key */
	sxu32 nByte,              /* Key length */
	lhcell **ppCell           /* OUT: Target cell on success */
	)
{
	lhash_bmap_rec *pRec;
	lhpage *pPage;
	lhcell *pCell;
	pgno iBucket;
	sxu32 nHash;
	int rc;
	/* Acquire the first page (hash Header) so that everything gets loaded autmatically */
	rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,1,0);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Compute the hash of the key first */
	nHash = pEngine->xHash(pKey,nByte);
	/* Extract the logical (i.e. not real) page number */
	iBucket = nHash & (pEngine->nmax_split_nucket - 1);
	if( iBucket >= (pEngine->split_bucket + pEngine->max_split_bucket) ){
		/* Low mask */
		iBucket = nHash & (pEngine->max_split_bucket - 1);
	}
	/* Map the logical bucket number to real page number */
	pRec = lhMapFindBucket(pEngine,iBucket);
	if( pRec == 0 ){
		/* No such entry */
		return UNQLITE_NOTFOUND;
	}
	/* Load the master page and it's slave page in-memory  */
	rc = lhLoadPage(pEngine,pRec->iReal,0,&pPage,0);
	if( rc != UNQLITE_OK ){
		/* IO error, unlikely scenario */
		return rc;
	}
	/* Lookup for the cell */
	pCell = lhFindCell(pPage,pKey,nByte,nHash);
	if( pCell == 0 ){
		/* No such entry */
		return UNQLITE_NOTFOUND;
	}
	if( ppCell ){
		*ppCell = pCell;
	}
	return UNQLITE_OK;
}
/*
 * Acquire a new page either from the free list or ask the pager
 * for a new one.
 */
static int lhAcquirePage(lhash_kv_engine *pEngine,unqlite_page **ppOut)
{
	unqlite_page *pPage;
	int rc;
	if( pEngine->nFreeList != 0 ){
		/* Acquire one from the free list */
		rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,pEngine->nFreeList,&pPage);
		if( rc == UNQLITE_OK ){
			/* Point to the next free page */
			SyBigEndianUnpack64(pPage->zData,&pEngine->nFreeList);
			/* Update the database header */
			rc = pEngine->pIo->xWrite(pEngine->pHeader);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			SyBigEndianPack64(&pEngine->pHeader->zData[4/*Magic*/+4/*Hash*/],pEngine->nFreeList);
			/* Tell the pager do not journal this page */
			pEngine->pIo->xDontJournal(pPage);
			/* Return to the caller */
			*ppOut = pPage;
			/* All done */
			return UNQLITE_OK;
		}
	}
	/* Acquire a new page */
	rc = pEngine->pIo->xNew(pEngine->pIo->pHandle,&pPage);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Point to the target page */
	*ppOut = pPage;
	return UNQLITE_OK;
}
/*
 * Write a bucket map record to disk.
 */
static int lhMapWriteRecord(lhash_kv_engine *pEngine,pgno iLogic,pgno iReal)
{
	lhash_bmap_page *pMap = &pEngine->sPageMap;
	unqlite_page *pPage = 0;
	int rc;
	if( pMap->iPtr > (pEngine->iPageSize - 16) /* 8 byte logical bucket number + 8 byte real bucket number */ ){
		unqlite_page *pOld;
		/* Point to the old page */
		rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,pMap->iNum,&pOld);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		/* Acquire a new page */
		rc = lhAcquirePage(pEngine,&pPage);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		/* Reflect the change  */
		pMap->iNext = 0;
		pMap->iNum = pPage->pgno;
		pMap->nRec = 0;
		pMap->iPtr = 8/* Next page number */+4/* Total records in the map*/;
		/* Link this page */
		rc = pEngine->pIo->xWrite(pOld);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		if( pOld->pgno == pEngine->pHeader->pgno ){
			/* First page (Hash header) */
			SyBigEndianPack64(&pOld->zData[4/*magic*/+4/*hash*/+8/* Free page */+8/*current split bucket*/+8/*Maximum split bucket*/],pPage->pgno);
		}else{
			/* Link the new page */
			SyBigEndianPack64(pOld->zData,pPage->pgno);
			/* Unref */
			pEngine->pIo->xPageUnref(pOld);
		}
		/* Assume the last bucket map page */
		rc = pEngine->pIo->xWrite(pPage);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		SyBigEndianPack64(pPage->zData,0); /* Next bucket map page on the list */
	}
	if( pPage == 0){
		/* Point to the current map page */
		rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,pMap->iNum,&pPage);
		if( rc != UNQLITE_OK ){
			return rc;
		}
	}
	/* Make page writable */
	rc = pEngine->pIo->xWrite(pPage);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Write the data */
	SyBigEndianPack64(&pPage->zData[pMap->iPtr],iLogic);
	pMap->iPtr += 8;
	SyBigEndianPack64(&pPage->zData[pMap->iPtr],iReal);
	pMap->iPtr += 8;
	/* Install the bucket map */
	rc = lhMapInstallBucket(pEngine,iLogic,iReal);
	if( rc == UNQLITE_OK ){
		/* Total number of records */
		pMap->nRec++;
		if( pPage->pgno == pEngine->pHeader->pgno ){
			/* Page one: Always writable */
			SyBigEndianPack32(
				&pPage->zData[4/*magic*/+4/*hash*/+8/* Free page */+8/*current split bucket*/+8/*Maximum split bucket*/+8/*Next map page*/],
				pMap->nRec);
		}else{
			/* Make page writable */
			rc = pEngine->pIo->xWrite(pPage);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			SyBigEndianPack32(&pPage->zData[8],pMap->nRec);
		}
	}
	return rc;
}
/*
 * Defragment a page.
 */
static int lhPageDefragment(lhpage *pPage)
{
	lhash_kv_engine *pEngine = pPage->pHash;
	unsigned char *zTmp,*zPtr,*zEnd,*zPayload;
	lhcell *pCell;
	/* Get a temporary page from the pager. This opertaion never fail */
	zTmp = pEngine->pIo->xTmpPage(pEngine->pIo->pHandle);
	/* Move the target cells to the beginning */
	pCell = pPage->pMaster->pList;
	/* Write the slave page number */
	SyBigEndianPack64(&zTmp[2/*Offset of the first cell */+2/*Offset of the first free block */],pPage->sHdr.iSlave);
	zPtr = &zTmp[L_HASH_PAGE_HDR_SZ]; /* Offset to start writing from */
	zEnd = &zTmp[pEngine->iPageSize];
	pPage->sHdr.iOfft = 0; /* Offset of the first cell */
	for(;;){
		if( pCell == 0 ){
			/* No more cells */
			break;
		}
		if( pCell->pPage->pRaw->pgno == pPage->pRaw->pgno ){
			/* Cell payload if locally stored */
			zPayload = 0;
			if( pCell->iOvfl == 0 ){
				zPayload = &pCell->pPage->pRaw->zData[pCell->iStart + L_HASH_CELL_SZ];
			}
			/* Move the cell */
			pCell->iNext = pPage->sHdr.iOfft;
			pCell->iStart = (sxu16)(zPtr - zTmp); /* Offset where this cell start */
			pPage->sHdr.iOfft = pCell->iStart;
			/* Write the cell header */
			/* 4 byte hash number */
			SyBigEndianPack32(zPtr,pCell->nHash);
			zPtr += 4;
			/* 4 byte ley length */
			SyBigEndianPack32(zPtr,pCell->nKey);
			zPtr += 4;
			/* 8 byte data length */
			SyBigEndianPack64(zPtr,pCell->nData);
			zPtr += 8;
			/* 2 byte offset of the next cell */
			SyBigEndianPack16(zPtr,pCell->iNext);
			zPtr += 2;
			/* 8 byte overflow page number */
			SyBigEndianPack64(zPtr,pCell->iOvfl);
			zPtr += 8;
			if( zPayload ){
				/* Local payload */
				SyMemcpy((const void *)zPayload,zPtr,(sxu32)(pCell->nKey + pCell->nData));
				zPtr += pCell->nKey + pCell->nData;
			}
			if( zPtr >= zEnd ){
				/* Can't happen */
				break;
			}
		}
		/* Point to the next page */
		pCell = pCell->pNext;
	}
	/* Mark the free block */
	pPage->nFree = (sxu16)(zEnd - zPtr); /* Block length */
	if( pPage->nFree > 3 ){
		pPage->sHdr.iFree = (sxu16)(zPtr - zTmp); /* Offset of the free block */
		/* Mark the block */
		SyBigEndianPack16(zPtr,0); /* Offset of the next free block */
		SyBigEndianPack16(&zPtr[2],pPage->nFree); /* Block length */
	}else{
		/* Block of length less than 4 bytes are simply discarded */
		pPage->nFree = 0;
		pPage->sHdr.iFree = 0;
	}
	/* Reflect the change */
	SyBigEndianPack16(zTmp,pPage->sHdr.iOfft);     /* Offset of the first cell */
	SyBigEndianPack16(&zTmp[2],pPage->sHdr.iFree); /* Offset of the first free block */
	SyMemcpy((const void *)zTmp,pPage->pRaw->zData,pEngine->iPageSize);
	/* All done */
	return UNQLITE_OK;
}
/*
** Allocate nByte bytes of space on a page.
**
** Return the index into pPage->pRaw->zData[] of the first byte of
** the new allocation. Or return 0 if there is not enough free
** space on the page to satisfy the allocation request.
**
** If the page contains nBytes of free space but does not contain
** nBytes of contiguous free space, then this routine automatically
** calls defragementPage() to consolidate all free space before 
** allocating the new chunk.
*/
static int lhAllocateSpace(lhpage *pPage,sxu64 nAmount,sxu16 *pOfft)
{
	const unsigned char *zEnd,*zPtr;
	sxu16 iNext,iBlksz,nByte;
	unsigned char *zPrev;
	int rc;
	if( (sxu64)pPage->nFree < nAmount ){
		/* Don't bother looking for a free chunk */
		return UNQLITE_FULL;
	}
	if( pPage->nCell < 10 && ((int)nAmount >= (pPage->pHash->iPageSize / 2)) ){
		/* Big chunk need an overflow page for its data */
		return UNQLITE_FULL;
	}
	zPtr = &pPage->pRaw->zData[pPage->sHdr.iFree];
	zEnd = &pPage->pRaw->zData[pPage->pHash->iPageSize];
	nByte = (sxu16)nAmount;
	zPrev = 0;
	iBlksz = 0; /* cc warning */
	/* Perform the lookup */
	for(;;){
		if( zPtr >= zEnd ){
			return UNQLITE_FULL;
		}
		/* Offset of the next free block */
		SyBigEndianUnpack16(zPtr,&iNext);
		/* Block size */
		SyBigEndianUnpack16(&zPtr[2],&iBlksz);
		if( iBlksz >= nByte ){
			/* Got one */
			break;
		}
		zPrev = (unsigned char *)zPtr;
		if( iNext == 0 ){
			/* No more free blocks, defragment the page */
			rc = lhPageDefragment(pPage);
			if( rc == UNQLITE_OK && pPage->nFree >= nByte) {
				/* Free blocks are merged together */
				iNext = 0;
				zPtr = &pPage->pRaw->zData[pPage->sHdr.iFree];
				iBlksz = pPage->nFree;
				zPrev = 0;
				break;
			}else{
				return UNQLITE_FULL;
			}
		}
		/* Point to the next free block */
		zPtr = &pPage->pRaw->zData[iNext];
	}
	/* Acquire writer lock on this page */
	rc = pPage->pHash->pIo->xWrite(pPage->pRaw);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Save block offset */
	*pOfft = (sxu16)(zPtr - pPage->pRaw->zData);
	/* Fix pointers */
	if( iBlksz >= nByte && (iBlksz - nByte) > 3 ){
		unsigned char *zBlock = &pPage->pRaw->zData[(*pOfft) + nByte];
		/* Create a new block */
		zPtr = zBlock;
		SyBigEndianPack16(zBlock,iNext); /* Offset of the next block */
		SyBigEndianPack16(&zBlock[2],iBlksz-nByte); /* Block size*/
		/* Offset of the new block */
		iNext = (sxu16)(zPtr - pPage->pRaw->zData);
		iBlksz = nByte;
	}
	/* Fix offsets */
	if( zPrev ){
		SyBigEndianPack16(zPrev,iNext);
	}else{
		/* First block */
		pPage->sHdr.iFree = iNext;
		/* Reflect on the page header */
		SyBigEndianPack16(&pPage->pRaw->zData[2/* Offset of the first cell1*/],iNext);
	}
	/* All done */
	pPage->nFree -= iBlksz;
	return UNQLITE_OK;
}
/*
 * Write the cell header into the corresponding offset.
 */
static int lhCellWriteHeader(lhcell *pCell)
{
	lhpage *pPage = pCell->pPage;
	unsigned char *zRaw = pPage->pRaw->zData;
	/* Seek to the desired location */
	zRaw += pCell->iStart;
	/* 4 byte hash number */
	SyBigEndianPack32(zRaw,pCell->nHash);
	zRaw += 4;
	/* 4 byte key length */
	SyBigEndianPack32(zRaw,pCell->nKey);
	zRaw += 4;
	/* 8 byte data length */
	SyBigEndianPack64(zRaw,pCell->nData);
	zRaw += 8;
	/* 2 byte offset of the next cell */
	pCell->iNext = pPage->sHdr.iOfft;
	SyBigEndianPack16(zRaw,pCell->iNext);
	zRaw += 2;
	/* 8 byte overflow page number */
	SyBigEndianPack64(zRaw,pCell->iOvfl);
	/* Update the page header */
	pPage->sHdr.iOfft = pCell->iStart;
	/* pEngine->pIo->xWrite() has been successfully called on this page */
	SyBigEndianPack16(pPage->pRaw->zData,pCell->iStart);
	/* All done */
	return UNQLITE_OK;
}
/*
 * Write local payload.
 */
static int lhCellWriteLocalPayload(lhcell *pCell,
	const void *pKey,sxu32 nKeylen,
	const void *pData,unqlite_int64 nDatalen
	)
{
	/* A writer lock have been acquired on this page */
	lhpage *pPage = pCell->pPage;
	unsigned char *zRaw = pPage->pRaw->zData;
	/* Seek to the desired location */
	zRaw += pCell->iStart + L_HASH_CELL_SZ;
	/* Write the key */
	SyMemcpy(pKey,(void *)zRaw,nKeylen);
	zRaw += nKeylen;
	if( nDatalen > 0 ){
		/* Write the Data */
		SyMemcpy(pData,(void *)zRaw,(sxu32)nDatalen);
	}
	return UNQLITE_OK;
}
/*
 * Allocate as much overflow page we need to store the cell payload.
 */
static int lhCellWriteOvflPayload(lhcell *pCell,const void *pKey,sxu32 nKeylen,...)
{
	lhpage *pPage = pCell->pPage;
	lhash_kv_engine *pEngine = pPage->pHash;
	unqlite_page *pOvfl,*pFirst,*pNew;
	const unsigned char *zPtr,*zEnd;
	unsigned char *zRaw,*zRawEnd;
	sxu32 nAvail;
	va_list ap;
	int rc;
	/* Acquire a new overflow page */
	rc = lhAcquirePage(pEngine,&pOvfl);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Acquire a writer lock */
	rc = pEngine->pIo->xWrite(pOvfl);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	pFirst = pOvfl;
	/* Link */
	pCell->iOvfl = pOvfl->pgno;
	/* Update the cell header */
	SyBigEndianPack64(&pPage->pRaw->zData[pCell->iStart + 4/*Hash*/ + 4/*Key*/ + 8/*Data*/ + 2 /*Next cell*/],pCell->iOvfl);
	/* Start the write process */
	zPtr = (const unsigned char *)pKey;
	zEnd = &zPtr[nKeylen];
	SyBigEndianPack64(pOvfl->zData,0); /* Next overflow page on the chain */
	zRaw = &pOvfl->zData[8/* Next ovfl page*/ + 8 /* Data page */ + 2 /* Data offset*/];
	zRawEnd = &pOvfl->zData[pEngine->iPageSize];
	pNew = pOvfl;
	/* Write the key */
	for(;;){
		if( zPtr >= zEnd ){
			break;
		}
		if( zRaw >= zRawEnd ){
			/* Acquire a new page */
			rc = lhAcquirePage(pEngine,&pNew);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			rc = pEngine->pIo->xWrite(pNew);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			/* Link */
			SyBigEndianPack64(pOvfl->zData,pNew->pgno);
			pEngine->pIo->xPageUnref(pOvfl);
			SyBigEndianPack64(pNew->zData,0); /* Next overflow page on the chain */
			pOvfl = pNew;
			zRaw = &pNew->zData[8];
			zRawEnd = &pNew->zData[pEngine->iPageSize];
		}
		nAvail = (sxu32)(zRawEnd-zRaw);
		nKeylen = (sxu32)(zEnd-zPtr);
		if( nKeylen > nAvail ){
			nKeylen = nAvail;
		}
		SyMemcpy((const void *)zPtr,(void *)zRaw,nKeylen);
		/* Synchronize pointers */
		zPtr += nKeylen;
		zRaw += nKeylen;
	}
	rc = UNQLITE_OK;
	va_start(ap,nKeylen);
	pCell->iDataPage = pNew->pgno;
	pCell->iDataOfft = (sxu16)(zRaw-pNew->zData);
	/* Write the data page and its offset */
	SyBigEndianPack64(&pFirst->zData[8/*Next ovfl*/],pCell->iDataPage);
	SyBigEndianPack16(&pFirst->zData[8/*Next ovfl*/+8/*Data page*/],pCell->iDataOfft);
	/* Write data */
	for(;;){
		const void *pData;
		sxu32 nDatalen;
		sxu64 nData;
		pData = va_arg(ap,const void *);
		if( pData == 0 ){
			/* No more chunks */
			break;
		}
		nData = va_arg(ap,sxu64);
		/* Write this chunk */
		zPtr = (const unsigned char *)pData;
		zEnd = &zPtr[nData];
		for(;;){
			if( zPtr >= zEnd ){
				break;
			}
			if( zRaw >= zRawEnd ){
				/* Acquire a new page */
				rc = lhAcquirePage(pEngine,&pNew);
				if( rc != UNQLITE_OK ){
					va_end(ap);
					return rc;
				}
				rc = pEngine->pIo->xWrite(pNew);
				if( rc != UNQLITE_OK ){
					va_end(ap);
					return rc;
				}
				/* Link */
				SyBigEndianPack64(pOvfl->zData,pNew->pgno);
				pEngine->pIo->xPageUnref(pOvfl);
				SyBigEndianPack64(pNew->zData,0); /* Next overflow page on the chain */
				pOvfl = pNew;
				zRaw = &pNew->zData[8];
				zRawEnd = &pNew->zData[pEngine->iPageSize];
			}
			nAvail = (sxu32)(zRawEnd-zRaw);
			nDatalen = (sxu32)(zEnd-zPtr);
			if( nDatalen > nAvail ){
				nDatalen = nAvail;
			}
			SyMemcpy((const void *)zPtr,(void *)zRaw,nDatalen);
			/* Synchronize pointers */
			zPtr += nDatalen;
			zRaw += nDatalen;
		}
	}
	/* Unref the overflow page */
	pEngine->pIo->xPageUnref(pOvfl);
	va_end(ap);
	return UNQLITE_OK;
}
/*
 * Restore a page to the free list.
 */
static int lhRestorePage(lhash_kv_engine *pEngine,unqlite_page *pPage)
{
	int rc;
	rc = pEngine->pIo->xWrite(pEngine->pHeader);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	rc = pEngine->pIo->xWrite(pPage);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Link to the list of free page */
	SyBigEndianPack64(pPage->zData,pEngine->nFreeList);
	pEngine->nFreeList = pPage->pgno;
	SyBigEndianPack64(&pEngine->pHeader->zData[4/*Magic*/+4/*Hash*/],pEngine->nFreeList);
	/* All done */
	return UNQLITE_OK;
}
/*
 * Restore cell space and mark it as a free block.
 */
static int lhRestoreSpace(lhpage *pPage,sxu16 iOfft,sxu16 nByte)
{
	unsigned char *zRaw;
	if( nByte < 4 ){
		/* At least 4 bytes of freespace must form a valid block */
		return UNQLITE_OK;
	}
	/* pEngine->pIo->xWrite() has been successfully called on this page */
	zRaw = &pPage->pRaw->zData[iOfft];
	/* Mark as a free block */
	SyBigEndianPack16(zRaw,pPage->sHdr.iFree); /* Offset of the next free block */
	zRaw += 2;
	SyBigEndianPack16(zRaw,nByte);
	/* Link */
	SyBigEndianPack16(&pPage->pRaw->zData[2/* offset of the first cell */],iOfft);
	pPage->sHdr.iFree = iOfft;
	pPage->nFree += nByte;
	return UNQLITE_OK;
}
/* Forward declaration */
static lhcell * lhFindSibeling(lhcell *pCell);
/*
 * Unlink a cell.
 */
static int lhUnlinkCell(lhcell *pCell)
{
	lhash_kv_engine *pEngine = pCell->pPage->pHash;
	lhpage *pPage = pCell->pPage;
	sxu16 nByte = L_HASH_CELL_SZ;
	lhcell *pPrev;
	int rc;
	rc = pEngine->pIo->xWrite(pPage->pRaw);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Bring the link */
	pPrev = lhFindSibeling(pCell);
	if( pPrev ){
		pPrev->iNext = pCell->iNext;
		/* Fix offsets in the page header */
		SyBigEndianPack16(&pPage->pRaw->zData[pPrev->iStart + 4/*Hash*/+4/*Key*/+8/*Data*/],pCell->iNext);
	}else{
		/* First entry on this page (either master or slave) */
		pPage->sHdr.iOfft = pCell->iNext;
		/* Update the page header */
		SyBigEndianPack16(pPage->pRaw->zData,pCell->iNext);
	}
	/* Restore cell space */
	if( pCell->iOvfl == 0 ){
		nByte += (sxu16)(pCell->nData + pCell->nKey);
	}
	lhRestoreSpace(pPage,pCell->iStart,nByte);
	/* Discard the cell from the in-memory hashtable */
	lhCellDiscard(pCell);
	return UNQLITE_OK;
}
/*
 * Remove a cell and its paylod (key + data).
 */
static int lhRecordRemove(lhcell *pCell)
{
	lhash_kv_engine *pEngine = pCell->pPage->pHash;
	int rc;
	if( pCell->iOvfl > 0){
		/* Discard overflow pages */
		unqlite_page *pOvfl;
		pgno iNext = pCell->iOvfl;
		for(;;){
			/* Point to the overflow page */
			rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,iNext,&pOvfl);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			/* Next page on the chain */
			SyBigEndianUnpack64(pOvfl->zData,&iNext);
			/* Restore the page to the free list */
			rc = lhRestorePage(pEngine,pOvfl);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			/* Unref */
			pEngine->pIo->xPageUnref(pOvfl);
			if( iNext == 0 ){
				break;
			}
		}
	}
	/* Unlink the cell */
	rc = lhUnlinkCell(pCell);
	return rc;
}
/*
 * Find cell sibeling.
 */
static lhcell * lhFindSibeling(lhcell *pCell)
{
	lhpage *pPage = pCell->pPage->pMaster;
	lhcell *pEntry;
	pEntry = pPage->pFirst; 
	while( pEntry ){
		if( pEntry->pPage == pCell->pPage && pEntry->iNext == pCell->iStart ){
			/* Sibeling found */
			return pEntry;
		}
		/* Point to the previous entry */
		pEntry = pEntry->pPrev; 
	}
	/* Last inserted cell */
	return 0;
}
/*
 * Move a cell to a new location with its new data.
 */
static int lhMoveLocalCell(
	lhcell *pCell,
	sxu16 iOfft,
	const void *pData,
	unqlite_int64 nData
	)
{
	sxu16 iKeyOfft = pCell->iStart + L_HASH_CELL_SZ;
	lhpage *pPage = pCell->pPage;
	lhcell *pSibeling;
	pSibeling = lhFindSibeling(pCell);
	if( pSibeling ){
		/* Fix link */
		SyBigEndianPack16(&pPage->pRaw->zData[pSibeling->iStart + 4/*Hash*/+4/*Key*/+8/*Data*/],pCell->iNext);
		pSibeling->iNext = pCell->iNext;
	}else{
		/* First cell, update page header only */
		SyBigEndianPack16(pPage->pRaw->zData,pCell->iNext);
		pPage->sHdr.iOfft = pCell->iNext;
	}
	/* Set the new offset */
	pCell->iStart = iOfft;
	pCell->nData = (sxu64)nData;
	/* Write the cell payload */
	lhCellWriteLocalPayload(pCell,(const void *)&pPage->pRaw->zData[iKeyOfft],pCell->nKey,pData,nData);
	/* Finally write the cell header */
	lhCellWriteHeader(pCell);
	/* All done */
	return UNQLITE_OK;
}
/*
 * Overwrite an existing record.
 */
static int lhRecordOverwrite(
	lhcell *pCell,
	const void *pData,unqlite_int64 nByte
	)
{
	lhash_kv_engine *pEngine = pCell->pPage->pHash;
	unsigned char *zRaw,*zRawEnd,*zPayload;
	const unsigned char *zPtr,*zEnd;
	unqlite_page *pOvfl,*pOld,*pNew;
	lhpage *pPage = pCell->pPage;
	sxu32 nAvail;
	pgno iOvfl;
	int rc;
	/* Acquire a writer lock on this page */
	rc = pEngine->pIo->xWrite(pPage->pRaw);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	if( pCell->iOvfl == 0 ){
		/* Local payload, try to deal with the free space issues */
		zPayload = &pPage->pRaw->zData[pCell->iStart + L_HASH_CELL_SZ + pCell->nKey];
		if( pCell->nData == (sxu64)nByte ){
			/* Best scenario, simply a memcpy operation */
			SyMemcpy(pData,(void *)zPayload,(sxu32)nByte);
		}else if( (sxu64)nByte < pCell->nData ){
			/* Shorter data, not so ugly */
			SyMemcpy(pData,(void *)zPayload,(sxu32)nByte);
			/* Update the cell header */
			SyBigEndianPack64(&pPage->pRaw->zData[pCell->iStart + 4 /* Hash */ + 4 /* Key */],nByte);
			/* Restore freespace */
			lhRestoreSpace(pPage,(sxu16)(pCell->iStart + L_HASH_CELL_SZ + pCell->nKey + nByte),(sxu16)(pCell->nData - nByte));
			/* New data size */
			pCell->nData = (sxu64)nByte;
		}else{
			sxu16 iOfft = 0; /* cc warning */
			/* Check if another chunk is available for this cell */
			rc = lhAllocateSpace(pPage,L_HASH_CELL_SZ + pCell->nKey + nByte,&iOfft);
			if( rc != UNQLITE_OK ){
				/* Transfer the payload to an overflow page */
				rc = lhCellWriteOvflPayload(pCell,&pPage->pRaw->zData[pCell->iStart + L_HASH_CELL_SZ],pCell->nKey,pData,nByte,(const void *)0);
				if( rc != UNQLITE_OK ){
					return rc;
				}
				/* Update the cell header */
				SyBigEndianPack64(&pPage->pRaw->zData[pCell->iStart + 4 /* Hash */ + 4 /* Key */],(sxu64)nByte);
				/* Restore freespace */
				lhRestoreSpace(pPage,(sxu16)(pCell->iStart + L_HASH_CELL_SZ),(sxu16)(pCell->nKey + pCell->nData));
				/* New data size */
				pCell->nData = (sxu64)nByte;
			}else{
				sxu16 iOldOfft = pCell->iStart;
				sxu32 iOld = (sxu32)pCell->nData;
				/* Space is available, transfer the cell */
				lhMoveLocalCell(pCell,iOfft,pData,nByte);
				/* Restore cell space */
				lhRestoreSpace(pPage,iOldOfft,(sxu16)(L_HASH_CELL_SZ + pCell->nKey + iOld));
			}
		}
		return UNQLITE_OK;
	}
	/* Point to the overflow page */
	rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,pCell->iDataPage,&pOvfl);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Relase all old overflow pages first */
	SyBigEndianUnpack64(pOvfl->zData,&iOvfl);
	pOld = pOvfl;
	for(;;){
		if( iOvfl == 0 ){
			/* No more overflow pages on the chain */
			break;
		}
		/* Point to the target page */
		if( UNQLITE_OK != pEngine->pIo->xGet(pEngine->pIo->pHandle,iOvfl,&pOld) ){
			/* Not so fatal if something goes wrong here */
			break;
		}
		/* Next overflow page to be released */
		SyBigEndianUnpack64(pOld->zData,&iOvfl);
		if( pOld != pOvfl ){ /* xx: chm is maniac */
			/* Restore the page to the free list */
			lhRestorePage(pEngine,pOld);
			/* Unref */
			pEngine->pIo->xPageUnref(pOld);
		}
	}
	/* Point to the data offset */
	zRaw = &pOvfl->zData[pCell->iDataOfft];
	zRawEnd = &pOvfl->zData[pEngine->iPageSize];
	/* The data to be stored */
	zPtr = (const unsigned char *)pData;
	zEnd = &zPtr[nByte];
	/* Start the overwrite process */
	/* Acquire a writer lock */
	rc = pEngine->pIo->xWrite(pOvfl);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	SyBigEndianPack64(pOvfl->zData,0);
	for(;;){
		sxu32 nLen;
		if( zPtr >= zEnd ){
			break;
		}
		if( zRaw >= zRawEnd ){
			/* Acquire a new page */
			rc = lhAcquirePage(pEngine,&pNew);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			rc = pEngine->pIo->xWrite(pNew);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			/* Link */
			SyBigEndianPack64(pOvfl->zData,pNew->pgno);
			pEngine->pIo->xPageUnref(pOvfl);
			SyBigEndianPack64(pNew->zData,0); /* Next overflow page on the chain */
			pOvfl = pNew;
			zRaw = &pNew->zData[8];
			zRawEnd = &pNew->zData[pEngine->iPageSize];
		}
		nAvail = (sxu32)(zRawEnd-zRaw);
		nLen = (sxu32)(zEnd-zPtr);
		if( nLen > nAvail ){
			nLen = nAvail;
		}
		SyMemcpy((const void *)zPtr,(void *)zRaw,nLen);
		/* Synchronize pointers */
		zPtr += nLen;
		zRaw += nLen;
	}
	/* Unref the last overflow page */
	pEngine->pIo->xPageUnref(pOvfl);
	/* Finally, update the cell header */
	pCell->nData = (sxu64)nByte;
	SyBigEndianPack64(&pPage->pRaw->zData[pCell->iStart + 4 /* Hash */ + 4 /* Key */],pCell->nData);
	/* All done */
	return UNQLITE_OK;
}
/*
 * Append data to an existing record.
 */
static int lhRecordAppend(
	lhcell *pCell,
	const void *pData,unqlite_int64 nByte
	)
{
	lhash_kv_engine *pEngine = pCell->pPage->pHash;
	const unsigned char *zPtr,*zEnd;
	lhpage *pPage = pCell->pPage;
	unsigned char *zRaw,*zRawEnd;
	unqlite_page *pOvfl,*pNew;
	sxu64 nDatalen;
	sxu32 nAvail;
	pgno iOvfl;
	int rc;
	if( pCell->nData + nByte < pCell->nData ){
		/* Overflow */
		pEngine->pIo->xErr(pEngine->pIo->pHandle,"Append operation will cause data overflow");
		return UNQLITE_LIMIT;
	}
	/* Acquire a writer lock on this page */
	rc = pEngine->pIo->xWrite(pPage->pRaw);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	if( pCell->iOvfl == 0 ){
		sxu16 iOfft = 0; /* cc warning */
		/* Local payload, check for a bigger place */
		rc = lhAllocateSpace(pPage,L_HASH_CELL_SZ + pCell->nKey + pCell->nData + nByte,&iOfft);
		if( rc != UNQLITE_OK ){
			/* Transfer the payload to an overflow page */
			rc = lhCellWriteOvflPayload(pCell,
				&pPage->pRaw->zData[pCell->iStart + L_HASH_CELL_SZ],pCell->nKey,
				(const void *)&pPage->pRaw->zData[pCell->iStart + L_HASH_CELL_SZ + pCell->nKey],pCell->nData,
				pData,nByte,
				(const void *)0);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			/* Update the cell header */
			SyBigEndianPack64(&pPage->pRaw->zData[pCell->iStart + 4 /* Hash */ + 4 /* Key */],pCell->nData + nByte);
			/* Restore freespace */
			lhRestoreSpace(pPage,(sxu16)(pCell->iStart + L_HASH_CELL_SZ),(sxu16)(pCell->nKey + pCell->nData));
			/* New data size */
			pCell->nData += nByte;
		}else{
			sxu16 iOldOfft = pCell->iStart;
			sxu32 iOld = (sxu32)pCell->nData;
			SyBlob sWorker;
			SyBlobInit(&sWorker,&pEngine->sAllocator);
			/* Copy the old data */
			rc = SyBlobAppend(&sWorker,(const void *)&pPage->pRaw->zData[pCell->iStart + L_HASH_CELL_SZ + pCell->nKey],(sxu32)pCell->nData);
			if( rc == SXRET_OK ){
				/* Append the new data */
				rc = SyBlobAppend(&sWorker,pData,(sxu32)nByte);
			}
			if( rc != UNQLITE_OK ){
				SyBlobRelease(&sWorker);
				return rc;
			}
			/* Space is available, transfer the cell */
			lhMoveLocalCell(pCell,iOfft,SyBlobData(&sWorker),(unqlite_int64)SyBlobLength(&sWorker));
			/* Restore cell space */
			lhRestoreSpace(pPage,iOldOfft,(sxu16)(L_HASH_CELL_SZ + pCell->nKey + iOld));
			/* All done */
			SyBlobRelease(&sWorker);
		}
		return UNQLITE_OK;
	}
	/* Point to the overflow page which hold the data */
	rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,pCell->iDataPage,&pOvfl);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Next overflow page in the chain */
	SyBigEndianUnpack64(pOvfl->zData,&iOvfl);
	/* Point to the end of the chunk */
	zRaw = &pOvfl->zData[pCell->iDataOfft];
	zRawEnd = &pOvfl->zData[pEngine->iPageSize];
	nDatalen = pCell->nData;
	nAvail = (sxu32)(zRawEnd - zRaw);
	for(;;){
		if( zRaw >= zRawEnd ){
			if( iOvfl == 0 ){
				/* Cant happen */
				pEngine->pIo->xErr(pEngine->pIo->pHandle,"Corrupt overflow page");
				return UNQLITE_CORRUPT;
			}
			rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,iOvfl,&pNew);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			/* Next overflow page on the chain */
			SyBigEndianUnpack64(pNew->zData,&iOvfl);
			/* Unref the previous overflow page */
			pEngine->pIo->xPageUnref(pOvfl);
			/* Point to the new chunk */
			zRaw = &pNew->zData[8];
			zRawEnd = &pNew->zData[pCell->pPage->pHash->iPageSize];
			nAvail = L_HASH_OVERFLOW_SIZE(pCell->pPage->pHash->iPageSize);
			pOvfl = pNew;
		}
		if( (sxu64)nAvail > nDatalen ){
			zRaw += nDatalen;
			break;
		}else{
			nDatalen -= nAvail;
		}
		zRaw += nAvail;
	}
	/* Start the append process */
	zPtr = (const unsigned char *)pData;
	zEnd = &zPtr[nByte];
	/* Acquire a writer lock */
	rc = pEngine->pIo->xWrite(pOvfl);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	for(;;){
		sxu32 nLen;
		if( zPtr >= zEnd ){
			break;
		}
		if( zRaw >= zRawEnd ){
			/* Acquire a new page */
			rc = lhAcquirePage(pEngine,&pNew);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			rc = pEngine->pIo->xWrite(pNew);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			/* Link */
			SyBigEndianPack64(pOvfl->zData,pNew->pgno);
			pEngine->pIo->xPageUnref(pOvfl);
			SyBigEndianPack64(pNew->zData,0); /* Next overflow page on the chain */
			pOvfl = pNew;
			zRaw = &pNew->zData[8];
			zRawEnd = &pNew->zData[pEngine->iPageSize];
		}
		nAvail = (sxu32)(zRawEnd-zRaw);
		nLen = (sxu32)(zEnd-zPtr);
		if( nLen > nAvail ){
			nLen = nAvail;
		}
		SyMemcpy((const void *)zPtr,(void *)zRaw,nLen);
		/* Synchronize pointers */
		zPtr += nLen;
		zRaw += nLen;
	}
	/* Unref the last overflow page */
	pEngine->pIo->xPageUnref(pOvfl);
	/* Finally, update the cell header */
	pCell->nData += nByte;
	SyBigEndianPack64(&pPage->pRaw->zData[pCell->iStart + 4 /* Hash */ + 4 /* Key */],pCell->nData);
	/* All done */
	return UNQLITE_OK;
}
/*
 * A write privilege have been acquired on this page.
 * Mark it as an empty page (No cells).
 */
static int lhSetEmptyPage(lhpage *pPage)
{
	unsigned char *zRaw = pPage->pRaw->zData;
	lhphdr *pHeader = &pPage->sHdr;
	sxu16 nByte;
	int rc;
	/* Acquire a writer lock */
	rc = pPage->pHash->pIo->xWrite(pPage->pRaw);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Offset of the first cell */
	SyBigEndianPack16(zRaw,0);
	zRaw += 2;
	/* Offset of the first free block */
	pHeader->iFree = L_HASH_PAGE_HDR_SZ;
	SyBigEndianPack16(zRaw,L_HASH_PAGE_HDR_SZ);
	zRaw += 2;
	/* Slave page number */
	SyBigEndianPack64(zRaw,0);
	zRaw += 8;
	/* Fill the free block */
	SyBigEndianPack16(zRaw,0); /* Offset of the next free block */
	zRaw += 2;
	nByte = (sxu16)L_HASH_MX_FREE_SPACE(pPage->pHash->iPageSize);
	SyBigEndianPack16(zRaw,nByte);
	pPage->nFree = nByte;
	/* Do not add this page to the hot dirty list */
	pPage->pHash->pIo->xDontMkHot(pPage->pRaw);
	return UNQLITE_OK;
}
/* Forward declaration */
static int lhSlaveStore(
	lhpage *pPage,
	const void *pKey,sxu32 nKeyLen,
	const void *pData,unqlite_int64 nDataLen,
	sxu32 nHash
	);
/*
 * Store a cell and its payload in a given page.
 */
static int lhStoreCell(
	lhpage *pPage, /* Target page */
	const void *pKey,sxu32 nKeyLen, /* Payload: Key */
	const void *pData,unqlite_int64 nDataLen, /* Payload: Data */
	sxu32 nHash, /* Hash of the key */
	int auto_append /* Auto append a slave page if full */
	)
{
	lhash_kv_engine *pEngine = pPage->pHash;
	int iNeedOvfl = 0; /* Need overflow page for this cell and its payload*/
	lhcell *pCell;
	sxu16 nOfft;
	int rc;
	/* Acquire a writer lock on this page first */
	rc = pEngine->pIo->xWrite(pPage->pRaw);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Check for a free block  */
	rc = lhAllocateSpace(pPage,L_HASH_CELL_SZ+nKeyLen+nDataLen,&nOfft);
	if( rc != UNQLITE_OK ){
		/* Check for a free block to hold a single cell only (without payload) */
		rc = lhAllocateSpace(pPage,L_HASH_CELL_SZ,&nOfft);
		if( rc != UNQLITE_OK ){
			if( !auto_append ){
				/* A split must be done */
				return UNQLITE_FULL;
			}else{
				/* Store this record in a slave page */
				rc = lhSlaveStore(pPage,pKey,nKeyLen,pData,nDataLen,nHash);
				return rc;
			}
		}
		iNeedOvfl = 1;
	}
	/* Allocate a new cell instance */
	pCell = lhNewCell(pEngine,pPage);
	if( pCell == 0 ){
		pEngine->pIo->xErr(pEngine->pIo->pHandle,"KV store is running out of memory");
		return UNQLITE_NOMEM;
	}
	/* Fill-in the structure */
	pCell->iStart = nOfft;
	pCell->nKey = nKeyLen;
	pCell->nData = (sxu64)nDataLen;
	pCell->nHash = nHash;
	if( nKeyLen < 262144 /* 256 KB */ ){
		/* Keep the key in-memory for fast lookup */
		SyBlobAppend(&pCell->sKey,pKey,nKeyLen);
	}
	/* Link the cell */
	rc = lhInstallCell(pCell);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Write the payload */
	if( iNeedOvfl ){
		rc = lhCellWriteOvflPayload(pCell,pKey,nKeyLen,pData,nDataLen,(const void *)0);
		if( rc != UNQLITE_OK ){
			lhCellDiscard(pCell);
			return rc;
		}
	}else{
		lhCellWriteLocalPayload(pCell,pKey,nKeyLen,pData,nDataLen);
	}
	/* Finally, Write the cell header */
	lhCellWriteHeader(pCell);
	/* All done */
	return UNQLITE_OK;
}
/*
 * Find a slave page capable of hosting the given amount.
 */
static int lhFindSlavePage(lhpage *pPage,sxu64 nAmount,sxu16 *pOfft,lhpage **ppSlave)
{
	lhash_kv_engine *pEngine = pPage->pHash;
	lhpage *pMaster = pPage->pMaster;
	lhpage *pSlave = pMaster->pSlave;
	unqlite_page *pRaw;
	lhpage *pNew;
	sxu16 iOfft;
	sxi32 i;
	int rc;
	/* Look for an already attached slave page */
	for( i = 0 ; i < pMaster->iSlave ; ++i ){
		/* Find a free chunk big enough */
		sxu16 size = L_HASH_CELL_SZ + nAmount;
		rc = lhAllocateSpace(pSlave,size,&iOfft);
		if( rc != UNQLITE_OK ){
			/* A space for cell header only */
			size = L_HASH_CELL_SZ;
			rc = lhAllocateSpace(pSlave,size,&iOfft);
		}
		if( rc == UNQLITE_OK ){
			/* All done */
			if( pOfft ){
				*pOfft = iOfft;
			}else{
				rc = lhRestoreSpace(pSlave, iOfft, size);
			}
			*ppSlave = pSlave;
			return rc;
		}
		/* Point to the next slave page */
		pSlave = pSlave->pNextSlave;
	}
	/* Acquire a new slave page */
	rc = lhAcquirePage(pEngine,&pRaw);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Last slave page */
	pSlave = pMaster->pSlave;
	if( pSlave == 0 ){
		/* First slave page */
		pSlave = pMaster;
	}
	/* Initialize the page */
	pNew = lhNewPage(pEngine,pRaw,pMaster);
	if( pNew == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Mark as an empty page */
	rc = lhSetEmptyPage(pNew);
	if( rc != UNQLITE_OK ){
		goto fail;
	}
	if( pOfft ){
		/* Look for a free block */
		if( UNQLITE_OK != lhAllocateSpace(pNew,L_HASH_CELL_SZ+nAmount,&iOfft) ){
			/* Cell header only */
			lhAllocateSpace(pNew,L_HASH_CELL_SZ,&iOfft); /* Never fail */
		}	
		*pOfft = iOfft;
	}
	/* Link this page to the previous slave page */
	rc = pEngine->pIo->xWrite(pSlave->pRaw);
	if( rc != UNQLITE_OK ){
		goto fail;
	}
	/* Reflect in the page header */
	SyBigEndianPack64(&pSlave->pRaw->zData[2/*Cell offset*/+2/*Free block offset*/],pRaw->pgno);
	pSlave->sHdr.iSlave = pRaw->pgno;
	/* All done */
	*ppSlave = pNew;
	return UNQLITE_OK;
fail:
	pEngine->pIo->xPageUnref(pNew->pRaw); /* pNew will be released in this call */
	return rc;

}
/*
 * Perform a store operation in a slave page.
 */
static int lhSlaveStore(
	lhpage *pPage, /* Master page */
	const void *pKey,sxu32 nKeyLen, /* Payload: key */
	const void *pData,unqlite_int64 nDataLen, /* Payload: data */
	sxu32 nHash /* Hash of the key */
	)
{
	lhpage *pSlave;
	int rc;
	/* Find a slave page */
	rc = lhFindSlavePage(pPage,nKeyLen + nDataLen,0,&pSlave);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Perform the insertion in the slave page */
	rc = lhStoreCell(pSlave,pKey,nKeyLen,pData,nDataLen,nHash,1);
	return rc;
}
/*
 * Transfer a cell to a new page (either a master or slave).
 */
static int lhTransferCell(lhcell *pTarget,lhpage *pPage)
{
	lhcell *pCell;
	sxu16 nOfft;
	int rc;
	/* Check for a free block to hold a single cell only */
	rc = lhAllocateSpace(pPage,L_HASH_CELL_SZ,&nOfft);
	if( rc != UNQLITE_OK ){
		/* Store in a slave page */
		rc = lhFindSlavePage(pPage,L_HASH_CELL_SZ,&nOfft,&pPage);
		if( rc != UNQLITE_OK ){
			return rc;
		}
	}
	/* Allocate a new cell instance */
	pCell = lhNewCell(pPage->pHash,pPage);
	if( pCell == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Fill-in the structure */
	pCell->iStart = nOfft;
	pCell->nData  = pTarget->nData;
	pCell->nKey   = pTarget->nKey;
	pCell->iOvfl  = pTarget->iOvfl;
	pCell->iDataOfft = pTarget->iDataOfft;
	pCell->iDataPage = pTarget->iDataPage;
	pCell->nHash = pTarget->nHash;
	SyBlobDup(&pTarget->sKey,&pCell->sKey);
	/* Link the cell */
	rc = lhInstallCell(pCell);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Finally, Write the cell header */
	lhCellWriteHeader(pCell);
	/* All done */
	return UNQLITE_OK;
}
/*
 * Perform a page split.
 */
static int lhPageSplit(
	lhpage *pOld,      /* Page to be split */
	lhpage *pNew,      /* New page */
	pgno split_bucket, /* Current split bucket */
	pgno high_mask     /* High mask (Max split bucket - 1) */
	)
{
	lhcell *pCell,*pNext;
	SyBlob sWorker;
	pgno iBucket;
	int rc; 
	SyBlobInit(&sWorker,&pOld->pHash->sAllocator);
	/* Perform the split */
	pCell = pOld->pList;
	for( ;; ){
		if( pCell == 0 ){
			/* No more cells */
			break;
		}
		/* Obtain the new logical bucket */
		iBucket = pCell->nHash & high_mask;
		pNext =  pCell->pNext;
		if( iBucket != split_bucket){
			rc = UNQLITE_OK;
			if( pCell->iOvfl ){
				/* Transfer the cell only */
				rc = lhTransferCell(pCell,pNew);
			}else{
				/* Transfer the cell and its payload */
				SyBlobReset(&sWorker);
				if( SyBlobLength(&pCell->sKey) < 1 ){
					/* Consume the key */
					rc = lhConsumeCellkey(pCell,unqliteDataConsumer,&pCell->sKey,0);
					if( rc != UNQLITE_OK ){
						goto fail;
					}
				}
				/* Consume the data (Very small data < 65k) */
				rc = lhConsumeCellData(pCell,unqliteDataConsumer,&sWorker);
				if( rc != UNQLITE_OK ){
					goto fail;
				}
				/* Perform the transfer */
				rc = lhStoreCell(
					pNew,
					SyBlobData(&pCell->sKey),(int)SyBlobLength(&pCell->sKey),
					SyBlobData(&sWorker),SyBlobLength(&sWorker),
					pCell->nHash,
					1
					);
			}
			if( rc != UNQLITE_OK ){
				goto fail;
			}
			/* Discard the cell from the old page */
			lhUnlinkCell(pCell);
		}
		/* Point to the next cell */
		pCell = pNext;
	}
	/* All done */
	rc = UNQLITE_OK;
fail:
	SyBlobRelease(&sWorker);
	return rc;
}
/*
 * Perform the infamous linear hash split operation.
 */
static int lhSplit(lhpage *pTarget,int *pRetry)
{
	lhash_kv_engine *pEngine = pTarget->pHash;
	lhash_bmap_rec *pRec;
	lhpage *pOld,*pNew;
	unqlite_page *pRaw;
	int rc;
	/* Get the real page number of the bucket to split */
	pRec = lhMapFindBucket(pEngine,pEngine->split_bucket);
	if( pRec == 0 ){
		/* Can't happen */
		return UNQLITE_CORRUPT;
	}
	/* Load the page to be split */
	rc = lhLoadPage(pEngine,pRec->iReal,0,&pOld,0);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Request a new page */
	rc = lhAcquirePage(pEngine,&pRaw);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Initialize the page */
	pNew = lhNewPage(pEngine,pRaw,0);
	if( pNew == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Mark as an empty page */
	rc = lhSetEmptyPage(pNew);
	if( rc != UNQLITE_OK ){
		goto fail;
	}
	/* Install and write the logical map record */
	rc = lhMapWriteRecord(pEngine,
		pEngine->split_bucket + pEngine->max_split_bucket,
		pRaw->pgno
		);
	if( rc != UNQLITE_OK ){
		goto fail;
	}
	if( pTarget->pRaw->pgno == pOld->pRaw->pgno ){
		*pRetry = 1;
	}
	/* Perform the split */
	rc = lhPageSplit(pOld,pNew,pEngine->split_bucket,pEngine->nmax_split_nucket - 1);
	if( rc != UNQLITE_OK ){
		goto fail;
	}
	/* Update the database header */
	pEngine->split_bucket++;
	/* Acquire a writer lock on the first page */
	rc = pEngine->pIo->xWrite(pEngine->pHeader);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	if( pEngine->split_bucket >= pEngine->max_split_bucket ){
		/* Increment the generation number */
		pEngine->split_bucket = 0;
		pEngine->max_split_bucket = pEngine->nmax_split_nucket;
		pEngine->nmax_split_nucket <<= 1;
		if( !pEngine->nmax_split_nucket ){
			/* If this happen to your installation, please tell us <chm@symisc.net> */
			pEngine->pIo->xErr(pEngine->pIo->pHandle,"Database page (64-bit integer) limit reached");
			return UNQLITE_LIMIT;
		}
		/* Reflect in the page header */
		SyBigEndianPack64(&pEngine->pHeader->zData[4/*Magic*/+4/*Hash*/+8/*Free list*/],pEngine->split_bucket);
		SyBigEndianPack64(&pEngine->pHeader->zData[4/*Magic*/+4/*Hash*/+8/*Free list*/+8/*Split bucket*/],pEngine->max_split_bucket);
	}else{
		/* Modify only the split bucket */
		SyBigEndianPack64(&pEngine->pHeader->zData[4/*Magic*/+4/*Hash*/+8/*Free list*/],pEngine->split_bucket);
	}
	/* All done */
	return UNQLITE_OK;
fail:
	pEngine->pIo->xPageUnref(pNew->pRaw);
	return rc;
}
/*
 * Store a record in the target page.
 */
static int lhRecordInstall(
	  lhpage *pPage, /* Target page */
	  sxu32 nHash,   /* Hash of the key */
	  const void *pKey,sxu32 nKeyLen,          /* Payload: Key */
	  const void *pData,unqlite_int64 nDataLen /* Payload: Data */
	  )
{
	int rc;
	rc = lhStoreCell(pPage,pKey,nKeyLen,pData,nDataLen,nHash,0);
	if( rc == UNQLITE_FULL ){
		int do_retry = 0;
		/* Split */
		rc = lhSplit(pPage,&do_retry);
		if( rc == UNQLITE_OK ){
			if( do_retry ){
				/* Re-calculate logical bucket number */
				return SXERR_RETRY;
			}
			/* Perform the store */
			rc = lhStoreCell(pPage,pKey,nKeyLen,pData,nDataLen,nHash,1);
		}
	}
	return rc;
}
/*
 * Insert a record (Either overwrite or append operation) in our database.
 */
static int lh_record_insert(
	  unqlite_kv_engine *pKv,         /* KV store */
	  const void *pKey,sxu32 nKeyLen, /* Payload: Key */
	  const void *pData,unqlite_int64 nDataLen, /* Payload: data */
	  int is_append /* True for an append operation */
	  )
{
	lhash_kv_engine *pEngine = (lhash_kv_engine *)pKv;
	lhash_bmap_rec *pRec;
	unqlite_page *pRaw;
	lhpage *pPage;
	lhcell *pCell;
	pgno iBucket;
	sxu32 nHash;
	int iCnt;
	int rc;

	/* Acquire the first page (DB hash Header) so that everything gets loaded autmatically */
	rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,1,0);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	iCnt = 0;
	/* Compute the hash of the key first */
	nHash = pEngine->xHash(pKey,(sxu32)nKeyLen);
retry:
	/* Extract the logical bucket number */
	iBucket = nHash & (pEngine->nmax_split_nucket - 1);
	if( iBucket >= pEngine->split_bucket + pEngine->max_split_bucket ){
		/* Low mask */
		iBucket = nHash & (pEngine->max_split_bucket - 1);
	}
	/* Map the logical bucket number to real page number */
	pRec = lhMapFindBucket(pEngine,iBucket);
	if( pRec == 0 ){
		/* Request a new page */
		rc = lhAcquirePage(pEngine,&pRaw);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		/* Initialize the page */
		pPage = lhNewPage(pEngine,pRaw,0);
		if( pPage == 0 ){
			return UNQLITE_NOMEM;
		}
		/* Mark as an empty page */
		rc = lhSetEmptyPage(pPage);
		if( rc != UNQLITE_OK ){
			pEngine->pIo->xPageUnref(pRaw); /* pPage will be released during this call */
			return rc;
		}
		/* Store the cell */
		rc = lhStoreCell(pPage,pKey,nKeyLen,pData,nDataLen,nHash,1);
		if( rc == UNQLITE_OK ){
			/* Install and write the logical map record */
			rc = lhMapWriteRecord(pEngine,iBucket,pRaw->pgno);
		}
		pEngine->pIo->xPageUnref(pRaw);
		return rc;
	}else{
		/* Load the page */
		rc = lhLoadPage(pEngine,pRec->iReal,0,&pPage,0);
		if( rc != UNQLITE_OK ){
			/* IO error, unlikely scenario */
			return rc;
		}
		/* Do not add this page to the hot dirty list */
		pEngine->pIo->xDontMkHot(pPage->pRaw);
		/* Lookup for the cell */
		pCell = lhFindCell(pPage,pKey,(sxu32)nKeyLen,nHash);
		if( pCell == 0 ){
			/* Create the record */
			rc = lhRecordInstall(pPage,nHash,pKey,nKeyLen,pData,nDataLen);
			if( rc == SXERR_RETRY && iCnt++ < 2 ){
				rc = UNQLITE_OK;
				goto retry;
			}
		}else{
			if( is_append ){
				/* Append operation */
				rc = lhRecordAppend(pCell,pData,nDataLen);
			}else{
				/* Overwrite old value */
				rc = lhRecordOverwrite(pCell,pData,nDataLen);
			}
		}
		pEngine->pIo->xPageUnref(pPage->pRaw);
	}
	return rc;
}
/*
 * Replace method.
 */
static int lhash_kv_replace(
	  unqlite_kv_engine *pKv,
	  const void *pKey,int nKeyLen,
	  const void *pData,unqlite_int64 nDataLen
	  )
{
	int rc;
	rc = lh_record_insert(pKv,pKey,(sxu32)nKeyLen,pData,nDataLen,0);
	return rc;
}
/*
 * Append method.
 */
static int lhash_kv_append(
	  unqlite_kv_engine *pKv,
	  const void *pKey,int nKeyLen,
	  const void *pData,unqlite_int64 nDataLen
	  )
{
	int rc;
	rc = lh_record_insert(pKv,pKey,(sxu32)nKeyLen,pData,nDataLen,1);
	return rc;
}
/*
 * Write the hash header (Page one).
 */
static int lhash_write_header(lhash_kv_engine *pEngine,unqlite_page *pHeader)
{
	unsigned char *zRaw = pHeader->zData;
	lhash_bmap_page *pMap;

	pEngine->pHeader = pHeader;
	/* 4 byte magic number */
	SyBigEndianPack32(zRaw,pEngine->nMagic);
	zRaw += 4;
	/* 4 byte hash value to identify a valid hash function */
	SyBigEndianPack32(zRaw,pEngine->xHash(L_HASH_WORD,sizeof(L_HASH_WORD)-1));
	zRaw += 4;
	/* List of free pages: Empty */
	SyBigEndianPack64(zRaw,0);
	zRaw += 8;
	/* Current split bucket */
	SyBigEndianPack64(zRaw,pEngine->split_bucket);
	zRaw += 8;
	/* Maximum split bucket */
	SyBigEndianPack64(zRaw,pEngine->max_split_bucket);
	zRaw += 8;
	/* Initialiaze the bucket map */
	pMap = &pEngine->sPageMap;
	/* Fill in the structure */
	pMap->iNum = pHeader->pgno;
	/* Next page in the bucket map */
	SyBigEndianPack64(zRaw,0);
	zRaw += 8;
	/* Total number of records in the bucket map */
	SyBigEndianPack32(zRaw,0);
	zRaw += 4;
	pMap->iPtr = (sxu16)(zRaw - pHeader->zData);
	/* All done */
	return UNQLITE_OK;
 }
/*
 * Exported: xOpen() method.
 */
static int lhash_kv_open(unqlite_kv_engine *pEngine,pgno dbSize)
{
	lhash_kv_engine *pHash = (lhash_kv_engine *)pEngine;
	unqlite_page *pHeader;
	int rc;
	if( dbSize < 1 ){
		/* A new database, create the header */
		rc = pEngine->pIo->xNew(pEngine->pIo->pHandle,&pHeader);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		/* Acquire a writer lock */
		rc = pEngine->pIo->xWrite(pHeader);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		/* Write the hash header */
		rc = lhash_write_header(pHash,pHeader);
		if( rc != UNQLITE_OK ){
			return rc;
		}
	}else{
		/* Acquire the page one of the database */
		rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,1,&pHeader);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		/* Read the database header */
		rc = lhash_read_header(pHash,pHeader);
		if( rc != UNQLITE_OK ){
			return rc;
		}
	}
	return UNQLITE_OK;
}
/*
 * Release a master or slave page. (xUnpin callback).
 */
static void lhash_page_release(void *pUserData)
{
	lhpage *pPage = (lhpage *)pUserData;
	lhash_kv_engine *pEngine = pPage->pHash;
	lhcell *pNext,*pCell = pPage->pList;
	unqlite_page *pRaw = pPage->pRaw;
	sxu32 n;
	/* Drop in-memory cells */
	for( n = 0 ; n < pPage->nCell ; ++n ){
		pNext = pCell->pNext;
		SyBlobRelease(&pCell->sKey);
		/* Release the cell instance */
		SyMemBackendPoolFree(&pEngine->sAllocator,(void *)pCell);
		/* Point to the next entry */
		pCell = pNext;
	}
	if( pPage->apCell ){
		/* Release the cell table */
		SyMemBackendFree(&pEngine->sAllocator,(void *)pPage->apCell);
	}
	/* Finally, release the whole page */
	SyMemBackendPoolFree(&pEngine->sAllocator,pPage);
	pRaw->pUserData = 0;
}
/*
 * Default hash function (DJB).
 */
static sxu32 lhash_bin_hash(const void *pSrc,sxu32 nLen)
{
	register unsigned char *zIn = (unsigned char *)pSrc;
	unsigned char *zEnd;
	sxu32 nH = 5381;
	if( nLen > 2048 /* 2K */ ){
		nLen = 2048;
	}
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
 * Exported: xInit() method.
 * Initialize the Key value storage engine.
 */
static int lhash_kv_init(unqlite_kv_engine *pEngine,int iPageSize)
{
	lhash_kv_engine *pHash = (lhash_kv_engine *)pEngine;
	int rc;

	/* This structure is always zeroed, go to the initialization directly */
	SyMemBackendInitFromParent(&pHash->sAllocator,unqliteExportMemBackend());
//#if defined(UNQLITE_ENABLE_THREADS)
//	/* Already protected by the upper layers */
//	SyMemBackendDisbaleMutexing(&pHash->sAllocator);
//#endif
	pHash->iPageSize = iPageSize;
	/* Default hash function */
	pHash->xHash = lhash_bin_hash;
	/* Default comparison function */
	pHash->xCmp = SyMemcmp;
	/* Allocate a new record map */
	pHash->nBuckSize = 32;
	pHash->apMap = (lhash_bmap_rec **)SyMemBackendAlloc(&pHash->sAllocator,pHash->nBuckSize *sizeof(lhash_bmap_rec *));
	if( pHash->apMap == 0 ){
		rc = UNQLITE_NOMEM;
		goto err;
	}
	/* Zero the table */
	SyZero(pHash->apMap,pHash->nBuckSize * sizeof(lhash_bmap_rec *));
	/* Linear hashing components */
	pHash->split_bucket = 0; /* Logical not real bucket number */
	pHash->max_split_bucket = 1;
	pHash->nmax_split_nucket = 2;
	pHash->nMagic = L_HASH_MAGIC;
	/* Install the cache unpin and reload callbacks */
	pHash->pIo->xSetUnpin(pHash->pIo->pHandle,lhash_page_release);
	pHash->pIo->xSetReload(pHash->pIo->pHandle,lhash_page_release);
	return UNQLITE_OK;
err:
	SyMemBackendRelease(&pHash->sAllocator);
	return rc;
}
/*
 * Exported: xRelease() method.
 * Release the Key value storage engine.
 */
static void lhash_kv_release(unqlite_kv_engine *pEngine)
{
	lhash_kv_engine *pHash = (lhash_kv_engine *)pEngine;
	/* Release the private memory backend */
	SyMemBackendRelease(&pHash->sAllocator);
}
/*
 *  Exported: xConfig() method.
 *  Configure the linear hash KV store.
 */
static int lhash_kv_config(unqlite_kv_engine *pEngine,int op,va_list ap)
{
	lhash_kv_engine *pHash = (lhash_kv_engine *)pEngine;
	int rc = UNQLITE_OK;
	switch(op){
	case UNQLITE_KV_CONFIG_HASH_FUNC: {
		/* Default hash function */
		if( pHash->nBuckRec > 0 ){
			/* Locked operation */
			rc = UNQLITE_LOCKED;
		}else{
			ProcHash xHash = va_arg(ap,ProcHash);
			if( xHash ){
				pHash->xHash = xHash;
			}
		}
		break;
									  }
	case UNQLITE_KV_CONFIG_CMP_FUNC: {
		/* Default comparison function */
		ProcCmp xCmp = va_arg(ap,ProcCmp);
		if( xCmp ){
			pHash->xCmp  = xCmp;
		}
		break;
									 }
	default:
		/* Unknown OP */
		rc = UNQLITE_UNKNOWN;
		break;
	}
	return rc;
}
/*
 * Each public cursor is identified by an instance of this structure.
 */
typedef struct lhash_kv_cursor lhash_kv_cursor;
struct lhash_kv_cursor
{
	unqlite_kv_engine *pStore; /* Must be first */
	/* Private fields */
	int iState;           /* Current state of the cursor */
	int is_first;         /* True to read the database header */
	lhcell *pCell;        /* Current cell we are processing */
	unqlite_page *pRaw;   /* Raw disk page */
	lhash_bmap_rec *pRec; /* Logical to real bucket map */
};
/* 
 * Possible state of the cursor
 */
#define L_HASH_CURSOR_STATE_NEXT_PAGE 1 /* Next page in the list */
#define L_HASH_CURSOR_STATE_CELL      2 /* Processing Cell */
#define L_HASH_CURSOR_STATE_DONE      3 /* Cursor does not point to anything */
/*
 * Initialize the cursor.
 */
static void lhInitCursor(unqlite_kv_cursor *pPtr)
{
	 lhash_kv_engine *pEngine = (lhash_kv_engine *)pPtr->pStore;
	 lhash_kv_cursor *pCur = (lhash_kv_cursor *)pPtr;
	 /* Init */
	 pCur->iState = L_HASH_CURSOR_STATE_NEXT_PAGE;
	 pCur->pCell = 0;
	 pCur->pRec = pEngine->pFirst;
	 pCur->pRaw = 0;
	 pCur->is_first = 1;
}
/*
 * Point to the next page on the database.
 */
static int lhCursorNextPage(lhash_kv_cursor *pPtr)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pPtr;
	lhash_bmap_rec *pRec;
	lhpage *pPage;
	int rc;
	for(;;){
		pRec = pCur->pRec;
		if( pRec == 0 ){
			pCur->iState = L_HASH_CURSOR_STATE_DONE;
			return UNQLITE_DONE;
		}
		if( pPtr->iState == L_HASH_CURSOR_STATE_CELL && pPtr->pRaw ){
			/* Unref this page */
			pCur->pStore->pIo->xPageUnref(pPtr->pRaw);
			pPtr->pRaw = 0;
		}
		/* Advance the map cursor */
		pCur->pRec = pRec->pPrev; /* Not a bug, reverse link */
		/* Load the next page on the list */
		rc = lhLoadPage((lhash_kv_engine *)pCur->pStore,pRec->iReal,0,&pPage,0);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		if( pPage->pList ){
			/* Reflect the change */
			pCur->pCell = pPage->pList;
			pCur->iState = L_HASH_CURSOR_STATE_CELL;
			pCur->pRaw = pPage->pRaw;
			break;
		}
		/* Empty page, discard this page and continue */
		pPage->pHash->pIo->xPageUnref(pPage->pRaw);
	}
	return UNQLITE_OK;
}
/*
 * Point to the previous page on the database.
 */
static int lhCursorPrevPage(lhash_kv_cursor *pPtr)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pPtr;
	lhash_bmap_rec *pRec;
	lhpage *pPage;
	int rc;
	for(;;){
		pRec = pCur->pRec;
		if( pRec == 0 ){
			pCur->iState = L_HASH_CURSOR_STATE_DONE;
			return UNQLITE_DONE;
		}
		if( pPtr->iState == L_HASH_CURSOR_STATE_CELL && pPtr->pRaw ){
			/* Unref this page */
			pCur->pStore->pIo->xPageUnref(pPtr->pRaw);
			pPtr->pRaw = 0;
		}
		/* Advance the map cursor */
		pCur->pRec = pRec->pNext; /* Not a bug, reverse link */
		/* Load the previous page on the list */
		rc = lhLoadPage((lhash_kv_engine *)pCur->pStore,pRec->iReal,0,&pPage,0);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		if( pPage->pFirst ){
			/* Reflect the change */
			pCur->pCell = pPage->pFirst;
			pCur->iState = L_HASH_CURSOR_STATE_CELL;
			pCur->pRaw = pPage->pRaw;
			break;
		}
		/* Discard this page and continue */
		pPage->pHash->pIo->xPageUnref(pPage->pRaw);
	}
	return UNQLITE_OK;
}
/*
 * Is a valid cursor.
 */
static int lhCursorValid(unqlite_kv_cursor *pPtr)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pPtr;
	return (pCur->iState == L_HASH_CURSOR_STATE_CELL) && pCur->pCell;
}
/*
 * Point to the first record.
 */
static int lhCursorFirst(unqlite_kv_cursor *pCursor)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pCursor;
	lhash_kv_engine *pEngine = (lhash_kv_engine *)pCursor->pStore;
	int rc;
	if( pCur->is_first ){
		/* Read the database header first */
		rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,1,0);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		pCur->is_first = 0;
	}
	/* Point to the first map record */
	pCur->pRec = pEngine->pFirst;
	/* Load the cells */
	rc = lhCursorNextPage(pCur);
	return rc;
}
/*
 * Point to the last record.
 */
static int lhCursorLast(unqlite_kv_cursor *pCursor)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pCursor;
	lhash_kv_engine *pEngine = (lhash_kv_engine *)pCursor->pStore;
	int rc;
	if( pCur->is_first ){
		/* Read the database header first */
		rc = pEngine->pIo->xGet(pEngine->pIo->pHandle,1,0);
		if( rc != UNQLITE_OK ){
			return rc;
		}
		pCur->is_first = 0;
	}
	/* Point to the last map record */
	pCur->pRec = pEngine->pList;
	/* Load the cells */
	rc = lhCursorPrevPage(pCur);
	return rc;
}
/*
 * Reset the cursor.
 */
static void lhCursorReset(unqlite_kv_cursor *pCursor)
{
	lhCursorFirst(pCursor);
}
/*
 * Point to the next record.
 */
static int lhCursorNext(unqlite_kv_cursor *pCursor)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pCursor;
	lhcell *pCell;
	int rc;
	if( pCur->iState != L_HASH_CURSOR_STATE_CELL || pCur->pCell == 0 ){
		/* Load the cells of the next page  */
		rc = lhCursorNextPage(pCur);
		return rc;
	}
	pCell = pCur->pCell;
	pCur->pCell = pCell->pNext;
	if( pCur->pCell == 0 ){
		/* Load the cells of the next page  */
		rc = lhCursorNextPage(pCur);
		return rc;
	}
	return UNQLITE_OK;
}
/*
 * Point to the previous record.
 */
static int lhCursorPrev(unqlite_kv_cursor *pCursor)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pCursor;
	lhcell *pCell;
	int rc;
	if( pCur->iState != L_HASH_CURSOR_STATE_CELL || pCur->pCell == 0 ){
		/* Load the cells of the previous page  */
		rc = lhCursorPrevPage(pCur);
		return rc;
	}
	pCell = pCur->pCell;
	pCur->pCell = pCell->pPrev;
	if( pCur->pCell == 0 ){
		/* Load the cells of the previous page  */
		rc = lhCursorPrevPage(pCur);
		return rc;
	}
	return UNQLITE_OK;
}
/*
 * Return key length.
 */
static int lhCursorKeyLength(unqlite_kv_cursor *pCursor,int *pLen)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pCursor;
	lhcell *pCell;
	
	if( pCur->iState != L_HASH_CURSOR_STATE_CELL || pCur->pCell == 0 ){
		/* Invalid state */
		return UNQLITE_INVALID;
	}
	/* Point to the target cell */
	pCell = pCur->pCell;
	/* Return key length */
	*pLen = (int)pCell->nKey;
	return UNQLITE_OK;
}
/*
 * Return data length.
 */
static int lhCursorDataLength(unqlite_kv_cursor *pCursor,unqlite_int64 *pLen)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pCursor;
	lhcell *pCell;
	
	if( pCur->iState != L_HASH_CURSOR_STATE_CELL || pCur->pCell == 0 ){
		/* Invalid state */
		return UNQLITE_INVALID;
	}
	/* Point to the target cell */
	pCell = pCur->pCell;
	/* Return data length */
	*pLen = (unqlite_int64)pCell->nData;
	return UNQLITE_OK;
}
/*
 * Consume the key.
 */
static int lhCursorKey(unqlite_kv_cursor *pCursor,int (*xConsumer)(const void *,unsigned int,void *),void *pUserData)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pCursor;
	lhcell *pCell;
	int rc;
	if( pCur->iState != L_HASH_CURSOR_STATE_CELL || pCur->pCell == 0 ){
		/* Invalid state */
		return UNQLITE_INVALID;
	}
	/* Point to the target cell */
	pCell = pCur->pCell;
	if( SyBlobLength(&pCell->sKey) > 0 ){
		/* Consume the key directly */
		rc = xConsumer(SyBlobData(&pCell->sKey),SyBlobLength(&pCell->sKey),pUserData);
	}else{
		/* Very large key */
		rc = lhConsumeCellkey(pCell,xConsumer,pUserData,0);
	}
	return rc;
}
/*
 * Consume the data.
 */
static int lhCursorData(unqlite_kv_cursor *pCursor,int (*xConsumer)(const void *,unsigned int,void *),void *pUserData)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pCursor;
	lhcell *pCell;
	int rc;
	if( pCur->iState != L_HASH_CURSOR_STATE_CELL || pCur->pCell == 0 ){
		/* Invalid state */
		return UNQLITE_INVALID;
	}
	/* Point to the target cell */
	pCell = pCur->pCell;
	/* Consume the data */
	rc = lhConsumeCellData(pCell,xConsumer,pUserData);
	return rc;
}
/*
 * Find a partiuclar record.
 */
static int lhCursorSeek(unqlite_kv_cursor *pCursor,const void *pKey,int nByte,int iPos)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pCursor;
	int rc;
	/* Perform a lookup */
	rc = lhRecordLookup((lhash_kv_engine *)pCur->pStore,pKey,nByte,&pCur->pCell);
	if( rc != UNQLITE_OK ){
		SXUNUSED(iPos);
		pCur->pCell = 0;
		pCur->iState = L_HASH_CURSOR_STATE_DONE;
		return rc;
	}
	pCur->iState = L_HASH_CURSOR_STATE_CELL;
	return UNQLITE_OK;
}
/*
 * Remove a particular record.
 */
static int lhCursorDelete(unqlite_kv_cursor *pCursor)
{
	lhash_kv_cursor *pCur = (lhash_kv_cursor *)pCursor;
	lhcell *pCell;
	int rc;
	if( pCur->iState != L_HASH_CURSOR_STATE_CELL || pCur->pCell == 0 ){
		/* Invalid state */
		return UNQLITE_INVALID;
	}
	/* Point to the target cell  */
	pCell = pCur->pCell;
	/* Point to the next entry */
	pCur->pCell = pCell->pNext;
	/* Perform the deletion */
	rc = lhRecordRemove(pCell);
	return rc;
}
/*
 * Export the linear-hash storage engine.
 */
UNQLITE_PRIVATE const unqlite_kv_methods * unqliteExportDiskKvStorage(void)
{
	static const unqlite_kv_methods sDiskStore = {
		"hash",                     /* zName */
		sizeof(lhash_kv_engine),    /* szKv */
		sizeof(lhash_kv_cursor),    /* szCursor */
		1,                          /* iVersion */
		lhash_kv_init,              /* xInit */
		lhash_kv_release,           /* xRelease */
		lhash_kv_config,            /* xConfig */
		lhash_kv_open,              /* xOpen */
		lhash_kv_replace,           /* xReplace */
		lhash_kv_append,            /* xAppend */
		lhInitCursor,               /* xCursorInit */
		lhCursorSeek,               /* xSeek */
		lhCursorFirst,              /* xFirst */
		lhCursorLast,               /* xLast */
		lhCursorValid,              /* xValid */
		lhCursorNext,               /* xNext */
		lhCursorPrev,               /* xPrev */
		lhCursorDelete,             /* xDelete */
		lhCursorKeyLength,          /* xKeyLength */
		lhCursorKey,                /* xKey */
		lhCursorDataLength,         /* xDataLength */
		lhCursorData,               /* xData */
		lhCursorReset,              /* xReset */
		0                           /* xRelease */                        
	};
	return &sDiskStore;
}
