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
 /* $SymiscID: api.c v1.7 FreeBSD 2012-12-18 06:54 stable <chm@symisc.net> $ */
#ifndef JX9_AMALGAMATION
#include "jx9Int.h"
#endif
/* This file implement the public interfaces presented to host-applications.
 * Routines in other files are for internal use by JX9 and should not be
 * accessed by users of the library.
 */
#define JX9_ENGINE_MAGIC 0xF874BCD7
#define JX9_ENGINE_MISUSE(ENGINE) (ENGINE == 0 || ENGINE->nMagic != JX9_ENGINE_MAGIC)
#define JX9_VM_MISUSE(VM) (VM == 0 || VM->nMagic == JX9_VM_STALE)
/* If another thread have released a working instance, the following macros
 * evaluates to true. These macros are only used when the library
 * is built with threading support enabled which is not the case in
 * the default built.
 */
#define JX9_THRD_ENGINE_RELEASE(ENGINE) (ENGINE->nMagic != JX9_ENGINE_MAGIC)
#define JX9_THRD_VM_RELEASE(VM) (VM->nMagic == JX9_VM_STALE)
/* IMPLEMENTATION: jx9@embedded@symisc 311-12-32 */
/*
 * All global variables are collected in the structure named "sJx9MPGlobal".
 * That way it is clear in the code when we are using static variable because
 * its name start with sJx9MPGlobal.
 */
static struct Jx9Global_Data
{
	SyMemBackend sAllocator;                /* Global low level memory allocator */
#if defined(JX9_ENABLE_THREADS)
	const SyMutexMethods *pMutexMethods;   /* Mutex methods */
	SyMutex *pMutex;                       /* Global mutex */
	sxu32 nThreadingLevel;                 /* Threading level: 0 == Single threaded/1 == Multi-Threaded 
										    * The threading level can be set using the [jx9_lib_config()]
											* interface with a configuration verb set to
											* JX9_LIB_CONFIG_THREAD_LEVEL_SINGLE or 
											* JX9_LIB_CONFIG_THREAD_LEVEL_MULTI
											*/
#endif
	const jx9_vfs *pVfs;                    /* Underlying virtual file system */
	sxi32 nEngine;                          /* Total number of active engines */
	jx9 *pEngines;                          /* List of active engine */
	sxu32 nMagic;                           /* Sanity check against library misuse */
}sJx9MPGlobal = {
	{0, 0, 0, 0, 0, 0, 0, 0, {0}}, 
#if defined(JX9_ENABLE_THREADS)
	0, 
	0, 
	0, 
#endif
	0, 
	0, 
	0, 
	0
};
#define JX9_LIB_MAGIC  0xEA1495BA
#define JX9_LIB_MISUSE (sJx9MPGlobal.nMagic != JX9_LIB_MAGIC)
/*
 * Supported threading level.
 * These options have meaning only when the library is compiled with multi-threading
 * support.That is, the JX9_ENABLE_THREADS compile time directive must be defined
 * when JX9 is built.
 * JX9_THREAD_LEVEL_SINGLE:
 * In this mode, mutexing is disabled and the library can only be used by a single thread.
 * JX9_THREAD_LEVEL_MULTI
 * In this mode, all mutexes including the recursive mutexes on [jx9] objects
 * are enabled so that the application is free to share the same engine
 * between different threads at the same time.
 */
#define JX9_THREAD_LEVEL_SINGLE 1 
#define JX9_THREAD_LEVEL_MULTI  2
/*
 * Configure a running JX9 engine instance.
 * return JX9_OK on success.Any other return
 * value indicates failure.
 * Refer to [jx9_config()].
 */
JX9_PRIVATE sxi32 jx9EngineConfig(jx9 *pEngine, sxi32 nOp, va_list ap)
{
	jx9_conf *pConf = &pEngine->xConf;
	int rc = JX9_OK;
	/* Perform the requested operation */
	switch(nOp){									 
	case JX9_CONFIG_ERR_LOG:{
		/* Extract compile-time error log if any */
		const char **pzPtr = va_arg(ap, const char **);
		int *pLen = va_arg(ap, int *);
		if( pzPtr == 0 ){
			rc = JX9_CORRUPT;
			break;
		}
		/* NULL terminate the error-log buffer */
		SyBlobNullAppend(&pConf->sErrConsumer);
		/* Point to the error-log buffer */
		*pzPtr = (const char *)SyBlobData(&pConf->sErrConsumer);
		if( pLen ){
			if( SyBlobLength(&pConf->sErrConsumer) > 1 /* NULL '\0' terminator */ ){
				*pLen = (int)SyBlobLength(&pConf->sErrConsumer);
			}else{
				*pLen = 0;
			}
		}
		break;
							}
	case JX9_CONFIG_ERR_ABORT:
		/* Reserved for future use */
		break;
	default:
		/* Unknown configuration verb */
		rc = JX9_CORRUPT;
		break;
	} /* Switch() */
	return rc;
}
/*
 * Configure the JX9 library.
 * Return JX9_OK on success. Any other return value indicates failure.
 * Refer to [jx9_lib_config()].
 */
static sxi32 Jx9CoreConfigure(sxi32 nOp, va_list ap)
{
	int rc = JX9_OK;
	switch(nOp){
	    case JX9_LIB_CONFIG_VFS:{
			/* Install a virtual file system */
			const jx9_vfs *pVfs = va_arg(ap, const jx9_vfs *);
			sJx9MPGlobal.pVfs = pVfs;
			break;
								}
		case JX9_LIB_CONFIG_USER_MALLOC: {
			/* Use an alternative low-level memory allocation routines */
			const SyMemMethods *pMethods = va_arg(ap, const SyMemMethods *);
			/* Save the memory failure callback (if available) */
			ProcMemError xMemErr = sJx9MPGlobal.sAllocator.xMemError;
			void *pMemErr = sJx9MPGlobal.sAllocator.pUserData;
			if( pMethods == 0 ){
				/* Use the built-in memory allocation subsystem */
				rc = SyMemBackendInit(&sJx9MPGlobal.sAllocator, xMemErr, pMemErr);
			}else{
				rc = SyMemBackendInitFromOthers(&sJx9MPGlobal.sAllocator, pMethods, xMemErr, pMemErr);
			}
			break;
										  }
		case JX9_LIB_CONFIG_MEM_ERR_CALLBACK: {
			/* Memory failure callback */
			ProcMemError xMemErr = va_arg(ap, ProcMemError);
			void *pUserData = va_arg(ap, void *);
			sJx9MPGlobal.sAllocator.xMemError = xMemErr;
			sJx9MPGlobal.sAllocator.pUserData = pUserData;
			break;
												 }	  
		case JX9_LIB_CONFIG_USER_MUTEX: {
#if defined(JX9_ENABLE_THREADS)
			/* Use an alternative low-level mutex subsystem */
			const SyMutexMethods *pMethods = va_arg(ap, const SyMutexMethods *);
#if defined (UNTRUST)
			if( pMethods == 0 ){
				rc = JX9_CORRUPT;
			}
#endif
			/* Sanity check */
			if( pMethods->xEnter == 0 || pMethods->xLeave == 0 || pMethods->xNew == 0){
				/* At least three criticial callbacks xEnter(), xLeave() and xNew() must be supplied */
				rc = JX9_CORRUPT;
				break;
			}
			if( sJx9MPGlobal.pMutexMethods ){
				/* Overwrite the previous mutex subsystem */
				SyMutexRelease(sJx9MPGlobal.pMutexMethods, sJx9MPGlobal.pMutex);
				if( sJx9MPGlobal.pMutexMethods->xGlobalRelease ){
					sJx9MPGlobal.pMutexMethods->xGlobalRelease();
				}
				sJx9MPGlobal.pMutex = 0;
			}
			/* Initialize and install the new mutex subsystem */
			if( pMethods->xGlobalInit ){
				rc = pMethods->xGlobalInit();
				if ( rc != JX9_OK ){
					break;
				}
			}
			/* Create the global mutex */
			sJx9MPGlobal.pMutex = pMethods->xNew(SXMUTEX_TYPE_FAST);
			if( sJx9MPGlobal.pMutex == 0 ){
				/*
				 * If the supplied mutex subsystem is so sick that we are unable to
				 * create a single mutex, there is no much we can do here.
				 */
				if( pMethods->xGlobalRelease ){
					pMethods->xGlobalRelease();
				}
				rc = JX9_CORRUPT;
				break;
			}
			sJx9MPGlobal.pMutexMethods = pMethods;			
			if( sJx9MPGlobal.nThreadingLevel == 0 ){
				/* Set a default threading level */
				sJx9MPGlobal.nThreadingLevel = JX9_THREAD_LEVEL_MULTI; 
			}
#endif
			break;
										   }
		case JX9_LIB_CONFIG_THREAD_LEVEL_SINGLE:
#if defined(JX9_ENABLE_THREADS)
			/* Single thread mode(Only one thread is allowed to play with the library) */
			sJx9MPGlobal.nThreadingLevel = JX9_THREAD_LEVEL_SINGLE;
#endif
			break;
		case JX9_LIB_CONFIG_THREAD_LEVEL_MULTI:
#if defined(JX9_ENABLE_THREADS)
			/* Multi-threading mode (library is thread safe and JX9 engines and virtual machines
			 * may be shared between multiple threads).
			 */
			sJx9MPGlobal.nThreadingLevel = JX9_THREAD_LEVEL_MULTI;
#endif
			break;
		default:
			/* Unknown configuration option */
			rc = JX9_CORRUPT;
			break;
	}
	return rc;
}
/*
 * [CAPIREF: jx9_lib_config()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_lib_config(int nConfigOp, ...)
{
	va_list ap;
	int rc;
	if( sJx9MPGlobal.nMagic == JX9_LIB_MAGIC ){
		/* Library is already initialized, this operation is forbidden */
		return JX9_LOOKED;
	}
	va_start(ap, nConfigOp);
	rc = Jx9CoreConfigure(nConfigOp, ap);
	va_end(ap);
	return rc;
}
/*
 * Global library initialization
 * Refer to [jx9_lib_init()]
 * This routine must be called to initialize the memory allocation subsystem, the mutex 
 * subsystem prior to doing any serious work with the library.The first thread to call
 * this routine does the initialization process and set the magic number so no body later
 * can re-initialize the library.If subsequent threads call this  routine before the first
 * thread have finished the initialization process, then the subsequent threads must block 
 * until the initialization process is done.
 */
static sxi32 Jx9CoreInitialize(void)
{
	const jx9_vfs *pVfs; /* Built-in vfs */
#if defined(JX9_ENABLE_THREADS)
	const SyMutexMethods *pMutexMethods = 0;
	SyMutex *pMaster = 0;
#endif
	int rc;
	/*
	 * If the library is already initialized, then a call to this routine
	 * is a no-op.
	 */
	if( sJx9MPGlobal.nMagic == JX9_LIB_MAGIC ){
		return JX9_OK; /* Already initialized */
	}
	if( sJx9MPGlobal.pVfs == 0 ){
		/* Point to the built-in vfs */
		pVfs = jx9ExportBuiltinVfs();
		/* Install it */
		jx9_lib_config(JX9_LIB_CONFIG_VFS, pVfs);
	}
#if defined(JX9_ENABLE_THREADS)
	if( sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_SINGLE ){
		pMutexMethods = sJx9MPGlobal.pMutexMethods;
		if( pMutexMethods == 0 ){
			/* Use the built-in mutex subsystem */
			pMutexMethods = SyMutexExportMethods();
			if( pMutexMethods == 0 ){
				return JX9_CORRUPT; /* Can't happen */
			}
			/* Install the mutex subsystem */
			rc = jx9_lib_config(JX9_LIB_CONFIG_USER_MUTEX, pMutexMethods);
			if( rc != JX9_OK ){
				return rc;
			}
		}
		/* Obtain a static mutex so we can initialize the library without calling malloc() */
		pMaster = SyMutexNew(pMutexMethods, SXMUTEX_TYPE_STATIC_1);
		if( pMaster == 0 ){
			return JX9_CORRUPT; /* Can't happen */
		}
	}
	/* Lock the master mutex */
	rc = JX9_OK;
	SyMutexEnter(pMutexMethods, pMaster); /* NO-OP if sJx9MPGlobal.nThreadingLevel == JX9_THREAD_LEVEL_SINGLE */
	if( sJx9MPGlobal.nMagic != JX9_LIB_MAGIC ){
#endif
		if( sJx9MPGlobal.sAllocator.pMethods == 0 ){
			/* Install a memory subsystem */
			rc = jx9_lib_config(JX9_LIB_CONFIG_USER_MALLOC, 0); /* zero mean use the built-in memory backend */
			if( rc != JX9_OK ){
				/* If we are unable to initialize the memory backend, there is no much we can do here.*/
				goto End;
			}
		}
#if defined(JX9_ENABLE_THREADS)
		if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE ){
			/* Protect the memory allocation subsystem */
			rc = SyMemBackendMakeThreadSafe(&sJx9MPGlobal.sAllocator, sJx9MPGlobal.pMutexMethods);
			if( rc != JX9_OK ){
				goto End;
			}
		}
#endif
		/* Our library is initialized, set the magic number */
		sJx9MPGlobal.nMagic = JX9_LIB_MAGIC;
		rc = JX9_OK;
#if defined(JX9_ENABLE_THREADS)
	} /* sJx9MPGlobal.nMagic != JX9_LIB_MAGIC */
#endif
End:
#if defined(JX9_ENABLE_THREADS)
	/* Unlock the master mutex */
	SyMutexLeave(pMutexMethods, pMaster); /* NO-OP if sJx9MPGlobal.nThreadingLevel == JX9_THREAD_LEVEL_SINGLE */
#endif
	return rc;
}
/*
 * Release an active JX9 engine and it's associated active virtual machines.
 */
static sxi32 EngineRelease(jx9 *pEngine)
{
	jx9_vm *pVm, *pNext;
	/* Release all active VM */
	pVm = pEngine->pVms;
	for(;;){
		if( pEngine->iVm < 1 ){
			break;
		}
		pNext = pVm->pNext;
		jx9VmRelease(pVm);
		pVm = pNext;
		pEngine->iVm--;
	}
	/* Set a dummy magic number */
	pEngine->nMagic = 0x7635;
	/* Release the private memory subsystem */
	SyMemBackendRelease(&pEngine->sAllocator); 
	return JX9_OK;
}
/*
 * Release all resources consumed by the library.
 * If JX9 is already shut when this routine is invoked then this
 * routine is a harmless no-op.
 * Note: This call is not thread safe. Refer to [jx9_lib_shutdown()].
 */
static void JX9CoreShutdown(void)
{
	jx9 *pEngine, *pNext;
	/* Release all active engines first */
	pEngine = sJx9MPGlobal.pEngines;
	for(;;){
		if( sJx9MPGlobal.nEngine < 1 ){
			break;
		}
		pNext = pEngine->pNext;
		EngineRelease(pEngine); 
		pEngine = pNext;
		sJx9MPGlobal.nEngine--;
	}
#if defined(JX9_ENABLE_THREADS)
	/* Release the mutex subsystem */
	if( sJx9MPGlobal.pMutexMethods ){
		if( sJx9MPGlobal.pMutex ){
			SyMutexRelease(sJx9MPGlobal.pMutexMethods, sJx9MPGlobal.pMutex);
			sJx9MPGlobal.pMutex = 0;
		}
		if( sJx9MPGlobal.pMutexMethods->xGlobalRelease ){
			sJx9MPGlobal.pMutexMethods->xGlobalRelease();
		}
		sJx9MPGlobal.pMutexMethods = 0;
	}
	sJx9MPGlobal.nThreadingLevel = 0;
#endif
	if( sJx9MPGlobal.sAllocator.pMethods ){
		/* Release the memory backend */
		SyMemBackendRelease(&sJx9MPGlobal.sAllocator);
	}
	sJx9MPGlobal.nMagic = 0x1928;	
}
/*
 * [CAPIREF: jx9_lib_shutdown()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_lib_shutdown(void)
{
	if( sJx9MPGlobal.nMagic != JX9_LIB_MAGIC ){
		/* Already shut */
		return JX9_OK;
	}
	JX9CoreShutdown();
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_lib_signature()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE const char * jx9_lib_signature(void)
{
	return JX9_SIG;
}
/*
 * [CAPIREF: jx9_init()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_init(jx9 **ppEngine)
{
	jx9 *pEngine;
	int rc;
#if defined(UNTRUST)
	if( ppEngine == 0 ){
		return JX9_CORRUPT;
	}
#endif
	*ppEngine = 0;
	/* One-time automatic library initialization */
	rc = Jx9CoreInitialize();
	if( rc != JX9_OK ){
		return rc;
	}
	/* Allocate a new engine */
	pEngine = (jx9 *)SyMemBackendPoolAlloc(&sJx9MPGlobal.sAllocator, sizeof(jx9));
	if( pEngine == 0 ){
		return JX9_NOMEM;
	}
	/* Zero the structure */
	SyZero(pEngine, sizeof(jx9));
	/* Initialize engine fields */
	pEngine->nMagic = JX9_ENGINE_MAGIC;
	rc = SyMemBackendInitFromParent(&pEngine->sAllocator, &sJx9MPGlobal.sAllocator);
	if( rc != JX9_OK ){
		goto Release;
	}
//#if defined(JX9_ENABLE_THREADS)
//	SyMemBackendDisbaleMutexing(&pEngine->sAllocator);
//#endif
	/* Default configuration */
	SyBlobInit(&pEngine->xConf.sErrConsumer, &pEngine->sAllocator);
	/* Install a default compile-time error consumer routine */
	pEngine->xConf.xErr = jx9VmBlobConsumer;
	pEngine->xConf.pErrData = &pEngine->xConf.sErrConsumer;
	/* Built-in vfs */
	pEngine->pVfs = sJx9MPGlobal.pVfs;
#if defined(JX9_ENABLE_THREADS)
	if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE ){
		 /* Associate a recursive mutex with this instance */
		 pEngine->pMutex = SyMutexNew(sJx9MPGlobal.pMutexMethods, SXMUTEX_TYPE_RECURSIVE);
		 if( pEngine->pMutex == 0 ){
			 rc = JX9_NOMEM;
			 goto Release;
		 }
	 }
#endif
	/* Link to the list of active engines */
#if defined(JX9_ENABLE_THREADS)
	/* Enter the global mutex */
	 SyMutexEnter(sJx9MPGlobal.pMutexMethods, sJx9MPGlobal.pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel == JX9_THREAD_LEVEL_SINGLE */
#endif
	MACRO_LD_PUSH(sJx9MPGlobal.pEngines, pEngine);
	sJx9MPGlobal.nEngine++;
#if defined(JX9_ENABLE_THREADS)
	/* Leave the global mutex */
	 SyMutexLeave(sJx9MPGlobal.pMutexMethods, sJx9MPGlobal.pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel == JX9_THREAD_LEVEL_SINGLE */
#endif
	/* Write a pointer to the new instance */
	*ppEngine = pEngine;
	return JX9_OK;
Release:
	SyMemBackendRelease(&pEngine->sAllocator);
	SyMemBackendPoolFree(&sJx9MPGlobal.sAllocator,pEngine);
	return rc;
}
/*
 * [CAPIREF: jx9_release()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_release(jx9 *pEngine)
{
	int rc;
	if( JX9_ENGINE_MISUSE(pEngine) ){
		return JX9_CORRUPT;
	}
#if defined(JX9_ENABLE_THREADS)
	 /* Acquire engine mutex */
	 SyMutexEnter(sJx9MPGlobal.pMutexMethods, pEngine->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
	 if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE && 
		 JX9_THRD_ENGINE_RELEASE(pEngine) ){
			 return JX9_ABORT; /* Another thread have released this instance */
	 }
#endif
	/* Release the engine */
	rc = EngineRelease(&(*pEngine));
#if defined(JX9_ENABLE_THREADS)
	 /* Leave engine mutex */
	 SyMutexLeave(sJx9MPGlobal.pMutexMethods, pEngine->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
	 /* Release engine mutex */
	 SyMutexRelease(sJx9MPGlobal.pMutexMethods, pEngine->pMutex) /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
#endif
#if defined(JX9_ENABLE_THREADS)
	/* Enter the global mutex */
	 SyMutexEnter(sJx9MPGlobal.pMutexMethods, sJx9MPGlobal.pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel == JX9_THREAD_LEVEL_SINGLE */
#endif
	/* Unlink from the list of active engines */
	MACRO_LD_REMOVE(sJx9MPGlobal.pEngines, pEngine);
	sJx9MPGlobal.nEngine--;
#if defined(JX9_ENABLE_THREADS)
	/* Leave the global mutex */
	 SyMutexLeave(sJx9MPGlobal.pMutexMethods, sJx9MPGlobal.pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel == JX9_THREAD_LEVEL_SINGLE */
#endif
	/* Release the memory chunk allocated to this engine */
	SyMemBackendPoolFree(&sJx9MPGlobal.sAllocator, pEngine);
	return rc;
}
/*
 * Compile a raw JX9 script.
 * To execute a JX9 code, it must first be compiled into a bytecode program using this routine.
 * If something goes wrong [i.e: compile-time error], your error log [i.e: error consumer callback]
 * should  display the appropriate error message and this function set ppVm to null and return
 * an error code that is different from JX9_OK. Otherwise when the script is successfully compiled
 * ppVm should hold the JX9 bytecode and it's safe to call [jx9_vm_exec(), jx9_vm_reset(), etc.].
 * This API does not actually evaluate the JX9 code. It merely compile and prepares the JX9 script
 * for evaluation.
 */
static sxi32 ProcessScript(
	jx9 *pEngine,          /* Running JX9 engine */
	jx9_vm **ppVm,         /* OUT: A pointer to the virtual machine */
	SyString *pScript,     /* Raw JX9 script to compile */
	sxi32 iFlags,          /* Compile-time flags */
	const char *zFilePath  /* File path if script come from a file. NULL otherwise */
	)
{
	jx9_vm *pVm;
	int rc;
	/* Allocate a new virtual machine */
	pVm = (jx9_vm *)SyMemBackendPoolAlloc(&pEngine->sAllocator, sizeof(jx9_vm));
	if( pVm == 0 ){
		/* If the supplied memory subsystem is so sick that we are unable to allocate
		 * a tiny chunk of memory, there is no much we can do here. */
		if( ppVm ){
			*ppVm = 0;
		}
		return JX9_NOMEM;
	}
	if( iFlags < 0 ){
		/* Default compile-time flags */
		iFlags = 0;
	}
	/* Initialize the Virtual Machine */
	rc = jx9VmInit(pVm, &(*pEngine));
	if( rc != JX9_OK ){
		SyMemBackendPoolFree(&pEngine->sAllocator, pVm);
		if( ppVm ){
			*ppVm = 0;
		}
		return JX9_VM_ERR;
	}
	if( zFilePath ){
		/* Push processed file path */
		jx9VmPushFilePath(pVm, zFilePath, -1, TRUE, 0);
	}
	/* Reset the error message consumer */
	SyBlobReset(&pEngine->xConf.sErrConsumer);
	/* Compile the script */
	jx9CompileScript(pVm, &(*pScript), iFlags);
	if( pVm->sCodeGen.nErr > 0 || pVm == 0){
		sxu32 nErr = pVm->sCodeGen.nErr;
		/* Compilation error or null ppVm pointer, release this VM */
		SyMemBackendRelease(&pVm->sAllocator);
		SyMemBackendPoolFree(&pEngine->sAllocator, pVm);
		if( ppVm ){
			*ppVm = 0;
		}
		return nErr > 0 ? JX9_COMPILE_ERR : JX9_OK;
	}
	/* Prepare the virtual machine for bytecode execution */
	rc = jx9VmMakeReady(pVm);
	if( rc != JX9_OK ){
		goto Release;
	}
	/* Install local import path which is the current directory */
	jx9_vm_config(pVm, JX9_VM_CONFIG_IMPORT_PATH, "./");
#if defined(JX9_ENABLE_THREADS)
	if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE ){
		 /* Associate a recursive mutex with this instance */
		 pVm->pMutex = SyMutexNew(sJx9MPGlobal.pMutexMethods, SXMUTEX_TYPE_RECURSIVE);
		 if( pVm->pMutex == 0 ){
			 goto Release;
		 }
	 }
#endif
	/* Script successfully compiled, link to the list of active virtual machines */
	MACRO_LD_PUSH(pEngine->pVms, pVm);
	pEngine->iVm++;
	/* Point to the freshly created VM */
	*ppVm = pVm;
	/* Ready to execute JX9 bytecode */
	return JX9_OK;
Release:
	SyMemBackendRelease(&pVm->sAllocator);
	SyMemBackendPoolFree(&pEngine->sAllocator, pVm);
	*ppVm = 0;
	return JX9_VM_ERR;
}
/*
 * [CAPIREF: jx9_compile()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_compile(jx9 *pEngine, const char *zSource, int nLen, jx9_vm **ppOutVm)
{
	SyString sScript;
	int rc;
	if( JX9_ENGINE_MISUSE(pEngine) ){
		return JX9_CORRUPT;
	}
	if( zSource == 0 ){
		/* Empty Jx9 statement ';' */
		zSource = ";";
		nLen = (int)sizeof(char);
	}
	if( nLen < 0 ){
		/* Compute input length automatically */
		nLen = (int)SyStrlen(zSource);
	}
	SyStringInitFromBuf(&sScript, zSource, nLen);
#if defined(JX9_ENABLE_THREADS)
	 /* Acquire engine mutex */
	 SyMutexEnter(sJx9MPGlobal.pMutexMethods, pEngine->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
	 if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE && 
		 JX9_THRD_ENGINE_RELEASE(pEngine) ){
			 return JX9_ABORT; /* Another thread have released this instance */
	 }
#endif
	/* Compile the script */
	rc = ProcessScript(&(*pEngine),ppOutVm,&sScript,0,0);
#if defined(JX9_ENABLE_THREADS)
	 /* Leave engine mutex */
	 SyMutexLeave(sJx9MPGlobal.pMutexMethods, pEngine->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
#endif
	/* Compilation result */
	return rc;
}
/*
 * [CAPIREF: jx9_compile_file()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_compile_file(jx9 *pEngine, const char *zFilePath, jx9_vm **ppOutVm)
{
	const jx9_vfs *pVfs;
	int rc;
	if( ppOutVm ){
		*ppOutVm = 0;
	}
	rc = JX9_OK; /* cc warning */
	if( JX9_ENGINE_MISUSE(pEngine) || SX_EMPTY_STR(zFilePath) ){
		return JX9_CORRUPT;
	}
#if defined(JX9_ENABLE_THREADS)
	 /* Acquire engine mutex */
	 SyMutexEnter(sJx9MPGlobal.pMutexMethods, pEngine->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
	 if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE && 
		 JX9_THRD_ENGINE_RELEASE(pEngine) ){
			 return JX9_ABORT; /* Another thread have released this instance */
	 }
#endif
	 /*
	  * Check if the underlying vfs implement the memory map
	  * [i.e: mmap() under UNIX/MapViewOfFile() under windows] function.
	  */
	 pVfs = pEngine->pVfs;
	 if( pVfs == 0 || pVfs->xMmap == 0 ){
		 /* Memory map routine not implemented */
		 rc = JX9_IO_ERR;
	 }else{
		 void *pMapView = 0; /* cc warning */
		 jx9_int64 nSize = 0; /* cc warning */
		 SyString sScript;
		 /* Try to get a memory view of the whole file */
		 rc = pVfs->xMmap(zFilePath, &pMapView, &nSize);
		 if( rc != JX9_OK ){
			 /* Assume an IO error */
			 rc = JX9_IO_ERR;
		 }else{
			 /* Compile the file */
			 SyStringInitFromBuf(&sScript, pMapView, nSize);
			 rc = ProcessScript(&(*pEngine), ppOutVm, &sScript,0,zFilePath);
			 /* Release the memory view of the whole file */
			 if( pVfs->xUnmap ){
				 pVfs->xUnmap(pMapView, nSize);
			 }
		 }
	 }
#if defined(JX9_ENABLE_THREADS)
	 /* Leave engine mutex */
	 SyMutexLeave(sJx9MPGlobal.pMutexMethods, pEngine->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
#endif
	/* Compilation result */
	return rc;
}
/*
 * [CAPIREF: jx9_vm_config()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_vm_config(jx9_vm *pVm, int iConfigOp, ...)
{
	va_list ap;
	int rc;
	/* Ticket 1433-002: NULL VM is harmless operation */
	if ( JX9_VM_MISUSE(pVm) ){
		return JX9_CORRUPT;
	}
#if defined(JX9_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sJx9MPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
	 if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE && 
		 JX9_THRD_VM_RELEASE(pVm) ){
			 return JX9_ABORT; /* Another thread have released this instance */
	 }
#endif
	/* Confiugure the virtual machine */
	va_start(ap, iConfigOp);
	rc = jx9VmConfigure(&(*pVm), iConfigOp, ap);
	va_end(ap);
#if defined(JX9_ENABLE_THREADS)
	 /* Leave VM mutex */
	 SyMutexLeave(sJx9MPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
/*
 * [CAPIREF: jx9_vm_release()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_vm_release(jx9_vm *pVm)
{
	jx9 *pEngine;
	int rc;
	/* Ticket 1433-002: NULL VM is harmless operation */
	if ( JX9_VM_MISUSE(pVm) ){
		return JX9_CORRUPT;
	}
#if defined(JX9_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sJx9MPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
	 if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE && 
		 JX9_THRD_VM_RELEASE(pVm) ){
			 return JX9_ABORT; /* Another thread have released this instance */
	 }
#endif
	pEngine = pVm->pEngine;
	rc = jx9VmRelease(&(*pVm));
#if defined(JX9_ENABLE_THREADS)
	 /* Leave VM mutex */
	 SyMutexLeave(sJx9MPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
	 /* Release VM mutex */
	 SyMutexRelease(sJx9MPGlobal.pMutexMethods, pVm->pMutex) /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
#endif
	if( rc == JX9_OK ){
		/* Unlink from the list of active VM */
#if defined(JX9_ENABLE_THREADS)
			/* Acquire engine mutex */
			SyMutexEnter(sJx9MPGlobal.pMutexMethods, pEngine->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
			if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE && 
				JX9_THRD_ENGINE_RELEASE(pEngine) ){
					return JX9_ABORT; /* Another thread have released this instance */
			}
#endif
		MACRO_LD_REMOVE(pEngine->pVms, pVm);
		pEngine->iVm--;
		/* Release the memory chunk allocated to this VM */
		SyMemBackendPoolFree(&pEngine->sAllocator, pVm);
#if defined(JX9_ENABLE_THREADS)
			/* Leave engine mutex */
			SyMutexLeave(sJx9MPGlobal.pMutexMethods, pEngine->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
#endif	
	}
	return rc;
}
/*
 * [CAPIREF: jx9_create_function()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_create_function(jx9_vm *pVm, const char *zName, int (*xFunc)(jx9_context *, int, jx9_value **), void *pUserData)
{
	SyString sName;
	int rc;
	/* Ticket 1433-002: NULL VM is harmless operation */
	if ( JX9_VM_MISUSE(pVm) ){
		return JX9_CORRUPT;
	}
	SyStringInitFromBuf(&sName, zName, SyStrlen(zName));
	/* Remove leading and trailing white spaces */
	SyStringFullTrim(&sName);
	/* Ticket 1433-003: NULL values are not allowed */
	if( sName.nByte < 1 || xFunc == 0 ){
		return JX9_CORRUPT;
	}
#if defined(JX9_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sJx9MPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
	 if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE && 
		 JX9_THRD_VM_RELEASE(pVm) ){
			 return JX9_ABORT; /* Another thread have released this instance */
	 }
#endif
	/* Install the foreign function */
	rc = jx9VmInstallForeignFunction(&(*pVm), &sName, xFunc, pUserData); 
#if defined(JX9_ENABLE_THREADS)
	 /* Leave VM mutex */
	 SyMutexLeave(sJx9MPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
#endif
	return rc;
}
JX9_PRIVATE int jx9DeleteFunction(jx9_vm *pVm,const char *zName)
{
	jx9_user_func *pFunc = 0; /* cc warning */
	int rc;
	/* Perform the deletion */
	rc = SyHashDeleteEntry(&pVm->hHostFunction, (const void *)zName, SyStrlen(zName), (void **)&pFunc);
	if( rc == JX9_OK ){
		/* Release internal fields */
		SySetRelease(&pFunc->aAux);
		SyMemBackendFree(&pVm->sAllocator, (void *)SyStringData(&pFunc->sName));
		SyMemBackendPoolFree(&pVm->sAllocator, pFunc);
	}
	return rc;
}
/*
 * [CAPIREF: jx9_create_constant()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_create_constant(jx9_vm *pVm, const char *zName, void (*xExpand)(jx9_value *, void *), void *pUserData)
{
	SyString sName;
	int rc;
	/* Ticket 1433-002: NULL VM is harmless operation */
	if ( JX9_VM_MISUSE(pVm) ){
		return JX9_CORRUPT;
	}
	SyStringInitFromBuf(&sName, zName, SyStrlen(zName));
	/* Remove leading and trailing white spaces */
	SyStringFullTrim(&sName);
	if( sName.nByte < 1 ){
		/* Empty constant name */
		return JX9_CORRUPT;
	}
	/* TICKET 1433-003: NULL pointer is harmless operation */
	if( xExpand == 0 ){
		return JX9_CORRUPT;
	}
#if defined(JX9_ENABLE_THREADS)
	 /* Acquire VM mutex */
	 SyMutexEnter(sJx9MPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
	 if( sJx9MPGlobal.nThreadingLevel > JX9_THREAD_LEVEL_SINGLE && 
		 JX9_THRD_VM_RELEASE(pVm) ){
			 return JX9_ABORT; /* Another thread have released this instance */
	 }
#endif
	/* Perform the registration */
	rc = jx9VmRegisterConstant(&(*pVm), &sName, xExpand, pUserData);
#if defined(JX9_ENABLE_THREADS)
	 /* Leave VM mutex */
	 SyMutexLeave(sJx9MPGlobal.pMutexMethods, pVm->pMutex); /* NO-OP if sJx9MPGlobal.nThreadingLevel != JX9_THREAD_LEVEL_MULTI */
#endif
	 return rc;
}
JX9_PRIVATE int Jx9DeleteConstant(jx9_vm *pVm,const char *zName)
{
	jx9_constant *pCons;
	int rc;
	/* Query the constant hashtable */
	 rc = SyHashDeleteEntry(&pVm->hConstant, (const void *)zName, SyStrlen(zName), (void **)&pCons);
	 if( rc == JX9_OK ){
		 /* Perform the deletion */
		 SyMemBackendFree(&pVm->sAllocator, (void *)SyStringData(&pCons->sName));
		 SyMemBackendPoolFree(&pVm->sAllocator, pCons);
	 }
	 return rc;
}
/*
 * [CAPIREF: jx9_new_scalar()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE jx9_value * jx9_new_scalar(jx9_vm *pVm)
{
	jx9_value *pObj;
	/* Ticket 1433-002: NULL VM is harmless operation */
	if ( JX9_VM_MISUSE(pVm) ){
		return 0;
	}
	/* Allocate a new scalar variable */
	pObj = (jx9_value *)SyMemBackendPoolAlloc(&pVm->sAllocator, sizeof(jx9_value));
	if( pObj == 0 ){
		return 0;
	}
	/* Nullify the new scalar */
	jx9MemObjInit(pVm, pObj);
	return pObj;
}
/*
 * [CAPIREF: jx9_new_array()] 
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE jx9_value * jx9_new_array(jx9_vm *pVm)
{
	jx9_hashmap *pMap;
	jx9_value *pObj;
	/* Ticket 1433-002: NULL VM is harmless operation */
	if ( JX9_VM_MISUSE(pVm) ){
		return 0;
	}
	/* Create a new hashmap first */
	pMap = jx9NewHashmap(&(*pVm), 0, 0);
	if( pMap == 0 ){
		return 0;
	}
	/* Associate a new jx9_value with this hashmap */
	pObj = (jx9_value *)SyMemBackendPoolAlloc(&pVm->sAllocator, sizeof(jx9_value));
	if( pObj == 0 ){
		jx9HashmapRelease(pMap, TRUE);
		return 0;
	}
	jx9MemObjInitFromArray(pVm, pObj, pMap);
	return pObj;
}
/*
 * [CAPIREF: jx9_release_value()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_release_value(jx9_vm *pVm, jx9_value *pValue)
{
	/* Ticket 1433-002: NULL VM is a harmless operation */
	if ( JX9_VM_MISUSE(pVm) ){
		return JX9_CORRUPT;
	}
	if( pValue ){
		/* Release the value */
		jx9MemObjRelease(pValue);
		SyMemBackendPoolFree(&pVm->sAllocator, pValue);
	}
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_to_int()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_to_int(jx9_value *pValue)
{
	int rc;
	rc = jx9MemObjToInteger(pValue);
	if( rc != JX9_OK ){
		return 0;
	}
	return (int)pValue->x.iVal;
}
/*
 * [CAPIREF: jx9_value_to_bool()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_to_bool(jx9_value *pValue)
{
	int rc;
	rc = jx9MemObjToBool(pValue);
	if( rc != JX9_OK ){
		return 0;
	}
	return (int)pValue->x.iVal;
}
/*
 * [CAPIREF: jx9_value_to_int64()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE jx9_int64 jx9_value_to_int64(jx9_value *pValue)
{
	int rc;
	rc = jx9MemObjToInteger(pValue);
	if( rc != JX9_OK ){
		return 0;
	}
	return pValue->x.iVal;
}
/*
 * [CAPIREF: jx9_value_to_double()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE double jx9_value_to_double(jx9_value *pValue)
{
	int rc;
	rc = jx9MemObjToReal(pValue);
	if( rc != JX9_OK ){
		return (double)0;
	}
	return (double)pValue->x.rVal;
}
/*
 * [CAPIREF: jx9_value_to_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE const char * jx9_value_to_string(jx9_value *pValue, int *pLen)
{
	jx9MemObjToString(pValue);
	if( SyBlobLength(&pValue->sBlob) > 0 ){
		SyBlobNullAppend(&pValue->sBlob);
		if( pLen ){
			*pLen = (int)SyBlobLength(&pValue->sBlob);
		}
		return (const char *)SyBlobData(&pValue->sBlob);
	}else{
		/* Return the empty string */
		if( pLen ){
			*pLen = 0;
		}
		return "";
	}
}
/*
 * [CAPIREF: jx9_value_to_resource()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE void * jx9_value_to_resource(jx9_value *pValue)
{
	if( (pValue->iFlags & MEMOBJ_RES) == 0 ){
		/* Not a resource, return NULL */
		return 0;
	}
	return pValue->x.pOther;
}
/*
 * [CAPIREF: jx9_value_compare()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_compare(jx9_value *pLeft, jx9_value *pRight, int bStrict)
{
	int rc;
	if( pLeft == 0 || pRight == 0 ){
		/* TICKET 1433-24: NULL values is harmless operation */
		return 1;
	}
	/* Perform the comparison */
	rc = jx9MemObjCmp(&(*pLeft), &(*pRight), bStrict, 0);
	/* Comparison result */
	return rc;
}
/*
 * [CAPIREF: jx9_result_int()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_result_int(jx9_context *pCtx, int iValue)
{
	return jx9_value_int(pCtx->pRet, iValue);
}
/*
 * [CAPIREF: jx9_result_int64()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_result_int64(jx9_context *pCtx, jx9_int64 iValue)
{
	return jx9_value_int64(pCtx->pRet, iValue);
}
/*
 * [CAPIREF: jx9_result_bool()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_result_bool(jx9_context *pCtx, int iBool)
{
	return jx9_value_bool(pCtx->pRet, iBool);
}
/*
 * [CAPIREF: jx9_result_double()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_result_double(jx9_context *pCtx, double Value)
{
	return jx9_value_double(pCtx->pRet, Value);
}
/*
 * [CAPIREF: jx9_result_null()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_result_null(jx9_context *pCtx)
{
	/* Invalidate any prior representation and set the NULL flag */
	jx9MemObjRelease(pCtx->pRet);
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_result_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_result_string(jx9_context *pCtx, const char *zString, int nLen)
{
	return jx9_value_string(pCtx->pRet, zString, nLen);
}
/*
 * [CAPIREF: jx9_result_string_format()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_result_string_format(jx9_context *pCtx, const char *zFormat, ...)
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
 * [CAPIREF: jx9_result_value()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_result_value(jx9_context *pCtx, jx9_value *pValue)
{
	int rc = JX9_OK;
	if( pValue == 0 ){
		jx9MemObjRelease(pCtx->pRet);
	}else{
		rc = jx9MemObjStore(pValue, pCtx->pRet);
	}
	return rc;
}
/*
 * [CAPIREF: jx9_result_resource()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_result_resource(jx9_context *pCtx, void *pUserData)
{
	return jx9_value_resource(pCtx->pRet, pUserData);
}
/*
 * [CAPIREF: jx9_context_new_scalar()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE jx9_value * jx9_context_new_scalar(jx9_context *pCtx)
{
	jx9_value *pVal;
	pVal = jx9_new_scalar(pCtx->pVm);
	if( pVal ){
		/* Record value address so it can be freed automatically
		 * when the calling function returns. 
		 */
		SySetPut(&pCtx->sVar, (const void *)&pVal);
	}
	return pVal;
}
/*
 * [CAPIREF: jx9_context_new_array()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE jx9_value * jx9_context_new_array(jx9_context *pCtx)
{
	jx9_value *pVal;
	pVal = jx9_new_array(pCtx->pVm);
	if( pVal ){
		/* Record value address so it can be freed automatically
		 * when the calling function returns. 
		 */
		SySetPut(&pCtx->sVar, (const void *)&pVal);
	}
	return pVal;
}
/*
 * [CAPIREF: jx9_context_release_value()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE void jx9_context_release_value(jx9_context *pCtx, jx9_value *pValue)
{
	jx9VmReleaseContextValue(&(*pCtx), pValue);
}
/*
 * [CAPIREF: jx9_context_alloc_chunk()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE void * jx9_context_alloc_chunk(jx9_context *pCtx, unsigned int nByte, int ZeroChunk, int AutoRelease)
{
	void *pChunk;
	pChunk = SyMemBackendAlloc(&pCtx->pVm->sAllocator, nByte);
	if( pChunk ){
		if( ZeroChunk ){
			/* Zero the memory chunk */
			SyZero(pChunk, nByte);
		}
		if( AutoRelease ){
			jx9_aux_data sAux;
			/* Track the chunk so that it can be released automatically 
			 * upon this context is destroyed.
			 */
			sAux.pAuxData = pChunk;
			SySetPut(&pCtx->sChunk, (const void *)&sAux);
		}
	}
	return pChunk;
}
/*
 * Check if the given chunk address is registered in the call context
 * chunk container.
 * Return TRUE if registered.FALSE otherwise.
 * Refer to [jx9_context_realloc_chunk(), jx9_context_free_chunk()].
 */
static jx9_aux_data * ContextFindChunk(jx9_context *pCtx, void *pChunk)
{
	jx9_aux_data *aAux, *pAux;
	sxu32 n;
	if( SySetUsed(&pCtx->sChunk) < 1 ){
		/* Don't bother processing, the container is empty */
		return 0;
	}
	/* Perform the lookup */
	aAux = (jx9_aux_data *)SySetBasePtr(&pCtx->sChunk);
	for( n = 0; n < SySetUsed(&pCtx->sChunk) ; ++n ){
		pAux = &aAux[n];
		if( pAux->pAuxData == pChunk ){
			/* Chunk found */
			return pAux;
		}
	}
	/* No such allocated chunk */
	return 0;
}
/*
 * [CAPIREF: jx9_context_realloc_chunk()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE void * jx9_context_realloc_chunk(jx9_context *pCtx, void *pChunk, unsigned int nByte)
{
	jx9_aux_data *pAux;
	void *pNew;
	pNew = SyMemBackendRealloc(&pCtx->pVm->sAllocator, pChunk, nByte);
	if( pNew ){
		pAux = ContextFindChunk(pCtx, pChunk);
		if( pAux ){
			pAux->pAuxData = pNew;
		}
	}
	return pNew;
}
/*
 * [CAPIREF: jx9_context_free_chunk()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE void jx9_context_free_chunk(jx9_context *pCtx, void *pChunk)
{
	jx9_aux_data *pAux;
	if( pChunk == 0 ){
		/* TICKET-1433-93: NULL chunk is a harmless operation */
		return;
	}
	pAux = ContextFindChunk(pCtx, pChunk);
	if( pAux ){
		/* Mark as destroyed */
		pAux->pAuxData = 0;
	}
	SyMemBackendFree(&pCtx->pVm->sAllocator, pChunk);
}
/*
 * [CAPIREF: jx9_array_fetch()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE jx9_value * jx9_array_fetch(jx9_value *pArray, const char *zKey, int nByte)
{
	jx9_hashmap_node *pNode;
	jx9_value *pValue;
	jx9_value skey;
	int rc;
	/* Make sure we are dealing with a valid hashmap */
	if( (pArray->iFlags & MEMOBJ_HASHMAP) == 0 ){
		return 0;
	}
	if( nByte < 0 ){
		nByte = (int)SyStrlen(zKey);
	}
	/* Convert the key to a jx9_value  */
	jx9MemObjInit(pArray->pVm, &skey);
	jx9MemObjStringAppend(&skey, zKey, (sxu32)nByte);
	/* Perform the lookup */
	rc = jx9HashmapLookup((jx9_hashmap *)pArray->x.pOther, &skey, &pNode);
	jx9MemObjRelease(&skey);
	if( rc != JX9_OK ){
		/* No such entry */
		return 0;
	}
	/* Extract the target value */
	pValue = (jx9_value *)SySetAt(&pArray->pVm->aMemObj, pNode->nValIdx);
	return pValue;
}
/*
 * [CAPIREF: jx9_array_walk()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_array_walk(jx9_value *pArray, int (*xWalk)(jx9_value *pValue, jx9_value *, void *), void *pUserData)
{
	int rc;
	if( xWalk == 0 ){
		return JX9_CORRUPT;
	}
	/* Make sure we are dealing with a valid hashmap */
	if( (pArray->iFlags & MEMOBJ_HASHMAP) == 0 ){
		return JX9_CORRUPT;
	}
	/* Start the walk process */
	rc = jx9HashmapWalk((jx9_hashmap *)pArray->x.pOther, xWalk, pUserData);
	return rc != JX9_OK ? JX9_ABORT /* User callback request an operation abort*/ : JX9_OK;
}
/*
 * [CAPIREF: jx9_array_add_elem()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_array_add_elem(jx9_value *pArray, jx9_value *pKey, jx9_value *pValue)
{
	int rc;
	/* Make sure we are dealing with a valid hashmap */
	if( (pArray->iFlags & MEMOBJ_HASHMAP) == 0 ){
		return JX9_CORRUPT;
	}
	/* Perform the insertion */
	rc = jx9HashmapInsert((jx9_hashmap *)pArray->x.pOther, &(*pKey), &(*pValue));
	return rc;
}
/*
 * [CAPIREF: jx9_array_add_strkey_elem()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_array_add_strkey_elem(jx9_value *pArray, const char *zKey, jx9_value *pValue)
{
	int rc;
	/* Make sure we are dealing with a valid hashmap */
	if( (pArray->iFlags & MEMOBJ_HASHMAP) == 0 ){
		return JX9_CORRUPT;
	}
	/* Perform the insertion */
	if( SX_EMPTY_STR(zKey) ){
		/* Empty key, assign an automatic index */
		rc = jx9HashmapInsert((jx9_hashmap *)pArray->x.pOther, 0, &(*pValue));
	}else{
		jx9_value sKey;
		jx9MemObjInitFromString(pArray->pVm, &sKey, 0);
		jx9MemObjStringAppend(&sKey, zKey, (sxu32)SyStrlen(zKey));
		rc = jx9HashmapInsert((jx9_hashmap *)pArray->x.pOther, &sKey, &(*pValue));
		jx9MemObjRelease(&sKey);
	}
	return rc;
}
/*
 * [CAPIREF: jx9_array_count()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE unsigned int jx9_array_count(jx9_value *pArray)
{
	jx9_hashmap *pMap;
	/* Make sure we are dealing with a valid hashmap */
	if( (pArray->iFlags & MEMOBJ_HASHMAP) == 0 ){
		return 0;
	}
	/* Point to the internal representation of the hashmap */
	pMap = (jx9_hashmap *)pArray->x.pOther;
	return pMap->nEntry;
}
/*
 * [CAPIREF: jx9_context_output()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_context_output(jx9_context *pCtx, const char *zString, int nLen)
{
	SyString sData;
	int rc;
	if( nLen < 0 ){
		nLen = (int)SyStrlen(zString);
	}
	SyStringInitFromBuf(&sData, zString, nLen);
	rc = jx9VmOutputConsume(pCtx->pVm, &sData);
	return rc;
}
/*
 * [CAPIREF: jx9_context_throw_error()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_context_throw_error(jx9_context *pCtx, int iErr, const char *zErr)
{
	int rc = JX9_OK;
	if( zErr ){
		rc = jx9VmThrowError(pCtx->pVm, &pCtx->pFunc->sName, iErr, zErr);
	}
	return rc;
}
/*
 * [CAPIREF: jx9_context_throw_error_format()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_context_throw_error_format(jx9_context *pCtx, int iErr, const char *zFormat, ...)
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
 * [CAPIREF: jx9_context_random_num()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE unsigned int jx9_context_random_num(jx9_context *pCtx)
{
	sxu32 n;
	n = jx9VmRandomNum(pCtx->pVm);
	return n;
}
/*
 * [CAPIREF: jx9_context_random_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_context_random_string(jx9_context *pCtx, char *zBuf, int nBuflen)
{
	if( nBuflen < 3 ){
		return JX9_CORRUPT;
	}
	jx9VmRandomString(pCtx->pVm, zBuf, nBuflen);
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_context_user_data()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE void * jx9_context_user_data(jx9_context *pCtx)
{
	return pCtx->pFunc->pUserData;
}
/*
 * [CAPIREF: jx9_context_push_aux_data()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_context_push_aux_data(jx9_context *pCtx, void *pUserData)
{
	jx9_aux_data sAux;
	int rc;
	sAux.pAuxData = pUserData;
	rc = SySetPut(&pCtx->pFunc->aAux, (const void *)&sAux);
	return rc;
}
/*
 * [CAPIREF: jx9_context_peek_aux_data()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE void * jx9_context_peek_aux_data(jx9_context *pCtx)
{
	jx9_aux_data *pAux;
	pAux = (jx9_aux_data *)SySetPeek(&pCtx->pFunc->aAux);
	return pAux ? pAux->pAuxData : 0;
}
/*
 * [CAPIREF: jx9_context_pop_aux_data()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE void * jx9_context_pop_aux_data(jx9_context *pCtx)
{
	jx9_aux_data *pAux;
	pAux = (jx9_aux_data *)SySetPop(&pCtx->pFunc->aAux);
	return pAux ? pAux->pAuxData : 0;
}
/*
 * [CAPIREF: jx9_context_result_buf_length()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE unsigned int jx9_context_result_buf_length(jx9_context *pCtx)
{
	return SyBlobLength(&pCtx->pRet->sBlob);
}
/*
 * [CAPIREF: jx9_function_name()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE const char * jx9_function_name(jx9_context *pCtx)
{
	SyString *pName;
	pName = &pCtx->pFunc->sName;
	return pName->zString;
}
/*
 * [CAPIREF: jx9_value_int()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_int(jx9_value *pVal, int iValue)
{
	/* Invalidate any prior representation */
	jx9MemObjRelease(pVal);
	pVal->x.iVal = (jx9_int64)iValue;
	MemObjSetType(pVal, MEMOBJ_INT);
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_int64()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_int64(jx9_value *pVal, jx9_int64 iValue)
{
	/* Invalidate any prior representation */
	jx9MemObjRelease(pVal);
	pVal->x.iVal = iValue;
	MemObjSetType(pVal, MEMOBJ_INT);
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_bool()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_bool(jx9_value *pVal, int iBool)
{
	/* Invalidate any prior representation */
	jx9MemObjRelease(pVal);
	pVal->x.iVal = iBool ? 1 : 0;
	MemObjSetType(pVal, MEMOBJ_BOOL);
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_null()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_null(jx9_value *pVal)
{
	/* Invalidate any prior representation and set the NULL flag */
	jx9MemObjRelease(pVal);
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_double()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_double(jx9_value *pVal, double Value)
{
	/* Invalidate any prior representation */
	jx9MemObjRelease(pVal);
	pVal->x.rVal = (jx9_real)Value;
	MemObjSetType(pVal, MEMOBJ_REAL);
	/* Try to get an integer representation also */
	jx9MemObjTryInteger(pVal);
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_string(jx9_value *pVal, const char *zString, int nLen)
{
	if((pVal->iFlags & MEMOBJ_STRING) == 0 ){
		/* Invalidate any prior representation */
		jx9MemObjRelease(pVal);
		MemObjSetType(pVal, MEMOBJ_STRING);
	}
	if( zString ){
		if( nLen < 0 ){
			/* Compute length automatically */
			nLen = (int)SyStrlen(zString);
		}
		SyBlobAppend(&pVal->sBlob, (const void *)zString, (sxu32)nLen);
	}
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_string_format()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_string_format(jx9_value *pVal, const char *zFormat, ...)
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
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_reset_string_cursor()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_reset_string_cursor(jx9_value *pVal)
{
	/* Reset the string cursor */
	SyBlobReset(&pVal->sBlob);
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_resource()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_resource(jx9_value *pVal, void *pUserData)
{
	/* Invalidate any prior representation */
	jx9MemObjRelease(pVal);
	/* Reflect the new type */
	pVal->x.pOther = pUserData;
	MemObjSetType(pVal, MEMOBJ_RES);
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_release()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_release(jx9_value *pVal)
{
	jx9MemObjRelease(pVal);
	return JX9_OK;
}
/*
 * [CAPIREF: jx9_value_is_int()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_int(jx9_value *pVal)
{
	return (pVal->iFlags & MEMOBJ_INT) ? TRUE : FALSE;
}
/*
 * [CAPIREF: jx9_value_is_float()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_float(jx9_value *pVal)
{
	return (pVal->iFlags & MEMOBJ_REAL) ? TRUE : FALSE;
}
/*
 * [CAPIREF: jx9_value_is_bool()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_bool(jx9_value *pVal)
{
	return (pVal->iFlags & MEMOBJ_BOOL) ? TRUE : FALSE;
}
/*
 * [CAPIREF: jx9_value_is_string()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_string(jx9_value *pVal)
{
	return (pVal->iFlags & MEMOBJ_STRING) ? TRUE : FALSE;
}
/*
 * [CAPIREF: jx9_value_is_null()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_null(jx9_value *pVal)
{
	return (pVal->iFlags & MEMOBJ_NULL) ? TRUE : FALSE;
}
/*
 * [CAPIREF: jx9_value_is_numeric()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_numeric(jx9_value *pVal)
{
	int rc;
	rc = jx9MemObjIsNumeric(pVal);
	return rc;
}
/*
 * [CAPIREF: jx9_value_is_callable()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_callable(jx9_value *pVal)
{
	int rc;
	rc = jx9VmIsCallable(pVal->pVm, pVal);
	return rc;
}
/*
 * [CAPIREF: jx9_value_is_scalar()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_scalar(jx9_value *pVal)
{
	return (pVal->iFlags & MEMOBJ_SCALAR) ? TRUE : FALSE;
}
/*
 * [CAPIREF: jx9_value_is_json_array()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_json_array(jx9_value *pVal)
{
	return (pVal->iFlags & MEMOBJ_HASHMAP) ? TRUE : FALSE;
}
/*
 * [CAPIREF: jx9_value_is_json_object()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_json_object(jx9_value *pVal)
{
	jx9_hashmap *pMap;
	if( (pVal->iFlags & MEMOBJ_HASHMAP) == 0 ){
		return FALSE;
	}
	pMap = (jx9_hashmap *)pVal->x.pOther;
	if( (pMap->iFlags & HASHMAP_JSON_OBJECT) == 0 ){
		return FALSE;
	}
	return TRUE;
}
/*
 * [CAPIREF: jx9_value_is_resource()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_resource(jx9_value *pVal)
{
	return (pVal->iFlags & MEMOBJ_RES) ? TRUE : FALSE;
}
/*
 * [CAPIREF: jx9_value_is_empty()]
 * Please refer to the official documentation for function purpose and expected parameters.
 */
JX9_PRIVATE int jx9_value_is_empty(jx9_value *pVal)
{
	int rc;
	rc = jx9MemObjIsEmpty(pVal);
	return rc;
}
