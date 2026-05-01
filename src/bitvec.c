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
 /* $SymiscID: bitvec.c v1.0 Win7 2026-02-27 15:16 stable <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif

/** This file implements an object that represents a dynmaic
** bitmap.
**
** A bitmap is used to record which pages of a database file have been
** journalled during a transaction, or which pages have the "dont-write"
** property.  Usually only a few pages are meet either condition.
** So the bitmap is usually sparse and has low cardinality.
*/
/*
 * Actually, this is not a bitmap but a simple hashtable where page 
 * number (64-bit unsigned integers) are used as the lookup keys.
 */
typedef struct bitvec_rec bitvec_rec;
struct bitvec_rec
{
	pgno iPage;                  /* Page number */
	bitvec_rec *pNext,*pNextCol; /* Collison link */
};
struct Bitvec
{
	SyMemBackend *pAlloc; /* Memory allocator */
	sxu32 nRec;           /* Total number of records */
	sxu32 nSize;          /* Table size */
	bitvec_rec **apRec;   /* Record table */
	bitvec_rec *pList;    /* List of records */
};
/* 
 * Allocate a new bitvec instance.
*/
UNQLITE_PRIVATE Bitvec * unqliteBitvecCreate(SyMemBackend *pAlloc,pgno iSize)
{
	bitvec_rec **apNew;
	Bitvec *p;
	
	p = (Bitvec *)SyMemBackendAlloc(pAlloc,sizeof(*p) );
	if( p == 0 ){
		SXUNUSED(iSize); /* cc warning */
		return 0;
	}
	/* Zero the structure */
	SyZero(p,sizeof(Bitvec));
	/* Allocate a new table */
	p->nSize = 64; /* Must be a power of two */
	apNew = (bitvec_rec **)SyMemBackendAlloc(pAlloc,p->nSize * sizeof(bitvec_rec *));
	if( apNew == 0 ){
		SyMemBackendFree(pAlloc,p);
		return 0;
	}
	/* Zero the new table */
	SyZero((void *)apNew,p->nSize * sizeof(bitvec_rec *));
	/* Fill-in */
	p->apRec = apNew;
	p->pAlloc = pAlloc;
	return p;
}
/*
 * Check if the given page number is already installed in the table.
 * Return true if installed. False otherwise.
 */
UNQLITE_PRIVATE int unqliteBitvecTest(Bitvec *p,pgno i)
{  
	bitvec_rec *pRec;
	/* Point to the desired bucket */
	pRec = p->apRec[i & (p->nSize - 1)];
	for(;;){
		if( pRec == 0 ){ break; }
		if( pRec->iPage == i ){
			/* Page found */
			return 1;
		}
		/* Point to the next entry */
		pRec = pRec->pNextCol;

		if( pRec == 0 ){ break; }
		if( pRec->iPage == i ){
			/* Page found */
			return 1;
		}
		/* Point to the next entry */
		pRec = pRec->pNextCol;


		if( pRec == 0 ){ break; }
		if( pRec->iPage == i ){
			/* Page found */
			return 1;
		}
		/* Point to the next entry */
		pRec = pRec->pNextCol;


		if( pRec == 0 ){ break; }
		if( pRec->iPage == i ){
			/* Page found */
			return 1;
		}
		/* Point to the next entry */
		pRec = pRec->pNextCol;
	}
	/* No such entry */
	return 0;
}
/*
 * Install a given page number in our bitmap (Actually, our hashtable).
 */
UNQLITE_PRIVATE int unqliteBitvecSet(Bitvec *p,pgno i)
{
	bitvec_rec *pRec;
	sxi32 iBuck;
	/* Allocate a new instance */
	pRec = (bitvec_rec *)SyMemBackendPoolAlloc(p->pAlloc,sizeof(bitvec_rec));
	if( pRec == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Zero the structure */
	SyZero(pRec,sizeof(bitvec_rec));
	/* Fill-in */
	pRec->iPage = i;
	iBuck = i & (p->nSize - 1);
	pRec->pNextCol = p->apRec[iBuck];
	p->apRec[iBuck] = pRec;
	pRec->pNext = p->pList;
	p->pList = pRec;
	p->nRec++;
	if( p->nRec >= (p->nSize * 3) && p->nRec < 100000 ){
		/* Grow the hashtable */
		sxu32 nNewSize = p->nSize << 1;
		bitvec_rec *pEntry,**apNew;
		sxu32 n;
		apNew = (bitvec_rec **)SyMemBackendAlloc(p->pAlloc, nNewSize * sizeof(bitvec_rec *));
		if( apNew ){
			sxu32 iBucket;
			/* Zero the new table */
			SyZero((void *)apNew, nNewSize * sizeof(bitvec_rec *));
			/* Rehash all entries */
			n = 0;
			pEntry = p->pList;
			for(;;){
				/* Loop one */
				if( n >= p->nRec ){
					break;
				}
				pEntry->pNextCol = 0;
				/* Install in the new bucket */
				iBucket = pEntry->iPage & (nNewSize - 1);
				pEntry->pNextCol = apNew[iBucket];
				apNew[iBucket] = pEntry;
				/* Point to the next entry */
				pEntry = pEntry->pNext;
				n++;
			}
			/* Release the old table and reflect the change */
			SyMemBackendFree(p->pAlloc,(void *)p->apRec);
			p->apRec = apNew;
			p->nSize  = nNewSize;
		}
	}
	return UNQLITE_OK;
}
/*
 * Destroy a bitvec instance. Reclaim all memory used.
 */
UNQLITE_PRIVATE void unqliteBitvecDestroy(Bitvec *p)
{
	bitvec_rec *pNext,*pRec = p->pList;
	SyMemBackend *pAlloc = p->pAlloc;
	
	for(;;){
		if( p->nRec < 1 ){
			break;
		}
		pNext = pRec->pNext;
		SyMemBackendPoolFree(pAlloc,(void *)pRec);
		pRec = pNext;
		p->nRec--;

		if( p->nRec < 1 ){
			break;
		}
		pNext = pRec->pNext;
		SyMemBackendPoolFree(pAlloc,(void *)pRec);
		pRec = pNext;
		p->nRec--;


		if( p->nRec < 1 ){
			break;
		}
		pNext = pRec->pNext;
		SyMemBackendPoolFree(pAlloc,(void *)pRec);
		pRec = pNext;
		p->nRec--;


		if( p->nRec < 1 ){
			break;
		}
		pNext = pRec->pNext;
		SyMemBackendPoolFree(pAlloc,(void *)pRec);
		pRec = pNext;
		p->nRec--;
	}
	SyMemBackendFree(pAlloc,(void *)p->apRec);
	SyMemBackendFree(pAlloc,p);
}
