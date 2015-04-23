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
 /* $SymiscID: api.c v2.0 FreeBSD 2012-11-08 23:07 stable <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif
/* This file implement the public interfaces presented to host-applications.
 * Routines in other files are for internal use by UnQLite and should not be
 * accessed by users of the library.
 */
#define UNQLITE_DB_MISUSE(DB) (DB == 0 || DB->nMagic != UNQLITE_DB_MAGIC)
#define UNQLITE_VM_MISUSE(VM) (VM == 0 || VM->nMagic == JX9_VM_STALE)
/* If another thread have released a working instance, the following macros
 * evaluates to true. These macros are only used when the library
 * is built with threading support enabled.
 */
#define UNQLITE_THRD_DB_RELEASE(DB) (DB->nMagic != UNQLITE_DB_MAGIC)
#define UNQLITE_THRD_VM_RELEASE(VM) (VM->nMagic == JX9_VM_STALE)
/* IMPLEMENTATION: unqlite@embedded@symisc 118-09-4785 */
/*
 * All global variables are collected in the structure named "sUnqlMPGlobal".
 * That way it is clear in the code when we are using static variable because
 * its name start with sUnqlMPGlobal.
 */
static struct unqlGlobal_Data
{
	SyMemBackend sAllocator;                /* Global low level memory allocator */
#if defined(UNQLITE_ENABLE_THREADS)
	const SyMutexMethods *pMutexMethods;   /* Mutex methods */
	SyMutex *pMutex;                       /* Global mutex */
	sxu32 nThreadingLevel;                 /* Threading level: 0 == Single threaded/1 == Multi-Threaded 
										    * The threading level can be set using the [unqlite_lib_config()]
											* interface with a configuration verb set to
											* UNQLITE_LIB_CONFIG_THREAD_LEVEL_SINGLE or 
											* UNQLITE_LIB_CONFIG_THREAD_LEVEL_MULTI
											*/
#endif
	SySet kv_storage;                      /* Installed KV storage engines */
	int iPageSize;                         /* Default Page size */
	unqlite_vfs *pVfs;                     /* Underlying virtual file system (Vfs) */
	sxi32 nDB;                             /* Total number of active DB handles */
	unqlite *pDB;                          /* List of active DB handles */
	sxu32 nMagic;                          /* Sanity check against library misuse */
}sUnqlMPGlobal = {
	{0, 0, 0, 0, 0, 0, 0, 0, {0}}, 
#if defined(UNQLITE_ENABLE_THREADS)
	0, 
	0, 
	0, 
#endif
	{0, 0, 0, 0, 0, 0, 0 },
	UNQLITE_DEFAULT_PAGE_SIZE,
	0, 
	0, 
	0, 
	0
};
#define UNQLITE_LIB_MAGIC  0xEA1495BA
#define UNQLITE_LIB_MISUSE (sUnqlMPGlobal.nMagic != UNQLITE_LIB_MAGIC)
/*
 * Supported threading level.
 * These options have meaning only when the library is compiled with multi-threading
 * support. That is, the UNQLITE_ENABLE_THREADS compile time directive must be defined
 * when UnQLite is built.
 * UNQLITE_THREAD_LEVEL_SINGLE:
 *  In this mode, mutexing is disabled and the library can only be used by a single thread.
 * UNQLITE_THREAD_LEVEL_MULTI
 *  In this mode, all mutexes including the recursive mutexes on [unqlite] objects
 *  are enabled so that the application is free to share the same database handle
 *  between different threads at the same time.
 */
#define UNQLITE_THREAD_LEVEL_SINGLE 1 
#define UNQLITE_THREAD_LEVEL_MULTI  2
/*
 * Find a Key Value storage engine from the set of installed engines.
 * Return a pointer to the storage engine methods on success. NULL on failure.
 */
UNQLITE_PRIVATE unqlite_kv_methods * unqliteFindKVStore(
	const char *zName, /* Storage engine name [i.e. Hash, B+tree, LSM, etc.] */
	sxu32 nByte /* zName length */
	)
{
	unqlite_kv_methods **apStore,*pEntry;
	sxu32 n,nMax;
	/* Point to the set of installed engines */
	apStore = (unqlite_kv_methods **)SySetBasePtr(&sUnqlMPGlobal.kv_storage);
	nMax = SySetUsed(&sUnqlMPGlobal.kv_storage);
	for( n = 0 ; n < nMax; ++n ){
		pEntry = apStore[n];
		if( nByte == SyStrlen(pEntry->zName) && SyStrnicmp(pEntry->zName,zName,nByte) == 0 ){
			/* Storage engine found */
			return pEntry;
		}
	}
	/* No such entry, return NULL */
	return 0;
}
/*
 * Configure the UnQLite library.
 * Return UNQLITE_OK on success. Any other return value indicates failure.
 * Refer to [unqlite_lib_config()].
 */
static sxi32 unqliteCoreConfigure(sxi32 nOp, va_list ap)
{
	int rc = UNQLITE_OK;
	switch(nOp){
	    case UNQLITE_LIB_CONFIG_PAGE_SIZE: {
			/* Default page size: Must be a power of two */
			int iPage = va_arg(ap,int);
			if( iPage >= UNQLITE_MIN_PAGE_SIZE && iPage <= UNQLITE_MAX_PAGE_SIZE ){
				if( !(iPage & (iPage - 1)) ){
					sUnqlMPGlobal.iPageSize = iPage;
				}else{
					/* Invalid page size */
					rc = UNQLITE_INVALID;
				}
			}else{
				/* Invalid page size */
				rc = UNQLITE_INVALID;
			}
			break;
										   }
	    case UNQLITE_LIB_CONFIG_STORAGE_ENGINE: {
			/* Install a key value storage engine */
			unqlite_kv_methods *pMethods = va_arg(ap,unqlite_kv_methods *);
			/* Make sure we are delaing with a valid methods */
			if( pMethods == 0 || SX_EMPTY_STR(pMethods->zName) || pMethods->xSeek == 0 || pMethods->xData == 0
				|| pMethods->xKey == 0 || pMethods->xDataLength == 0 || pMethods->xKeyLength == 0 
				|| pMethods->szKv < (int)sizeof(unqlite_kv_engine) ){
					rc = UNQLITE_INVALID;
					break;
			}
			/* Install it */
			rc = SySetPut(&sUnqlMPGlobal.kv_storage,(const void *)&pMethods);
			break;
												}
	    case UNQLITE_LIB_CONFIG_VFS:{
			/* Install a virtual file system */
			unqlite_vfs *pVfs = va_arg(ap,unqlite_vfs *);
			if( pVfs ){
			 sUnqlMPGlobal.pVfs = pVfs;
			}
			break;
								}
		case UNQLITE_LIB_CONFIG_USER_MALLOC: {
			/* Use an alternative low-level memory allocation routines */
			const SyMemMethods *pMethods = va_arg(ap, const SyMemMethods *);
			/* Save the memory failure callback (if available) */
			ProcMemError xMemErr = sUnqlMPGlobal.sAllocator.xMemError;
			void *pMemErr = sUnqlMPGlobal.sAllocator.pUserData;
			if( pMethods == 0 ){
				/* Use the built-in memory allocation subsystem */
				rc = SyMemBackendInit(&sUnqlMPGlobal.sAllocator, xMemErr, pMemErr);
			}else{
				rc = SyMemBackendInitFromOthers(&sUnqlMPGlobal.sAllocator, pMethods, xMemErr, pMemErr);
			}
			break;
										  }
		case UNQLITE_LIB_CONFIG_MEM_ERR_CALLBACK: {
			/* Memory failure callback */
			ProcMemError xMemErr = va_arg(ap, ProcMemError);
			void *pUserData = va_arg(ap, void *);
			sUnqlMPGlobal.sAllocator.xMemError = xMemErr;
			sUnqlMPGlobal.sAllocator.pUserData = pUserData;
			break;
												 }	  
		case UNQLITE_LIB_CONFIG_USER_MUTEX: {
#if defined(UNQLITE_ENABLE_THREADS)
			/* Use an alternative low-level mutex subsystem */
			const SyMutexMethods *pMethods = va_arg(ap, const SyMutexMethods *);
#if defined (UNTRUST)
			if( pMethods == 0 ){
				rc = UNQLITE_CORRUPT;
			}
#endif
			/* Sanity check */
			if( pMethods->xEnter == 0 || pMethods->xLeave == 0 || pMethods->xNew == 0){
				/* At least three criticial callbacks xEnter(), xLeave() and xNew() must be supplied */
				rc = UNQLITE_CORRUPT;
				break;
			}
			if( sUnqlMPGlobal.pMutexMethods ){
				/* Overwrite the previous mutex subsystem */
				SyMutexRelease(sUnqlMPGlobal.pMutexMethods, sUnqlMPGlobal.pMutex);
				if( sUnqlMPGlobal.pMutexMethods->xGlobalRelease ){
					sUnqlMPGlobal.pMutexMethods->xGlobalRelease();
				}
				sUnqlMPGlobal.pMutex = 0;
			}
			/* Initialize and install the new mutex subsystem */
			if( pMethods->xGlobalInit ){
				rc = pMethods->xGlobalInit();
				if ( rc != UNQLITE_OK ){
					break;
				}
			}
			/* Create the global mutex */
			sUnqlMPGlobal.pMutex = pMethods->xNew(SXMUTEX_TYPE_FAST);
			if( sUnqlMPGlobal.pMutex == 0 ){
				/*
				 * If the supplied mutex subsystem is so sick that we are unable to
				 * create a single mutex, there is no much we can do here.
				 */
				if( pMethods->xGlobalRelease ){
					pMethods->xGlobalRelease();
				}
				rc = UNQLITE_CORRUPT;
				break;
			}
			sUnqlMPGlobal.pMutexMethods = pMethods;			
			if( sUnqlMPGlobal.nThreadingLevel == 0 ){
				/* Set a default threading level */
				sUnqlMPGlobal.nThreadingLevel = UNQLITE_THREAD_LEVEL_MULTI; 
			}
#endif
			break;
										   }
		case UNQLITE_LIB_CONFIG_THREAD_LEVEL_SINGLE:
#if defined(UNQLITE_ENABLE_THREADS)
			/* Single thread mode (Only one thread is allowed to play with the library) */
			sUnqlMPGlobal.nThreadingLevel = UNQLITE_THREAD_LEVEL_SINGLE;
			jx9_lib_config(JX9_LIB_CONFIG_THREAD_LEVEL_SINGLE);
#endif
			break;
		case UNQLITE_LIB_CONFIG_THREAD_LEVEL_MULTI:
#if defined(UNQLITE_ENABLE_THREADS)
			/* Multi-threading mode (library is thread safe and database handles and virtual machines
			 * may be shared between multiple threads).
			 */
			sUnqlMPGlobal.nThreadingLevel = UNQLITE_THREAD_LEVEL_MULTI;
			jx9_lib_config(JX9_LIB_CONFIG_THREAD_LEVEL_MULTI);
#endif
			break;
		default:
			/* Unknown configuration option */
			rc = UNQLITE_CORRUPT;
			break;
	}
	return rc;
}
/*
 * [CAPIREF: unqlite_lib_config()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_lib_config(int nConfigOp,...)
{
	va_list ap;
	int rc;
	if( sUnqlMPGlobal.nMagic == UNQLITE_LIB_MAGIC ){
		/* Library is already initialized, this operation is forbidden */
		return UNQLITE_LOCKED;
	}
	va_start(ap,nConfigOp);
	rc = unqliteCoreConfigure(nConfigOp,ap);
	va_end(ap);
	return rc;
}
/*
 * Global library initialization
 * Refer to [unqlite_lib_init()]
 * This routine must be called to initialize the memory allocation subsystem, the mutex 
 * subsystem prior to doing any serious work with the library. The first thread to call
 * this routine does the initialization process and set the magic number so no body later
 * can re-initialize the library. If subsequent threads call this  routine before the first
 * thread have finished the initialization process, then the subsequent threads must block 
 * until the initialization process is done.
 */
static sxi32 unqliteCoreInitialize(void)
{
	const unqlite_kv_methods *pMethods;
	const unqlite_vfs *pVfs; /* Built-in vfs */
#if defined(UNQLITE_ENABLE_THREADS)
	const SyMutexMethods *pMutexMethods = 0;
	SyMutex *pMaster = 0;
#endif
	int rc;
	/*
	 * If the library is already initialized, then a call to this routine
	 * is a no-op.
	 */
	if( sUnqlMPGlobal.nMagic == UNQLITE_LIB_MAGIC ){
		return UNQLITE_OK; /* Already initialized */
	}
	if( sUnqlMPGlobal.pVfs == 0 ){  /* Allow setting your own vfs */
		/* Point to the built-in vfs */
		pVfs = unqliteExportBuiltinVfs();
		/* Install it */
		unqlite_lib_config(UNQLITE_LIB_CONFIG_VFS, pVfs);
	}
#if defined(UNQLITE_ENABLE_THREADS)
	if( sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_SINGLE ){
		pMutexMethods = sUnqlMPGlobal.pMutexMethods;
		if( pMutexMethods == 0 ){
			/* Use the built-in mutex subsystem */
			pMutexMethods = SyMutexExportMethods();
			if( pMutexMethods == 0 ){
				return UNQLITE_CORRUPT; /* Can't happen */
			}
			/* Install the mutex subsystem */
			rc = unqlite_lib_config(UNQLITE_LIB_CONFIG_USER_MUTEX, pMutexMethods);
			if( rc != UNQLITE_OK ){
				return rc;
			}
		}
		/* Obtain a static mutex so we can initialize the library without calling malloc() */
		pMaster = SyMutexNew(pMutexMethods, SXMUTEX_TYPE_STATIC_1);
		if( pMaster == 0 ){
			return UNQLITE_CORRUPT; /* Can't happen */
		}
	}
	/* Lock the master mutex */
	rc = UNQLITE_OK;
	SyMutexEnter(pMutexMethods, pMaster); /* NO-OP if sUnqlMPGlobal.nThreadingLevel == UNQLITE_THREAD_LEVEL_SINGLE */
	if( sUnqlMPGlobal.nMagic != UNQLITE_LIB_MAGIC ){
#endif
		if( sUnqlMPGlobal.sAllocator.pMethods == 0 ){
			/* Install a memory subsystem */
			rc = unqlite_lib_config(UNQLITE_LIB_CONFIG_USER_MALLOC, 0); /* zero mean use the built-in memory backend */
			if( rc != UNQLITE_OK ){
				/* If we are unable to initialize the memory backend, there is no much we can do here.*/
				goto End;
			}
		}
#if defined(UNQLITE_ENABLE_THREADS)
		if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE ){
			/* Protect the memory allocation subsystem */
			rc = SyMemBackendMakeThreadSafe(&sUnqlMPGlobal.sAllocator, sUnqlMPGlobal.pMutexMethods);
			if( rc != UNQLITE_OK ){
				goto End;
			}
		}
#endif
		SySetInit(&sUnqlMPGlobal.kv_storage,&sUnqlMPGlobal.sAllocator,sizeof(unqlite_kv_methods *));
		/* Install the built-in Key Value storage engines */
		pMethods = unqliteExportMemKvStorage(); /* In-memory storage */
		unqlite_lib_config(UNQLITE_LIB_CONFIG_STORAGE_ENGINE,pMethods);
		/* Default disk key/value storage engine */
		pMethods = unqliteExportDiskKvStorage(); /* Disk storage */
		unqlite_lib_config(UNQLITE_LIB_CONFIG_STORAGE_ENGINE,pMethods);
		/* Default page size */
		if( sUnqlMPGlobal.iPageSize < UNQLITE_MIN_PAGE_SIZE ){
			unqlite_lib_config(UNQLITE_LIB_CONFIG_PAGE_SIZE,UNQLITE_DEFAULT_PAGE_SIZE);
		}
		/* Our library is initialized, set the magic number */
		sUnqlMPGlobal.nMagic = UNQLITE_LIB_MAGIC;
		rc = UNQLITE_OK;
#if defined(UNQLITE_ENABLE_THREADS)
	} /* sUnqlMPGlobal.nMagic != UNQLITE_LIB_MAGIC */
#endif
End:
#if defined(UNQLITE_ENABLE_THREADS)
	/* Unlock the master mutex */
	SyMutexLeave(pMutexMethods, pMaster); /* NO-OP if sUnqlMPGlobal.nThreadingLevel == UNQLITE_THREAD_LEVEL_SINGLE */
#endif
	return rc;
}
/* Forward declaration */
static int unqliteVmRelease(unqlite_vm *pVm);
/*
 * Release a single instance of an unqlite database handle.
 */
static int unqliteDbRelease(unqlite *pDb)
{
	unqlite_db *pStore = &pDb->sDB;
	unqlite_vm *pVm,*pNext;
	int rc = UNQLITE_OK;
	if( (pDb->iFlags & UNQLITE_FL_DISABLE_AUTO_COMMIT) == 0 ){
		/* Commit any outstanding transaction */
		rc = unqlitePagerCommit(pStore->pPager);
		if( rc != UNQLITE_OK ){
			/* Rollback the transaction */
			rc = unqlitePagerRollback(pStore->pPager,FALSE);
		}
	}else{
		/* Rollback any outstanding transaction */
		rc = unqlitePagerRollback(pStore->pPager,FALSE);
	}
	/* Close the pager */
	unqlitePagerClose(pStore->pPager);
	/* Release any active VM's */
	pVm = pDb->pVms;
	for(;;){
		if( pDb->iVm < 1 ){
			break;
		}
		/* Point to the next entry */
		pNext = pVm->pNext;
		unqliteVmRelease(pVm);
		pVm = pNext;
		pDb->iVm--;
	}
	/* Release the Jx9 handle */
	jx9_release(pStore->pJx9);
	/* Set a dummy magic number */
	pDb->nMagic = 0x7250;
	/* Release the whole memory subsystem */
	SyMemBackendRelease(&pDb->sMem);
	/* Commit or rollback result */
	return rc;
}
/*
 * Release all resources consumed by the library.
 * Note: This call is not thread safe. Refer to [unqlite_lib_shutdown()].
 */
static void unqliteCoreShutdown(void)
{
	unqlite *pDb, *pNext;
	/* Release all active databases handles */
	pDb = sUnqlMPGlobal.pDB;
	for(;;){
		if( sUnqlMPGlobal.nDB < 1 ){
			break;
		}
		pNext = pDb->pNext;
		unqliteDbRelease(pDb); 
		pDb = pNext;
		sUnqlMPGlobal.nDB--;
	}
	/* Release the storage methods container */
	SySetRelease(&sUnqlMPGlobal.kv_storage);
#if defined(UNQLITE_ENABLE_THREADS)
	/* Release the mutex subsystem */
	if( sUnqlMPGlobal.pMutexMethods ){
		if( sUnqlMPGlobal.pMutex ){
			SyMutexRelease(sUnqlMPGlobal.pMutexMethods, sUnqlMPGlobal.pMutex);
			sUnqlMPGlobal.pMutex = 0;
		}
		if( sUnqlMPGlobal.pMutexMethods->xGlobalRelease ){
			sUnqlMPGlobal.pMutexMethods->xGlobalRelease();
		}
		sUnqlMPGlobal.pMutexMethods = 0;
	}
	sUnqlMPGlobal.nThreadingLevel = 0;
#endif
	if( sUnqlMPGlobal.sAllocator.pMethods ){
		/* Release the memory backend */
		SyMemBackendRelease(&sUnqlMPGlobal.sAllocator);
	}
	sUnqlMPGlobal.nMagic = 0x1764;
	/* Finally, shutdown the Jx9 library */
	jx9_lib_shutdown();
}
/*
 * [CAPIREF: unqlite_lib_init()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_lib_init(void)
{
	int rc;
	rc = unqliteCoreInitialize();
	return rc;
}
/*
 * [CAPIREF: unqlite_lib_shutdown()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_lib_shutdown(void)
{
	if( sUnqlMPGlobal.nMagic != UNQLITE_LIB_MAGIC ){
		/* Already shut */
		return UNQLITE_OK;
	}
	unqliteCoreShutdown();
	return UNQLITE_OK;
}
/*
 * [CAPIREF: unqlite_lib_is_threadsafe()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_lib_is_threadsafe(void)
{
	if( sUnqlMPGlobal.nMagic != UNQLITE_LIB_MAGIC ){
		return 0;
	}
#if defined(UNQLITE_ENABLE_THREADS)
		if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE ){
			/* Muli-threading support is enabled */
			return 1;
		}else{
			/* Single-threading */
			return 0;
		}
#else
	return 0;
#endif
}
/*
 *
 * [CAPIREF: unqlite_lib_version()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
const char * unqlite_lib_version(void)
{
	return UNQLITE_VERSION;
}
/*
 *
 * [CAPIREF: unqlite_lib_signature()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
const char * unqlite_lib_signature(void)
{
	return UNQLITE_SIG;
}
/*
 *
 * [CAPIREF: unqlite_lib_ident()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
const char * unqlite_lib_ident(void)
{
	return UNQLITE_IDENT;
}
/*
 *
 * [CAPIREF: unqlite_lib_copyright()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
const char * unqlite_lib_copyright(void)
{
	return UNQLITE_COPYRIGHT;
}
/*
 * Remove harmfull and/or stale flags passed to the [unqlite_open()] interface.
 */
static unsigned int unqliteSanityzeFlag(unsigned int iFlags)
{
	iFlags &= ~UNQLITE_OPEN_EXCLUSIVE; /* Reserved flag */
	if( iFlags & UNQLITE_OPEN_TEMP_DB ){
		/* Omit journaling for temporary database */
		iFlags |= UNQLITE_OPEN_OMIT_JOURNALING|UNQLITE_OPEN_CREATE;
	}
	if( (iFlags & (UNQLITE_OPEN_READONLY|UNQLITE_OPEN_READWRITE)) == 0 ){
		/* Auto-append the R+W flag */
		iFlags |= UNQLITE_OPEN_READWRITE;
	}
	if( iFlags & UNQLITE_OPEN_CREATE ){
		iFlags &= ~(UNQLITE_OPEN_MMAP|UNQLITE_OPEN_READONLY);
		/* Auto-append the R+W flag */
		iFlags |= UNQLITE_OPEN_READWRITE;
	}else{
		if( iFlags & UNQLITE_OPEN_READONLY ){
			iFlags &= ~UNQLITE_OPEN_READWRITE;
		}else if( iFlags & UNQLITE_OPEN_READWRITE ){
			iFlags &= ~UNQLITE_OPEN_MMAP;
		}
	}
	return iFlags;
}
/*
 * This routine does the work of initializing a database handle on behalf
 * of [unqlite_open()].
 */
static int unqliteInitDatabase(
	unqlite *pDB,            /* Database handle */
	SyMemBackend *pParent,   /* Master memory backend */
	const char *zFilename,   /* Target database */
	unsigned int iFlags      /* Open flags */
	)
{
	unqlite_db *pStorage = &pDB->sDB;
	int rc;
	/* Initialiaze the memory subsystem */
	SyMemBackendInitFromParent(&pDB->sMem,pParent);
//#if defined(UNQLITE_ENABLE_THREADS)
//	/* No need for internal mutexes */
//	SyMemBackendDisbaleMutexing(&pDB->sMem);
//#endif
	SyBlobInit(&pDB->sErr,&pDB->sMem);
	/* Sanityze flags */
	iFlags = unqliteSanityzeFlag(iFlags);
	/* Init the pager and the transaction manager */
	rc = unqlitePagerOpen(sUnqlMPGlobal.pVfs,pDB,zFilename,iFlags);
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Allocate a new Jx9 engine handle */
	rc = jx9_init(&pStorage->pJx9);
	if( rc != JX9_OK ){
		return rc;
	}
	return UNQLITE_OK;
}
/*
 * Allocate and initialize a new UnQLite Virtual Mahcine and attach it
 * to the compiled Jx9 script.
 */
static int unqliteInitVm(unqlite *pDb,jx9_vm *pJx9Vm,unqlite_vm **ppOut)
{
	unqlite_vm *pVm;

	*ppOut = 0;
	/* Allocate a new VM instance */
	pVm = (unqlite_vm *)SyMemBackendPoolAlloc(&pDb->sMem,sizeof(unqlite_vm));
	if( pVm == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Zero the structure */
	SyZero(pVm,sizeof(unqlite_vm));
	/* Initialize */
	SyMemBackendInitFromParent(&pVm->sAlloc,&pDb->sMem);
	/* Allocate a new collection table */
	pVm->apCol = (unqlite_col **)SyMemBackendAlloc(&pVm->sAlloc,32 * sizeof(unqlite_col *)); 
	if( pVm->apCol == 0 ){
		goto fail;
	}
	pVm->iColSize = 32; /* Must be a power of two */
	/* Zero the table */
	SyZero((void *)pVm->apCol,pVm->iColSize * sizeof(unqlite_col *));
#if defined(UNQLITE_ENABLE_THREADS)
	if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE ){
		 /* Associate a recursive mutex with this instance */
		 pVm->pMutex = SyMutexNew(sUnqlMPGlobal.pMutexMethods, SXMUTEX_TYPE_RECURSIVE);
		 if( pVm->pMutex == 0 ){
			 goto fail;
		 }
	 }
#endif
	/* Link the VM to the list of active virtual machines */
	pVm->pJx9Vm = pJx9Vm;
	pVm->pDb = pDb;
	MACRO_LD_PUSH(pDb->pVms,pVm);
	pDb->iVm++;
	/* Register Jx9 functions */
	unqliteRegisterJx9Functions(pVm);
	/* Set the magic number */
	pVm->nMagic = JX9_VM_INIT; /* Same magic number as Jx9 */
	/* All done */
	*ppOut = pVm;
	return UNQLITE_OK;
fail:
	SyMemBackendRelease(&pVm->sAlloc);
	SyMemBackendPoolFree(&pDb->sMem,pVm);
	return UNQLITE_NOMEM;
}
/*
 * Release an active VM.
 */
static int unqliteVmRelease(unqlite_vm *pVm)
{
	/* Release the Jx9 VM */
	jx9_vm_release(pVm->pJx9Vm);
	/* Release the private memory backend */
	SyMemBackendRelease(&pVm->sAlloc);
	/* Upper layer will discard this VM from the list
	 * of active VM.
	 */
	return UNQLITE_OK;
}
/*
 * Return the default page size.
 */
UNQLITE_PRIVATE int unqliteGetPageSize(void)
{
	int iSize =  sUnqlMPGlobal.iPageSize;
	if( iSize < UNQLITE_MIN_PAGE_SIZE || iSize > UNQLITE_MAX_PAGE_SIZE ){
		iSize = UNQLITE_DEFAULT_PAGE_SIZE;
	}
	return iSize;
}
/*
 * Generate an error message.
 */
UNQLITE_PRIVATE int unqliteGenError(unqlite *pDb,const char *zErr)
{
	int rc;
	/* Append the error message */
	rc = SyBlobAppend(&pDb->sErr,(const void *)zErr,SyStrlen(zErr));
	/* Append a new line */
	SyBlobAppend(&pDb->sErr,(const void *)"\n",sizeof(char));
	return rc;
}
/*
 * Generate an error message (Printf like).
 */
UNQLITE_PRIVATE int unqliteGenErrorFormat(unqlite *pDb,const char *zFmt,...)
{
	va_list ap;
	int rc;
	va_start(ap,zFmt);
	rc = SyBlobFormatAp(&pDb->sErr,zFmt,ap);
	va_end(ap);
	/* Append a new line */
	SyBlobAppend(&pDb->sErr,(const void *)"\n",sizeof(char));
	return rc;
}
/*
 * Generate an error message (Out of memory).
 */
UNQLITE_PRIVATE int unqliteGenOutofMem(unqlite *pDb)
{
	int rc;
	rc = unqliteGenError(pDb,"unQLite is running out of memory");
	return rc;
}
/*
 * Configure a working UnQLite database handle.
 */
static int unqliteConfigure(unqlite *pDb,int nOp,va_list ap)
{
	int rc = UNQLITE_OK;
	switch(nOp){
	case UNQLITE_CONFIG_JX9_ERR_LOG:
		/* Jx9 compile-time error log */
		rc = jx9EngineConfig(pDb->sDB.pJx9,JX9_CONFIG_ERR_LOG,ap);
		break;
	case UNQLITE_CONFIG_MAX_PAGE_CACHE: {
		int max_page = va_arg(ap,int);
		/* Maximum number of page to cache (Simple hint). */
		rc = unqlitePagerSetCachesize(pDb->sDB.pPager,max_page);
		break;
										}
	case UNQLITE_CONFIG_ERR_LOG: {
		/* Database error log if any */
		const char **pzPtr = va_arg(ap, const char **);
		int *pLen = va_arg(ap, int *);
		if( pzPtr == 0 ){
			rc = JX9_CORRUPT;
			break;
		}
		/* NULL terminate the error-log buffer */
		SyBlobNullAppend(&pDb->sErr);
		/* Point to the error-log buffer */
		*pzPtr = (const char *)SyBlobData(&pDb->sErr);
		if( pLen ){
			if( SyBlobLength(&pDb->sErr) > 1 /* NULL '\0' terminator */ ){
				*pLen = (int)SyBlobLength(&pDb->sErr);
			}else{
				*pLen = 0;
			}
		}
		break;
								 }
	case UNQLITE_CONFIG_DISABLE_AUTO_COMMIT:{
		/* Disable auto-commit */
		pDb->iFlags |= UNQLITE_FL_DISABLE_AUTO_COMMIT;
		break;
											}
	case UNQLITE_CONFIG_GET_KV_NAME: {
		/* Name of the underlying KV storage engine */
		const char **pzPtr = va_arg(ap,const char **);
		if( pzPtr ){
			unqlite_kv_engine *pEngine;
			pEngine = unqlitePagerGetKvEngine(pDb);
			/* Point to the name */
			*pzPtr = pEngine->pIo->pMethods->zName;
		}
		break;
									 }
	default:
		/* Unknown configuration option */
		rc = UNQLITE_UNKNOWN;
		break;
	}
	return rc;
}
/*
 * Export the global (master) memory allocator to submodules.
 */
UNQLITE_PRIVATE const SyMemBackend * unqliteExportMemBackend(void)
{
	return &sUnqlMPGlobal.sAllocator;
}
/*
 * [CAPIREF: unqlite_open()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_open(unqlite **ppDB,const char *zFilename,unsigned int iMode)
{
	unqlite *pHandle;
	int rc;
#if defined(UNTRUST)
	if( ppDB == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	*ppDB = 0;
	/* One-time automatic library initialization */
	rc = unqliteCoreInitialize();
	if( rc != UNQLITE_OK ){
		return rc;
	}
	/* Allocate a new database handle */
	pHandle = (unqlite *)SyMemBackendPoolAlloc(&sUnqlMPGlobal.sAllocator, sizeof(unqlite));
	if( pHandle == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Zero the structure */
	SyZero(pHandle,sizeof(unqlite));
	if( iMode < 1 ){
		/* Assume a read-only database */
		iMode = UNQLITE_OPEN_READONLY|UNQLITE_OPEN_MMAP;
	}
	/* Init the database */
	rc = unqliteInitDatabase(pHandle,&sUnqlMPGlobal.sAllocator,zFilename,iMode);
	if( rc != UNQLITE_OK ){
		goto Release;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	if( !(iMode & UNQLITE_OPEN_NOMUTEX) && (sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE) ){
		 /* Associate a recursive mutex with this instance */
		 pHandle->pMutex = SyMutexNew(sUnqlMPGlobal.pMutexMethods, SXMUTEX_TYPE_RECURSIVE);
		 if( pHandle->pMutex == 0 ){
			 rc = UNQLITE_NOMEM;
			 goto Release;
		 }
	 }
#endif
	/* Link to the list of active DB handles */
#if defined(UNQLITE_ENABLE_THREADS)
	/* Enter the global mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, sUnqlMPGlobal.pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel == UNQLITE_THREAD_LEVEL_SINGLE */
#endif
	 MACRO_LD_PUSH(sUnqlMPGlobal.pDB,pHandle);
	 sUnqlMPGlobal.nDB++;
#if defined(UNQLITE_ENABLE_THREADS)
	/* Leave the global mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods, sUnqlMPGlobal.pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel == UNQLITE_THREAD_LEVEL_SINGLE */
#endif
	/* Set the magic number to identify a valid DB handle */
	 pHandle->nMagic = UNQLITE_DB_MAGIC;
	/* Make the handle available to the caller */
	*ppDB = pHandle;
	return UNQLITE_OK;
Release:
	SyMemBackendRelease(&pHandle->sMem);
	SyMemBackendPoolFree(&sUnqlMPGlobal.sAllocator,pHandle);
	return rc;
}
/*
 * [CAPIREF: unqlite_config()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_config(unqlite *pDb,int nConfigOp,...)
{
	va_list ap;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 va_start(ap, nConfigOp);
	 rc = unqliteConfigure(&(*pDb),nConfigOp, ap);
	 va_end(ap);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_close()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_close(unqlite *pDb)
{
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	/* Release the database handle */
	rc = unqliteDbRelease(pDb);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 /* Release DB mutex */
	 SyMutexRelease(sUnqlMPGlobal.pMutexMethods, pDb->pMutex) /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
#if defined(UNQLITE_ENABLE_THREADS)
	/* Enter the global mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, sUnqlMPGlobal.pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel == UNQLITE_THREAD_LEVEL_SINGLE */
#endif
	/* Unlink from the list of active database handles */
	 MACRO_LD_REMOVE(sUnqlMPGlobal.pDB, pDb);
	sUnqlMPGlobal.nDB--;
#if defined(UNQLITE_ENABLE_THREADS)
	/* Leave the global mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods, sUnqlMPGlobal.pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel == UNQLITE_THREAD_LEVEL_SINGLE */
#endif
	/* Release the memory chunk allocated to this handle */
	SyMemBackendPoolFree(&sUnqlMPGlobal.sAllocator,pDb);
	return rc;
}
/*
 * [CAPIREF: unqlite_compile()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_compile(unqlite *pDb,const char *zJx9,int nByte,unqlite_vm **ppOut)
{
	jx9_vm *pVm;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) || ppOut == 0){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT;
	 }
#endif
	 /* Compile the Jx9 script first */
	 rc = jx9_compile(pDb->sDB.pJx9,zJx9,nByte,&pVm);
	 if( rc == JX9_OK ){
		 /* Allocate a new unqlite VM instance */
		 rc = unqliteInitVm(pDb,pVm,ppOut);
		 if( rc != UNQLITE_OK ){
			 /* Release the Jx9 VM */
			 jx9_vm_release(pVm);
		 }
	 }
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_compile_file()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_compile_file(unqlite *pDb,const char *zPath,unqlite_vm **ppOut)
{
	jx9_vm *pVm;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) || ppOut == 0){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT;
	 }
#endif
	 /* Compile the Jx9 script first */
	rc = jx9_compile_file(pDb->sDB.pJx9,zPath,&pVm);
	if( rc == JX9_OK ){
		/* Allocate a new unqlite VM instance */
		rc = unqliteInitVm(pDb,pVm,ppOut);
		if( rc != UNQLITE_OK ){
			/* Release the Jx9 VM */
			jx9_vm_release(pVm);
		}
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * Configure an unqlite virtual machine (Mostly Jx9 VM) instance.
 */
static int unqliteVmConfig(unqlite_vm *pVm,sxi32 iOp,va_list ap)
{
	int rc;
	rc = jx9VmConfigure(pVm->pJx9Vm,iOp,ap);
	return rc;
}
/*
 * [CAPIREF: unqlite_vm_config()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_vm_config(unqlite_vm *pVm,int iOp,...)
{
	va_list ap;
	int rc;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 va_start(ap,iOp);
	 rc = unqliteVmConfig(pVm,iOp,ap);
	 va_end(ap);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_vm_exec()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_vm_exec(unqlite_vm *pVm)
{
	int rc;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	/* Execute the Jx9 bytecode program */
	 rc = jx9VmByteCodeExec(pVm->pJx9Vm);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_vm_release()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_vm_release(unqlite_vm *pVm)
{
	int rc;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	/* Release the VM */
	 rc = unqliteVmRelease(pVm);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave VM mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 /* Release VM mutex */
	 SyMutexRelease(sUnqlMPGlobal.pMutexMethods,pVm->pMutex) /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	 if( rc == UNQLITE_OK ){
		 unqlite *pDb = pVm->pDb;
		 /* Unlink from the list of active VM's */
#if defined(UNQLITE_ENABLE_THREADS)
			/* Acquire DB mutex */
			SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
			if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
				UNQLITE_THRD_DB_RELEASE(pDb) ){
					return UNQLITE_ABORT; /* Another thread have released this instance */
			}
#endif
		MACRO_LD_REMOVE(pDb->pVms, pVm);
		pDb->iVm--;
		/* Release the memory chunk allocated to this instance */
		SyMemBackendPoolFree(&pDb->sMem,pVm);
#if defined(UNQLITE_ENABLE_THREADS)
			/* Leave DB mutex */
			SyMutexLeave(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	 }
	 return rc;
}
/*
 * [CAPIREF: unqlite_vm_reset()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_vm_reset(unqlite_vm *pVm)
{
	int rc;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	/* Reset the Jx9 VM */
	 rc = jx9VmReset(pVm->pJx9Vm);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_vm_dump()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_vm_dump(unqlite_vm *pVm, int (*xConsumer)(const void *, unsigned int, void *), void *pUserData)
{
	int rc;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	/* Dump the Jx9 VM */
	 rc = jx9VmDump(pVm->pJx9Vm,xConsumer,pUserData);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_vm_extract_variable()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
unqlite_value * unqlite_vm_extract_variable(unqlite_vm *pVm,const char *zVarname)
{
	unqlite_value *pValue;
	SyString sVariable;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return 0;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return 0; /* Another thread have released this instance */
	 }
#endif
	 /* Extract the target variable */
	SyStringInitFromBuf(&sVariable,zVarname,SyStrlen(zVarname));
	pValue = jx9VmExtractVariable(pVm->pJx9Vm,&sVariable);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return pValue;
}
/*
 * [CAPIREF: unqlite_create_function()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_create_function(unqlite_vm *pVm, const char *zName,int (*xFunc)(unqlite_context *,int,unqlite_value **),void *pUserData)
{
	SyString sName;
	int rc;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return UNQLITE_CORRUPT;
	}
	SyStringInitFromBuf(&sName, zName, SyStrlen(zName));
	/* Remove leading and trailing white spaces */
	SyStringFullTrim(&sName);
	/* Ticket 1433-003: NULL values are not allowed */
	if( sName.nByte < 1 || xFunc == 0 ){
		return UNQLITE_INVALID;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Install the foreign function */
	 rc = jx9VmInstallForeignFunction(pVm->pJx9Vm,&sName,xFunc,pUserData);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_delete_function()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_delete_function(unqlite_vm *pVm, const char *zName)
{
	int rc;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Unlink the foreign function */
	 rc = jx9DeleteFunction(pVm->pJx9Vm,zName);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_create_constant()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_create_constant(unqlite_vm *pVm,const char *zName,void (*xExpand)(unqlite_value *, void *),void *pUserData)
{
	SyString sName;
	int rc;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return UNQLITE_CORRUPT;
	}
	SyStringInitFromBuf(&sName, zName, SyStrlen(zName));
	/* Remove leading and trailing white spaces */
	SyStringFullTrim(&sName);
	if( sName.nByte < 1 ){
		/* Empty constant name */
		return UNQLITE_INVALID;
	}
	/* TICKET 1433-003: NULL pointer is harmless operation */
	if( xExpand == 0 ){
		return UNQLITE_INVALID;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Install the foreign constant */
	 rc = jx9VmRegisterConstant(pVm->pJx9Vm,&sName,xExpand,pUserData);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_delete_constant()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_delete_constant(unqlite_vm *pVm, const char *zName)
{
	int rc;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Unlink the foreign constant */
	 rc = Jx9DeleteConstant(pVm->pJx9Vm,zName);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_value_int()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_int(unqlite_value *pVal, int iValue)
{
	return jx9_value_int(pVal,iValue);
}
/*
 * [CAPIREF: unqlite_value_int64()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_int64(unqlite_value *pVal,unqlite_int64 iValue)
{
	return jx9_value_int64(pVal,iValue);
}
/*
 * [CAPIREF: unqlite_value_bool()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_bool(unqlite_value *pVal, int iBool)
{
	return jx9_value_bool(pVal,iBool);
}
/*
 * [CAPIREF: unqlite_value_null()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_null(unqlite_value *pVal)
{
	return jx9_value_null(pVal);
}
/*
 * [CAPIREF: unqlite_value_double()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_double(unqlite_value *pVal, double Value)
{
	return jx9_value_double(pVal,Value);
}
/*
 * [CAPIREF: unqlite_value_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_string(unqlite_value *pVal, const char *zString, int nLen)
{
	return jx9_value_string(pVal,zString,nLen);
}
/*
 * [CAPIREF: unqlite_value_string_format()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_string_format(unqlite_value *pVal, const char *zFormat,...)
{
	va_list ap;
	int rc;
	if((pVal->iFlags & MEMOBJ_STRING) == 0 ){
		/* Invalidate any prior representation */
		jx9MemObjRelease(pVal);
		MemObjSetType(pVal, MEMOBJ_STRING);
	}
	va_start(ap, zFormat);
	rc = SyBlobFormatAp(&pVal->sBlob, zFormat, ap);
	va_end(ap);
	return UNQLITE_OK;
}
/*
 * [CAPIREF: unqlite_value_reset_string_cursor()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_reset_string_cursor(unqlite_value *pVal)
{
	return jx9_value_reset_string_cursor(pVal);
}
/*
 * [CAPIREF: unqlite_value_resource()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_resource(unqlite_value *pVal,void *pUserData)
{
	return jx9_value_resource(pVal,pUserData);
}
/*
 * [CAPIREF: unqlite_value_release()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_release(unqlite_value *pVal)
{
	return jx9_value_release(pVal);
}
/*
 * [CAPIREF: unqlite_value_to_int()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_to_int(unqlite_value *pValue)
{
	return jx9_value_to_int(pValue);
}
/*
 * [CAPIREF: unqlite_value_to_bool()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_to_bool(unqlite_value *pValue)
{
	return jx9_value_to_bool(pValue);
}
/*
 * [CAPIREF: unqlite_value_to_int64()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
unqlite_int64 unqlite_value_to_int64(unqlite_value *pValue)
{
	return jx9_value_to_int64(pValue);
}
/*
 * [CAPIREF: unqlite_value_to_double()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
double unqlite_value_to_double(unqlite_value *pValue)
{
	return jx9_value_to_double(pValue);
}
/*
 * [CAPIREF: unqlite_value_to_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
const char * unqlite_value_to_string(unqlite_value *pValue, int *pLen)
{
	return jx9_value_to_string(pValue,pLen);
}
/*
 * [CAPIREF: unqlite_value_to_resource()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
void * unqlite_value_to_resource(unqlite_value *pValue)
{
	return jx9_value_to_resource(pValue);
}
/*
 * [CAPIREF: unqlite_value_compare()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_compare(unqlite_value *pLeft, unqlite_value *pRight, int bStrict)
{
	return jx9_value_compare(pLeft,pRight,bStrict);
}
/*
 * [CAPIREF: unqlite_result_int()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_result_int(unqlite_context *pCtx, int iValue)
{
	return jx9_result_int(pCtx,iValue);
}
/*
 * [CAPIREF: unqlite_result_int64()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_result_int64(unqlite_context *pCtx, unqlite_int64 iValue)
{
	return jx9_result_int64(pCtx,iValue);
}
/*
 * [CAPIREF: unqlite_result_bool()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_result_bool(unqlite_context *pCtx, int iBool)
{
	return jx9_result_bool(pCtx,iBool);
}
/*
 * [CAPIREF: unqlite_result_double()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_result_double(unqlite_context *pCtx, double Value)
{
	return jx9_result_double(pCtx,Value);
}
/*
 * [CAPIREF: unqlite_result_null()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_result_null(unqlite_context *pCtx)
{
	return jx9_result_null(pCtx);
}
/*
 * [CAPIREF: unqlite_result_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_result_string(unqlite_context *pCtx, const char *zString, int nLen)
{
	return jx9_result_string(pCtx,zString,nLen);
}
/*
 * [CAPIREF: unqlite_result_string_format()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_result_string_format(unqlite_context *pCtx, const char *zFormat, ...)
{
	jx9_value *p;
	va_list ap;
	int rc;
	p = pCtx->pRet;
	if( (p->iFlags & MEMOBJ_STRING) == 0 ){
		/* Invalidate any prior representation */
		jx9MemObjRelease(p);
		MemObjSetType(p, MEMOBJ_STRING);
	}
	/* Format the given string */
	va_start(ap, zFormat);
	rc = SyBlobFormatAp(&p->sBlob, zFormat, ap);
	va_end(ap);
	return rc;
}
/*
 * [CAPIREF: unqlite_result_value()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_result_value(unqlite_context *pCtx, unqlite_value *pValue)
{
	return jx9_result_value(pCtx,pValue);
}
/*
 * [CAPIREF: unqlite_result_resource()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_result_resource(unqlite_context *pCtx, void *pUserData)
{
	return jx9_result_resource(pCtx,pUserData);
}
/*
 * [CAPIREF: unqlite_value_is_int()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_int(unqlite_value *pVal)
{
	return jx9_value_is_int(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_float()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_float(unqlite_value *pVal)
{
	return jx9_value_is_float(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_bool()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_bool(unqlite_value *pVal)
{
	return jx9_value_is_bool(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_string(unqlite_value *pVal)
{
	return jx9_value_is_string(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_null()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_null(unqlite_value *pVal)
{
	return jx9_value_is_null(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_numeric()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_numeric(unqlite_value *pVal)
{
	return jx9_value_is_numeric(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_callable()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_callable(unqlite_value *pVal)
{
	return jx9_value_is_callable(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_scalar()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_scalar(unqlite_value *pVal)
{
	return jx9_value_is_scalar(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_json_array()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_json_array(unqlite_value *pVal)
{
	return jx9_value_is_json_array(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_json_object()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_json_object(unqlite_value *pVal)
{
	return jx9_value_is_json_object(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_resource()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_resource(unqlite_value *pVal)
{
	return jx9_value_is_resource(pVal);
}
/*
 * [CAPIREF: unqlite_value_is_empty()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_value_is_empty(unqlite_value *pVal)
{
	return jx9_value_is_empty(pVal);
}
/*
 * [CAPIREF: unqlite_array_fetch()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
unqlite_value * unqlite_array_fetch(unqlite_value *pArray, const char *zKey, int nByte)
{
	return jx9_array_fetch(pArray,zKey,nByte);
}
/*
 * [CAPIREF: unqlite_array_walk()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_array_walk(unqlite_value *pArray, int (*xWalk)(unqlite_value *, unqlite_value *, void *), void *pUserData)
{
	return jx9_array_walk(pArray,xWalk,pUserData);
}
/*
 * [CAPIREF: unqlite_array_add_elem()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_array_add_elem(unqlite_value *pArray, unqlite_value *pKey, unqlite_value *pValue)
{
	return jx9_array_add_elem(pArray,pKey,pValue);
}
/*
 * [CAPIREF: unqlite_array_add_strkey_elem()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_array_add_strkey_elem(unqlite_value *pArray, const char *zKey, unqlite_value *pValue)
{
	return jx9_array_add_strkey_elem(pArray,zKey,pValue);
}
/*
 * [CAPIREF: unqlite_array_count()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_array_count(unqlite_value *pArray)
{
	return (int)jx9_array_count(pArray);
}
/*
 * [CAPIREF: unqlite_vm_new_scalar()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
unqlite_value * unqlite_vm_new_scalar(unqlite_vm *pVm)
{
	unqlite_value *pValue;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return 0;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return 0; /* Another thread have released this instance */
	 }
#endif
	 pValue = jx9_new_scalar(pVm->pJx9Vm);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return pValue;
}
/*
 * [CAPIREF: unqlite_vm_new_array()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
unqlite_value * unqlite_vm_new_array(unqlite_vm *pVm)
{
	unqlite_value *pValue;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return 0;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return 0; /* Another thread have released this instance */
	 }
#endif
	 pValue = jx9_new_array(pVm->pJx9Vm);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return pValue;
}
/*
 * [CAPIREF: unqlite_vm_release_value()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_vm_release_value(unqlite_vm *pVm,unqlite_value *pValue)
{
	int rc;
	if( UNQLITE_VM_MISUSE(pVm) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_VM_RELEASE(pVm) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 rc = jx9_release_value(pVm->pJx9Vm,pValue);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pVm->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_context_output()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_context_output(unqlite_context *pCtx, const char *zString, int nLen)
{
	return jx9_context_output(pCtx,zString,nLen);
}
/*
 * [CAPIREF: unqlite_context_output_format()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_context_output_format(unqlite_context *pCtx,const char *zFormat, ...)
{
	va_list ap;
	int rc;
	va_start(ap, zFormat);
	rc = jx9VmOutputConsumeAp(pCtx->pVm,zFormat, ap);
	va_end(ap);
	return rc;
}
/*
 * [CAPIREF: unqlite_context_throw_error()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_context_throw_error(unqlite_context *pCtx, int iErr, const char *zErr)
{
	return jx9_context_throw_error(pCtx,iErr,zErr);
}
/*
 * [CAPIREF: unqlite_context_throw_error_format()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_context_throw_error_format(unqlite_context *pCtx, int iErr, const char *zFormat, ...)
{
	va_list ap;
	int rc;
	if( zFormat == 0){
		return JX9_OK;
	}
	va_start(ap, zFormat);
	rc = jx9VmThrowErrorAp(pCtx->pVm, &pCtx->pFunc->sName, iErr, zFormat, ap);
	va_end(ap);
	return rc;
}
/*
 * [CAPIREF: unqlite_context_random_num()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
unsigned int unqlite_context_random_num(unqlite_context *pCtx)
{
	return jx9_context_random_num(pCtx);
}
/*
 * [CAPIREF: unqlite_context_random_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_context_random_string(unqlite_context *pCtx, char *zBuf, int nBuflen)
{
	return jx9_context_random_string(pCtx,zBuf,nBuflen);
}
/*
 * [CAPIREF: unqlite_context_user_data()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
void * unqlite_context_user_data(unqlite_context *pCtx)
{
	return jx9_context_user_data(pCtx);
}
/*
 * [CAPIREF: unqlite_context_push_aux_data()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_context_push_aux_data(unqlite_context *pCtx, void *pUserData)
{
	return jx9_context_push_aux_data(pCtx,pUserData);
}
/*
 * [CAPIREF: unqlite_context_peek_aux_data()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
void * unqlite_context_peek_aux_data(unqlite_context *pCtx)
{
	return jx9_context_peek_aux_data(pCtx);
}
/*
 * [CAPIREF: unqlite_context_pop_aux_data()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
void * unqlite_context_pop_aux_data(unqlite_context *pCtx)
{
	return jx9_context_pop_aux_data(pCtx);
}
/*
 * [CAPIREF: unqlite_context_result_buf_length()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
unsigned int unqlite_context_result_buf_length(unqlite_context *pCtx)
{
	return jx9_context_result_buf_length(pCtx);
}
/*
 * [CAPIREF: unqlite_function_name()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
const char * unqlite_function_name(unqlite_context *pCtx)
{
	return jx9_function_name(pCtx);
}
/*
 * [CAPIREF: unqlite_context_new_scalar()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
unqlite_value * unqlite_context_new_scalar(unqlite_context *pCtx)
{
	return jx9_context_new_scalar(pCtx);
}
/*
 * [CAPIREF: unqlite_context_new_array()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
unqlite_value * unqlite_context_new_array(unqlite_context *pCtx)
{
	return jx9_context_new_array(pCtx);
}
/*
 * [CAPIREF: unqlite_context_release_value()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
void unqlite_context_release_value(unqlite_context *pCtx,unqlite_value *pValue)
{
	jx9_context_release_value(pCtx,pValue);
}
/*
 * [CAPIREF: unqlite_context_alloc_chunk()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
void * unqlite_context_alloc_chunk(unqlite_context *pCtx,unsigned int nByte,int ZeroChunk,int AutoRelease)
{
	return jx9_context_alloc_chunk(pCtx,nByte,ZeroChunk,AutoRelease);
}
/*
 * [CAPIREF: unqlite_context_realloc_chunk()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
void * unqlite_context_realloc_chunk(unqlite_context *pCtx,void *pChunk,unsigned int nByte)
{
	return jx9_context_realloc_chunk(pCtx,pChunk,nByte);
}
/*
 * [CAPIREF: unqlite_context_free_chunk()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
void unqlite_context_free_chunk(unqlite_context *pCtx,void *pChunk)
{
	jx9_context_free_chunk(pCtx,pChunk);
}
/*
 * [CAPIREF: unqlite_kv_store()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_store(unqlite *pDb,const void *pKey,int nKeyLen,const void *pData,unqlite_int64 nDataLen)
{
	unqlite_kv_engine *pEngine;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Point to the underlying storage engine */
	 pEngine = unqlitePagerGetKvEngine(pDb);
	 if( pEngine->pIo->pMethods->xReplace == 0 ){
		 /* Storage engine does not implement such method */
		 unqliteGenError(pDb,"xReplace() method not implemented in the underlying storage engine");
		 rc = UNQLITE_NOTIMPLEMENTED;
	 }else{
		 if( nKeyLen < 0 ){
			 /* Assume a null terminated string and compute it's length */
			 nKeyLen = SyStrlen((const char *)pKey);
		 }
		 if( !nKeyLen ){
			 unqliteGenError(pDb,"Empty key");
			 rc = UNQLITE_EMPTY;
		 }else{
			 /* Perform the requested operation */
			 rc = pEngine->pIo->pMethods->xReplace(pEngine,pKey,nKeyLen,pData,nDataLen);
		 }
	 }
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_store_fmt()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_store_fmt(unqlite *pDb,const void *pKey,int nKeyLen,const char *zFormat,...)
{
	unqlite_kv_engine *pEngine;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Point to the underlying storage engine */
	 pEngine = unqlitePagerGetKvEngine(pDb);
	 if( pEngine->pIo->pMethods->xReplace == 0 ){
		 /* Storage engine does not implement such method */
		 unqliteGenError(pDb,"xReplace() method not implemented in the underlying storage engine");
		 rc = UNQLITE_NOTIMPLEMENTED;
	 }else{
		 if( nKeyLen < 0 ){
			 /* Assume a null terminated string and compute it's length */
			 nKeyLen = SyStrlen((const char *)pKey);
		 }
		 if( !nKeyLen ){
			 unqliteGenError(pDb,"Empty key");
			 rc = UNQLITE_EMPTY;
		 }else{
			 SyBlob sWorker; /* Working buffer */
			 va_list ap;
			 SyBlobInit(&sWorker,&pDb->sMem);
			 /* Format the data */
			 va_start(ap,zFormat);
			 SyBlobFormatAp(&sWorker,zFormat,ap);
			 va_end(ap);
			 /* Perform the requested operation */
			 rc = pEngine->pIo->pMethods->xReplace(pEngine,pKey,nKeyLen,SyBlobData(&sWorker),SyBlobLength(&sWorker));
			 /* Clean up */
			 SyBlobRelease(&sWorker);
		 }
	 }
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_append()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_append(unqlite *pDb,const void *pKey,int nKeyLen,const void *pData,unqlite_int64 nDataLen)
{
	unqlite_kv_engine *pEngine;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Point to the underlying storage engine */
	 pEngine = unqlitePagerGetKvEngine(pDb);
	 if( pEngine->pIo->pMethods->xAppend == 0 ){
		 /* Storage engine does not implement such method */
		 unqliteGenError(pDb,"xAppend() method not implemented in the underlying storage engine");
		 rc = UNQLITE_NOTIMPLEMENTED;
	 }else{
		 if( nKeyLen < 0 ){
			 /* Assume a null terminated string and compute it's length */
			 nKeyLen = SyStrlen((const char *)pKey);
		 }
		 if( !nKeyLen ){
			 unqliteGenError(pDb,"Empty key");
			 rc = UNQLITE_EMPTY;
		 }else{
			 /* Perform the requested operation */
			 rc = pEngine->pIo->pMethods->xAppend(pEngine,pKey,nKeyLen,pData,nDataLen);
		 }
	 }
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_append_fmt()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_append_fmt(unqlite *pDb,const void *pKey,int nKeyLen,const char *zFormat,...)
{
	unqlite_kv_engine *pEngine;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Point to the underlying storage engine */
	 pEngine = unqlitePagerGetKvEngine(pDb);
	 if( pEngine->pIo->pMethods->xAppend == 0 ){
		 /* Storage engine does not implement such method */
		 unqliteGenError(pDb,"xAppend() method not implemented in the underlying storage engine");
		 rc = UNQLITE_NOTIMPLEMENTED;
	 }else{
		 if( nKeyLen < 0 ){
			 /* Assume a null terminated string and compute it's length */
			 nKeyLen = SyStrlen((const char *)pKey);
		 }
		 if( !nKeyLen ){
			 unqliteGenError(pDb,"Empty key");
			 rc = UNQLITE_EMPTY;
		 }else{
			 SyBlob sWorker; /* Working buffer */
			 va_list ap;
			 SyBlobInit(&sWorker,&pDb->sMem);
			 /* Format the data */
			 va_start(ap,zFormat);
			 SyBlobFormatAp(&sWorker,zFormat,ap);
			 va_end(ap);
			 /* Perform the requested operation */
			 rc = pEngine->pIo->pMethods->xAppend(pEngine,pKey,nKeyLen,SyBlobData(&sWorker),SyBlobLength(&sWorker));
			 /* Clean up */
			 SyBlobRelease(&sWorker);
		 }
	 }
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_fetch()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_fetch(unqlite *pDb,const void *pKey,int nKeyLen,void *pBuf,unqlite_int64 *pBufLen)
{
	unqlite_kv_methods *pMethods;
	unqlite_kv_engine *pEngine;
	unqlite_kv_cursor *pCur;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Point to the underlying storage engine */
	 pEngine = unqlitePagerGetKvEngine(pDb);
	 pMethods = pEngine->pIo->pMethods;
	 pCur = pDb->sDB.pCursor;
	 if( nKeyLen < 0 ){
		 /* Assume a null terminated string and compute it's length */
		 nKeyLen = SyStrlen((const char *)pKey);
	 }
	 if( !nKeyLen ){
		  unqliteGenError(pDb,"Empty key");
		  rc = UNQLITE_EMPTY;
	 }else{
		  /* Seek to the record position */
		  rc = pMethods->xSeek(pCur,pKey,nKeyLen,UNQLITE_CURSOR_MATCH_EXACT);
	 }
	 if( rc == UNQLITE_OK ){
		 if( pBuf == 0 ){
			 /* Data length only */
			 rc = pMethods->xDataLength(pCur,pBufLen);
		 }else{
			 SyBlob sBlob;
			 /* Initialize the data consumer */
			 SyBlobInitFromBuf(&sBlob,pBuf,(sxu32)*pBufLen);
			 /* Consume the data */
			 rc = pMethods->xData(pCur,unqliteDataConsumer,&sBlob);
			 /* Data length */
			 *pBufLen = (unqlite_int64)SyBlobLength(&sBlob);
			 /* Cleanup */
			 SyBlobRelease(&sBlob);
		 }
	 }
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_fetch_callback()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_fetch_callback(unqlite *pDb,const void *pKey,int nKeyLen,int (*xConsumer)(const void *,unsigned int,void *),void *pUserData)
{
	unqlite_kv_methods *pMethods;
	unqlite_kv_engine *pEngine;
	unqlite_kv_cursor *pCur;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Point to the underlying storage engine */
	 pEngine = unqlitePagerGetKvEngine(pDb);
	 pMethods = pEngine->pIo->pMethods;
	 pCur = pDb->sDB.pCursor;
	 if( nKeyLen < 0 ){
		 /* Assume a null terminated string and compute it's length */
		 nKeyLen = SyStrlen((const char *)pKey);
	 }
	 if( !nKeyLen ){
		 unqliteGenError(pDb,"Empty key");
		 rc = UNQLITE_EMPTY;
	 }else{
		 /* Seek to the record position */
		 rc = pMethods->xSeek(pCur,pKey,nKeyLen,UNQLITE_CURSOR_MATCH_EXACT);
	 }
	 if( rc == UNQLITE_OK && xConsumer ){
		 /* Consume the data directly */
		 rc = pMethods->xData(pCur,xConsumer,pUserData);	 
	 }
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_delete()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_delete(unqlite *pDb,const void *pKey,int nKeyLen)
{
	unqlite_kv_methods *pMethods;
	unqlite_kv_engine *pEngine;
	unqlite_kv_cursor *pCur;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Point to the underlying storage engine */
	 pEngine = unqlitePagerGetKvEngine(pDb);
	 pMethods = pEngine->pIo->pMethods;
	 pCur = pDb->sDB.pCursor;
	 if( pMethods->xDelete == 0 ){
		 /* Storage engine does not implement such method */
		 unqliteGenError(pDb,"xDelete() method not implemented in the underlying storage engine");
		 rc = UNQLITE_NOTIMPLEMENTED;
	 }else{
		 if( nKeyLen < 0 ){
			 /* Assume a null terminated string and compute it's length */
			 nKeyLen = SyStrlen((const char *)pKey);
		 }
		 if( !nKeyLen ){
			 unqliteGenError(pDb,"Empty key");
			 rc = UNQLITE_EMPTY;
		 }else{
			 /* Seek to the record position */
			 rc = pMethods->xSeek(pCur,pKey,nKeyLen,UNQLITE_CURSOR_MATCH_EXACT);
		 }
		 if( rc == UNQLITE_OK ){
			 /* Exact match found, delete the entry */
			 rc = pMethods->xDelete(pCur);
		 }
	 }
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_config()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_config(unqlite *pDb,int iOp,...)
{
	unqlite_kv_engine *pEngine;
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Point to the underlying storage engine */
	 pEngine = unqlitePagerGetKvEngine(pDb);
	 if( pEngine->pIo->pMethods->xConfig == 0 ){
		 /* Storage engine does not implements such method */
		 unqliteGenError(pDb,"xConfig() method not implemented in the underlying storage engine");
		 rc = UNQLITE_NOTIMPLEMENTED;
	 }else{
		 va_list ap;
		 /* Configure the storage engine */
		 va_start(ap,iOp);
		 rc = pEngine->pIo->pMethods->xConfig(pEngine,iOp,ap);
		 va_end(ap);
	 }
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_init()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_init(unqlite *pDb,unqlite_kv_cursor **ppOut)
{
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) || ppOut == 0 /* Noop */){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Allocate a new cursor */
	 rc = unqliteInitCursor(pDb,ppOut);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	 return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_release()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_release(unqlite *pDb,unqlite_kv_cursor *pCur)
{
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) || pCur == 0 /* Noop */){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Release the cursor */
	 rc = unqliteReleaseCursor(pDb,pCur);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	 return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_first_entry()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_first_entry(unqlite_kv_cursor *pCursor)
{
	int rc;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	/* Check if the requested method is implemented by the underlying storage engine */
	if( pCursor->pStore->pIo->pMethods->xFirst == 0 ){
		rc = UNQLITE_NOTIMPLEMENTED;
	}else{
		/* Seek to the first entry */
		rc = pCursor->pStore->pIo->pMethods->xFirst(pCursor);
	}
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_last_entry()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_last_entry(unqlite_kv_cursor *pCursor)
{
	int rc;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	/* Check if the requested method is implemented by the underlying storage engine */
	if( pCursor->pStore->pIo->pMethods->xLast == 0 ){
		rc = UNQLITE_NOTIMPLEMENTED;
	}else{
		/* Seek to the last entry */
		rc = pCursor->pStore->pIo->pMethods->xLast(pCursor);
	}
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_valid_entry()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_valid_entry(unqlite_kv_cursor *pCursor)
{
	int rc;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	/* Check if the requested method is implemented by the underlying storage engine */
	if( pCursor->pStore->pIo->pMethods->xValid == 0 ){
		rc = UNQLITE_NOTIMPLEMENTED;
	}else{
		rc = pCursor->pStore->pIo->pMethods->xValid(pCursor);
	}
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_next_entry()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_next_entry(unqlite_kv_cursor *pCursor)
{
	int rc;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	/* Check if the requested method is implemented by the underlying storage engine */
	if( pCursor->pStore->pIo->pMethods->xNext == 0 ){
		rc = UNQLITE_NOTIMPLEMENTED;
	}else{
		/* Seek to the next entry */
		rc = pCursor->pStore->pIo->pMethods->xNext(pCursor);
	}
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_prev_entry()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_prev_entry(unqlite_kv_cursor *pCursor)
{
	int rc;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	/* Check if the requested method is implemented by the underlying storage engine */
	if( pCursor->pStore->pIo->pMethods->xPrev == 0 ){
		rc = UNQLITE_NOTIMPLEMENTED;
	}else{
		/* Seek to the previous entry */
		rc = pCursor->pStore->pIo->pMethods->xPrev(pCursor);
	}
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_delete_entry()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_delete_entry(unqlite_kv_cursor *pCursor)
{
	int rc;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	/* Check if the requested method is implemented by the underlying storage engine */
	if( pCursor->pStore->pIo->pMethods->xDelete == 0 ){
		rc = UNQLITE_NOTIMPLEMENTED;
	}else{
		/* Delete the entry */
		rc = pCursor->pStore->pIo->pMethods->xDelete(pCursor);
	}
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_reset()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_reset(unqlite_kv_cursor *pCursor)
{
	int rc = UNQLITE_OK;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	/* Check if the requested method is implemented by the underlying storage engine */
	if( pCursor->pStore->pIo->pMethods->xReset == 0 ){
		rc = UNQLITE_NOTIMPLEMENTED;
	}else{
		/* Reset */
		pCursor->pStore->pIo->pMethods->xReset(pCursor);
	}
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_seek()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_seek(unqlite_kv_cursor *pCursor,const void *pKey,int nKeyLen,int iPos)
{
	int rc = UNQLITE_OK;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	if( nKeyLen < 0 ){
		/* Assume a null terminated string and compute it's length */
		nKeyLen = SyStrlen((const char *)pKey);
	}
	if( !nKeyLen ){
		rc = UNQLITE_EMPTY;
	}else{
		/* Seek to the desired location */
		rc = pCursor->pStore->pIo->pMethods->xSeek(pCursor,pKey,nKeyLen,iPos);
	}
	return rc;
}
/*
 * Default data consumer callback. That is, all retrieved is redirected to this
 * routine which store the output in an internal blob.
 */
UNQLITE_PRIVATE int unqliteDataConsumer(
	const void *pOut,   /* Data to consume */
	unsigned int nLen,  /* Data length */
	void *pUserData     /* User private data */
	)
{
	 sxi32 rc;
	 /* Store the output in an internal BLOB */
	 rc = SyBlobAppend((SyBlob *)pUserData, pOut, nLen);
	 return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_data_callback()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_key_callback(unqlite_kv_cursor *pCursor,int (*xConsumer)(const void *,unsigned int,void *),void *pUserData)
{
	int rc;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	/* Consume the key directly */
	rc = pCursor->pStore->pIo->pMethods->xKey(pCursor,xConsumer,pUserData);
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_key()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_key(unqlite_kv_cursor *pCursor,void *pBuf,int *pnByte)
{
	int rc;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	if( pBuf == 0 ){
		/* Key length only */
		rc = pCursor->pStore->pIo->pMethods->xKeyLength(pCursor,pnByte);
	}else{
		SyBlob sBlob;
		if( (*pnByte) < 0 ){
			return UNQLITE_CORRUPT;
		}
		/* Initialize the data consumer */
		SyBlobInitFromBuf(&sBlob,pBuf,(sxu32)(*pnByte));
		/* Consume the key */
		rc = pCursor->pStore->pIo->pMethods->xKey(pCursor,unqliteDataConsumer,&sBlob);
		 /* Key length */
		*pnByte = SyBlobLength(&sBlob);
		/* Cleanup */
		SyBlobRelease(&sBlob);
	}
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_data_callback()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_data_callback(unqlite_kv_cursor *pCursor,int (*xConsumer)(const void *,unsigned int,void *),void *pUserData)
{
	int rc;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	/* Consume the data directly */
	rc = pCursor->pStore->pIo->pMethods->xData(pCursor,xConsumer,pUserData);
	return rc;
}
/*
 * [CAPIREF: unqlite_kv_cursor_data()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_kv_cursor_data(unqlite_kv_cursor *pCursor,void *pBuf,unqlite_int64 *pnByte)
{
	int rc;
#ifdef UNTRUST
	if( pCursor == 0 ){
		return UNQLITE_CORRUPT;
	}
#endif
	if( pBuf == 0 ){
		/* Data length only */
		rc = pCursor->pStore->pIo->pMethods->xDataLength(pCursor,pnByte);
	}else{
		SyBlob sBlob;
		if( (*pnByte) < 0 ){
			return UNQLITE_CORRUPT;
		}
		/* Initialize the data consumer */
		SyBlobInitFromBuf(&sBlob,pBuf,(sxu32)(*pnByte));
		/* Consume the data */
		rc = pCursor->pStore->pIo->pMethods->xData(pCursor,unqliteDataConsumer,&sBlob);
		/* Data length */
		*pnByte = SyBlobLength(&sBlob);
		/* Cleanup */
		SyBlobRelease(&sBlob);
	}
	return rc;
}
/*
 * [CAPIREF: unqlite_begin()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_begin(unqlite *pDb)
{
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Begin the write transaction */
	 rc = unqlitePagerBegin(pDb->sDB.pPager);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	 return rc;
}
/*
 * [CAPIREF: unqlite_commit()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_commit(unqlite *pDb)
{
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Commit the transaction */
	 rc = unqlitePagerCommit(pDb->sDB.pPager);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	 return rc;
}
/*
 * [CAPIREF: unqlite_rollback()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
int unqlite_rollback(unqlite *pDb)
{
	int rc;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Rollback the transaction */
	 rc = unqlitePagerRollback(pDb->sDB.pPager,TRUE);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	 return rc;
}
/*
 * [CAPIREF: unqlite_util_load_mmaped_file()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
UNQLITE_APIEXPORT int unqlite_util_load_mmaped_file(const char *zFile,void **ppMap,unqlite_int64 *pFileSize)
{
	const jx9_vfs *pVfs;
	int rc;
	if( SX_EMPTY_STR(zFile) || ppMap == 0 || pFileSize == 0){
		/* Sanity check */
		return UNQLITE_CORRUPT;
	}
	*ppMap = 0;
	/* Extract the Jx9 Vfs */
	pVfs = jx9ExportBuiltinVfs();
	/*
	 * Check if the underlying vfs implement the memory map routines
	 * [i.e: mmap() under UNIX/MapViewOfFile() under windows].
	 */
	if( pVfs == 0 || pVfs->xMmap == 0 ){
		rc = UNQLITE_NOTIMPLEMENTED;
	 }else{ 
		 /* Try to get a read-only memory view of the whole file */
		 rc = pVfs->xMmap(zFile,ppMap,pFileSize);
	 }
	return rc;
}
/*
 * [CAPIREF: unqlite_util_release_mmaped_file()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
UNQLITE_APIEXPORT int unqlite_util_release_mmaped_file(void *pMap,unqlite_int64 iFileSize)
{
	const jx9_vfs *pVfs;
	int rc = UNQLITE_OK;
	if( pMap == 0 ){
		return UNQLITE_OK;
	}
	/* Extract the Jx9 Vfs */
	pVfs = jx9ExportBuiltinVfs();
	if( pVfs == 0 || pVfs->xUnmap == 0 ){
		rc = UNQLITE_NOTIMPLEMENTED;
	 }else{ 
		 pVfs->xUnmap(pMap,iFileSize);
	 }
	return rc;
}
/*
 * [CAPIREF: unqlite_util_random_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
UNQLITE_APIEXPORT int unqlite_util_random_string(unqlite *pDb,char *zBuf,unsigned int buf_size)
{
	if( UNQLITE_DB_MISUSE(pDb) ){
		return UNQLITE_CORRUPT;
	}
	if( zBuf == 0 || buf_size < 3 ){
		/* Buffer must be long enough to hold three bytes */
		return UNQLITE_INVALID;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return UNQLITE_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /* Generate the random string */
	 unqlitePagerRandomString(pDb->sDB.pPager,zBuf,buf_size);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	 return UNQLITE_OK;
}
/*
 * [CAPIREF: unqlite_util_random_num()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
UNQLITE_APIEXPORT unsigned int unqlite_util_random_num(unqlite *pDb)
{
	sxu32 iNum;
	if( UNQLITE_DB_MISUSE(pDb) ){
		return 0;
	}
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Acquire DB mutex */
	 SyMutexEnter(sUnqlMPGlobal.pMutexMethods, pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
	 if( sUnqlMPGlobal.nThreadingLevel > UNQLITE_THREAD_LEVEL_SINGLE && 
		 UNQLITE_THRD_DB_RELEASE(pDb) ){
			 return 0; /* Another thread have released this instance */
	 }
#endif
	 /* Generate the random number */
	 iNum = unqlitePagerRandomNum(pDb->sDB.pPager);
#if defined(UNQLITE_ENABLE_THREADS)
	 /* Leave DB mutex */
	 SyMutexLeave(sUnqlMPGlobal.pMutexMethods,pDb->pMutex); /* NO-OP if sUnqlMPGlobal.nThreadingLevel != UNQLITE_THREAD_LEVEL_MULTI */
#endif
	 return iNum;
}
