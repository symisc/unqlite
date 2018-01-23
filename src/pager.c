/*
 * Symisc unQLite: An Embeddable NoSQL (Post Modern) Database Engine.
 * Copyright (C) 2012-2013, Symisc Systems http://unqlite.org/
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
 /* $SymiscID: pager.c v1.1 Win7 2012-11-29 03:46 stable <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif
/*
** This file implements the pager and the transaction manager for UnQLite (Mostly inspired from the SQLite3 Source tree).
**
** The Pager.eState variable stores the current 'state' of a pager. A
** pager may be in any one of the seven states shown in the following
** state diagram.
**
**                            OPEN <------+------+
**                              |         |      |
**                              V         |      |
**               +---------> READER-------+      |
**               |              |                |
**               |              V                |
**               |<-------WRITER_LOCKED--------->| 
**               |              |                |  
**               |              V                |
**               |<------WRITER_CACHEMOD-------->|
**               |              |                |
**               |              V                |
**               |<-------WRITER_DBMOD---------->|
**               |              |                |
**               |              V                |
**               +<------WRITER_FINISHED-------->+
** 
**  OPEN:
**
**    The pager starts up in this state. Nothing is guaranteed in this
**    state - the file may or may not be locked and the database size is
**    unknown. The database may not be read or written.
**
**    * No read or write transaction is active.
**    * Any lock, or no lock at all, may be held on the database file.
**    * The dbSize and dbOrigSize variables may not be trusted.
**
**  READER:
**
**    In this state all the requirements for reading the database in 
**    rollback mode are met. Unless the pager is (or recently
**    was) in exclusive-locking mode, a user-level read transaction is 
**    open. The database size is known in this state.
** 
**    * A read transaction may be active (but a write-transaction cannot).
**    * A SHARED or greater lock is held on the database file.
**    * The dbSize variable may be trusted (even if a user-level read 
**      transaction is not active). The dbOrigSize variables
**      may not be trusted at this point.
**    * Even if a read-transaction is not open, it is guaranteed that 
**      there is no hot-journal in the file-system.
**
**  WRITER_LOCKED:
**
**    The pager moves to this state from READER when a write-transaction
**    is first opened on the database. In WRITER_LOCKED state, all locks 
**    required to start a write-transaction are held, but no actual 
**    modifications to the cache or database have taken place.
**
**    In rollback mode, a RESERVED or (if the transaction was opened with 
**    EXCLUSIVE flag) EXCLUSIVE lock is obtained on the database file when
**    moving to this state, but the journal file is not written to or opened 
**    to in this state. If the transaction is committed or rolled back while 
**    in WRITER_LOCKED state, all that is required is to unlock the database 
**    file.
**
**    * A write transaction is active.
**    * If the connection is open in rollback-mode, a RESERVED or greater 
**      lock is held on the database file.
**    * The dbSize and dbOrigSize variables are all valid.
**    * The contents of the pager cache have not been modified.
**    * The journal file may or may not be open.
**    * Nothing (not even the first header) has been written to the journal.
**
**  WRITER_CACHEMOD:
**
**    A pager moves from WRITER_LOCKED state to this state when a page is
**    first modified by the upper layer. In rollback mode the journal file
**    is opened (if it is not already open) and a header written to the
**    start of it. The database file on disk has not been modified.
**
**    * A write transaction is active.
**    * A RESERVED or greater lock is held on the database file.
**    * The journal file is open and the first header has been written 
**      to it, but the header has not been synced to disk.
**    * The contents of the page cache have been modified.
**
**  WRITER_DBMOD:
**
**    The pager transitions from WRITER_CACHEMOD into WRITER_DBMOD state
**    when it modifies the contents of the database file.
**
**    * A write transaction is active.
**    * An EXCLUSIVE or greater lock is held on the database file.
**    * The journal file is open and the first header has been written 
**      and synced to disk.
**    * The contents of the page cache have been modified (and possibly
**      written to disk).
**
**  WRITER_FINISHED:
**
**    A rollback-mode pager changes to WRITER_FINISHED state from WRITER_DBMOD
**    state after the entire transaction has been successfully written into the
**    database file. In this state the transaction may be committed simply
**    by finalizing the journal file. Once in WRITER_FINISHED state, it is 
**    not possible to modify the database further. At this point, the upper 
**    layer must either commit or rollback the transaction.
**
**    * A write transaction is active.
**    * An EXCLUSIVE or greater lock is held on the database file.
**    * All writing and syncing of journal and database data has finished.
**      If no error occured, all that remains is to finalize the journal to
**      commit the transaction. If an error did occur, the caller will need
**      to rollback the transaction. 
**  
**
*/
#define PAGER_OPEN                  0
#define PAGER_READER                1
#define PAGER_WRITER_LOCKED         2
#define PAGER_WRITER_CACHEMOD       3
#define PAGER_WRITER_DBMOD          4
#define PAGER_WRITER_FINISHED       5
/*
** Journal files begin with the following magic string.  The data
** was obtained from /dev/random.  It is used only as a sanity check.
**
** NOTE: These values must be different from the one used by SQLite3
** to avoid journal file collision.
**
*/
static const unsigned char aJournalMagic[] = {
  0xa6, 0xe8, 0xcd, 0x2b, 0x1c, 0x92, 0xdb, 0x9f,
};
/*
** The journal header size for this pager. This is usually the same 
** size as a single disk sector. See also setSectorSize().
*/
#define JOURNAL_HDR_SZ(pPager) (pPager->iSectorSize)
/*
 * Database page handle.
 * Each raw disk page is represented in memory by an instance
 * of the following structure.
 */
typedef struct Page Page;
struct Page {
  /* Must correspond to unqlite_page */
  unsigned char *zData;           /* Content of this page */
  void *pUserData;                /* Extra content */
  pgno pgno;                      /* Page number for this page */
  /**********************************************************************
  ** Elements above are public.  All that follows is private to pcache.c
  ** and should not be accessed by other modules.
  */
  Pager *pPager;                 /* The pager this page is part of */
  int flags;                     /* Page flags defined below */
  int nRef;                      /* Number of users of this page */
  Page *pNext, *pPrev;    /* A list of all pages */
  Page *pDirtyNext;             /* Next element in list of dirty pages */
  Page *pDirtyPrev;             /* Previous element in list of dirty pages */
  Page *pNextCollide,*pPrevCollide; /* Collission chain */
  Page *pNextHot,*pPrevHot;    /* Hot dirty pages chain */
};
/* Bit values for Page.flags */
#define PAGE_DIRTY             0x002  /* Page has changed */
#define PAGE_NEED_SYNC         0x004  /* fsync the rollback journal before
                                       ** writing this page to the database */
#define PAGE_DONT_WRITE        0x008  /* Dont write page content to disk */
#define PAGE_NEED_READ         0x010  /* Content is unread */
#define PAGE_IN_JOURNAL        0x020  /* Page written to the journal */
#define PAGE_HOT_DIRTY         0x040  /* Hot dirty page */
#define PAGE_DONT_MAKE_HOT     0x080  /* Dont make this page Hot. In other words,
									   * do not link it to the hot dirty list.
									   */
/*
 * Each active database pager is represented by an instance of
 * the following structure.
 */
struct Pager
{
  SyMemBackend *pAllocator;      /* Memory backend */
  unqlite *pDb;                  /* DB handle that own this instance */
  unqlite_kv_engine *pEngine;    /* Underlying KV storage engine */
  char *zFilename;               /* Name of the database file */
  char *zJournal;                /* Name of the journal file */
  unqlite_vfs *pVfs;             /* Underlying virtual file system */
  unqlite_file *pfd,*pjfd;       /* File descriptors for database and journal */
  pgno dbSize;                   /* Number of pages in the file */
  pgno dbOrigSize;               /* dbSize before the current change */
  sxi64 dbByteSize;              /* Database size in bytes */
  void *pMmap;                   /* Read-only Memory view (mmap) of the whole file if requested (UNQLITE_OPEN_MMAP). */
  sxu32 nRec;                    /* Number of pages written to the journal */
  SyPRNGCtx sPrng;               /* PRNG Context */
  sxu32 cksumInit;               /* Quasi-random value added to every checksum */
  sxu32 iOpenFlags;              /* Flag passed to unqlite_open() after processing */
  sxi64 iJournalOfft;            /* Journal offset we are reading from */
  int (*xBusyHandler)(void *);   /* Busy handler */
  void *pBusyHandlerArg;         /* First arg to xBusyHandler() */
  void (*xPageUnpin)(void *);    /* Page Unpin callback */
  void (*xPageReload)(void *);   /* Page Reload callback */
  Bitvec *pVec;                  /* Bitmap */
  Page *pHeader;                 /* Page one of the database (Unqlite header) */
  Sytm tmCreate;                 /* Database creation time */
  SyString sKv;                  /* Underlying Key/Value storage engine name */
  int iState;                    /* Pager state */
  int iLock;                     /* Lock state */
  sxi32 iFlags;                  /* Control flags (see below) */
  int is_mem;                    /* True for an in-memory database */
  int is_rdonly;                 /* True for a read-only database */
  int no_jrnl;                   /* TRUE to omit journaling */
  int iPageSize;                 /* Page size in bytes (default 4K) */
  int iSectorSize;               /* Size of a single sector on disk */
  unsigned char *zTmpPage;       /* Temporary page */
  Page *pFirstDirty;             /* First dirty pages */
  Page *pDirty;                  /* Transient list of dirty pages */
  Page *pAll;                    /* List of all pages */
  Page *pHotDirty;               /* List of hot dirty pages */
  Page *pFirstHot;               /* First hot dirty page */
  sxu32 nHot;                    /* Total number of hot dirty pages */
  Page **apHash;                 /* Page table */
  sxu32 nSize;                   /* apHash[] size: Must be a power of two  */
  sxu32 nPage;                   /* Total number of page loaded in memory */
  sxu32 nCacheMax;               /* Maximum page to cache*/
};
/* Control flags */
#define PAGER_CTRL_COMMIT_ERR   0x001 /* Commit error */
#define PAGER_CTRL_DIRTY_COMMIT 0x002 /* Dirty commit has been applied */ 
/*
** Read a 32-bit integer from the given file descriptor. 
** All values are stored on disk as big-endian.
*/
static int ReadInt32(unqlite_file *pFd,sxu32 *pOut,sxi64 iOfft)
{
	unsigned char zBuf[4];
	int rc;
	rc = unqliteOsRead(pFd,zBuf,sizeof(zBuf),iOfft);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	SyBigEndianUnpack32(zBuf,pOut);
	return UNQLITE_OK;
}
/*
** Read a 64-bit integer from the given file descriptor. 
** All values are stored on disk as big-endian.
*/
static int ReadInt64(unqlite_file *pFd,sxu64 *pOut,sxi64 iOfft)
{
	unsigned char zBuf[8];
	int rc;
	rc = unqliteOsRead(pFd,zBuf,sizeof(zBuf),iOfft);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	SyBigEndianUnpack64(zBuf,pOut);
	return UNQLITE_OK;
}
/*
** Write a 32-bit integer into the given file descriptor.
*/
static int WriteInt32(unqlite_file *pFd,sxu32 iNum,sxi64 iOfft)
{
	unsigned char zBuf[4];
	int rc;
	SyBigEndianPack32(zBuf,iNum);
	rc = unqliteOsWrite(pFd,zBuf,sizeof(zBuf),iOfft);
	return rc;
}
/*
** Write a 64-bit integer into the given file descriptor.
*/
static int WriteInt64(unqlite_file *pFd,sxu64 iNum,sxi64 iOfft)
{
	unsigned char zBuf[8];
	int rc;
	SyBigEndianPack64(zBuf,iNum);
	rc = unqliteOsWrite(pFd,zBuf,sizeof(zBuf),iOfft);
	return rc;
}
/*
** The maximum allowed sector size. 64KiB. If the xSectorsize() method 
** returns a value larger than this, then MAX_SECTOR_SIZE is used instead.
** This could conceivably cause corruption following a power failure on
** such a system. This is currently an undocumented limit.
*/
#define MAX_SECTOR_SIZE 0x10000
/*
** Get the size of a single sector on disk.
** The sector size will be used used  to determine the size
** and alignment of journal header and within created journal files.
**
** The default sector size is set to 512.
*/
static int GetSectorSize(unqlite_file *pFd)
{
	int iSectorSize = UNQLITE_DEFAULT_SECTOR_SIZE;
	if( pFd ){
		iSectorSize = unqliteOsSectorSize(pFd);
		if( iSectorSize < 32 ){
			iSectorSize = 512;
		}
		if( iSectorSize > MAX_SECTOR_SIZE ){
			iSectorSize = MAX_SECTOR_SIZE;
		}
	}
	return iSectorSize;
}
/* Hash function for page number  */
#define PAGE_HASH(PNUM) (PNUM)
/*
 * Fetch a page from the cache.
 */
static Page * pager_fetch_page(Pager *pPager,pgno page_num)
{
	Page *pEntry;
	if( pPager->nPage < 1 ){
		/* Don't bother hashing */
		return 0;
	}
	/* Perform the lookup */
	pEntry = pPager->apHash[PAGE_HASH(page_num) & (pPager->nSize - 1)];
	for(;;){
		if( pEntry == 0 ){
			break;
		}
		if( pEntry->pgno == page_num ){
			return pEntry;
		}
		/* Point to the next entry in the colission chain */
		pEntry = pEntry->pNextCollide;
	}
	/* No such page */
	return 0;
}
/*
 * Allocate and initialize a new page.
 */
static Page * pager_alloc_page(Pager *pPager,pgno num_page)
{
	Page *pNew;
	
	pNew = (Page *)SyMemBackendPoolAlloc(pPager->pAllocator,sizeof(Page)+pPager->iPageSize);
	if( pNew == 0 ){
		return 0;
	}
	/* Zero the structure */
	SyZero(pNew,sizeof(Page)+pPager->iPageSize);
	/* Page data */
	pNew->zData = (unsigned char *)&pNew[1];
	/* Fill in the structure */
	pNew->pPager = pPager;
	pNew->nRef = 1;
	pNew->pgno = num_page;
	return pNew;
}
/*
 * Increment the reference count of a given page.
 */
static void page_ref(Page *pPage)
{
    if( pPage->pPager->pAllocator->pMutexMethods ){
        SyMutexEnter(pPage->pPager->pAllocator->pMutexMethods, pPage->pPager->pAllocator->pMutex);
    }
	pPage->nRef++;
    if( pPage->pPager->pAllocator->pMutexMethods ){
        SyMutexLeave(pPage->pPager->pAllocator->pMutexMethods, pPage->pPager->pAllocator->pMutex);
    }
}
/*
 * Release an in-memory page after its reference count reach zero.
 */
static int pager_release_page(Pager *pPager,Page *pPage)
{
	int rc = UNQLITE_OK;
	if( !(pPage->flags & PAGE_DIRTY)){
		/* Invoke the unpin callback if available */
		if( pPager->xPageUnpin && pPage->pUserData ){
			pPager->xPageUnpin(pPage->pUserData);
		}
		pPage->pUserData = 0;
		SyMemBackendPoolFree(pPager->pAllocator,pPage);
	}else{
		/* Dirty page, it will be released later when a dirty commit
		 * or the final commit have been applied.
		 */
		rc = UNQLITE_LOCKED;
	}
	return rc;
}
/* Forward declaration */
static int pager_unlink_page(Pager *pPager,Page *pPage);
/*
 * Decrement the reference count of a given page.
 */
static void page_unref(Page *pPage)
{
	int nRef;
    if( pPage->pPager->pAllocator->pMutexMethods ){
        SyMutexEnter(pPage->pPager->pAllocator->pMutexMethods, pPage->pPager->pAllocator->pMutex);
    }
	nRef = pPage->nRef--;
    if( pPage->pPager->pAllocator->pMutexMethods ){
        SyMutexLeave(pPage->pPager->pAllocator->pMutexMethods, pPage->pPager->pAllocator->pMutex);
    }
	if( nRef == 0){
		Pager *pPager = pPage->pPager;
		if( !(pPage->flags & PAGE_DIRTY)  ){
			pager_unlink_page(pPager,pPage);
			/* Release the page */
			pager_release_page(pPager,pPage);
		}else{
			if( pPage->flags & PAGE_DONT_MAKE_HOT ){
				/* Do not add this page to the hot dirty list */
				return;
			}
			if( !(pPage->flags & PAGE_HOT_DIRTY) ){
				/* Add to the hot dirty list */
				pPage->pPrevHot = 0;
				if( pPager->pFirstHot == 0 ){
					pPager->pFirstHot = pPager->pHotDirty = pPage;
				}else{
					pPage->pNextHot = pPager->pHotDirty;
					if( pPager->pHotDirty ){
						pPager->pHotDirty->pPrevHot = pPage;
					}
					pPager->pHotDirty = pPage;
				}
				pPager->nHot++;
				pPage->flags |= PAGE_HOT_DIRTY;
			}
		}
	}
}
/*
 * Link a freshly created page to the list of active page.
 */
static int pager_link_page(Pager *pPager,Page *pPage)
{
	sxu32 nBucket;
	/* Install in the corresponding bucket */
	nBucket = PAGE_HASH(pPage->pgno) & (pPager->nSize - 1);
	pPage->pNextCollide = pPager->apHash[nBucket];
	if( pPager->apHash[nBucket] ){
		pPager->apHash[nBucket]->pPrevCollide = pPage;
	}
	pPager->apHash[nBucket] = pPage;
	/* Link to the list of active pages */
	MACRO_LD_PUSH(pPager->pAll,pPage);
	pPager->nPage++;
	if( (pPager->nPage >= pPager->nSize * 4)  && pPager->nPage < 100000 ){
		/* Grow the hashtable */
		sxu32 nNewSize = pPager->nSize << 1;
		Page *pEntry,**apNew;
		sxu32 n;
		apNew = (Page **)SyMemBackendAlloc(pPager->pAllocator, nNewSize * sizeof(Page *));
		if( apNew ){
			sxu32 iBucket;
			/* Zero the new table */
			SyZero((void *)apNew, nNewSize * sizeof(Page *));
			/* Rehash all entries */
			n = 0;
			pEntry = pPager->pAll;
			for(;;){
				/* Loop one */
				if( n >= pPager->nPage ){
					break;
				}
				pEntry->pNextCollide = pEntry->pPrevCollide = 0;
				/* Install in the new bucket */
				iBucket = PAGE_HASH(pEntry->pgno) & (nNewSize - 1);
				pEntry->pNextCollide = apNew[iBucket];
				if( apNew[iBucket] ){
					apNew[iBucket]->pPrevCollide = pEntry;
				}
				apNew[iBucket] = pEntry;
				/* Point to the next entry */
				pEntry = pEntry->pNext;
				n++;
			}
			/* Release the old table and reflect the change */
			SyMemBackendFree(pPager->pAllocator,(void *)pPager->apHash);
			pPager->apHash = apNew;
			pPager->nSize  = nNewSize;
		}
	}
	return UNQLITE_OK;
}
/*
 * Unlink a page from the list of active pages.
 */
static int pager_unlink_page(Pager *pPager,Page *pPage)
{
	if( pPage->pNextCollide ){
		pPage->pNextCollide->pPrevCollide = pPage->pPrevCollide;
	}
	if( pPage->pPrevCollide ){
		pPage->pPrevCollide->pNextCollide = pPage->pNextCollide;
	}else{
		sxu32 nBucket = PAGE_HASH(pPage->pgno) & (pPager->nSize - 1);
		pPager->apHash[nBucket] = pPage->pNextCollide;
	}
	MACRO_LD_REMOVE(pPager->pAll,pPage);
	pPager->nPage--;
	return UNQLITE_OK;
}
/*
 * Update the content of a cached page.
 */
static int pager_fill_page(Pager *pPager,pgno iNum,void *pContents)
{
	Page *pPage;
	/* Fetch the page from the catch */
	pPage = pager_fetch_page(pPager,iNum);
	if( pPage == 0 ){
		return SXERR_NOTFOUND;
	}
	/* Reflect the change */
	SyMemcpy(pContents,pPage->zData,pPager->iPageSize);

	return UNQLITE_OK;
}
/*
 * Read the content of a page from disk.
 */
static int pager_get_page_contents(Pager *pPager,Page *pPage,int noContent)
{
	int rc = UNQLITE_OK;
	if( pPager->is_mem || noContent || pPage->pgno >= pPager->dbSize ){
		/* Do not bother reading, zero the page contents only */
		SyZero(pPage->zData,pPager->iPageSize);
		return UNQLITE_OK;
	}
	if( (pPager->iOpenFlags & UNQLITE_OPEN_MMAP) && (pPager->pMmap /* Paranoid edition */) ){
		unsigned char *zMap = (unsigned char *)pPager->pMmap;
		pPage->zData = &zMap[pPage->pgno * pPager->iPageSize];
	}else{
		/* Read content */
		rc = unqliteOsRead(pPager->pfd,pPage->zData,pPager->iPageSize,pPage->pgno * pPager->iPageSize);
	}
	return rc;
}
/*
 * Add a page to the dirty list.
 */
static void pager_page_to_dirty_list(Pager *pPager,Page *pPage)
{
	if( pPage->flags & PAGE_DIRTY ){
		/* Already set */
		return;
	}
	/* Mark the page as dirty */
	pPage->flags |= PAGE_DIRTY|PAGE_NEED_SYNC|PAGE_IN_JOURNAL;
	/* Link to the list */
	pPage->pDirtyPrev = 0;
	pPage->pDirtyNext = pPager->pDirty;
	if( pPager->pDirty ){
		pPager->pDirty->pDirtyPrev = pPage;
	}
	pPager->pDirty = pPage;
	if( pPager->pFirstDirty == 0 ){
		pPager->pFirstDirty = pPage;
	}
}
/*
 * Merge sort.
 * The merge sort implementation is based on the one used by
 * the PH7 Embeddable PHP Engine (http://ph7.symisc.net/).
 */
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
static Page * page_merge_dirty(Page *pA, Page *pB)
{
	Page result, *pTail;
    /* Prevent compiler warning */
	result.pDirtyNext = result.pDirtyPrev = 0;
	pTail = &result;
	while( pA && pB ){
		if( pA->pgno < pB->pgno ){
			pTail->pDirtyPrev = pA;
			pA->pDirtyNext = pTail;
			pTail = pA;
			pA = pA->pDirtyPrev;
		}else{
			pTail->pDirtyPrev = pB;
			pB->pDirtyNext = pTail;
			pTail = pB;
			pB = pB->pDirtyPrev;
		}
	}
	if( pA ){
		pTail->pDirtyPrev = pA;
		pA->pDirtyNext = pTail;
	}else if( pB ){
		pTail->pDirtyPrev = pB;
		pB->pDirtyNext = pTail;
	}else{
		pTail->pDirtyPrev = pTail->pDirtyNext = 0;
	}
	return result.pDirtyPrev;
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
static Page * pager_get_dirty_pages(Pager *pPager)
{
	Page *a[N_SORT_BUCKET], *p, *pIn;
	sxu32 i;
	if( pPager->pFirstDirty == 0 ){
		/* Don't bother sorting, the list is already empty */
		return 0;
	}
	SyZero(a, sizeof(a));
	/* Point to the first inserted entry */
	pIn = pPager->pFirstDirty;
	while( pIn ){
		p = pIn;
		pIn = p->pDirtyPrev;
		p->pDirtyPrev = 0;
		for(i=0; i<N_SORT_BUCKET-1; i++){
			if( a[i]==0 ){
				a[i] = p;
				break;
			}else{
				p = page_merge_dirty(a[i], p);
				a[i] = 0;
			}
		}
		if( i==N_SORT_BUCKET-1 ){
			/* To get here, there need to be 2^(N_SORT_BUCKET) elements in he input list.
			 * But that is impossible.
			 */
			a[i] = page_merge_dirty(a[i], p);
		}
	}
	p = a[0];
	for(i=1; i<N_SORT_BUCKET; i++){
		p = page_merge_dirty(p,a[i]);
	}
	p->pDirtyNext = 0;
	return p;
}
/*
 * See block comment above.
 */
static Page * page_merge_hot(Page *pA, Page *pB)
{
	Page result, *pTail;
    /* Prevent compiler warning */
	result.pNextHot = result.pPrevHot = 0;
	pTail = &result;
	while( pA && pB ){
		if( pA->pgno < pB->pgno ){
			pTail->pPrevHot = pA;
			pA->pNextHot = pTail;
			pTail = pA;
			pA = pA->pPrevHot;
		}else{
			pTail->pPrevHot = pB;
			pB->pNextHot = pTail;
			pTail = pB;
			pB = pB->pPrevHot;
		}
	}
	if( pA ){
		pTail->pPrevHot = pA;
		pA->pNextHot = pTail;
	}else if( pB ){
		pTail->pPrevHot = pB;
		pB->pNextHot = pTail;
	}else{
		pTail->pPrevHot = pTail->pNextHot = 0;
	}
	return result.pPrevHot;
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
static Page * pager_get_hot_pages(Pager *pPager)
{
	Page *a[N_SORT_BUCKET], *p, *pIn;
	sxu32 i;
	if( pPager->pFirstHot == 0 ){
		/* Don't bother sorting, the list is already empty */
		return 0;
	}
	SyZero(a, sizeof(a));
	/* Point to the first inserted entry */
	pIn = pPager->pFirstHot;
	while( pIn ){
		p = pIn;
		pIn = p->pPrevHot;
		p->pPrevHot = 0;
		for(i=0; i<N_SORT_BUCKET-1; i++){
			if( a[i]==0 ){
				a[i] = p;
				break;
			}else{
				p = page_merge_hot(a[i], p);
				a[i] = 0;
			}
		}
		if( i==N_SORT_BUCKET-1 ){
			/* To get here, there need to be 2^(N_SORT_BUCKET) elements in he input list.
			 * But that is impossible.
			 */
			a[i] = page_merge_hot(a[i], p);
		}
	}
	p = a[0];
	for(i=1; i<N_SORT_BUCKET; i++){
		p = page_merge_hot(p,a[i]);
	}
	p->pNextHot = 0;
	return p;
}
/*
** The format for the journal header is as follows:
** - 8 bytes: Magic identifying journal format.
** - 4 bytes: Number of records in journal.
** - 4 bytes: Random number used for page hash.
** - 8 bytes: Initial database page count.
** - 4 bytes: Sector size used by the process that wrote this journal.
** - 4 bytes: Database page size.
** 
** Followed by (JOURNAL_HDR_SZ - 28) bytes of unused space.
*/
/*
** Open the journal file and extract its header information.
**
** If the header is read successfully, *pNRec is set to the number of
** page records following this header and *pDbSize is set to the size of the
** database before the transaction began, in pages. Also, pPager->cksumInit
** is set to the value read from the journal header. UNQLITE_OK is returned
** in this case.
**
** If the journal header file appears to be corrupted, UNQLITE_DONE is
** returned and *pNRec and *PDbSize are undefined.  If JOURNAL_HDR_SZ bytes
** cannot be read from the journal file an error code is returned.
*/
static int pager_read_journal_header(
  Pager *pPager,               /* Pager object */
  sxu32 *pNRec,                /* OUT: Value read from the nRec field */
  pgno  *pDbSize               /* OUT: Value of original database size field */
)
{
	sxu32 iPageSize,iSectorSize;
	unsigned char zMagic[8];
	sxi64 iHdrOfft;
	sxi64 iSize;
	int rc;
	/* Offset to start reading from */
	iHdrOfft = 0;
	/* Get the size of the journal */
	rc = unqliteOsFileSize(pPager->pjfd,&iSize);
	if( rc != UNQLITE_OK ){
		return UNQLITE_DONE;
	}
	/* If the journal file is too small, return UNQLITE_DONE. */
	if( 32 /* Minimum sector size */> iSize ){
		return UNQLITE_DONE;
	}
	/* Make sure we are dealing with a valid journal */
	rc = unqliteOsRead(pPager->pjfd,zMagic,sizeof(zMagic),iHdrOfft);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	if( SyMemcmp(zMagic,aJournalMagic,sizeof(zMagic)) != 0 ){
		return UNQLITE_DONE;
	}
	iHdrOfft += sizeof(zMagic);
	 /* Read the first three 32-bit fields of the journal header: The nRec
      ** field, the checksum-initializer and the database size at the start
      ** of the transaction. Return an error code if anything goes wrong.
      */
	rc = ReadInt32(pPager->pjfd,pNRec,iHdrOfft);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	iHdrOfft += 4;
	rc = ReadInt32(pPager->pjfd,&pPager->cksumInit,iHdrOfft);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	iHdrOfft += 4;
	rc = ReadInt64(pPager->pjfd,pDbSize,iHdrOfft);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	iHdrOfft += 8;
	/* Read the page-size and sector-size journal header fields. */
	rc = ReadInt32(pPager->pjfd,&iSectorSize,iHdrOfft);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	iHdrOfft += 4;
	rc = ReadInt32(pPager->pjfd,&iPageSize,iHdrOfft);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Check that the values read from the page-size and sector-size fields
    ** are within range. To be 'in range', both values need to be a power
    ** of two greater than or equal to 512 or 32, and not greater than their 
    ** respective compile time maximum limits.
    */
    if( iPageSize < UNQLITE_MIN_PAGE_SIZE || iSectorSize<32
     || iPageSize > UNQLITE_MAX_PAGE_SIZE || iSectorSize>MAX_SECTOR_SIZE
     || ((iPageSize-1)&iPageSize)!=0    || ((iSectorSize-1)&iSectorSize)!=0 
    ){
      /* If the either the page-size or sector-size in the journal-header is 
      ** invalid, then the process that wrote the journal-header must have 
      ** crashed before the header was synced. In this case stop reading 
      ** the journal file here.
      */
      return UNQLITE_DONE;
    }
    /* Update the assumed sector-size to match the value used by 
    ** the process that created this journal. If this journal was
    ** created by a process other than this one, then this routine
    ** is being called from within pager_playback(). The local value
    ** of Pager.sectorSize is restored at the end of that routine.
    */
    pPager->iSectorSize = iSectorSize;
	pPager->iPageSize = iPageSize;
	/* Ready to rollback */
	pPager->iJournalOfft = JOURNAL_HDR_SZ(pPager);
	/* All done */
	return UNQLITE_OK;
}
/*
 * Write the journal header in the given memory buffer.
 * The given buffer is big enough to hold the whole header.
 */
static int pager_write_journal_header(Pager *pPager,unsigned char *zBuf)
{
	unsigned char *zPtr = zBuf;
	/* 8 bytes magic number */
	SyMemcpy(aJournalMagic,zPtr,sizeof(aJournalMagic));
	zPtr += sizeof(aJournalMagic);
	/* 4 bytes: Number of records in journal. */
	SyBigEndianPack32(zPtr,0);
	zPtr += 4;
	/* 4 bytes: Random number used to compute page checksum. */
	SyBigEndianPack32(zPtr,pPager->cksumInit);
	zPtr += 4;
	/* 8 bytes: Initial database page count. */
	SyBigEndianPack64(zPtr,pPager->dbOrigSize);
	zPtr += 8;
	/* 4 bytes: Sector size used by the process that wrote this journal. */
	SyBigEndianPack32(zPtr,(sxu32)pPager->iSectorSize);
	zPtr += 4;
	/* 4 bytes: Database page size. */
	SyBigEndianPack32(zPtr,(sxu32)pPager->iPageSize);
	return UNQLITE_OK;
}
/*
** Parameter aData must point to a buffer of pPager->pageSize bytes
** of data. Compute and return a checksum based ont the contents of the 
** page of data and the current value of pPager->cksumInit.
**
** This is not a real checksum. It is really just the sum of the 
** random initial value (pPager->cksumInit) and every 200th byte
** of the page data, starting with byte offset (pPager->pageSize%200).
** Each byte is interpreted as an 8-bit unsigned integer.
**
** Changing the formula used to compute this checksum results in an
** incompatible journal file format.
**
** If journal corruption occurs due to a power failure, the most likely 
** scenario is that one end or the other of the record will be changed. 
** It is much less likely that the two ends of the journal record will be
** correct and the middle be corrupt.  Thus, this "checksum" scheme,
** though fast and simple, catches the mostly likely kind of corruption.
*/
static sxu32 pager_cksum(Pager *pPager,const unsigned char *zData)
{
  sxu32 cksum = pPager->cksumInit;         /* Checksum value to return */
  int i = pPager->iPageSize-200;          /* Loop counter */
  while( i>0 ){
    cksum += zData[i];
    i -= 200;
  }
  return cksum;
}
/*
** Read a single page from the journal file opened on file descriptor
** jfd. Playback this one page. Update the offset to read from.
*/
static int pager_play_back_one_page(Pager *pPager,sxi64 *pOfft,unsigned char *zTmp)
{
	unsigned char *zData = zTmp;
	sxi64 iOfft; /* Offset to read from */
	pgno iNum;   /* Pager number */
	sxu32 ckSum; /* Sanity check */
	int rc;
	/* Offset to start reading from */
	iOfft = *pOfft;
	/* Database page number */
	rc = ReadInt64(pPager->pjfd,&iNum,iOfft);
	if( rc != UNQLITE_OK ){ return rc; }
	iOfft += 8;
	/* Page data */
	rc = unqliteOsRead(pPager->pjfd,zData,pPager->iPageSize,iOfft);
	if( rc != UNQLITE_OK ){ return rc; }
	iOfft += pPager->iPageSize;
	/* Page cksum */
	rc = ReadInt32(pPager->pjfd,&ckSum,iOfft);
	if( rc != UNQLITE_OK ){ return rc; }
	iOfft += 4;
	/* Synchronize pointers */
	*pOfft = iOfft;
	/* Make sure we are dealing with a valid page */
	if( ckSum != pager_cksum(pPager,zData) ){
		/* Ignore that page */
		return SXERR_IGNORE;
	}
	if( iNum >= pPager->dbSize ){
		/* Ignore that page */
		return UNQLITE_OK;
	}
	/* playback */
	rc = unqliteOsWrite(pPager->pfd,zData,pPager->iPageSize,iNum * pPager->iPageSize);
	if( rc == UNQLITE_OK ){
		/* Flush the cache */
		pager_fill_page(pPager,iNum,zData);
	}
	return rc;
}
/*
** Playback the journal and thus restore the database file to
** the state it was in before we started making changes.  
**
** The journal file format is as follows: 
**
**  (1)  8 byte prefix.  A copy of aJournalMagic[].
**  (2)  4 byte big-endian integer which is the number of valid page records
**       in the journal. 
**  (3)  4 byte big-endian integer which is the initial value for the 
**       sanity checksum.
**  (4)  8 byte integer which is the number of pages to truncate the
**       database to during a rollback.
**  (5)  4 byte big-endian integer which is the sector size.  The header
**       is this many bytes in size.
**  (6)  4 byte big-endian integer which is the page size.
**  (7)  zero padding out to the next sector size.
**  (8)  Zero or more pages instances, each as follows:
**        +  4 byte page number.
**        +  pPager->pageSize bytes of data.
**        +  4 byte checksum
**
** When we speak of the journal header, we mean the first 7 items above.
** Each entry in the journal is an instance of the 8th item.
**
** Call the value from the second bullet "nRec".  nRec is the number of
** valid page entries in the journal.  In most cases, you can compute the
** value of nRec from the size of the journal file.  But if a power
** failure occurred while the journal was being written, it could be the
** case that the size of the journal file had already been increased but
** the extra entries had not yet made it safely to disk.  In such a case,
** the value of nRec computed from the file size would be too large.  For
** that reason, we always use the nRec value in the header.
**
** If the file opened as the journal file is not a well-formed
** journal file then all pages up to the first corrupted page are rolled
** back (or no pages if the journal header is corrupted). The journal file
** is then deleted and SQLITE_OK returned, just as if no corruption had
** been encountered.
**
** If an I/O or malloc() error occurs, the journal-file is not deleted
** and an error code is returned.
**
*/
static int pager_playback(Pager *pPager)
{
	unsigned char *zTmp = 0; /* cc warning */
	sxu32 n,nRec;
	sxi64 iOfft;
	int rc;
	/* Read the journal header*/
	rc = pager_read_journal_header(pPager,&nRec,&pPager->dbSize);
	if( rc != UNQLITE_OK ){
		if( rc == UNQLITE_DONE ){
			goto end_playback;
		}
		unqliteGenErrorFormat(pPager->pDb,"IO error while reading journal file '%s' header",pPager->zJournal);
		return rc;
	}
	/* Truncate the database back to its original size */
	rc = unqliteOsTruncate(pPager->pfd,pPager->iPageSize * pPager->dbSize);
	if( rc != UNQLITE_OK ){
		unqliteGenError(pPager->pDb,"IO error while truncating database file");
		return rc;
	}
	/* Allocate a temporary page */
	zTmp = (unsigned char *)SyMemBackendAlloc(pPager->pAllocator,(sxu32)pPager->iPageSize);
	if( zTmp == 0 ){
		unqliteGenOutofMem(pPager->pDb);
		return UNQLITE_NOMEM;
	}
	SyZero((void *)zTmp,(sxu32)pPager->iPageSize);
	/* Copy original pages out of the journal and back into the 
    ** database file and/or page cache.
    */
	iOfft = pPager->iJournalOfft;
	for( n = 0 ; n < nRec ; ++n ){
		rc = pager_play_back_one_page(pPager,&iOfft,zTmp);
		if( rc != UNQLITE_OK ){
			if( rc != SXERR_IGNORE ){
				unqliteGenError(pPager->pDb,"Page playback error");
				goto end_playback;
			}
		}
	}
end_playback:
	/* Release the temp page */
	SyMemBackendFree(pPager->pAllocator,(void *)zTmp);
	if( rc == UNQLITE_OK ){
		/* Sync the database file */
		unqliteOsSync(pPager->pfd,UNQLITE_SYNC_FULL);
	}
	if( rc == UNQLITE_DONE ){
		rc = UNQLITE_OK;
	}
	/* Return to the caller */
	return rc;
}
/*
** Unlock the database file to level eLock, which must be either NO_LOCK
** or SHARED_LOCK. Regardless of whether or not the call to xUnlock()
** succeeds, set the Pager.iLock variable to match the (attempted) new lock.
**
** Except, if Pager.iLock is set to NO_LOCK when this function is
** called, do not modify it. See the comment above the #define of 
** NO_LOCK for an explanation of this.
*/
static int pager_unlock_db(Pager *pPager, int eLock)
{
  int rc = UNQLITE_OK;
  if( pPager->iLock != NO_LOCK ){
    rc = unqliteOsUnlock(pPager->pfd,eLock);
    pPager->iLock = eLock;
  }
  return rc;
}
/*
** Lock the database file to level eLock, which must be either SHARED_LOCK,
** RESERVED_LOCK or EXCLUSIVE_LOCK. If the caller is successful, set the
** Pager.eLock variable to the new locking state. 
**
** Except, if Pager.eLock is set to NO_LOCK when this function is 
** called, do not modify it unless the new locking state is EXCLUSIVE_LOCK. 
** See the comment above the #define of NO_LOCK for an explanation 
** of this.
*/
static int pager_lock_db(Pager *pPager, int eLock){
  int rc = UNQLITE_OK;
  if( pPager->iLock < eLock || pPager->iLock == NO_LOCK ){
    rc = unqliteOsLock(pPager->pfd, eLock);
    if( rc==UNQLITE_OK ){
      pPager->iLock = eLock;
    }else{
		unqliteGenError(pPager->pDb,
			rc == UNQLITE_BUSY ? "Another process or thread hold the requested lock" : "Error while requesting database lock"
			);
	}
  }
  return rc;
}
/*
** Try to obtain a lock of type locktype on the database file. If
** a similar or greater lock is already held, this function is a no-op
** (returning UNQLITE_OK immediately).
**
** Otherwise, attempt to obtain the lock using unqliteOsLock(). Invoke 
** the busy callback if the lock is currently not available. Repeat 
** until the busy callback returns false or until the attempt to 
** obtain the lock succeeds.
**
** Return UNQLITE_OK on success and an error code if we cannot obtain
** the lock. If the lock is obtained successfully, set the Pager.state 
** variable to locktype before returning.
*/
static int pager_wait_on_lock(Pager *pPager, int locktype){
  int rc;                              /* Return code */
  do {
    rc = pager_lock_db(pPager,locktype);
  }while( rc==UNQLITE_BUSY && pPager->xBusyHandler && pPager->xBusyHandler(pPager->pBusyHandlerArg) );
  return rc;
}
/*
** This function is called after transitioning from PAGER_OPEN to
** PAGER_SHARED state. It tests if there is a hot journal present in
** the file-system for the given pager. A hot journal is one that 
** needs to be played back. According to this function, a hot-journal
** file exists if the following criteria are met:
**
**   * The journal file exists in the file system, and
**   * No process holds a RESERVED or greater lock on the database file, and
**   * The database file itself is greater than 0 bytes in size, and
**   * The first byte of the journal file exists and is not 0x00.
**
** If the current size of the database file is 0 but a journal file
** exists, that is probably an old journal left over from a prior
** database with the same name. In this case the journal file is
** just deleted using OsDelete, *pExists is set to 0 and UNQLITE_OK
** is returned.
**
** If a hot-journal file is found to exist, *pExists is set to 1 and 
** UNQLITE_OK returned. If no hot-journal file is present, *pExists is
** set to 0 and UNQLITE_OK returned. If an IO error occurs while trying
** to determine whether or not a hot-journal file exists, the IO error
** code is returned and the value of *pExists is undefined.
*/
static int pager_has_hot_journal(Pager *pPager, int *pExists)
{
  unqlite_vfs *pVfs = pPager->pVfs;
  int rc = UNQLITE_OK;           /* Return code */
  int exists = 1;               /* True if a journal file is present */

  *pExists = 0;
  rc = unqliteOsAccess(pVfs, pPager->zJournal, UNQLITE_ACCESS_EXISTS, &exists);
  if( rc==UNQLITE_OK && exists ){
    int locked = 0;             /* True if some process holds a RESERVED lock */

    /* Race condition here:  Another process might have been holding the
    ** the RESERVED lock and have a journal open at the unqliteOsAccess() 
    ** call above, but then delete the journal and drop the lock before
    ** we get to the following unqliteOsCheckReservedLock() call.  If that
    ** is the case, this routine might think there is a hot journal when
    ** in fact there is none.  This results in a false-positive which will
    ** be dealt with by the playback routine.
    */
    rc = unqliteOsCheckReservedLock(pPager->pfd, &locked);
    if( rc==UNQLITE_OK && !locked ){
      sxi64 n = 0;                    /* Size of db file in bytes */
 
      /* Check the size of the database file. If it consists of 0 pages,
      ** then delete the journal file. See the header comment above for 
      ** the reasoning here.  Delete the obsolete journal file under
      ** a RESERVED lock to avoid race conditions.
      */
      rc = unqliteOsFileSize(pPager->pfd,&n);
      if( rc==UNQLITE_OK ){
        if( n < 1 ){
          if( pager_lock_db(pPager, RESERVED_LOCK)==UNQLITE_OK ){
            unqliteOsDelete(pVfs, pPager->zJournal, 0);
			pager_unlock_db(pPager, SHARED_LOCK);
          }
        }else{
          /* The journal file exists and no other connection has a reserved
          ** or greater lock on the database file. */
			*pExists = 1;
        }
      }
    }
  }
  return rc;
}
/*
 * Rollback a journal file. (See block-comment above).
 */
static int pager_journal_rollback(Pager *pPager,int check_hot)
{
	int rc;
	if( check_hot ){
		int iExists = 0; /* cc warning */
		/* Check if the journal file exists */
		rc = pager_has_hot_journal(pPager,&iExists);
		if( rc != UNQLITE_OK  ){
			/* IO error */
			return rc;
		}
		if( !iExists ){
			/* Journal file does not exists */
			return UNQLITE_OK;
		}
	}
	if( pPager->is_rdonly ){
		unqliteGenErrorFormat(pPager->pDb,
			"Cannot rollback journal file '%s' due to a read-only database handle",pPager->zJournal);
		return UNQLITE_READ_ONLY;
	}
	/* Get an EXCLUSIVE lock on the database file. At this point it is
      ** important that a RESERVED lock is not obtained on the way to the
      ** EXCLUSIVE lock. If it were, another process might open the
      ** database file, detect the RESERVED lock, and conclude that the
      ** database is safe to read while this process is still rolling the 
      ** hot-journal back.
      ** 
      ** Because the intermediate RESERVED lock is not requested, any
      ** other process attempting to access the database file will get to 
      ** this point in the code and fail to obtain its own EXCLUSIVE lock 
      ** on the database file.
      **
      ** Unless the pager is in locking_mode=exclusive mode, the lock is
      ** downgraded to SHARED_LOCK before this function returns.
      */
	/* Open the journal file */
	rc = unqliteOsOpen(pPager->pVfs,pPager->pAllocator,pPager->zJournal,&pPager->pjfd,UNQLITE_OPEN_READWRITE);
	if( rc != UNQLITE_OK ){
		unqliteGenErrorFormat(pPager->pDb,"IO error while opening journal file: '%s'",pPager->zJournal);
		goto fail;
	}
	rc = pager_lock_db(pPager,EXCLUSIVE_LOCK);
	if( rc != UNQLITE_OK ){
		unqliteGenError(pPager->pDb,"Cannot acquire an exclusive lock on the database while journal rollback");
		goto fail;
	}
	/* Sync the journal file */
	unqliteOsSync(pPager->pjfd,UNQLITE_SYNC_NORMAL);
	/* Finally rollback the database */
	rc = pager_playback(pPager);
	/* Switch back to shared lock */
	pager_unlock_db(pPager,SHARED_LOCK);
fail:
	/* Close the journal handle */
	unqliteOsCloseFree(pPager->pAllocator,pPager->pjfd);
	pPager->pjfd = 0;
	if( rc == UNQLITE_OK ){
		/* Delete the journal file */
		unqliteOsDelete(pPager->pVfs,pPager->zJournal,TRUE);
	}
	return rc;
}
/*
 * Write the unqlite header (First page). (Big-Endian)
 */
static int pager_write_db_header(Pager *pPager)
{
	unsigned char *zRaw = pPager->pHeader->zData;
	unqlite_kv_engine *pEngine = pPager->pEngine;
	sxu32 nDos;
	sxu16 nLen;
	/* Database signature */
	SyMemcpy(UNQLITE_DB_SIG,zRaw,sizeof(UNQLITE_DB_SIG)-1);
	zRaw += sizeof(UNQLITE_DB_SIG)-1;
	/* Database magic number */
	SyBigEndianPack32(zRaw,UNQLITE_DB_MAGIC);
	zRaw += 4; /* 4 byte magic number */
	/* Database creation time */
	SyZero(&pPager->tmCreate,sizeof(Sytm));
	if( pPager->pVfs->xCurrentTime ){
		pPager->pVfs->xCurrentTime(pPager->pVfs,&pPager->tmCreate);
	}
	/* DOS time format (4 bytes) */
	SyTimeFormatToDos(&pPager->tmCreate,&nDos);
	SyBigEndianPack32(zRaw,nDos);
	zRaw += 4; /* 4 byte DOS time */
	/* Sector size */
	SyBigEndianPack32(zRaw,(sxu32)pPager->iSectorSize);
	zRaw += 4; /* 4 byte sector size */
	/* Page size */
	SyBigEndianPack32(zRaw,(sxu32)pPager->iPageSize);
	zRaw += 4; /* 4 byte page size */
	/* Key value storage engine */
	nLen = (sxu16)SyStrlen(pEngine->pIo->pMethods->zName);
	SyBigEndianPack16(zRaw,nLen); /* 2 byte storage engine name */
	zRaw += 2;
	SyMemcpy((const void *)pEngine->pIo->pMethods->zName,(void *)zRaw,nLen);
	zRaw += nLen;
	/* All rest are meta-data available to the host application */
	return UNQLITE_OK;
}
/*
 * Read the unqlite header (first page). (Big-Endian)
 */
static int pager_extract_header(Pager *pPager,const unsigned char *zRaw,sxu32 nByte)
{
	const unsigned char *zEnd = &zRaw[nByte];
	sxu32 nDos,iMagic;
	sxu16 nLen;
	char *zKv;
	/* Database signature */
	if( SyMemcmp(UNQLITE_DB_SIG,zRaw,sizeof(UNQLITE_DB_SIG)-1) != 0 ){
		/* Corrupt database */
		return UNQLITE_CORRUPT;
	}
	zRaw += sizeof(UNQLITE_DB_SIG)-1;
	/* Database magic number */
	SyBigEndianUnpack32(zRaw,&iMagic);
	zRaw += 4; /* 4 byte magic number */
	if( iMagic != UNQLITE_DB_MAGIC ){
		/* Corrupt database */
		return UNQLITE_CORRUPT;
	}
	/* Database creation time */
	SyBigEndianUnpack32(zRaw,&nDos);
	zRaw += 4; /* 4 byte DOS time format */
	SyDosTimeFormat(nDos,&pPager->tmCreate);
	/* Sector size */
	SyBigEndianUnpack32(zRaw,(sxu32 *)&pPager->iSectorSize);
	zRaw += 4; /* 4 byte sector size */
	/* Page size */
	SyBigEndianUnpack32(zRaw,(sxu32 *)&pPager->iPageSize);
	zRaw += 4; /* 4 byte page size */
	/* Check that the values read from the page-size and sector-size fields
    ** are within range. To be 'in range', both values need to be a power
    ** of two greater than or equal to 512 or 32, and not greater than their 
    ** respective compile time maximum limits.
    */
    if( pPager->iPageSize<UNQLITE_MIN_PAGE_SIZE || pPager->iSectorSize<32
     || pPager->iPageSize>UNQLITE_MAX_PAGE_SIZE || pPager->iSectorSize>MAX_SECTOR_SIZE
     || ((pPager->iPageSize<-1)&pPager->iPageSize)!=0    || ((pPager->iSectorSize-1)&pPager->iSectorSize)!=0 
    ){
      return UNQLITE_CORRUPT;
	}
	/* Key value storage engine */
	SyBigEndianUnpack16(zRaw,&nLen); /* 2 byte storage engine length */
	zRaw += 2;
	if( nLen > (sxu16)(zEnd - zRaw) ){
		nLen = (sxu16)(zEnd - zRaw);
	}
	zKv = (char *)SyMemBackendDup(pPager->pAllocator,(const char *)zRaw,nLen);
	if( zKv == 0 ){
		return UNQLITE_NOMEM;
	}
	SyStringInitFromBuf(&pPager->sKv,zKv,nLen);
	return UNQLITE_OK;
}
/*
 * Read the database header.
 */
static int pager_read_db_header(Pager *pPager)
{
	unsigned char zRaw[UNQLITE_MIN_PAGE_SIZE]; /* Minimum page size */
	sxi64 n = 0;              /* Size of db file in bytes */
	int rc;
	/* Get the file size first */
	rc = unqliteOsFileSize(pPager->pfd,&n);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	pPager->dbByteSize = n;
	if( n > 0 ){
		unqlite_kv_methods *pMethods;
		SyString *pKv;
		pgno nPage;
		if( n < UNQLITE_MIN_PAGE_SIZE ){
			/* A valid unqlite database must be at least 512 bytes long */
			unqliteGenError(pPager->pDb,"Malformed database image");
			return UNQLITE_CORRUPT;
		}
		/* Read the database header */
		rc = unqliteOsRead(pPager->pfd,zRaw,sizeof(zRaw),0);
		if( rc != UNQLITE_OK ){
			unqliteGenError(pPager->pDb,"IO error while reading database header");
			return rc;
		}
		/* Extract the header */
		rc = pager_extract_header(pPager,zRaw,sizeof(zRaw));
		if( rc != UNQLITE_OK ){
			unqliteGenError(pPager->pDb,rc == UNQLITE_NOMEM ? "Unqlite is running out of memory" : "Malformed database image");
			return rc;
		}
		/* Update pager state  */
		nPage = (pgno)(n / pPager->iPageSize);
		if( nPage==0 && n>0 ){
			nPage = 1;
		}
		pPager->dbSize = nPage;
		/* Laod the target Key/Value storage engine */
		pKv = &pPager->sKv;
		pMethods = unqliteFindKVStore(pKv->zString,pKv->nByte);
		if( pMethods == 0 ){
			unqliteGenErrorFormat(pPager->pDb,"No such Key/Value storage engine '%z'",pKv);
			return UNQLITE_NOTIMPLEMENTED;
		}
		/* Install the new KV storage engine */
		rc = unqlitePagerRegisterKvEngine(pPager,pMethods);
		if( rc != UNQLITE_OK ){
			return rc;
		}
	}else{
		/* Set a default page and sector size */
		pPager->iSectorSize = GetSectorSize(pPager->pfd);
		pPager->iPageSize = unqliteGetPageSize();
		SyStringInitFromBuf(&pPager->sKv,pPager->pEngine->pIo->pMethods->zName,SyStrlen(pPager->pEngine->pIo->pMethods->zName));
		pPager->dbSize = 0;
	}
	/* Allocate a temporary page size */
	pPager->zTmpPage = (unsigned char *)SyMemBackendAlloc(pPager->pAllocator,(sxu32)pPager->iPageSize);
	if( pPager->zTmpPage == 0 ){
		unqliteGenOutofMem(pPager->pDb);
		return UNQLITE_NOMEM;
	}
	SyZero(pPager->zTmpPage,(sxu32)pPager->iPageSize);
	return UNQLITE_OK;
}
/*
 * Write the database header.
 */
static int pager_create_header(Pager *pPager)
{
	Page *pHeader;
	int rc;
	/* Allocate a new page */
	pHeader = pager_alloc_page(pPager,0);
	if( pHeader == 0 ){
		return UNQLITE_NOMEM;
	}
	pPager->pHeader = pHeader;
	/* Link the page */
	pager_link_page(pPager,pHeader);
	/* Add to the dirty list */
	pager_page_to_dirty_list(pPager,pHeader);
	/* Write the database header */
	rc = pager_write_db_header(pPager);
	return rc;
}
/*
** This function is called to obtain a shared lock on the database file.
** It is illegal to call unqlitePagerAcquire() until after this function
** has been successfully called. If a shared-lock is already held when
** this function is called, it is a no-op.
**
** The following operations are also performed by this function.
**
**   1) If the pager is currently in PAGER_OPEN state (no lock held
**      on the database file), then an attempt is made to obtain a
**      SHARED lock on the database file. Immediately after obtaining
**      the SHARED lock, the file-system is checked for a hot-journal,
**      which is played back if present. 
**
** If everything is successful, UNQLITE_OK is returned. If an IO error 
** occurs while locking the database, checking for a hot-journal file or 
** rolling back a journal file, the IO error code is returned.
*/
static int pager_shared_lock(Pager *pPager)
{
	int rc = UNQLITE_OK;
	if( pPager->iState == PAGER_OPEN ){
		unqlite_kv_methods *pMethods;
		/* Open the target database */
		rc = unqliteOsOpen(pPager->pVfs,pPager->pAllocator,pPager->zFilename,&pPager->pfd,pPager->iOpenFlags);
		if( rc != UNQLITE_OK ){
			unqliteGenErrorFormat(pPager->pDb,
				"IO error while opening the target database file: %s",pPager->zFilename
				);
			return rc;
		}
		/* Try to obtain a shared lock */
		rc = pager_wait_on_lock(pPager,SHARED_LOCK);
		if( rc == UNQLITE_OK ){
			if( pPager->iLock <= SHARED_LOCK ){
				/* Rollback any hot journal */
				rc = pager_journal_rollback(pPager,1);
				if( rc != UNQLITE_OK ){
					return rc;
				}
			}
			/* Read the database header */
			rc = pager_read_db_header(pPager);
			if( rc != UNQLITE_OK ){
				return rc;
			}
			if(pPager->dbSize > 0 ){
				if( pPager->iOpenFlags & UNQLITE_OPEN_MMAP ){
					const jx9_vfs *pVfs = jx9ExportBuiltinVfs();
					/* Obtain a read-only memory view of the whole file */
					if( pVfs && pVfs->xMmap ){
						int vr;
						vr = pVfs->xMmap(pPager->zFilename,&pPager->pMmap,&pPager->dbByteSize);
						if( vr != JX9_OK ){
							/* Generate a warning */
							unqliteGenError(pPager->pDb,"Cannot obtain a read-only memory view of the target database");
							pPager->iOpenFlags &= ~UNQLITE_OPEN_MMAP;
						}
					}else{
						/* Generate a warning */
						unqliteGenError(pPager->pDb,"Cannot obtain a read-only memory view of the target database");
						pPager->iOpenFlags &= ~UNQLITE_OPEN_MMAP;
					}
				}
			}
			/* Update the pager state */
			pPager->iState = PAGER_READER;
			/* Invoke the xOpen methods if available */
			pMethods = pPager->pEngine->pIo->pMethods;
			if( pMethods->xOpen ){
				rc = pMethods->xOpen(pPager->pEngine,pPager->dbSize);
				if( rc != UNQLITE_OK ){
					unqliteGenErrorFormat(pPager->pDb,
						"xOpen() method of the underlying KV engine '%z' failed",
						&pPager->sKv
						);
					pager_unlock_db(pPager,NO_LOCK);
					pPager->iState = PAGER_OPEN;
					return rc;
				}
			}
		}else if( rc == UNQLITE_BUSY ){
			unqliteGenError(pPager->pDb,"Another process or thread have a reserved or exclusive lock on this database");
		}		
	}
	return rc;
}
/*
** Begin a write-transaction on the specified pager object. If a 
** write-transaction has already been opened, this function is a no-op.
*/
UNQLITE_PRIVATE int unqlitePagerBegin(Pager *pPager)
{
	int rc;
	/* Obtain a shared lock on the database first */
	rc = pager_shared_lock(pPager);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	if( pPager->iState >= PAGER_WRITER_LOCKED ){
		return UNQLITE_OK;
	}
	if( pPager->is_rdonly ){
		unqliteGenError(pPager->pDb,"Read-only database");
		/* Read only database */
		return UNQLITE_READ_ONLY;
	}
	/* Obtain a reserved lock on the database */
	rc = pager_wait_on_lock(pPager,RESERVED_LOCK);
	if( rc == UNQLITE_OK ){
		/* Create the bitvec */
		pPager->pVec = unqliteBitvecCreate(pPager->pAllocator,pPager->dbSize);
		if( pPager->pVec == 0 ){
			unqliteGenOutofMem(pPager->pDb);
			rc = UNQLITE_NOMEM;
			goto fail;
		}
		/* Change to the WRITER_LOCK state */
		pPager->iState = PAGER_WRITER_LOCKED;
		pPager->dbOrigSize = pPager->dbSize;
		pPager->iJournalOfft = 0;
		pPager->nRec = 0;
		if( pPager->dbSize < 1 ){
			/* Write the  database header */
			rc = pager_create_header(pPager);
			if( rc != UNQLITE_OK ){
				goto fail;
			}
			pPager->dbSize = 1;
		}
	}else if( rc == UNQLITE_BUSY ){
		unqliteGenError(pPager->pDb,"Another process or thread have a reserved lock on this database");
	}
	return rc;
fail:
	/* Downgrade to shared lock */
	pager_unlock_db(pPager,SHARED_LOCK);
	return rc;
}
/*
** This function is called at the start of every write transaction.
** There must already be a RESERVED or EXCLUSIVE lock on the database 
** file when this routine is called.
**
*/
static int unqliteOpenJournal(Pager *pPager)
{
	unsigned char *zHeader;
	int rc = UNQLITE_OK;
	if( pPager->is_mem || pPager->no_jrnl ){
		/* Journaling is omitted for this database */
		goto finish;
	}
	if( pPager->iState >= PAGER_WRITER_CACHEMOD ){
		/* Already opened */
		return UNQLITE_OK;
	}
	/* Delete any previously journal with the same name */
	unqliteOsDelete(pPager->pVfs,pPager->zJournal,1);
	/* Open the journal file */
	rc = unqliteOsOpen(pPager->pVfs,pPager->pAllocator,pPager->zJournal,
		&pPager->pjfd,UNQLITE_OPEN_CREATE|UNQLITE_OPEN_READWRITE);
	if( rc != UNQLITE_OK ){
		unqliteGenErrorFormat(pPager->pDb,"IO error while opening journal file: %s",pPager->zJournal);
		return rc;
	}
	/* Write the journal header */
	zHeader = (unsigned char *)SyMemBackendAlloc(pPager->pAllocator,(sxu32)pPager->iSectorSize);
	if( zHeader == 0 ){
		rc = UNQLITE_NOMEM;
		goto fail;
	}
	pager_write_journal_header(pPager,zHeader);
	/* Perform the disk write */
	rc = unqliteOsWrite(pPager->pjfd,zHeader,pPager->iSectorSize,0);
	/* Offset to start writing from */
	pPager->iJournalOfft = pPager->iSectorSize;
	/* All done, journal will be synced later */
	SyMemBackendFree(pPager->pAllocator,zHeader);
finish:
	if( rc == UNQLITE_OK ){
		pPager->iState = PAGER_WRITER_CACHEMOD;
		return UNQLITE_OK;
	}
fail:
	/* Unlink the journal file if something goes wrong */
	unqliteOsCloseFree(pPager->pAllocator,pPager->pjfd);
	unqliteOsDelete(pPager->pVfs,pPager->zJournal,0);
	pPager->pjfd = 0;
	return rc;
}
/*
** Sync the journal. In other words, make sure all the pages that have
** been written to the journal have actually reached the surface of the
** disk and can be restored in the event of a hot-journal rollback.
*
* This routine try also to obtain an exlusive lock on the database.
*/
static int unqliteFinalizeJournal(Pager *pPager,int *pRetry,int close_jrnl)
{
	int rc;
	*pRetry = 0;
	/* Grab the exclusive lock first */
	rc = pager_lock_db(pPager,EXCLUSIVE_LOCK);
	if( rc != UNQLITE_OK ){
		/* Retry the excusive lock process */
		*pRetry = 1;
		rc = UNQLITE_OK;
	}
	if( pPager->no_jrnl ){
		/* Journaling is omitted, return immediately */
		return UNQLITE_OK;
	}
	/* Write the total number of database records */
	rc = WriteInt32(pPager->pjfd,pPager->nRec,8 /* sizeof(aJournalRec) */);
	if( rc != UNQLITE_OK ){
		if( pPager->nRec > 0 ){
			return rc;
		}else{
			/* Not so fatal */
			rc = UNQLITE_OK;
		}
	}
	/* Sync the journal and close it */
	rc = unqliteOsSync(pPager->pjfd,UNQLITE_SYNC_NORMAL);
	if( close_jrnl ){
		/* close the journal file */
		if( UNQLITE_OK != unqliteOsCloseFree(pPager->pAllocator,pPager->pjfd) ){
			if( rc != UNQLITE_OK /* unqliteOsSync */ ){
				return rc;
			}
		}
		pPager->pjfd = 0;
	}
	if( (*pRetry) == 1 ){
		if( pager_lock_db(pPager,EXCLUSIVE_LOCK) == UNQLITE_OK ){
			/* Got exclusive lock */
			*pRetry = 0;
		}
	}
	return UNQLITE_OK;
}
/*
 * Mark a single data page as writeable. The page is written into the 
 * main journal as required.
 */
static int page_write(Pager *pPager,Page *pPage)
{
	int rc;
	if( !pPager->is_mem && !pPager->no_jrnl ){
		/* Write the page to the transaction journal */
		if( pPage->pgno < pPager->dbOrigSize && !unqliteBitvecTest(pPager->pVec,pPage->pgno) ){
			sxu32 cksum;
			if( pPager->nRec == SXU32_HIGH ){
				/* Journal Limit reached */
				unqliteGenError(pPager->pDb,"Journal record limit reached, commit your changes");
				return UNQLITE_LIMIT;
			}
			/* Write the page number */
			rc = WriteInt64(pPager->pjfd,pPage->pgno,pPager->iJournalOfft);
			if( rc != UNQLITE_OK ){ return rc; }
			/* Write the raw page */
			/** CODEC */
			rc = unqliteOsWrite(pPager->pjfd,pPage->zData,pPager->iPageSize,pPager->iJournalOfft + 8);
			if( rc != UNQLITE_OK ){ return rc; }
			/* Compute the checksum */
			cksum = pager_cksum(pPager,pPage->zData);
			rc = WriteInt32(pPager->pjfd,cksum,pPager->iJournalOfft + 8 + pPager->iPageSize);
			if( rc != UNQLITE_OK ){ return rc; }
			/* Update the journal offset */
			pPager->iJournalOfft += 8 /* page num */ + pPager->iPageSize + 4 /* cksum */;
			pPager->nRec++;
			/* Mark as journalled  */
			unqliteBitvecSet(pPager->pVec,pPage->pgno);
		}
	}
	/* Add the page to the dirty list */
	pager_page_to_dirty_list(pPager,pPage);
	/* Update the database size and return. */
	if( (1 + pPage->pgno) > pPager->dbSize ){
		pPager->dbSize = 1 + pPage->pgno;
		if( pPager->dbSize == SXU64_HIGH ){
			unqliteGenError(pPager->pDb,"Database maximum page limit (64-bit) reached");
			return UNQLITE_LIMIT;
		}
	}	
	return UNQLITE_OK;
}
/*
** The argument is the first in a linked list of dirty pages connected
** by the PgHdr.pDirty pointer. This function writes each one of the
** in-memory pages in the list to the database file. The argument may
** be NULL, representing an empty list. In this case this function is
** a no-op.
**
** The pager must hold at least a RESERVED lock when this function
** is called. Before writing anything to the database file, this lock
** is upgraded to an EXCLUSIVE lock. If the lock cannot be obtained,
** UNQLITE_BUSY is returned and no data is written to the database file.
*/
static int pager_write_dirty_pages(Pager *pPager,Page *pDirty)
{
	int rc = UNQLITE_OK;
	Page *pNext;
	for(;;){
		if( pDirty == 0 ){
			break;
		}
		/* Point to the next dirty page */
		pNext = pDirty->pDirtyPrev; /* Not a bug: Reverse link */
		if( (pDirty->flags & PAGE_DONT_WRITE) == 0 ){
			rc = unqliteOsWrite(pPager->pfd,pDirty->zData,pPager->iPageSize,pDirty->pgno * pPager->iPageSize);
			if( rc != UNQLITE_OK ){
				/* A rollback should be done */
				break;
			}
		}
		/* Remove stale flags */
		pDirty->flags &= ~(PAGE_DIRTY|PAGE_DONT_WRITE|PAGE_NEED_SYNC|PAGE_IN_JOURNAL|PAGE_HOT_DIRTY);
		if( pDirty->nRef < 1 ){
			/* Unlink the page now it is unused */
			pager_unlink_page(pPager,pDirty);
			/* Release the page */
			pager_release_page(pPager,pDirty);
		}
		/* Point to the next page */
		pDirty = pNext;
	}
	pPager->pDirty = pPager->pFirstDirty = 0;
	pPager->pHotDirty = pPager->pFirstHot = 0;
	pPager->nHot = 0;
	return rc;
}
/*
** The argument is the first in a linked list of hot dirty pages connected
** by the PgHdr.pHotDirty pointer. This function writes each one of the
** in-memory pages in the list to the database file. The argument may
** be NULL, representing an empty list. In this case this function is
** a no-op.
**
** The pager must hold at least a RESERVED lock when this function
** is called. Before writing anything to the database file, this lock
** is upgraded to an EXCLUSIVE lock. If the lock cannot be obtained,
** UNQLITE_BUSY is returned and no data is written to the database file.
*/
static int pager_write_hot_dirty_pages(Pager *pPager,Page *pDirty)
{
	int rc = UNQLITE_OK;
	Page *pNext;
	for(;;){
		if( pDirty == 0 ){
			break;
		}
		/* Point to the next page */
		pNext = pDirty->pPrevHot; /* Not a bug: Reverse link */
		if( (pDirty->flags & PAGE_DONT_WRITE) == 0 ){
			rc = unqliteOsWrite(pPager->pfd,pDirty->zData,pPager->iPageSize,pDirty->pgno * pPager->iPageSize);
			if( rc != UNQLITE_OK ){
				break;
			}
		}
		/* Remove stale flags */
		pDirty->flags &= ~(PAGE_DIRTY|PAGE_DONT_WRITE|PAGE_NEED_SYNC|PAGE_IN_JOURNAL|PAGE_HOT_DIRTY);
		/* Unlink from the list of dirty pages */
		if( pDirty->pDirtyPrev ){
			pDirty->pDirtyPrev->pDirtyNext = pDirty->pDirtyNext;
		}else{
			pPager->pDirty = pDirty->pDirtyNext;
		}
		if( pDirty->pDirtyNext ){
			pDirty->pDirtyNext->pDirtyPrev = pDirty->pDirtyPrev;
		}else{
			pPager->pFirstDirty = pDirty->pDirtyPrev;
		}
		/* Discard */
		pager_unlink_page(pPager,pDirty);
		/* Release the page */
		pager_release_page(pPager,pDirty);
		/* Next hot page */
		pDirty = pNext;
	}
	return rc;
}
/*
 * Commit a transaction: Phase one.
 */
static int pager_commit_phase1(Pager *pPager)
{
	int get_excl = 0;
	Page *pDirty;
	int rc;
	/* If no database changes have been made, return early. */
	if( pPager->iState < PAGER_WRITER_CACHEMOD ){
		return UNQLITE_OK;
	}
	if( pPager->is_mem ){
		/* An in-memory database */
		return UNQLITE_OK;
	}
	if( pPager->is_rdonly ){
		/* Read-Only DB */
		unqliteGenError(pPager->pDb,"Read-Only database");
		return UNQLITE_READ_ONLY;
	}
	/* Finalize the journal file */
	rc = unqliteFinalizeJournal(pPager,&get_excl,1);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Get the dirty pages */
	pDirty = pager_get_dirty_pages(pPager);
	if( get_excl ){
		/* Wait one last time for the exclusive lock */
		rc = pager_wait_on_lock(pPager,EXCLUSIVE_LOCK);
		if( rc != UNQLITE_OK ){
			unqliteGenError(pPager->pDb,"Cannot obtain an Exclusive lock on the target database");
			return rc;
		}
	}
	if( pPager->iFlags & PAGER_CTRL_DIRTY_COMMIT ){
		/* Synce the database first if a dirty commit have been applied */
		unqliteOsSync(pPager->pfd,UNQLITE_SYNC_NORMAL);
	}
	/* Write the dirty pages */
	rc = pager_write_dirty_pages(pPager,pDirty);
	if( rc != UNQLITE_OK ){
		/* Rollback your DB */
		pPager->iFlags |= PAGER_CTRL_COMMIT_ERR;
		pPager->pFirstDirty = pDirty;
		unqliteGenError(pPager->pDb,"IO error while writing dirty pages, rollback your database");
		return rc;
	}
	/* release all pages */
    {
        Page *p;

        while (1) {
            p = pPager->pAll;
            if (p == NULL) {
                break;
            }
            pager_unlink_page(pPager, p);
        }
    }
	/* If the file on disk is not the same size as the database image,
     * then use unqliteOsTruncate to grow or shrink the file here.
     */
	if( pPager->dbSize != pPager->dbOrigSize ){
		unqliteOsTruncate(pPager->pfd,pPager->iPageSize * pPager->dbSize);
	}
	/* Sync the database file */
	unqliteOsSync(pPager->pfd,UNQLITE_SYNC_FULL);
	/* Remove stale flags */
	pPager->iJournalOfft = 0;
	pPager->nRec = 0;
	return UNQLITE_OK;
}
/*
 * Commit a transaction: Phase two.
 */
static int pager_commit_phase2(Pager *pPager)
{
	if( !pPager->is_mem ){
		if( pPager->iState == PAGER_OPEN ){
			return UNQLITE_OK;
		}
		if( pPager->iState != PAGER_READER ){
			if( !pPager->no_jrnl ){
				/* Finally, unlink the journal file */
				unqliteOsDelete(pPager->pVfs,pPager->zJournal,1);
			}
			/* Downgrade to shraed lock */
			pager_unlock_db(pPager,SHARED_LOCK);
			pPager->iState = PAGER_READER;
			if( pPager->pVec ){
				unqliteBitvecDestroy(pPager->pVec);
				pPager->pVec = 0;
			}
		}
	}
	return UNQLITE_OK;
}
/*
 * Perform a dirty commit.
 */
static int pager_dirty_commit(Pager *pPager)
{
	int get_excl = 0;
	Page *pHot;
	int rc;
	/* Finalize the journal file without closing it */
	rc = unqliteFinalizeJournal(pPager,&get_excl,0);
	if( rc != UNQLITE_OK ){
		/* It's not a fatal error if something goes wrong here since
		 * its not the final commit.
		 */
		return UNQLITE_OK;
	}
	/* Point to the list of hot pages */
	pHot = pager_get_hot_pages(pPager);
	if( pHot == 0 ){
		return UNQLITE_OK;
	}
	if( get_excl ){
		/* Wait one last time for the exclusive lock */
		rc = pager_wait_on_lock(pPager,EXCLUSIVE_LOCK);
		if( rc != UNQLITE_OK ){
			/* Not so fatal, will try another time */
			return UNQLITE_OK;
		}
	}
	/* Tell that a dirty commit happen */
	pPager->iFlags |= PAGER_CTRL_DIRTY_COMMIT;
	/* Write the hot pages now */
	rc = pager_write_hot_dirty_pages(pPager,pHot);
	if( rc != UNQLITE_OK ){
		pPager->iFlags |= PAGER_CTRL_COMMIT_ERR;
		unqliteGenError(pPager->pDb,"IO error while writing hot dirty pages, rollback your database");
		return rc;
	}
	pPager->pFirstHot = pPager->pHotDirty = 0;
	pPager->nHot = 0;
	/* No need to sync the database file here, since the journal is already
	 * open here and this is not the final commit.
	 */
	return UNQLITE_OK;
}
/*
** Commit a transaction and sync the database file for the pager pPager.
**
** This routine ensures that:
**
**   * the journal is synced,
**   * all dirty pages are written to the database file, 
**   * the database file is truncated (if required), and
**   * the database file synced.
**   * the journal file is deleted.
*/
UNQLITE_PRIVATE int unqlitePagerCommit(Pager *pPager)
{
	int rc;
	/* Commit: Phase One */
	rc = pager_commit_phase1(pPager);
	if( rc != UNQLITE_OK ){
		goto fail;
	}
	/* Commit: Phase Two */
	rc = pager_commit_phase2(pPager);
	if( rc != UNQLITE_OK ){
		goto fail;
	}
	/* Remove stale flags */
	pPager->iFlags &= ~PAGER_CTRL_COMMIT_ERR;
	/* All done */
	return UNQLITE_OK;
fail:
	/* Disable the auto-commit flag */
	pPager->pDb->iFlags |= UNQLITE_FL_DISABLE_AUTO_COMMIT;
	return rc;
}
/*
 * Reset the pager to its initial state. This is caused by
 * a rollback operation.
 */
static int pager_reset_state(Pager *pPager,int bResetKvEngine)
{
	unqlite_kv_engine *pEngine = pPager->pEngine;
	Page *pNext,*pPtr = pPager->pAll;
	const unqlite_kv_io *pIo;
	int rc;
	/* Remove stale flags */
	pPager->iFlags &= ~(PAGER_CTRL_COMMIT_ERR|PAGER_CTRL_DIRTY_COMMIT);
	pPager->iJournalOfft = 0;
	pPager->nRec = 0;
	/* Database original size */
	pPager->dbSize = pPager->dbOrigSize;
	/* Discard all in-memory pages */
	for(;;){
		if( pPtr == 0 ){
			break;
		}
		pNext = pPtr->pNext; /* Reverse link */
		/* Remove stale flags */
		pPtr->flags &= ~(PAGE_DIRTY|PAGE_DONT_WRITE|PAGE_NEED_SYNC|PAGE_IN_JOURNAL|PAGE_HOT_DIRTY);
		/* Release the page */
		pager_release_page(pPager,pPtr);
		/* Point to the next page */
		pPtr = pNext;
	}
	pPager->pAll = 0;
	pPager->nPage = 0;
	pPager->pDirty = pPager->pFirstDirty = 0;
	pPager->pHotDirty = pPager->pFirstHot = 0;
	pPager->nHot = 0;
	if( pPager->apHash ){
		/* Zero the table */
		SyZero((void *)pPager->apHash,sizeof(Page *) * pPager->nSize);
	}
	if( pPager->pVec ){
		unqliteBitvecDestroy(pPager->pVec);
		pPager->pVec = 0;
	}
	/* Switch back to shared lock */
	pager_unlock_db(pPager,SHARED_LOCK);
	pPager->iState = PAGER_READER;
	if( bResetKvEngine ){
		/* Reset the underlying KV engine */
		pIo = pEngine->pIo;
		if( pIo->pMethods->xRelease ){
			/* Call the release callback */
			pIo->pMethods->xRelease(pEngine);
		}
		/* Zero the structure */
		SyZero(pEngine,(sxu32)pIo->pMethods->szKv);
		/* Fill in */
		pEngine->pIo = pIo;
		if( pIo->pMethods->xInit ){
			/* Call the init method */
			rc = pIo->pMethods->xInit(pEngine,pPager->iPageSize);
			if( rc != UNQLITE_OK ){
				return rc;
			}
		}
		if( pIo->pMethods->xOpen ){
			/* Call the xOpen method */
			rc = pIo->pMethods->xOpen(pEngine,pPager->dbSize);
			if( rc != UNQLITE_OK ){
				return rc;
			}
		}
	}
	/* All done */
	return UNQLITE_OK;
}
/*
** If a write transaction is open, then all changes made within the 
** transaction are reverted and the current write-transaction is closed.
** The pager falls back to PAGER_READER state if successful.
**
** Otherwise, in rollback mode, this function performs two functions:
**
**   1) It rolls back the journal file, restoring all database file and 
**      in-memory cache pages to the state they were in when the transaction
**      was opened, and
**
**   2) It finalizes the journal file, so that it is not used for hot
**      rollback at any point in the future (i.e. deletion).
**
** Finalization of the journal file (task 2) is only performed if the 
** rollback is successful.
**
*/
UNQLITE_PRIVATE int unqlitePagerRollback(Pager *pPager,int bResetKvEngine)
{
	int rc = UNQLITE_OK;
	if( pPager->iState < PAGER_WRITER_LOCKED ){
		/* A write transaction must be opened */
		return UNQLITE_OK;
	}
	if( pPager->is_mem ){
		/* As of this release 1.1.6: Transactions are not supported for in-memory databases */
		return UNQLITE_OK;
	}
	if( pPager->is_rdonly ){
		/* Read-Only DB */
		unqliteGenError(pPager->pDb,"Read-Only database");
		return UNQLITE_READ_ONLY;
	}
	if( pPager->iState >= PAGER_WRITER_CACHEMOD ){
		if( !pPager->no_jrnl ){
			/* Close any outstanding joural file */
			if( pPager->pjfd ){
				/* Sync the journal file */
				unqliteOsSync(pPager->pjfd,UNQLITE_SYNC_NORMAL);
			}
			unqliteOsCloseFree(pPager->pAllocator,pPager->pjfd);
			pPager->pjfd = 0;
			if( pPager->iFlags & (PAGER_CTRL_COMMIT_ERR|PAGER_CTRL_DIRTY_COMMIT) ){
				/* Perform the rollback */
				rc = pager_journal_rollback(pPager,0);
				if( rc != UNQLITE_OK ){
					/* Set the auto-commit flag */
					pPager->pDb->iFlags |= UNQLITE_FL_DISABLE_AUTO_COMMIT;
					return rc;
				}
			}
		}
		/* Unlink the journal file */
		unqliteOsDelete(pPager->pVfs,pPager->zJournal,1);
		/* Reset the pager state */
		rc = pager_reset_state(pPager,bResetKvEngine);
		if( rc != UNQLITE_OK ){
			/* Mostly an unlikely scenario */
			pPager->pDb->iFlags |= UNQLITE_FL_DISABLE_AUTO_COMMIT; /* Set the auto-commit flag */
			unqliteGenError(pPager->pDb,"Error while reseting pager to its initial state");
			return rc;
		}
	}else{
		/* Downgrade to shared lock */
		pager_unlock_db(pPager,SHARED_LOCK);
		pPager->iState = PAGER_READER;
	}
	return UNQLITE_OK;
}
/*
 *  Mark a data page as non writeable.
 */
static int unqlitePagerDontWrite(unqlite_page *pMyPage)
{
	Page *pPage = (Page *)pMyPage;
	if( pPage->pgno > 0 /* Page 0 is always writeable */ ){
		pPage->flags |= PAGE_DONT_WRITE;
	}
	return UNQLITE_OK;
}
/*
** Mark a data page as writeable. This routine must be called before 
** making changes to a page. The caller must check the return value 
** of this function and be careful not to change any page data unless 
** this routine returns UNQLITE_OK.
*/
static int unqlitePageWrite(unqlite_page *pMyPage)
{
	Page *pPage = (Page *)pMyPage;
	Pager *pPager = pPage->pPager;
	int rc;
	/* Begin the write transaction */
	rc = unqlitePagerBegin(pPager);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	if( pPager->iState == PAGER_WRITER_LOCKED ){
		/* The journal file needs to be opened. Higher level routines have already
		 ** obtained the necessary locks to begin the write-transaction, but the
		 ** rollback journal might not yet be open. Open it now if this is the case.
		 */
		rc = unqliteOpenJournal(pPager);
		if( rc != UNQLITE_OK ){
			return rc;
		}
	}
	if( pPager->nHot > 127 ){
		/* Write hot dirty pages */
		rc = pager_dirty_commit(pPager);
		if( rc != UNQLITE_OK ){
			/* A rollback must be done */
			unqliteGenError(pPager->pDb,"Please perform a rollback");
			return rc;
		}
	}
	/* Write the page to the journal file */
	rc = page_write(pPager,pPage);
	return rc;
}
/*
** Acquire a reference to page number pgno in pager pPager (a page
** reference has type unqlite_page*). If the requested reference is 
** successfully obtained, it is copied to *ppPage and UNQLITE_OK returned.
**
** If the requested page is already in the cache, it is returned. 
** Otherwise, a new page object is allocated and populated with data
** read from the database file.
*/
static int unqlitePagerAcquire(
  Pager *pPager,      /* The pager open on the database file */
  pgno pgno,          /* Page number to fetch */
  unqlite_page **ppPage,    /* OUT: Acquired page */
  int fetchOnly,      /* Cache lookup only */
  int noContent       /* Do not bother reading content from disk if true */
)
{
	Page *pPage;
	int rc;
	/* Acquire a shared lock (if not yet done) on the database and rollback any hot-journal if present */
	rc = pager_shared_lock(pPager);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Fetch the page from the cache */
	pPage = pager_fetch_page(pPager,pgno);
	if( fetchOnly ){
		if( ppPage ){
			*ppPage = (unqlite_page *)pPage;
		}
		return pPage ? UNQLITE_OK : UNQLITE_NOTFOUND;
	}
	if( pPage == 0 ){
		/* Allocate a new page */
		pPage = pager_alloc_page(pPager,pgno);
		if( pPage == 0 ){
			unqliteGenOutofMem(pPager->pDb);
			return UNQLITE_NOMEM;
		}
		/* Read page contents */
		rc = pager_get_page_contents(pPager,pPage,noContent);
		if( rc != UNQLITE_OK ){
			SyMemBackendPoolFree(pPager->pAllocator,pPage);
			return rc;
		}
		/* Link the page */
		pager_link_page(pPager,pPage);
	}else{
		if( ppPage ){
			page_ref(pPage);
		}
	}
	/* All done, page is loaded in memeory */
	if( ppPage ){
		*ppPage = (unqlite_page *)pPage;
	}
	return UNQLITE_OK;
}
/*
 * Return true if we are dealing with an in-memory database.
 */
static int unqliteInMemory(const char *zFilename)
{
	sxu32 n;
	if( SX_EMPTY_STR(zFilename) ){
		/* NULL or the empty string means an in-memory database */
		return TRUE;
	}
	n = SyStrlen(zFilename);
	if( n == sizeof(":mem:") - 1 && 
		SyStrnicmp(zFilename,":mem:",sizeof(":mem:") - 1) == 0 ){
			return TRUE;
	}
	if( n == sizeof(":memory:") - 1 && 
		SyStrnicmp(zFilename,":memory:",sizeof(":memory:") - 1) == 0 ){
			return TRUE;
	}
	return FALSE;
}
/*
 * Allocate a new KV cursor.
 */
UNQLITE_PRIVATE int unqliteInitCursor(unqlite *pDb,unqlite_kv_cursor **ppOut)
{
	unqlite_kv_methods *pMethods;
	unqlite_kv_cursor *pCur;
	sxu32 nByte;
	/* Storage engine methods */
	pMethods = pDb->sDB.pPager->pEngine->pIo->pMethods;
	if( pMethods->szCursor < 1 ){
		/* Implementation does not supprt cursors */
		unqliteGenErrorFormat(pDb,"Storage engine '%s' does not support cursors",pMethods->zName);
		return UNQLITE_NOTIMPLEMENTED;
	}
	nByte = pMethods->szCursor;
	if( nByte < sizeof(unqlite_kv_cursor) ){
		nByte += sizeof(unqlite_kv_cursor);
	}
	pCur = (unqlite_kv_cursor *)SyMemBackendPoolAlloc(&pDb->sMem,nByte);
	if( pCur == 0 ){
		unqliteGenOutofMem(pDb);
		return UNQLITE_NOMEM;
	}
	/* Zero the structure */
	SyZero(pCur,nByte);
	/* Save the cursor */
	pCur->pStore = pDb->sDB.pPager->pEngine;
	/* Invoke the initialization callback if any */
	if( pMethods->xCursorInit ){
		pMethods->xCursorInit(pCur);
	}
	/* All done */
	*ppOut = pCur;
	return UNQLITE_OK;
}
/*
 * Release a cursor.
 */
UNQLITE_PRIVATE int unqliteReleaseCursor(unqlite *pDb,unqlite_kv_cursor *pCur)
{
	unqlite_kv_methods *pMethods;
	/* Storage engine methods */
	pMethods = pDb->sDB.pPager->pEngine->pIo->pMethods;
	/* Invoke the release callback if available */
	if( pMethods->xCursorRelease ){
		pMethods->xCursorRelease(pCur);
	}
	/* Finally, free the whole instance */
	SyMemBackendPoolFree(&pDb->sMem,pCur);
	return UNQLITE_OK;
}
/*
 * Release the underlying KV storage engine and invoke
 * its associated callbacks if available.
 */
static void pager_release_kv_engine(Pager *pPager)
{
	unqlite_kv_engine *pEngine = pPager->pEngine;
	unqlite_db *pStorage = &pPager->pDb->sDB;
	if( pStorage->pCursor ){
		/* Release the associated cursor */
		unqliteReleaseCursor(pPager->pDb,pStorage->pCursor);
		pStorage->pCursor = 0;
	}
	if( pEngine->pIo->pMethods->xRelease ){
		pEngine->pIo->pMethods->xRelease(pEngine);
	}
	/* Release the whole instance */
	SyMemBackendFree(&pPager->pDb->sMem,(void *)pEngine->pIo);
	SyMemBackendFree(&pPager->pDb->sMem,(void *)pEngine);
	pPager->pEngine = 0;
}
/* Forward declaration */
static int pager_kv_io_init(Pager *pPager,unqlite_kv_methods *pMethods,unqlite_kv_io *pIo);
/*
 * Allocate, initialize and register a new KV storage engine
 * within this database instance.
 */
UNQLITE_PRIVATE int unqlitePagerRegisterKvEngine(Pager *pPager,unqlite_kv_methods *pMethods)
{
	unqlite_db *pStorage = &pPager->pDb->sDB;
	unqlite *pDb = pPager->pDb;
	unqlite_kv_engine *pEngine;
	unqlite_kv_io *pIo;
	sxu32 nByte;
	int rc;
	if( pPager->pEngine ){
		if( pMethods == pPager->pEngine->pIo->pMethods ){
			/* Ticket 1432: Same implementation */
			return UNQLITE_OK;
		}
		/* Release the old KV engine */
		pager_release_kv_engine(pPager);
	}
	/* Allocate a new KV engine instance */
	nByte = (sxu32)pMethods->szKv;
	pEngine = (unqlite_kv_engine *)SyMemBackendAlloc(&pDb->sMem,nByte);
	if( pEngine == 0 ){
		unqliteGenOutofMem(pDb);
		return UNQLITE_NOMEM;
	}
	pIo = (unqlite_kv_io *)SyMemBackendAlloc(&pDb->sMem,sizeof(unqlite_kv_io));
	if( pIo == 0 ){
		SyMemBackendFree(&pDb->sMem,pEngine);
		unqliteGenOutofMem(pDb);
		return UNQLITE_NOMEM;
	}
	/* Zero the structure */
	SyZero(pIo,sizeof(unqlite_io_methods));
	SyZero(pEngine,nByte);
	/* Populate the IO structure */
	pager_kv_io_init(pPager,pMethods,pIo);
	pEngine->pIo = pIo;
	/* Invoke the init callback if avaialble */
	if( pMethods->xInit ){
		rc = pMethods->xInit(pEngine,unqliteGetPageSize());
		if( rc != UNQLITE_OK ){
			unqliteGenErrorFormat(pDb,
				"xInit() method of the underlying KV engine '%z' failed",&pPager->sKv);
			goto fail;
		}
		pEngine->pIo = pIo;
	}
	pPager->pEngine = pEngine;
	/* Allocate a new cursor */
	rc = unqliteInitCursor(pDb,&pStorage->pCursor);
	if( rc != UNQLITE_OK ){
		goto fail;
	}
	return UNQLITE_OK;
fail:
	SyMemBackendFree(&pDb->sMem,pEngine);
	SyMemBackendFree(&pDb->sMem,pIo);
	return rc;
}
/*
 * Return the underlying KV storage engine instance.
 */
UNQLITE_PRIVATE unqlite_kv_engine * unqlitePagerGetKvEngine(unqlite *pDb)
{
	return pDb->sDB.pPager->pEngine;
}
/*
* Allocate and initialize a new Pager object. The pager should
* eventually be freed by passing it to unqlitePagerClose().
*
* The zFilename argument is the path to the database file to open.
* If zFilename is NULL or ":memory:" then all information is held
* in cache. It is never written to disk.  This can be used to implement
* an in-memory database.
*/
UNQLITE_PRIVATE int unqlitePagerOpen(
  unqlite_vfs *pVfs,       /* The virtual file system to use */
  unqlite *pDb,            /* Database handle */
  const char *zFilename,   /* Name of the database file to open */
  unsigned int iFlags      /* flags controlling this file */
  )
{
	unqlite_kv_methods *pMethods = 0;
	int is_mem,rd_only,no_jrnl;
	Pager *pPager;
	sxu32 nByte;
	sxu32 nLen;
	int rc;

	/* Select the appropriate KV storage subsytem  */
	if( (iFlags & UNQLITE_OPEN_IN_MEMORY) || unqliteInMemory(zFilename) ){
		/* An in-memory database, record that  */
		pMethods = unqliteFindKVStore("mem",sizeof("mem") - 1); /* Always available */
		iFlags |= UNQLITE_OPEN_IN_MEMORY;
	}else{
		/* Install the default key value storage subsystem [i.e. Linear Hash] */
		pMethods = unqliteFindKVStore("hash",sizeof("hash")-1);
		if( pMethods == 0 ){
			/* Use the b+tree storage backend if the linear hash storage is not available */
			pMethods = unqliteFindKVStore("btree",sizeof("btree")-1);
		}
	}
	if( pMethods == 0 ){
		/* Can't happen */
		unqliteGenError(pDb,"Cannot install a default Key/Value storage engine");
		return UNQLITE_NOTIMPLEMENTED;
	}
	is_mem = (iFlags & UNQLITE_OPEN_IN_MEMORY) != 0;
	rd_only = (iFlags & UNQLITE_OPEN_READONLY) != 0;
	no_jrnl = (iFlags & UNQLITE_OPEN_OMIT_JOURNALING) != 0;
	rc = UNQLITE_OK;
	if( is_mem ){
		/* Omit journaling for in-memory database */
		no_jrnl = 1;
	}
	/* Total number of bytes to allocate */
	nByte = sizeof(Pager);
	nLen = 0;
	if( !is_mem ){
		nLen = SyStrlen(zFilename);
		nByte += pVfs->mxPathname + nLen + sizeof(char) /* null termniator */;
	}
	/* Allocate */
	pPager = (Pager *)SyMemBackendAlloc(&pDb->sMem,nByte);
	if( pPager == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Zero the structure */
	SyZero(pPager,nByte);
	/* Fill-in the structure */
	pPager->pAllocator = &pDb->sMem;
	pPager->pDb = pDb;
	pDb->sDB.pPager = pPager;
	/* Allocate page table */
	pPager->nSize = 128; /* Must be a power of two */
	nByte = pPager->nSize * sizeof(Page *);
	pPager->apHash = (Page **)SyMemBackendAlloc(pPager->pAllocator,nByte);
	if( pPager->apHash == 0 ){
		rc = UNQLITE_NOMEM;
		goto fail;
	}
	SyZero(pPager->apHash,nByte);
	pPager->is_mem = is_mem;
	pPager->no_jrnl = no_jrnl;
	pPager->is_rdonly = rd_only;
	pPager->iOpenFlags = iFlags;
	pPager->pVfs = pVfs;
	SyRandomnessInit(&pPager->sPrng,0,0);
	SyRandomness(&pPager->sPrng,(void *)&pPager->cksumInit,sizeof(sxu32));
	/* Unlimited cache size */
	pPager->nCacheMax = SXU32_HIGH;
	/* Copy filename and journal name */
	if( !is_mem ){
		pPager->zFilename = (char *)&pPager[1];
		rc = UNQLITE_OK;
		if( pVfs->xFullPathname ){
			rc = pVfs->xFullPathname(pVfs,zFilename,pVfs->mxPathname + nLen,pPager->zFilename);
		}
		if( rc != UNQLITE_OK ){
			/* Simple filename copy */
			SyMemcpy(zFilename,pPager->zFilename,nLen);
			pPager->zFilename[nLen] = 0;
			rc = UNQLITE_OK;
		}else{
			nLen = SyStrlen(pPager->zFilename);
		}
		pPager->zJournal = (char *) SyMemBackendAlloc(pPager->pAllocator,nLen + sizeof(UNQLITE_JOURNAL_FILE_SUFFIX) + sizeof(char));
		if( pPager->zJournal == 0 ){
			rc = UNQLITE_NOMEM;
			goto fail;
		}
		/* Copy filename */
		SyMemcpy(pPager->zFilename,pPager->zJournal,nLen);
		/* Copy journal suffix */
		SyMemcpy(UNQLITE_JOURNAL_FILE_SUFFIX,&pPager->zJournal[nLen],sizeof(UNQLITE_JOURNAL_FILE_SUFFIX)-1);
		/* Append the nul terminator to the journal path */
		pPager->zJournal[nLen + ( sizeof(UNQLITE_JOURNAL_FILE_SUFFIX) - 1)] = 0;
	}
	/* Finally, register the selected KV engine */
	rc = unqlitePagerRegisterKvEngine(pPager,pMethods);
	if( rc != UNQLITE_OK ){
		goto fail;
	}
	/* Set the pager state */
	if( pPager->is_mem ){
		pPager->iState = PAGER_WRITER_FINISHED;
		pPager->iLock = EXCLUSIVE_LOCK;
	}else{
		pPager->iState = PAGER_OPEN;
		pPager->iLock = NO_LOCK;
	}
	/* All done, ready for processing */
	return UNQLITE_OK;
fail:
	SyMemBackendFree(&pDb->sMem,pPager);
	return rc;
}
/*
 * Set a cache limit. Note that, this is a simple hint, the pager is not
 * forced to honor this limit.
 */
UNQLITE_PRIVATE int unqlitePagerSetCachesize(Pager *pPager,int mxPage)
{
	if( mxPage < 256 ){
		return UNQLITE_INVALID;
	}
	pPager->nCacheMax = mxPage;
	return UNQLITE_OK;
}
/*
 * Shutdown the page cache. Free all memory and close the database file.
 */
UNQLITE_PRIVATE int unqlitePagerClose(Pager *pPager)
{
	/* Release the KV engine */
	pager_release_kv_engine(pPager);
	if( pPager->iOpenFlags & UNQLITE_OPEN_MMAP ){
		const jx9_vfs *pVfs = jx9ExportBuiltinVfs();
		if( pVfs && pVfs->xUnmap && pPager->pMmap ){
			pVfs->xUnmap(pPager->pMmap,pPager->dbByteSize);
		}
	}
	if( !pPager->is_mem && pPager->iState > PAGER_OPEN ){
		/* Release all lock on this database handle */
		pager_unlock_db(pPager,NO_LOCK);
		/* Close the file  */
		unqliteOsCloseFree(pPager->pAllocator,pPager->pfd);
	}
	if( pPager->pVec ){
		unqliteBitvecDestroy(pPager->pVec);
		pPager->pVec = 0;
	}
	return UNQLITE_OK;
}
/*
 * Generate a random string.
 */
UNQLITE_PRIVATE void unqlitePagerRandomString(Pager *pPager,char *zBuf,sxu32 nLen)
{
	static const char zBase[] = {"abcdefghijklmnopqrstuvwxyz"}; /* English Alphabet */
	sxu32 i;
	/* Generate a binary string first */
	SyRandomness(&pPager->sPrng,zBuf,nLen);
	/* Turn the binary string into english based alphabet */
	for( i = 0 ; i < nLen ; ++i ){
		 zBuf[i] = zBase[zBuf[i] % (sizeof(zBase)-1)];
	 }
}
/*
 * Generate a random number.
 */
UNQLITE_PRIVATE sxu32 unqlitePagerRandomNum(Pager *pPager)
{
	sxu32 iNum;
	SyRandomness(&pPager->sPrng,(void *)&iNum,sizeof(iNum));
	return iNum;
}
/* Exported KV IO Methods */
/* 
 * Refer to [unqlitePagerAcquire()]
 */
static int unqliteKvIoPageGet(unqlite_kv_handle pHandle,pgno iNum,unqlite_page **ppPage)
{
	int rc;
	rc = unqlitePagerAcquire((Pager *)pHandle,iNum,ppPage,0,0);
	return rc;
}
/* 
 * Refer to [unqlitePagerAcquire()]
 */
static int unqliteKvIoPageLookup(unqlite_kv_handle pHandle,pgno iNum,unqlite_page **ppPage)
{
	int rc;
	rc = unqlitePagerAcquire((Pager *)pHandle,iNum,ppPage,1,0);
	return rc;
}
/* 
 * Refer to [unqlitePagerAcquire()]
 */
static int unqliteKvIoNewPage(unqlite_kv_handle pHandle,unqlite_page **ppPage)
{
	Pager *pPager = (Pager *)pHandle;
	int rc;
	/* 
	 * Acquire a reader-lock first so that pPager->dbSize get initialized.
	 */
	rc = pager_shared_lock(pPager);
	if( rc == UNQLITE_OK ){
		rc = unqlitePagerAcquire(pPager,pPager->dbSize == 0 ? /* Page 0 is reserved */ 1 : pPager->dbSize ,ppPage,0,0);
	}
	return rc;
}
/* 
 * Refer to [unqlitePageWrite()]
 */
static int unqliteKvIopageWrite(unqlite_page *pPage)
{
	int rc;
	if( pPage == 0 ){
		/* TICKET 1433-0348 */
		return UNQLITE_OK;
	}
	rc = unqlitePageWrite(pPage);
	return rc;
}
/* 
 * Refer to [unqlitePagerDontWrite()]
 */
static int unqliteKvIoPageDontWrite(unqlite_page *pPage)
{
	int rc;
	if( pPage == 0 ){
		/* TICKET 1433-0348 */
		return UNQLITE_OK;
	}
	rc = unqlitePagerDontWrite(pPage);
	return rc;
}
/* 
 * Refer to [unqliteBitvecSet()]
 */
static int unqliteKvIoPageDontJournal(unqlite_page *pRaw)
{
	Page *pPage = (Page *)pRaw;
	Pager *pPager;
	if( pPage == 0 ){
		/* TICKET 1433-0348 */
		return UNQLITE_OK;
	}
	pPager = pPage->pPager;
	if( pPager->iState >= PAGER_WRITER_LOCKED ){
		if( !pPager->no_jrnl && pPager->pVec && !unqliteBitvecTest(pPager->pVec,pPage->pgno) ){
			unqliteBitvecSet(pPager->pVec,pPage->pgno);
		}
	}
	return UNQLITE_OK;
}
/* 
 * Do not add a page to the hot dirty list.
 */
static int unqliteKvIoPageDontMakeHot(unqlite_page *pRaw)
{
	Page *pPage = (Page *)pRaw;
	
	if( pPage == 0 ){
		/* TICKET 1433-0348 */
		return UNQLITE_OK;
	}
	pPage->flags |= PAGE_DONT_MAKE_HOT;

	/* Remove from hot dirty list if it is already there */
	if( pPage->flags & PAGE_HOT_DIRTY ){
		Pager *pPager = pPage->pPager;
		if( pPage->pNextHot ){
			pPage->pNextHot->pPrevHot = pPage->pPrevHot;
		}
		if( pPage->pPrevHot ){
			pPage->pPrevHot->pNextHot = pPage->pNextHot;
		}
		if( pPager->pFirstHot == pPage ){
			pPager->pFirstHot = pPage->pPrevHot;
		}
		if( pPager->pHotDirty == pPage ){
			pPager->pHotDirty = pPage->pNextHot;
		}
		pPager->nHot--;
		pPage->flags &= ~PAGE_HOT_DIRTY;
	}

	return UNQLITE_OK;
}
/* 
 * Refer to [page_ref()]
 */
static int unqliteKvIopage_ref(unqlite_page *pPage)
{
	if( pPage ){
		page_ref((Page *)pPage);
	}
	return UNQLITE_OK;
}
/* 
 * Refer to [page_unref()]
 */
static int unqliteKvIoPageUnRef(unqlite_page *pPage)
{
	if( pPage ){
		page_unref((Page *)pPage);
	}
	return UNQLITE_OK;
}
/* 
 * Refer to the declaration of the [Pager] structure
 */
static int unqliteKvIoReadOnly(unqlite_kv_handle pHandle)
{
	return ((Pager *)pHandle)->is_rdonly;
}
/* 
 * Refer to the declaration of the [Pager] structure
 */
static int unqliteKvIoPageSize(unqlite_kv_handle pHandle)
{
	return ((Pager *)pHandle)->iPageSize;
}
/* 
 * Refer to the declaration of the [Pager] structure
 */
static unsigned char * unqliteKvIoTempPage(unqlite_kv_handle pHandle)
{
	return ((Pager *)pHandle)->zTmpPage;
}
/* 
 * Set a page unpin callback.
 * Refer to the declaration of the [Pager] structure
 */
static void unqliteKvIoPageUnpin(unqlite_kv_handle pHandle,void (*xPageUnpin)(void *))
{
	Pager *pPager = (Pager *)pHandle;
	pPager->xPageUnpin = xPageUnpin;
}
/* 
 * Set a page reload callback.
 * Refer to the declaration of the [Pager] structure
 */
static void unqliteKvIoPageReload(unqlite_kv_handle pHandle,void (*xPageReload)(void *))
{
	Pager *pPager = (Pager *)pHandle;
	pPager->xPageReload = xPageReload;
}
/* 
 * Log an error.
 * Refer to the declaration of the [Pager] structure
 */
static void unqliteKvIoErr(unqlite_kv_handle pHandle,const char *zErr)
{
	Pager *pPager = (Pager *)pHandle;
	unqliteGenError(pPager->pDb,zErr);
}
/*
 * Init an instance of the [unqlite_kv_io] structure.
 */
static int pager_kv_io_init(Pager *pPager,unqlite_kv_methods *pMethods,unqlite_kv_io *pIo)
{
	pIo->pHandle =  pPager;
	pIo->pMethods = pMethods;
	
	pIo->xGet    = unqliteKvIoPageGet;
	pIo->xLookup = unqliteKvIoPageLookup;
	pIo->xNew    = unqliteKvIoNewPage;
	
	pIo->xWrite     = unqliteKvIopageWrite; 
	pIo->xDontWrite = unqliteKvIoPageDontWrite;
	pIo->xDontJournal = unqliteKvIoPageDontJournal;
	pIo->xDontMkHot = unqliteKvIoPageDontMakeHot;

	pIo->xPageRef   = unqliteKvIopage_ref;
	pIo->xPageUnref = unqliteKvIoPageUnRef;

	pIo->xPageSize = unqliteKvIoPageSize;
	pIo->xReadOnly = unqliteKvIoReadOnly;

	pIo->xTmpPage =  unqliteKvIoTempPage;

	pIo->xSetUnpin = unqliteKvIoPageUnpin;
	pIo->xSetReload = unqliteKvIoPageReload;

	pIo->xErr = unqliteKvIoErr;

	return UNQLITE_OK;
}
