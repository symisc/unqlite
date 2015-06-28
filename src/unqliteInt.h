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
 /* $SymiscID: unqliteInt.h v1.7 FreeBSD 2012-11-02 11:25 devel <chm@symisc.net> $ */
#ifndef __UNQLITEINT_H__
#define __UNQLITEINT_H__
/* Internal interface definitions for UnQLite. */
#ifdef UNQLITE_AMALGAMATION
/* Marker for routines not intended for external use */
#define UNQLITE_PRIVATE static
#define JX9_AMALGAMATION
#else
#define UNQLITE_PRIVATE
#include "unqlite.h"
#include "jx9Int.h"
#endif 
/* forward declaration */
typedef struct unqlite_db unqlite_db;
/*
** The following values may be passed as the second argument to
** UnqliteOsLock(). The various locks exhibit the following semantics:
**
** SHARED:    Any number of processes may hold a SHARED lock simultaneously.
** RESERVED:  A single process may hold a RESERVED lock on a file at
**            any time. Other processes may hold and obtain new SHARED locks.
** PENDING:   A single process may hold a PENDING lock on a file at
**            any one time. Existing SHARED locks may persist, but no new
**            SHARED locks may be obtained by other processes.
** EXCLUSIVE: An EXCLUSIVE lock precludes all other locks.
**
** PENDING_LOCK may not be passed directly to UnqliteOsLock(). Instead, a
** process that requests an EXCLUSIVE lock may actually obtain a PENDING
** lock. This can be upgraded to an EXCLUSIVE lock by a subsequent call to
** UnqliteOsLock().
*/
#define NO_LOCK         0
#define SHARED_LOCK     1
#define RESERVED_LOCK   2
#define PENDING_LOCK    3
#define EXCLUSIVE_LOCK  4
/*
 * UnQLite Locking Strategy (Same as SQLite3)
 *
 * The following #defines specify the range of bytes used for locking.
 * SHARED_SIZE is the number of bytes available in the pool from which
 * a random byte is selected for a shared lock.  The pool of bytes for
 * shared locks begins at SHARED_FIRST. 
 *
 * The same locking strategy and byte ranges are used for Unix and Windows.
 * This leaves open the possiblity of having clients on winNT, and
 * unix all talking to the same shared file and all locking correctly.
 * To do so would require that samba (or whatever
 * tool is being used for file sharing) implements locks correctly between
 * windows and unix.  I'm guessing that isn't likely to happen, but by
 * using the same locking range we are at least open to the possibility.
 *
 * Locking in windows is mandatory.  For this reason, we cannot store
 * actual data in the bytes used for locking.  The pager never allocates
 * the pages involved in locking therefore.  SHARED_SIZE is selected so
 * that all locks will fit on a single page even at the minimum page size.
 * PENDING_BYTE defines the beginning of the locks.  By default PENDING_BYTE
 * is set high so that we don't have to allocate an unused page except
 * for very large databases.  But one should test the page skipping logic 
 * by setting PENDING_BYTE low and running the entire regression suite.
 *
 * Changing the value of PENDING_BYTE results in a subtly incompatible
 * file format.  Depending on how it is changed, you might not notice
 * the incompatibility right away, even running a full regression test.
 * The default location of PENDING_BYTE is the first byte past the
 * 1GB boundary.
 */
#define PENDING_BYTE     (0x40000000)
#define RESERVED_BYTE    (PENDING_BYTE+1)
#define SHARED_FIRST     (PENDING_BYTE+2)
#define SHARED_SIZE      510
/*
 * The default size of a disk sector in bytes.
 */
#ifndef UNQLITE_DEFAULT_SECTOR_SIZE
#define UNQLITE_DEFAULT_SECTOR_SIZE 512
#endif
/*
 * Each open database file is managed by a separate instance
 * of the "Pager" structure.
 */
typedef struct Pager Pager;
/*
 * Each database file to be accessed by the system is an instance
 * of the following structure.
 */
struct unqlite_db
{
	Pager *pPager;              /* Pager and Transaction manager */
	jx9 *pJx9;                  /* Jx9 Engine handle */
	unqlite_kv_cursor *pCursor; /* Database cursor for common usage */
};
/*
 * Each database connection is an instance of the following structure.
 */
struct unqlite
{
	SyMemBackend sMem;              /* Memory allocator subsystem */
	SyBlob sErr;                    /* Error log */
	unqlite_db sDB;                 /* Storage backend */
#if defined(UNQLITE_ENABLE_THREADS)
	const SyMutexMethods *pMethods;  /* Mutex methods */
	SyMutex *pMutex;                 /* Per-handle mutex */
#endif
	unqlite_vm *pVms;                /* List of active VM */
	sxi32 iVm;                       /* Total number of active VM */
	sxi32 iFlags;                    /* Control flags (See below)  */
	unqlite *pNext,*pPrev;           /* List of active DB handles */
	sxu32 nMagic;                    /* Sanity check against misuse */
};
#define UNQLITE_FL_DISABLE_AUTO_COMMIT   0x001 /* Disable auto-commit on close */
/*
 * VM control flags (Mostly related to collection handling).
 */
#define UNQLITE_VM_COLLECTION_CREATE     0x001 /* Create a new collection */
#define UNQLITE_VM_COLLECTION_EXISTS     0x002 /* Exists old collection */
#define UNQLITE_VM_AUTO_LOAD             0x004 /* Auto load a collection from the vfs */
/* Forward declaration */
typedef struct unqlite_col_record unqlite_col_record;
typedef struct unqlite_col unqlite_col;
/*
 * Each an in-memory collection record is stored in an instance
 * of the following structure.
 */
struct unqlite_col_record
{
	unqlite_col *pCol;                      /* Collecion this record belong */
	jx9_int64 nId;                          /* Unique record ID */
	jx9_value sValue;                       /* In-memory value of the record */
	unqlite_col_record *pNextCol,*pPrevCol; /* Collision chain */
	unqlite_col_record *pNext,*pPrev;       /* Linked list of records */
};
/* 
 * Magic number to identify a valid collection on disk.
 */
#define UNQLITE_COLLECTION_MAGIC 0x611E /* sizeof(unsigned short) 2 bytes */
/*
 * A loaded collection is identified by an instance of the following structure.
 */
struct unqlite_col
{
	unqlite_vm *pVm;   /* VM that own this instance */
	SyString sName;    /* ID of the collection */
	sxu32 nHash;       /* sName hash */
	jx9_value sSchema; /* Collection schema */
	sxu32 nSchemaOfft; /* Shema offset in sHeader */
	SyBlob sWorker;    /* General purpose working buffer */
	SyBlob sHeader;    /* Collection binary header */
	jx9_int64 nLastid; /* Last collection record ID */
	jx9_int64 nCurid;  /* Current record ID */
	jx9_int64 nTotRec; /* Total number of records in the collection */
	int iFlags;        /* Control flags (see below) */
	unqlite_col_record **apRecord; /* Hashtable of loaded records */
	unqlite_col_record *pList;     /* Linked list of records */
	sxu32 nRec;        /* Total number of records in apRecord[] */     
	sxu32 nRecSize;    /* apRecord[] size */
	Sytm sCreation;    /* Colleation creation time */
	unqlite_kv_cursor *pCursor; /* Cursor pointing to the raw binary data */
	unqlite_col *pNext,*pPrev;  /* Next and previous collection in the chain */
	unqlite_col *pNextCol,*pPrevCol; /* Collision chain */
};
/*
 * Each unQLite Virtual Machine resulting from successful compilation of
 * a Jx9 script is represented by an instance of the following structure.
 */
struct unqlite_vm
{
	unqlite *pDb;              /* Database handle that own this instance */
	SyMemBackend sAlloc;       /* Private memory allocator */
#if defined(UNQLITE_ENABLE_THREADS)
	SyMutex *pMutex;           /* Recursive mutex associated with this VM. */
#endif
	unqlite_col **apCol;       /* Table of loaded collections */
	unqlite_col *pCol;         /* List of loaded collections */
	sxu32 iCol;                /* Total number of loaded collections */
	sxu32 iColSize;            /* apCol[] size  */
	jx9_vm *pJx9Vm;            /* Compiled Jx9 script*/
	unqlite_vm *pNext,*pPrev;  /* Linked list of active unQLite VM */
	sxu32 nMagic;              /* Magic number to avoid misuse */
};
/* 
 * Database signature to identify a valid database image.
 */
#define UNQLITE_DB_SIG "unqlite"
/*
 * Database magic number (4 bytes).
 */
#define UNQLITE_DB_MAGIC   0xDB7C2712
/*
 * Maximum page size in bytes.
 */
#ifdef UNQLITE_MAX_PAGE_SIZE
# undef UNQLITE_MAX_PAGE_SIZE
#endif
#define UNQLITE_MAX_PAGE_SIZE 65536 /* 65K */
/*
 * Minimum page size in bytes.
 */
#ifdef UNQLITE_MIN_PAGE_SIZE
# undef UNQLITE_MIN_PAGE_SIZE
#endif
#define UNQLITE_MIN_PAGE_SIZE 512
/*
 * The default size of a database page.
 */
#ifndef UNQLITE_DEFAULT_PAGE_SIZE
# undef UNQLITE_DEFAULT_PAGE_SIZE
#endif
# define UNQLITE_DEFAULT_PAGE_SIZE 4096 /* 4K */
/* Forward declaration */
typedef struct Bitvec Bitvec;
/* Private library functions */
/* api.c */
UNQLITE_PRIVATE const SyMemBackend * unqliteExportMemBackend(void);
UNQLITE_PRIVATE int unqliteDataConsumer(
	const void *pOut,   /* Data to consume */
	unsigned int nLen,  /* Data length */
	void *pUserData     /* User private data */
	);
UNQLITE_PRIVATE unqlite_kv_methods * unqliteFindKVStore(
	const char *zName, /* Storage engine name [i.e. Hash, B+tree, LSM, etc.] */
	sxu32 nByte        /* zName length */
	);
UNQLITE_PRIVATE int unqliteGetPageSize(void);
UNQLITE_PRIVATE int unqliteGenError(unqlite *pDb,const char *zErr);
UNQLITE_PRIVATE int unqliteGenErrorFormat(unqlite *pDb,const char *zFmt,...);
UNQLITE_PRIVATE int unqliteGenOutofMem(unqlite *pDb);
/* unql_vm.c */
UNQLITE_PRIVATE int unqliteExistsCollection(unqlite_vm *pVm, SyString *pName);
UNQLITE_PRIVATE int unqliteCreateCollection(unqlite_vm *pVm,SyString *pName);
UNQLITE_PRIVATE jx9_int64 unqliteCollectionLastRecordId(unqlite_col *pCol);
UNQLITE_PRIVATE jx9_int64 unqliteCollectionCurrentRecordId(unqlite_col *pCol);
UNQLITE_PRIVATE int unqliteCollectionCacheRemoveRecord(unqlite_col *pCol,jx9_int64 nId);
UNQLITE_PRIVATE jx9_int64 unqliteCollectionTotalRecords(unqlite_col *pCol);
UNQLITE_PRIVATE void unqliteCollectionResetRecordCursor(unqlite_col *pCol);
UNQLITE_PRIVATE int unqliteCollectionFetchNextRecord(unqlite_col *pCol,jx9_value *pValue);
UNQLITE_PRIVATE int unqliteCollectionFetchRecordById(unqlite_col *pCol,jx9_int64 nId,jx9_value *pValue);
UNQLITE_PRIVATE unqlite_col * unqliteCollectionFetch(unqlite_vm *pVm,SyString *pCol,int iFlag);
UNQLITE_PRIVATE int unqliteCollectionSetSchema(unqlite_col *pCol,jx9_value *pValue);
UNQLITE_PRIVATE int unqliteCollectionPut(unqlite_col *pCol,jx9_value *pValue,int iFlag);
UNQLITE_PRIVATE int unqliteCollectionDropRecord(unqlite_col *pCol,jx9_int64 nId,int wr_header,int log_err);
UNQLITE_PRIVATE int unqliteCollectionUpdateRecord(unqlite_col *pCol,jx9_int64 nId, jx9_value *pValue,int iFlag);
UNQLITE_PRIVATE int unqliteDropCollection(unqlite_col *pCol);
/* unql_jx9.c */
UNQLITE_PRIVATE int unqliteRegisterJx9Functions(unqlite_vm *pVm);
/* fastjson.c */
UNQLITE_PRIVATE sxi32 FastJsonEncode(
	jx9_value *pValue, /* Value to encode */
	SyBlob *pOut,      /* Store encoded value here */
	int iNest          /* Nesting limit */ 
	);
UNQLITE_PRIVATE sxi32 FastJsonDecode(
	const void *pIn, /* Binary JSON  */
	sxu32 nByte,     /* Chunk delimiter */
	jx9_value *pOut, /* Decoded value */
	const unsigned char **pzPtr,
	int iNest /* Nesting limit */
	);
/* vfs.c [io_win.c, io_unix.c ] */
UNQLITE_PRIVATE const unqlite_vfs * unqliteExportBuiltinVfs(void);
/* mem_kv.c */
UNQLITE_PRIVATE const unqlite_kv_methods * unqliteExportMemKvStorage(void);
/* lhash_kv.c */
UNQLITE_PRIVATE const unqlite_kv_methods * unqliteExportDiskKvStorage(void);
/* os.c */
UNQLITE_PRIVATE int unqliteOsRead(unqlite_file *id, void *pBuf, unqlite_int64 amt, unqlite_int64 offset);
UNQLITE_PRIVATE int unqliteOsWrite(unqlite_file *id, const void *pBuf, unqlite_int64 amt, unqlite_int64 offset);
UNQLITE_PRIVATE int unqliteOsTruncate(unqlite_file *id, unqlite_int64 size);
UNQLITE_PRIVATE int unqliteOsSync(unqlite_file *id, int flags);
UNQLITE_PRIVATE int unqliteOsFileSize(unqlite_file *id, unqlite_int64 *pSize);
UNQLITE_PRIVATE int unqliteOsLock(unqlite_file *id, int lockType);
UNQLITE_PRIVATE int unqliteOsUnlock(unqlite_file *id, int lockType);
UNQLITE_PRIVATE int unqliteOsCheckReservedLock(unqlite_file *id, int *pResOut);
UNQLITE_PRIVATE int unqliteOsSectorSize(unqlite_file *id);
UNQLITE_PRIVATE int unqliteOsOpen(
  unqlite_vfs *pVfs,
  SyMemBackend *pAlloc,
  const char *zPath, 
  unqlite_file **ppOut, 
  unsigned int flags
);
UNQLITE_PRIVATE int unqliteOsCloseFree(SyMemBackend *pAlloc,unqlite_file *pId);
UNQLITE_PRIVATE int unqliteOsDelete(unqlite_vfs *pVfs, const char *zPath, int dirSync);
UNQLITE_PRIVATE int unqliteOsAccess(unqlite_vfs *pVfs,const char *zPath,int flags,int *pResOut);
/* bitmap.c */
UNQLITE_PRIVATE Bitvec *unqliteBitvecCreate(SyMemBackend *pAlloc,pgno iSize);
UNQLITE_PRIVATE int unqliteBitvecTest(Bitvec *p,pgno i);
UNQLITE_PRIVATE int unqliteBitvecSet(Bitvec *p,pgno i);
UNQLITE_PRIVATE void unqliteBitvecDestroy(Bitvec *p);
/* pager.c */
UNQLITE_PRIVATE int unqliteInitCursor(unqlite *pDb,unqlite_kv_cursor **ppOut);
UNQLITE_PRIVATE int unqliteReleaseCursor(unqlite *pDb,unqlite_kv_cursor *pCur);
UNQLITE_PRIVATE int unqlitePagerSetCachesize(Pager *pPager,int mxPage);
UNQLITE_PRIVATE int unqlitePagerClose(Pager *pPager);
UNQLITE_PRIVATE int unqlitePagerOpen(
  unqlite_vfs *pVfs,       /* The virtual file system to use */
  unqlite *pDb,            /* Database handle */
  const char *zFilename,   /* Name of the database file to open */
  unsigned int iFlags      /* flags controlling this file */
  );
UNQLITE_PRIVATE int unqlitePagerRegisterKvEngine(Pager *pPager,unqlite_kv_methods *pMethods);
UNQLITE_PRIVATE unqlite_kv_engine * unqlitePagerGetKvEngine(unqlite *pDb);
UNQLITE_PRIVATE int unqlitePagerBegin(Pager *pPager);
UNQLITE_PRIVATE int unqlitePagerCommit(Pager *pPager);
UNQLITE_PRIVATE int unqlitePagerRollback(Pager *pPager,int bResetKvEngine);
UNQLITE_PRIVATE void unqlitePagerRandomString(Pager *pPager,char *zBuf,sxu32 nLen);
UNQLITE_PRIVATE sxu32 unqlitePagerRandomNum(Pager *pPager);
#endif /* __UNQLITEINT_H__ */
