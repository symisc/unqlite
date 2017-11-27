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
 /* $SymiscID: jx9_vm.c v1.0 FreeBSD 2012-12-09 00:19 stable <chm@symisc.net> $ */
#ifndef JX9_AMALGAMATION
#include "jx9Int.h"
#endif
/*
 * The code in this file implements execution method of the JX9 Virtual Machine.
 * The JX9 compiler (implemented in 'compiler.c' and 'parse.c') generates a bytecode program
 * which is then executed by the virtual machine implemented here to do the work of the JX9
 * statements.
 * JX9 bytecode programs are similar in form to assembly language. The program consists
 * of a linear sequence of operations .Each operation has an opcode and 3 operands.
 * Operands P1 and P2 are integers where the first is signed while the second is unsigned.
 * Operand P3 is an arbitrary pointer specific to each instruction. The P2 operand is usually
 * the jump destination used by the OP_JMP, OP_JZ, OP_JNZ, ... instructions.
 * Opcodes will typically ignore one or more operands. Many opcodes ignore all three operands.
 * Computation results are stored on a stack. Each entry on the stack is of type jx9_value.
 * JX9 uses the jx9_value object to represent all values that can be stored in a JX9 variable.
 * Since JX9 uses dynamic typing for the values it stores. Values stored in jx9_value objects
 * can be integers, floating point values, strings, arrays, object instances (object in the JX9 jargon)
 * and so on.
 * Internally, the JX9 virtual machine manipulates nearly all values as jx9_values structures.
 * Each jx9_value may cache multiple representations(string, integer etc.) of the same value.
 * An implicit conversion from one type to the other occurs as necessary.
 * Most of the code in this file is taken up by the [VmByteCodeExec()] function which does
 * the work of interpreting a JX9 bytecode program. But other routines are also provided
 * to help in building up a program instruction by instruction.
 */
/*
 * Each active virtual machine frame is represented by an instance 
 * of the following structure.
 * VM Frame hold local variables and other stuff related to function call.
 */
struct VmFrame
{
	VmFrame *pParent; /* Parent frame or NULL if global scope */
	void *pUserData;  /* Upper layer private data associated with this frame */
	SySet sLocal;     /* Local variables container (VmSlot instance) */
	jx9_vm *pVm;      /* VM that own this frame */
	SyHash hVar;      /* Variable hashtable for fast lookup */
	SySet sArg;       /* Function arguments container */
	sxi32 iFlags;     /* Frame configuration flags (See below)*/
	sxu32 iExceptionJump; /* Exception jump destination */
};
/*
 * When a user defined variable is  garbage collected, memory object index
 * is stored in an instance of the following structure and put in the free object
 * table so that it can be reused again without allocating a new memory object.
 */
typedef struct VmSlot VmSlot;
struct VmSlot
{
	sxu32 nIdx;      /* Index in pVm->aMemObj[] */ 
	void *pUserData; /* Upper-layer private data */
};
/*
 * Each parsed URI is recorded and stored in an instance of the following structure.
 * This structure and it's related routines are taken verbatim from the xHT project
 * [A modern embeddable HTTP engine implementing all the RFC2616 methods]
 * the xHT project is developed internally by Symisc Systems.
 */
typedef struct SyhttpUri SyhttpUri;
struct SyhttpUri 
{ 
	SyString sHost;     /* Hostname or IP address */ 
	SyString sPort;     /* Port number */ 
	SyString sPath;     /* Mandatory resource path passed verbatim (Not decoded) */ 
	SyString sQuery;    /* Query part */	 
	SyString sFragment; /* Fragment part */ 
	SyString sScheme;   /* Scheme */ 
	SyString sUser;     /* Username */ 
	SyString sPass;     /* Password */
	SyString sRaw;      /* Raw URI */
};
/* 
 * An instance of the following structure is used to record all MIME headers seen
 * during a HTTP interaction. 
 * This structure and it's related routines are taken verbatim from the xHT project
 * [A modern embeddable HTTP engine implementing all the RFC2616 methods]
 * the xHT project is developed internally by Symisc Systems.
 */  
typedef struct SyhttpHeader SyhttpHeader;
struct SyhttpHeader 
{ 
	SyString sName;    /* Header name [i.e:"Content-Type", "Host", "User-Agent"]. NOT NUL TERMINATED */ 
	SyString sValue;   /* Header values [i.e: "text/html"]. NOT NUL TERMINATED */ 
};
/*
 * Supported HTTP methods.
 */
#define HTTP_METHOD_GET  1 /* GET */
#define HTTP_METHOD_HEAD 2 /* HEAD */
#define HTTP_METHOD_POST 3 /* POST */
#define HTTP_METHOD_PUT  4 /* PUT */
#define HTTP_METHOD_OTHR 5 /* Other HTTP methods [i.e: DELETE, TRACE, OPTIONS...]*/
/*
 * Supported HTTP protocol version.
 */
#define HTTP_PROTO_10 1 /* HTTP/1.0 */
#define HTTP_PROTO_11 2 /* HTTP/1.1 */
/*
 * Register a constant and it's associated expansion callback so that
 * it can be expanded from the target JX9 program.
 * The constant expansion mechanism under JX9 is extremely powerful yet
 * simple and work as follows:
 * Each registered constant have a C procedure associated with it.
 * This procedure known as the constant expansion callback is responsible
 * of expanding the invoked constant to the desired value, for example:
 * The C procedure associated with the "__PI__" constant expands to 3.14 (the value of PI).
 * The "__OS__" constant procedure expands to the name of the host Operating Systems
 * (Windows, Linux, ...) and so on.
 * Please refer to the official documentation for additional information.
 */
JX9_PRIVATE sxi32 jx9VmRegisterConstant(
	jx9_vm *pVm,            /* Target VM */
	const SyString *pName,  /* Constant name */
	ProcConstant xExpand,   /* Constant expansion callback */
	void *pUserData         /* Last argument to xExpand() */
	)
{
	jx9_constant *pCons;
	SyHashEntry *pEntry;
	char *zDupName;
	sxi32 rc;
	pEntry = SyHashGet(&pVm->hConstant, (const void *)pName->zString, pName->nByte);
	if( pEntry ){
		/* Overwrite the old definition and return immediately */
		pCons = (jx9_constant *)pEntry->pUserData;
		pCons->xExpand = xExpand;
		pCons->pUserData = pUserData;
		return SXRET_OK;
	}
	/* Allocate a new constant instance */
	pCons = (jx9_constant *)SyMemBackendPoolAlloc(&pVm->sAllocator, sizeof(jx9_constant));
	if( pCons == 0 ){
		return 0;
	}
	/* Duplicate constant name */
	zDupName = SyMemBackendStrDup(&pVm->sAllocator, pName->zString, pName->nByte);
	if( zDupName == 0 ){
		SyMemBackendPoolFree(&pVm->sAllocator, pCons);
		return 0;
	}
	/* Install the constant */
	SyStringInitFromBuf(&pCons->sName, zDupName, pName->nByte);
	pCons->xExpand = xExpand;
	pCons->pUserData = pUserData;
	rc = SyHashInsert(&pVm->hConstant, (const void *)zDupName, SyStringLength(&pCons->sName), pCons);
	if( rc != SXRET_OK ){
		SyMemBackendFree(&pVm->sAllocator, zDupName);
		SyMemBackendPoolFree(&pVm->sAllocator, pCons);
		return rc;
	}
	/* All done, constant can be invoked from JX9 code */
	return SXRET_OK;
}
/*
 * Allocate a new foreign function instance.
 * This function return SXRET_OK on success. Any other
 * return value indicates failure.
 * Please refer to the official documentation for an introduction to
 * the foreign function mechanism.
 */
static sxi32 jx9NewForeignFunction(
	jx9_vm *pVm,              /* Target VM */
	const SyString *pName,    /* Foreign function name */
	ProcHostFunction xFunc,  /* Foreign function implementation */
	void *pUserData,          /* Foreign function private data */
	jx9_user_func **ppOut     /* OUT: VM image of the foreign function */
	)
{
	jx9_user_func *pFunc;
	char *zDup;
	/* Allocate a new user function */
	pFunc = (jx9_user_func *)SyMemBackendPoolAlloc(&pVm->sAllocator, sizeof(jx9_user_func));
	if( pFunc == 0 ){
		return SXERR_MEM;
	}
	/* Duplicate function name */
	zDup = SyMemBackendStrDup(&pVm->sAllocator, pName->zString, pName->nByte);
	if( zDup == 0 ){
		SyMemBackendPoolFree(&pVm->sAllocator, pFunc);
		return SXERR_MEM;
	}
	/* Zero the structure */
	SyZero(pFunc, sizeof(jx9_user_func));
	/* Initialize structure fields */
	SyStringInitFromBuf(&pFunc->sName, zDup, pName->nByte);
	pFunc->pVm   = pVm;
	pFunc->xFunc = xFunc;
	pFunc->pUserData = pUserData;
	SySetInit(&pFunc->aAux, &pVm->sAllocator, sizeof(jx9_aux_data));
	/* Write a pointer to the new function */
	*ppOut = pFunc;
	return SXRET_OK;
}
/*
 * Install a foreign function and it's associated callback so that
 * it can be invoked from the target JX9 code.
 * This function return SXRET_OK on successful registration. Any other
 * return value indicates failure.
 * Please refer to the official documentation for an introduction to
 * the foreign function mechanism.
 */
JX9_PRIVATE sxi32 jx9VmInstallForeignFunction(
	jx9_vm *pVm,              /* Target VM */
	const SyString *pName,    /* Foreign function name */
	ProcHostFunction xFunc,  /* Foreign function implementation */
	void *pUserData           /* Foreign function private data */
	)
{
	jx9_user_func *pFunc;
	SyHashEntry *pEntry;
	sxi32 rc;
	/* Overwrite any previously registered function with the same name */
	pEntry = SyHashGet(&pVm->hHostFunction, pName->zString, pName->nByte);
	if( pEntry ){
		pFunc = (jx9_user_func *)pEntry->pUserData;
		pFunc->pUserData = pUserData;
		pFunc->xFunc = xFunc;
		SySetReset(&pFunc->aAux);
		return SXRET_OK;
	}
	/* Create a new user function */
	rc = jx9NewForeignFunction(&(*pVm), &(*pName), xFunc, pUserData, &pFunc);
	if( rc != SXRET_OK ){
		return rc;
	}
	/* Install the function in the corresponding hashtable */
	rc = SyHashInsert(&pVm->hHostFunction, SyStringData(&pFunc->sName), pName->nByte, pFunc);
	if( rc != SXRET_OK ){
		SyMemBackendFree(&pVm->sAllocator, (void *)SyStringData(&pFunc->sName));
		SyMemBackendPoolFree(&pVm->sAllocator, pFunc);
		return rc;
	}
	/* User function successfully installed */
	return SXRET_OK;
}
/*
 * Initialize a VM function.
 */
JX9_PRIVATE sxi32 jx9VmInitFuncState(
	jx9_vm *pVm,        /* Target VM */
	jx9_vm_func *pFunc, /* Target Fucntion */
	const char *zName,  /* Function name */
	sxu32 nByte,        /* zName length */
	sxi32 iFlags,       /* Configuration flags */
	void *pUserData     /* Function private data */
	)
{
	/* Zero the structure */
	SyZero(pFunc, sizeof(jx9_vm_func));	
	/* Initialize structure fields */
	/* Arguments container */
	SySetInit(&pFunc->aArgs, &pVm->sAllocator, sizeof(jx9_vm_func_arg));
	/* Static variable container */
	SySetInit(&pFunc->aStatic, &pVm->sAllocator, sizeof(jx9_vm_func_static_var));
	/* Bytecode container */
	SySetInit(&pFunc->aByteCode, &pVm->sAllocator, sizeof(VmInstr));
    /* Preallocate some instruction slots */
	SySetAlloc(&pFunc->aByteCode, 0x10);
	pFunc->iFlags = iFlags;
	pFunc->pUserData = pUserData;
	SyStringInitFromBuf(&pFunc->sName, zName, nByte);
	return SXRET_OK;
}
/*
 * Install a user defined function in the corresponding VM container.
 */
JX9_PRIVATE sxi32 jx9VmInstallUserFunction(
	jx9_vm *pVm,        /* Target VM */
	jx9_vm_func *pFunc, /* Target function */
	SyString *pName     /* Function name */
	)
{
	SyHashEntry *pEntry;
	sxi32 rc;
	if( pName == 0 ){
		/* Use the built-in name */
		pName = &pFunc->sName;
	}
	/* Check for duplicates (functions with the same name) first */
	pEntry = SyHashGet(&pVm->hFunction, pName->zString, pName->nByte);
	if( pEntry ){
		jx9_vm_func *pLink = (jx9_vm_func *)pEntry->pUserData;
		if( pLink != pFunc ){
			/* Link */
			pFunc->pNextName = pLink;
			pEntry->pUserData = pFunc;
		}
		return SXRET_OK;
	}
	/* First time seen */
	pFunc->pNextName = 0;
	rc = SyHashInsert(&pVm->hFunction, pName->zString, pName->nByte, pFunc);
	return rc;
}
/*
 * Instruction builder interface.
 */
JX9_PRIVATE sxi32 jx9VmEmitInstr(
	jx9_vm *pVm,  /* Target VM */
	sxi32 iOp,    /* Operation to perform */
	sxi32 iP1,    /* First operand */
	sxu32 iP2,    /* Second operand */
	void *p3,     /* Third operand */
	sxu32 *pIndex /* Instruction index. NULL otherwise */
	)
{
	VmInstr sInstr;
	sxi32 rc;
	/* Fill the VM instruction */
	sInstr.iOp = (sxu8)iOp; 
	sInstr.iP1 = iP1; 
	sInstr.iP2 = iP2; 
	sInstr.p3  = p3;  
	if( pIndex ){
		/* Instruction index in the bytecode array */
		*pIndex = SySetUsed(pVm->pByteContainer);
	}
	/* Finally, record the instruction */
	rc = SySetPut(pVm->pByteContainer, (const void *)&sInstr);
	if( rc != SXRET_OK ){
		jx9GenCompileError(&pVm->sCodeGen, E_ERROR, 1, "Fatal, Cannot emit instruction due to a memory failure");
		/* Fall throw */
	}
	return rc;
}
/*
 * Swap the current bytecode container with the given one.
 */
JX9_PRIVATE sxi32 jx9VmSetByteCodeContainer(jx9_vm *pVm, SySet *pContainer)
{
	if( pContainer == 0 ){
		/* Point to the default container */
		pVm->pByteContainer = &pVm->aByteCode;
	}else{
		/* Change container */
		pVm->pByteContainer = &(*pContainer);
	}
	return SXRET_OK;
}
/*
 * Return the current bytecode container.
 */
JX9_PRIVATE SySet * jx9VmGetByteCodeContainer(jx9_vm *pVm)
{
	return pVm->pByteContainer;
}
/*
 * Extract the VM instruction rooted at nIndex.
 */
JX9_PRIVATE VmInstr * jx9VmGetInstr(jx9_vm *pVm, sxu32 nIndex)
{
	VmInstr *pInstr;
	pInstr = (VmInstr *)SySetAt(pVm->pByteContainer, nIndex);
	return pInstr;
}
/*
 * Return the total number of VM instructions recorded so far.
 */
JX9_PRIVATE sxu32 jx9VmInstrLength(jx9_vm *pVm)
{
	return SySetUsed(pVm->pByteContainer);
}
/*
 * Pop the last VM instruction.
 */
JX9_PRIVATE VmInstr * jx9VmPopInstr(jx9_vm *pVm)
{
	return (VmInstr *)SySetPop(pVm->pByteContainer);
}
/*
 * Peek the last VM instruction.
 */
JX9_PRIVATE VmInstr * jx9VmPeekInstr(jx9_vm *pVm)
{
	return (VmInstr *)SySetPeek(pVm->pByteContainer);
}
/*
 * Allocate a new virtual machine frame.
 */
static VmFrame * VmNewFrame(
	jx9_vm *pVm,              /* Target VM */
	void *pUserData          /* Upper-layer private data */
	)
{
	VmFrame *pFrame;
	/* Allocate a new vm frame */
	pFrame = (VmFrame *)SyMemBackendPoolAlloc(&pVm->sAllocator, sizeof(VmFrame));
	if( pFrame == 0 ){
		return 0;
	}
	/* Zero the structure */
	SyZero(pFrame, sizeof(VmFrame));
	/* Initialize frame fields */
	pFrame->pUserData = pUserData;
	pFrame->pVm = pVm;
	SyHashInit(&pFrame->hVar, &pVm->sAllocator, 0, 0);
	SySetInit(&pFrame->sArg, &pVm->sAllocator, sizeof(VmSlot));
	SySetInit(&pFrame->sLocal, &pVm->sAllocator, sizeof(VmSlot));
	return pFrame;
}
/*
 * Enter a VM frame.
 */
static sxi32 VmEnterFrame(
	jx9_vm *pVm,               /* Target VM */
	void *pUserData,           /* Upper-layer private data */
	VmFrame **ppFrame          /* OUT: Top most active frame */
	)
{
	VmFrame *pFrame;
	/* Allocate a new frame */
	pFrame = VmNewFrame(&(*pVm), pUserData);
	if( pFrame == 0 ){
		return SXERR_MEM;
	}
	/* Link to the list of active VM frame */
	pFrame->pParent = pVm->pFrame;
	pVm->pFrame = pFrame;
	if( ppFrame ){
		/* Write a pointer to the new VM frame */
		*ppFrame = pFrame;
	}
	return SXRET_OK;
}
/*
 * Link a foreign variable with the TOP most active frame.
 * Refer to the JX9_OP_UPLINK instruction implementation for more
 * information.
 */
static sxi32 VmFrameLink(jx9_vm *pVm,SyString *pName)
{
	VmFrame *pTarget, *pFrame;
	SyHashEntry *pEntry = 0;
	sxi32 rc;
	/* Point to the upper frame */
	pFrame = pVm->pFrame;
	pTarget = pFrame;
	pFrame = pTarget->pParent;
	while( pFrame ){
		/* Query the current frame */
		pEntry = SyHashGet(&pFrame->hVar, (const void *)pName->zString, pName->nByte);
		if( pEntry ){
			/* Variable found */
			break;
		}		
		/* Point to the upper frame */
		pFrame = pFrame->pParent;
	}
	if( pEntry == 0 ){
		/* Inexistant variable */
		return SXERR_NOTFOUND;
	}
	/* Link to the current frame */
	rc = SyHashInsert(&pTarget->hVar, pEntry->pKey, pEntry->nKeyLen, pEntry->pUserData);
	return rc;
}
/*
 * Leave the top-most active frame.
 */
static void VmLeaveFrame(jx9_vm *pVm)
{
	VmFrame *pFrame = pVm->pFrame;
	if( pFrame ){
		/* Unlink from the list of active VM frame */
		pVm->pFrame = pFrame->pParent;
		if( pFrame->pParent  ){
			VmSlot  *aSlot;
			sxu32 n;
			/* Restore local variable to the free pool so that they can be reused again */
			aSlot = (VmSlot *)SySetBasePtr(&pFrame->sLocal);
			for(n = 0 ; n < SySetUsed(&pFrame->sLocal) ; ++n ){
				/* Unset the local variable */
				jx9VmUnsetMemObj(&(*pVm), aSlot[n].nIdx);
			}
		}
		/* Release internal containers */
		SyHashRelease(&pFrame->hVar);
		SySetRelease(&pFrame->sArg);
		SySetRelease(&pFrame->sLocal);
		/* Release the whole structure */
		SyMemBackendPoolFree(&pVm->sAllocator, pFrame);
	}
}
/*
 * Compare two functions signature and return the comparison result.
 */
static int VmOverloadCompare(SyString *pFirst, SyString *pSecond)
{
	const char *zSend = &pSecond->zString[pSecond->nByte];
	const char *zFend = &pFirst->zString[pFirst->nByte];
	const char *zSin = pSecond->zString;
	const char *zFin = pFirst->zString;
	const char *zPtr = zFin;
	for(;;){
		if( zFin >= zFend || zSin >= zSend ){
			break;
		}
		if( zFin[0] != zSin[0] ){
			/* mismatch */
			break;
		}
		zFin++;
		zSin++;
	}
	return (int)(zFin-zPtr);
}
/*
 * Select the appropriate VM function for the current call context.
 * This is the implementation of the powerful 'function overloading' feature
 * introduced by the version 2 of the JX9 engine.
 * Refer to the official documentation for more information.
 */
static jx9_vm_func * VmOverload(
	jx9_vm *pVm,         /* Target VM */
	jx9_vm_func *pList,  /* Linked list of candidates for overloading */
	jx9_value *aArg,     /* Array of passed arguments */
	int nArg             /* Total number of passed arguments  */
	)
{
	int iTarget, i, j, iCur, iMax;
	jx9_vm_func *apSet[10];   /* Maximum number of candidates */
	jx9_vm_func *pLink;
	SyString sArgSig;
	SyBlob sSig;

	pLink = pList;
	i = 0;
	/* Put functions expecting the same number of passed arguments */
	while( i < (int)SX_ARRAYSIZE(apSet) ){
		if( pLink == 0 ){
			break;
		}
		if( (int)SySetUsed(&pLink->aArgs) == nArg ){
			/* Candidate for overloading */
			apSet[i++] = pLink;
		}
		/* Point to the next entry */
		pLink = pLink->pNextName;
	}
	if( i < 1 ){
		/* No candidates, return the head of the list */
		return pList;
	}
	if( nArg < 1 || i < 2 ){
		/* Return the only candidate */
		return apSet[0];
	}
	/* Calculate function signature */
	SyBlobInit(&sSig, &pVm->sAllocator);
	for( j = 0 ; j < nArg ; j++ ){
		int c = 'n'; /* null */
		if( aArg[j].iFlags & MEMOBJ_HASHMAP ){
			/* Hashmap */
			c = 'h';
		}else if( aArg[j].iFlags & MEMOBJ_BOOL ){
			/* bool */
			c = 'b';
		}else if( aArg[j].iFlags & MEMOBJ_INT ){
			/* int */
			c = 'i';
		}else if( aArg[j].iFlags & MEMOBJ_STRING ){
			/* String */
			c = 's';
		}else if( aArg[j].iFlags & MEMOBJ_REAL ){
			/* Float */
			c = 'f';
		}
		if( c > 0 ){
			SyBlobAppend(&sSig, (const void *)&c, sizeof(char));
		}
	}
	SyStringInitFromBuf(&sArgSig, SyBlobData(&sSig), SyBlobLength(&sSig));
	iTarget = 0;
	iMax = -1;
	/* Select the appropriate function */
	for( j = 0 ; j < i ; j++ ){
		/* Compare the two signatures */
		iCur = VmOverloadCompare(&sArgSig, &apSet[j]->sSignature);
		if( iCur > iMax ){
			iMax = iCur;
			iTarget = j;
		}
	}
	SyBlobRelease(&sSig);
	/* Appropriate function for the current call context */
	return apSet[iTarget];
}
/* 
 * Dummy read-only buffer used for slot reservation.
 */
static const char zDummy[sizeof(jx9_value)] = { 0 }; /* Must be >= sizeof(jx9_value) */ 
/*
 * Reserve a constant memory object.
 * Return a pointer to the raw jx9_value on success. NULL on failure.
 */
JX9_PRIVATE jx9_value * jx9VmReserveConstObj(jx9_vm *pVm, sxu32 *pIndex)
{
	jx9_value *pObj;
	sxi32 rc;
	if( pIndex ){
		/* Object index in the object table */
		*pIndex = SySetUsed(&pVm->aLitObj);
	}
	/* Reserve a slot for the new object */
	rc = SySetPut(&pVm->aLitObj, (const void *)zDummy);
	if( rc != SXRET_OK ){
		/* If the supplied memory subsystem is so sick that we are unable to allocate
		 * a tiny chunk of memory, there is no much we can do here.
		 */
		return 0;
	}
	pObj = (jx9_value *)SySetPeek(&pVm->aLitObj);
	return pObj;
}
/*
 * Reserve a memory object.
 * Return a pointer to the raw jx9_value on success. NULL on failure.
 */
static jx9_value * VmReserveMemObj(jx9_vm *pVm, sxu32 *pIndex)
{
	jx9_value *pObj;
	sxi32 rc;
	if( pIndex ){
		/* Object index in the object table */
		*pIndex = SySetUsed(&pVm->aMemObj);
	}
	/* Reserve a slot for the new object */
	rc = SySetPut(&pVm->aMemObj, (const void *)zDummy);
	if( rc != SXRET_OK ){
		/* If the supplied memory subsystem is so sick that we are unable to allocate
		 * a tiny chunk of memory, there is no much we can do here.
		 */
		return 0;
	}
	pObj = (jx9_value *)SySetPeek(&pVm->aMemObj);
	return pObj;
}
/* Forward declaration */
static sxi32 VmEvalChunk(jx9_vm *pVm, jx9_context *pCtx, SyString *pChunk, int iFlags, int bTrueReturn);
/*
 * Built-in functions that cannot be implemented directly as foreign functions.
 */
#define JX9_BUILTIN_LIB \
	"function scandir(string $directory, int $sort_order = SCANDIR_SORT_ASCENDING)"\
    "{"\
	"  if( func_num_args() < 1 ){ return FALSE; }"\
	"  $aDir = [];"\
	"  $pHandle = opendir($directory);"\
	"  if( $pHandle == FALSE ){ return FALSE; }"\
	"  while(FALSE !== ($pEntry = readdir($pHandle)) ){"\
	"      $aDir[] = $pEntry;"\
	"   }"\
	"  closedir($pHandle);"\
	"  if( $sort_order == SCANDIR_SORT_DESCENDING ){"\
	"      rsort($aDir);"\
	"  }else if( $sort_order == SCANDIR_SORT_ASCENDING ){"\
	"      sort($aDir);"\
	"  }"\
	"  return $aDir;"\
	"}"\
	"function glob(string $pattern, int $iFlags = 0){"\
	"/* Open the target directory */"\
	"$zDir = dirname($pattern);"\
	"if(!is_string($zDir) ){ $zDir = './'; }"\
	"$pHandle = opendir($zDir);"\
	"if( $pHandle == FALSE ){"\
	"   /* IO error while opening the current directory, return FALSE */"\
	"	return FALSE;"\
	"}"\
	"$pattern = basename($pattern);"\
	"$pArray = []; /* Empty array */"\
	"/* Loop throw available entries */"\
	"while( FALSE !== ($pEntry = readdir($pHandle)) ){"\
	" /* Use the built-in strglob function which is a Symisc eXtension for wildcard comparison*/"\
	"	$rc = strglob($pattern, $pEntry);"\
	"	if( $rc ){"\
	"	   if( is_dir($pEntry) ){"\
	"	      if( $iFlags & GLOB_MARK ){"\
	"		     /* Adds a slash to each directory returned */"\
	"			 $pEntry .= DIRECTORY_SEPARATOR;"\
	"		  }"\
	"	   }else if( $iFlags & GLOB_ONLYDIR ){"\
	"	     /* Not a directory, ignore */"\
	"		 continue;"\
	"	   }"\
	"	   /* Add the entry */"\
	"	   $pArray[] = $pEntry;"\
	"	}"\
	" }"\
	"/* Close the handle */"\
	"closedir($pHandle);"\
	"if( ($iFlags & GLOB_NOSORT) == 0 ){"\
	"  /* Sort the array */"\
	"  sort($pArray);"\
	"}"\
	"if( ($iFlags & GLOB_NOCHECK) && sizeof($pArray) < 1 ){"\
	"  /* Return the search pattern if no files matching were found */"\
	"  $pArray[] = $pattern;"\
	"}"\
	"/* Return the created array */"\
	"return $pArray;"\
   "}"\
   "/* Creates a temporary file */"\
   "function tmpfile(){"\
   "  /* Extract the temp directory */"\
   "  $zTempDir = sys_get_temp_dir();"\
   "  if( strlen($zTempDir) < 1 ){"\
   "    /* Use the current dir */"\
   "    $zTempDir = '.';"\
   "  }"\
   "  /* Create the file */"\
   "  $pHandle = fopen($zTempDir.DIRECTORY_SEPARATOR.'JX9'.rand_str(12), 'w+');"\
   "  return $pHandle;"\
   "}"\
   "/* Creates a temporary filename */"\
   "function tempnam(string $zDir = sys_get_temp_dir() /* Symisc eXtension */, string $zPrefix = 'JX9')"\
   "{"\
   "   return $zDir.DIRECTORY_SEPARATOR.$zPrefix.rand_str(12);"\
   "}"\
	"function max(){"\
    "  $pArgs = func_get_args();"\
    " if( sizeof($pArgs) < 1 ){"\
	"  return null;"\
    " }"\
    " if( sizeof($pArgs) < 2 ){"\
    " $pArg = $pArgs[0];"\
	" if( !is_array($pArg) ){"\
	"   return $pArg; "\
	" }"\
	" if( sizeof($pArg) < 1 ){"\
	"   return null;"\
	" }"\
	" $pArg = array_copy($pArgs[0]);"\
	" reset($pArg);"\
	" $max = current($pArg);"\
	" while( FALSE !== ($val = next($pArg)) ){"\
	"   if( $val > $max ){"\
	"     $max = $val;"\
    " }"\
	" }"\
	" return $max;"\
    " }"\
    " $max = $pArgs[0];"\
    " for( $i = 1; $i < sizeof($pArgs) ; ++$i ){"\
    " $val = $pArgs[$i];"\
	"if( $val > $max ){"\
	" $max = $val;"\
	"}"\
    " }"\
	" return $max;"\
    "}"\
	"function min(){"\
    "  $pArgs = func_get_args();"\
    " if( sizeof($pArgs) < 1 ){"\
	"  return null;"\
    " }"\
    " if( sizeof($pArgs) < 2 ){"\
    " $pArg = $pArgs[0];"\
	" if( !is_array($pArg) ){"\
	"   return $pArg; "\
	" }"\
	" if( sizeof($pArg) < 1 ){"\
	"   return null;"\
	" }"\
	" $pArg = array_copy($pArgs[0]);"\
	" reset($pArg);"\
	" $min = current($pArg);"\
	" while( FALSE !== ($val = next($pArg)) ){"\
	"   if( $val < $min ){"\
	"     $min = $val;"\
    " }"\
	" }"\
	" return $min;"\
    " }"\
    " $min = $pArgs[0];"\
    " for( $i = 1; $i < sizeof($pArgs) ; ++$i ){"\
    " $val = $pArgs[$i];"\
	"if( $val < $min ){"\
	" $min = $val;"\
	" }"\
    " }"\
	" return $min;"\
	"}"
/*
 * Initialize a freshly allocated JX9 Virtual Machine so that we can
 * start compiling the target JX9 program.
 */
JX9_PRIVATE sxi32 jx9VmInit(
	 jx9_vm *pVm, /* Initialize this */
	 jx9 *pEngine /* Master engine */
	 )
{
	SyString sBuiltin;
	jx9_value *pObj;
	sxi32 rc;
	/* Zero the structure */
	SyZero(pVm, sizeof(jx9_vm));
	/* Initialize VM fields */
	pVm->pEngine = &(*pEngine);
	SyMemBackendInitFromParent(&pVm->sAllocator, &pEngine->sAllocator);
	/* Instructions containers */
	SySetInit(&pVm->aByteCode, &pVm->sAllocator, sizeof(VmInstr));
	SySetAlloc(&pVm->aByteCode, 0xFF);
	pVm->pByteContainer = &pVm->aByteCode;
	/* Object containers */
	SySetInit(&pVm->aMemObj, &pVm->sAllocator, sizeof(jx9_value));
	SySetAlloc(&pVm->aMemObj, 0xFF);
	/* Virtual machine internal containers */
	SyBlobInit(&pVm->sConsumer, &pVm->sAllocator);
	SyBlobInit(&pVm->sWorker, &pVm->sAllocator);
	SyBlobInit(&pVm->sArgv, &pVm->sAllocator);
	SySetInit(&pVm->aLitObj, &pVm->sAllocator, sizeof(jx9_value));
	SySetAlloc(&pVm->aLitObj, 0xFF);
	SyHashInit(&pVm->hHostFunction, &pVm->sAllocator, 0, 0);
	SyHashInit(&pVm->hFunction, &pVm->sAllocator, 0, 0);
	SyHashInit(&pVm->hConstant, &pVm->sAllocator, 0, 0);
	SyHashInit(&pVm->hSuper, &pVm->sAllocator, 0, 0);
	SySetInit(&pVm->aFreeObj, &pVm->sAllocator, sizeof(VmSlot));
	/* Configuration containers */
	SySetInit(&pVm->aFiles, &pVm->sAllocator, sizeof(SyString));
	SySetInit(&pVm->aPaths, &pVm->sAllocator, sizeof(SyString));
	SySetInit(&pVm->aIncluded, &pVm->sAllocator, sizeof(SyString));
	SySetInit(&pVm->aIOstream, &pVm->sAllocator, sizeof(jx9_io_stream *));
	/* Error callbacks containers */
	jx9MemObjInit(&(*pVm), &pVm->sAssertCallback);
	/* Set a default recursion limit */
#if defined(__WINNT__) || defined(__UNIXES__)
	pVm->nMaxDepth = 32;
#else
	pVm->nMaxDepth = 16;
#endif
	/* Default assertion flags */
	pVm->iAssertFlags = JX9_ASSERT_WARNING; /* Issue a warning for each failed assertion */
	/* PRNG context */
	SyRandomnessInit(&pVm->sPrng, 0, 0);
	/* Install the null constant */
	pObj = jx9VmReserveConstObj(&(*pVm), 0);
	if( pObj == 0 ){
		rc = SXERR_MEM;
		goto Err;
	}
	jx9MemObjInit(pVm, pObj);
	/* Install the boolean TRUE constant */
	pObj = jx9VmReserveConstObj(&(*pVm), 0);
	if( pObj == 0 ){
		rc = SXERR_MEM;
		goto Err;
	}
	jx9MemObjInitFromBool(pVm, pObj, 1);
	/* Install the boolean FALSE constant */
	pObj = jx9VmReserveConstObj(&(*pVm), 0);
	if( pObj == 0 ){
		rc = SXERR_MEM;
		goto Err;
	}
	jx9MemObjInitFromBool(pVm, pObj, 0);
	/* Create the global frame */
	rc = VmEnterFrame(&(*pVm), 0, 0);
	if( rc != SXRET_OK ){
		goto Err;
	}
	/* Initialize the code generator */
	rc = jx9InitCodeGenerator(pVm, pEngine->xConf.xErr, pEngine->xConf.pErrData);
	if( rc != SXRET_OK ){
		goto Err;
	}
	/* VM correctly initialized, set the magic number */
	pVm->nMagic = JX9_VM_INIT;
	SyStringInitFromBuf(&sBuiltin,JX9_BUILTIN_LIB, sizeof(JX9_BUILTIN_LIB)-1);
	/* Compile the built-in library */
	VmEvalChunk(&(*pVm), 0, &sBuiltin, 0, FALSE);
	/* Reset the code generator */
	jx9ResetCodeGenerator(&(*pVm), pEngine->xConf.xErr, pEngine->xConf.pErrData);
	return SXRET_OK;
Err:
	SyMemBackendRelease(&pVm->sAllocator);
	return rc;
}
/*
 * Default VM output consumer callback.That is, all VM output is redirected to this
 * routine which store the output in an internal blob.
 * The output can be extracted later after program execution [jx9_vm_exec()] via
 * the [jx9_vm_config()] interface with a configuration verb set to
 * jx9VM_CONFIG_EXTRACT_OUTPUT.
 * Refer to the official docurmentation for additional information.
 * Note that for performance reason it's preferable to install a VM output
 * consumer callback via (jx9VM_CONFIG_OUTPUT) rather than waiting for the VM
 * to finish executing and extracting the output.
 */
JX9_PRIVATE sxi32 jx9VmBlobConsumer(
	const void *pOut,   /* VM Generated output*/
	unsigned int nLen,  /* Generated output length */
	void *pUserData     /* User private data */
	)
{
	 sxi32 rc;
	 /* Store the output in an internal BLOB */
	 rc = SyBlobAppend((SyBlob *)pUserData, pOut, nLen);
	 return rc;
}
#define VM_STACK_GUARD 16
/*
 * Allocate a new operand stack so that we can start executing
 * our compiled JX9 program.
 * Return a pointer to the operand stack (array of jx9_values)
 * on success. NULL (Fatal error) on failure.
 */
static jx9_value * VmNewOperandStack(
	jx9_vm *pVm, /* Target VM */
	sxu32 nInstr /* Total numer of generated bytecode instructions */
	)
{
	jx9_value *pStack;
  /* No instruction ever pushes more than a single element onto the
  ** stack and the stack never grows on successive executions of the
  ** same loop. So the total number of instructions is an upper bound
  ** on the maximum stack depth required.
  **
  ** Allocation all the stack space we will ever need.
  */
	nInstr += VM_STACK_GUARD;
	pStack = (jx9_value *)SyMemBackendAlloc(&pVm->sAllocator, nInstr * sizeof(jx9_value));
	if( pStack == 0 ){
		return 0;
	}
	/* Initialize the operand stack */
	while( nInstr > 0 ){
		jx9MemObjInit(&(*pVm), &pStack[nInstr - 1]);
		--nInstr;
	}
	/* Ready for bytecode execution */
	return pStack;
}
/* Forward declaration */
static sxi32 VmRegisterSpecialFunction(jx9_vm *pVm);
/*
 * Prepare the Virtual Machine for bytecode execution.
 * This routine gets called by the JX9 engine after
 * successful compilation of the target JX9 program.
 */
JX9_PRIVATE sxi32 jx9VmMakeReady(
	jx9_vm *pVm /* Target VM */
	)
{
	sxi32 rc;
	if( pVm->nMagic != JX9_VM_INIT ){
		/* Initialize your VM first */
		return SXERR_CORRUPT;
	}
	/* Mark the VM ready for bytecode execution */
	pVm->nMagic = JX9_VM_RUN; 
	/* Release the code generator now we have compiled our program */
	jx9ResetCodeGenerator(pVm, 0, 0);
	/* Emit the DONE instruction */
	rc = jx9VmEmitInstr(&(*pVm), JX9_OP_DONE, 0, 0, 0, 0);
	if( rc != SXRET_OK ){
		return SXERR_MEM;
	}
	/* Script return value */
	jx9MemObjInit(&(*pVm), &pVm->sExec); /* Assume a NULL return value */
	/* Allocate a new operand stack */	
	pVm->aOps = VmNewOperandStack(&(*pVm), SySetUsed(pVm->pByteContainer));
	if( pVm->aOps == 0 ){
		return SXERR_MEM;
	}
	/* Set the default VM output consumer callback and it's 
	 * private data. */
	pVm->sVmConsumer.xConsumer = jx9VmBlobConsumer;
	pVm->sVmConsumer.pUserData = &pVm->sConsumer;
	/* Register special functions first [i.e: print, func_get_args(), die, etc.] */
	rc = VmRegisterSpecialFunction(&(*pVm));
	if( rc != SXRET_OK ){
		/* Don't worry about freeing memory, everything will be released shortly */
		return rc;
	}
	/* Create superglobals [i.e: $GLOBALS, $_GET, $_POST...] */
	rc = jx9HashmapLoadBuiltin(&(*pVm));
	if( rc != SXRET_OK ){
		/* Don't worry about freeing memory, everything will be released shortly */
		return rc;
	}
	/* Register built-in constants [i.e: JX9_EOL, JX9_OS...] */
	jx9RegisterBuiltInConstant(&(*pVm));
	/* Register built-in functions [i.e: is_null(), array_diff(), strlen(), etc.] */
	jx9RegisterBuiltInFunction(&(*pVm));
	/* VM is ready for bytecode execution */
	return SXRET_OK;
}
/*
 * Reset a Virtual Machine to it's initial state.
 */
JX9_PRIVATE sxi32 jx9VmReset(jx9_vm *pVm)
{
	if( pVm->nMagic != JX9_VM_RUN && pVm->nMagic != JX9_VM_EXEC ){
		return SXERR_CORRUPT;
	}
	/* TICKET 1433-003: As of this version, the VM is automatically reset */
	SyBlobReset(&pVm->sConsumer);
	jx9MemObjRelease(&pVm->sExec);
	/* Set the ready flag */
	pVm->nMagic = JX9_VM_RUN;
	return SXRET_OK;
}
/*
 * Release a Virtual Machine.
 * Every virtual machine must be destroyed in order to avoid memory leaks.
 */
JX9_PRIVATE sxi32 jx9VmRelease(jx9_vm *pVm)
{
	/* Set the stale magic number */
	pVm->nMagic = JX9_VM_STALE;
	/* Release the private memory subsystem */
	SyMemBackendRelease(&pVm->sAllocator);
	return SXRET_OK;
}
/*
 * Initialize a foreign function call context.
 * The context in which a foreign function executes is stored in a jx9_context object.
 * A pointer to a jx9_context object is always first parameter to application-defined foreign
 * functions.
 * The application-defined foreign function implementation will pass this pointer through into
 * calls to dozens of interfaces, these includes jx9_result_int(), jx9_result_string(), jx9_result_value(), 
 * jx9_context_new_scalar(), jx9_context_alloc_chunk(), jx9_context_output(), jx9_context_throw_error()
 * and many more. Refer to the C/C++ Interfaces documentation for additional information.
 */
static sxi32 VmInitCallContext(
	jx9_context *pOut,    /* Call Context */
	jx9_vm *pVm,          /* Target VM */
	jx9_user_func *pFunc, /* Foreign function to execute shortly */
	jx9_value *pRet,      /* Store return value here*/
	sxi32 iFlags          /* Control flags */
	)
{
	pOut->pFunc = pFunc;
	pOut->pVm   = pVm;
	SySetInit(&pOut->sVar, &pVm->sAllocator, sizeof(jx9_value *));
	SySetInit(&pOut->sChunk, &pVm->sAllocator, sizeof(jx9_aux_data));
	/* Assume a null return value */
	MemObjSetType(pRet, MEMOBJ_NULL);
	pOut->pRet = pRet;
	pOut->iFlags = iFlags;
	return SXRET_OK;
}
/*
 * Release a foreign function call context and cleanup the mess
 * left behind.
 */
static void VmReleaseCallContext(jx9_context *pCtx)
{
	sxu32 n;
	if( SySetUsed(&pCtx->sVar) > 0 ){
		jx9_value **apObj = (jx9_value **)SySetBasePtr(&pCtx->sVar);
		for( n = 0 ; n < SySetUsed(&pCtx->sVar) ; ++n ){
			if( apObj[n] == 0 ){
				/* Already released */
				continue;
			}
			jx9MemObjRelease(apObj[n]);
			SyMemBackendPoolFree(&pCtx->pVm->sAllocator, apObj[n]);
		}
		SySetRelease(&pCtx->sVar);
	}
	if( SySetUsed(&pCtx->sChunk) > 0 ){
		jx9_aux_data *aAux;
		void *pChunk;
		/* Automatic release of dynamically allocated chunk 
		 * using [jx9_context_alloc_chunk()].
		 */
		aAux = (jx9_aux_data *)SySetBasePtr(&pCtx->sChunk);
		for( n = 0; n < SySetUsed(&pCtx->sChunk) ; ++n ){
			pChunk = aAux[n].pAuxData;
			/* Release the chunk */
			if( pChunk ){
				SyMemBackendFree(&pCtx->pVm->sAllocator, pChunk);
			}
		}
		SySetRelease(&pCtx->sChunk);
	}
}
/*
 * Release a jx9_value allocated from the body of a foreign function.
 * Refer to [jx9_context_release_value()] for additional information.
 */
JX9_PRIVATE void jx9VmReleaseContextValue(
	jx9_context *pCtx, /* Call context */
	jx9_value *pValue  /* Release this value */
	)
{
	if( pValue == 0 ){
		/* NULL value is a harmless operation */
		return;
	}
	if( SySetUsed(&pCtx->sVar) > 0 ){
		jx9_value **apObj = (jx9_value **)SySetBasePtr(&pCtx->sVar);
		sxu32 n;
		for( n = 0 ; n < SySetUsed(&pCtx->sVar) ; ++n ){
			if( apObj[n] == pValue ){
				jx9MemObjRelease(pValue);
				SyMemBackendPoolFree(&pCtx->pVm->sAllocator, pValue);
				/* Mark as released */
				apObj[n] = 0;
				break;
			}
		}
	}
}
/*
 * Pop and release as many memory object from the operand stack.
 */
static void VmPopOperand(
	jx9_value **ppTos, /* Operand stack */
	sxi32 nPop         /* Total number of memory objects to pop */
	)
{
	jx9_value *pTos = *ppTos;
	while( nPop > 0 ){
		jx9MemObjRelease(pTos);
		pTos--;
		nPop--;
	}
	/* Top of the stack */
	*ppTos = pTos;
}
/*
 * Reserve a memory object.
 * Return a pointer to the raw jx9_value on success. NULL on failure.
 */
JX9_PRIVATE jx9_value * jx9VmReserveMemObj(jx9_vm *pVm,sxu32 *pIdx)
{
	jx9_value *pObj = 0;
	VmSlot *pSlot;
	sxu32 nIdx;
	/* Check for a free slot */
	nIdx = SXU32_HIGH; /* cc warning */
	pSlot = (VmSlot *)SySetPop(&pVm->aFreeObj);
	if( pSlot ){
		pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pSlot->nIdx);
		nIdx = pSlot->nIdx;
	}
	if( pObj == 0 ){
		/* Reserve a new memory object */
		pObj = VmReserveMemObj(&(*pVm), &nIdx);
		if( pObj == 0 ){
			return 0;
		}
	}
	/* Set a null default value */
	jx9MemObjInit(&(*pVm), pObj);
	if( pIdx ){
		*pIdx = nIdx;
	}
	pObj->nIdx = nIdx;
	return pObj;
}
/*
 * Extract a variable value from the top active VM frame.
 * Return a pointer to the variable value on success. 
 * NULL otherwise (non-existent variable/Out-of-memory, ...).
 */
static jx9_value * VmExtractMemObj(
	jx9_vm *pVm,           /* Target VM */
	const SyString *pName, /* Variable name */
	int bDup,              /* True to duplicate variable name */
	int bCreate            /* True to create the variable if non-existent */
	)
{
	int bNullify = FALSE;
	SyHashEntry *pEntry;
	VmFrame *pFrame;
	jx9_value *pObj;
	sxu32 nIdx;
	sxi32 rc;
	/* Point to the top active frame */
	pFrame = pVm->pFrame;
	/* Perform the lookup */
	if( pName == 0 || pName->nByte < 1 ){
		static const SyString sAnnon = { " " , sizeof(char) };
		pName = &sAnnon;
		/* Always nullify the object */
		bNullify = TRUE;
		bDup = FALSE;
	}
	/* Check the superglobals table first */
	pEntry = SyHashGet(&pVm->hSuper, (const void *)pName->zString, pName->nByte);
	if( pEntry == 0 ){
		/* Query the top active frame */
		pEntry = SyHashGet(&pFrame->hVar, (const void *)pName->zString, pName->nByte);
		if( pEntry == 0 ){
			char *zName = (char *)pName->zString;
			VmSlot sLocal;
			if( !bCreate ){
				/* Do not create the variable, return NULL */
				return 0;
			}
			/* No such variable, automatically create a new one and install
			 * it in the current frame.
			 */
			pObj = jx9VmReserveMemObj(&(*pVm),&nIdx);
			if( pObj == 0 ){
				return 0;
			}
			if( bDup ){
				/* Duplicate name */
				zName = SyMemBackendStrDup(&pVm->sAllocator, pName->zString, pName->nByte);
				if( zName == 0 ){
					return 0;
				}
			}
			/* Link to the top active VM frame */
			rc = SyHashInsert(&pFrame->hVar, zName, pName->nByte, SX_INT_TO_PTR(nIdx));
			if( rc != SXRET_OK ){
				/* Return the slot to the free pool */
				sLocal.nIdx = nIdx;
				sLocal.pUserData = 0;
				SySetPut(&pVm->aFreeObj, (const void *)&sLocal);
				return 0;
			}
			if( pFrame->pParent != 0 ){
				/* Local variable */
				sLocal.nIdx = nIdx;
				SySetPut(&pFrame->sLocal, (const void *)&sLocal);
			}
		}else{
			/* Extract variable contents */
			nIdx = (sxu32)SX_PTR_TO_INT(pEntry->pUserData);
			pObj = (jx9_value *)SySetAt(&pVm->aMemObj, nIdx);
			if( bNullify && pObj ){
				jx9MemObjRelease(pObj);
			}
		}
	}else{
		/* Superglobal */
		nIdx = (sxu32)SX_PTR_TO_INT(pEntry->pUserData);
		pObj = (jx9_value *)SySetAt(&pVm->aMemObj, nIdx);
	}
	return pObj;
}
/*
 * Extract a superglobal variable such as $_GET, $_POST, $_HEADERS, .... 
 * Return a pointer to the variable value on success.NULL otherwise.
 */
static jx9_value * VmExtractSuper(
	jx9_vm *pVm,       /* Target VM */
	const char *zName, /* Superglobal name: NOT NULL TERMINATED */
	sxu32 nByte        /* zName length */
	)
{
	SyHashEntry *pEntry;
	jx9_value *pValue;
	sxu32 nIdx;
	/* Query the superglobal table */
	pEntry = SyHashGet(&pVm->hSuper, (const void *)zName, nByte);
	if( pEntry == 0 ){
		/* No such entry */
		return 0;
	}
	/* Extract the superglobal index in the global object pool */
	nIdx = SX_PTR_TO_INT(pEntry->pUserData);
	/* Extract the variable value  */
	pValue = (jx9_value *)SySetAt(&pVm->aMemObj, nIdx);
	return pValue;
}
/*
 * Perform a raw hashmap insertion.
 * Refer to the [jx9VmConfigure()] implementation for additional information.
 */
static sxi32 VmHashmapInsert(
	jx9_hashmap *pMap,  /* Target hashmap  */
	const char *zKey,   /* Entry key */
	int nKeylen,        /* zKey length*/
	const char *zData,  /* Entry data */
	int nLen            /* zData length */
	)
{
	jx9_value sKey,sValue;
	jx9_value *pKey;
	sxi32 rc;
	pKey = 0;
	jx9MemObjInit(pMap->pVm, &sKey);
	jx9MemObjInitFromString(pMap->pVm, &sValue, 0);
	if( zKey ){
		if( nKeylen < 0 ){
			nKeylen = (int)SyStrlen(zKey);
		}
		jx9MemObjStringAppend(&sKey, zKey, (sxu32)nKeylen);
		pKey = &sKey;
	}
	if( zData ){
		if( nLen < 0 ){
			/* Compute length automatically */
			nLen = (int)SyStrlen(zData);
		}
		jx9MemObjStringAppend(&sValue, zData, (sxu32)nLen);
	}
	/* Perform the insertion */
	rc = jx9HashmapInsert(&(*pMap),pKey,&sValue);
	jx9MemObjRelease(&sKey);
	jx9MemObjRelease(&sValue);
	return rc;
}
/* Forward declaration */
static sxi32 VmHttpProcessRequest(jx9_vm *pVm, const char *zRequest, int nByte);
/*
 * Configure a working virtual machine instance.
 *
 * This routine is used to configure a JX9 virtual machine obtained by a prior
 * successful call to one of the compile interface such as jx9_compile()
 * jx9_compile_v2() or jx9_compile_file().
 * The second argument to this function is an integer configuration option
 * that determines what property of the JX9 virtual machine is to be configured.
 * Subsequent arguments vary depending on the configuration option in the second
 * argument. There are many verbs but the most important are JX9_VM_CONFIG_OUTPUT, 
 * JX9_VM_CONFIG_HTTP_REQUEST and JX9_VM_CONFIG_ARGV_ENTRY.
 * Refer to the official documentation for the list of allowed verbs.
 */
JX9_PRIVATE sxi32 jx9VmConfigure(
	jx9_vm *pVm, /* Target VM */
	sxi32 nOp,   /* Configuration verb */
	va_list ap   /* Subsequent option arguments */
	)
{
	sxi32 rc = SXRET_OK;
	switch(nOp){
	case JX9_VM_CONFIG_OUTPUT: {
		ProcConsumer xConsumer = va_arg(ap, ProcConsumer);
		void *pUserData = va_arg(ap, void *);
		/* VM output consumer callback */
#ifdef UNTRUST
		if( xConsumer == 0 ){
			rc = SXERR_CORRUPT;
			break;
		}
#endif
		/* Install the output consumer */
		pVm->sVmConsumer.xConsumer = xConsumer;
		pVm->sVmConsumer.pUserData = pUserData;
		break;
							   }
	case JX9_VM_CONFIG_IMPORT_PATH: {
		/* Import path */
		  const char *zPath;
		  SyString sPath;
		  zPath = va_arg(ap, const char *);
#if defined(UNTRUST)
		  if( zPath == 0 ){
			  rc = SXERR_EMPTY;
			  break;
		  }
#endif
		  SyStringInitFromBuf(&sPath, zPath, SyStrlen(zPath));
		  /* Remove trailing slashes and backslashes */
#ifdef __WINNT__
		  SyStringTrimTrailingChar(&sPath, '\\');
#endif
		  SyStringTrimTrailingChar(&sPath, '/');
		  /* Remove leading and trailing white spaces */
		  SyStringFullTrim(&sPath);
		  if( sPath.nByte > 0 ){
			  /* Store the path in the corresponding conatiner */
			  rc = SySetPut(&pVm->aPaths, (const void *)&sPath);
		  }
		  break;
									 }
	case JX9_VM_CONFIG_ERR_REPORT:
		/* Run-Time Error report */
		pVm->bErrReport = 1;
		break;
	case JX9_VM_CONFIG_RECURSION_DEPTH:{
		/* Recursion depth */
		int nDepth = va_arg(ap, int);
		if( nDepth > 2 && nDepth < 1024 ){
			pVm->nMaxDepth = nDepth;
		}
		break;
									   }
	case JX9_VM_OUTPUT_LENGTH: {
		/* VM output length in bytes */
		sxu32 *pOut = va_arg(ap, sxu32 *);
#ifdef UNTRUST
		if( pOut == 0 ){
			rc = SXERR_CORRUPT;
			break;
		}
#endif
		*pOut = pVm->nOutputLen;
		break;
							   }
	case JX9_VM_CONFIG_CREATE_VAR: {
		/* Create a new superglobal/global variable */
		const char *zName = va_arg(ap, const char *);
		jx9_value *pValue = va_arg(ap, jx9_value *);
		SyHashEntry *pEntry;
		jx9_value *pObj;
		sxu32 nByte;
		sxu32 nIdx; 
#ifdef UNTRUST
		if( SX_EMPTY_STR(zName) || pValue == 0 ){
			rc = SXERR_CORRUPT;
			break;
		}
#endif
		nByte = SyStrlen(zName);
		/* Check if the superglobal is already installed */
		pEntry = SyHashGet(&pVm->hSuper, (const void *)zName, nByte);
		if( pEntry ){
			/* Variable already installed */
			nIdx = SX_PTR_TO_INT(pEntry->pUserData);
			/* Extract contents */
			pObj = (jx9_value *)SySetAt(&pVm->aMemObj, nIdx);
			if( pObj ){
				/* Overwrite old contents */
				jx9MemObjStore(pValue, pObj);
			}
		}else{
			/* Install a new variable */
			pObj = jx9VmReserveMemObj(&(*pVm),&nIdx);
			if( pObj == 0 ){
				rc = SXERR_MEM;
				break;
			}
			/* Copy value */
			jx9MemObjStore(pValue, pObj);
			/* Install the superglobal */
			rc = SyHashInsert(&pVm->hSuper, (const void *)zName, nByte, SX_INT_TO_PTR(nIdx));
		}
		break;
									}
	case JX9_VM_CONFIG_SERVER_ATTR:
	case JX9_VM_CONFIG_ENV_ATTR:  {
		const char *zKey   = va_arg(ap, const char *);
		const char *zValue = va_arg(ap, const char *);
		int nLen = va_arg(ap, int);
		jx9_hashmap *pMap;
		jx9_value *pValue;
		if( nOp == JX9_VM_CONFIG_ENV_ATTR ){
			/* Extract the $_ENV superglobal */
			pValue = VmExtractSuper(&(*pVm), "_ENV", sizeof("_ENV")-1);
		}else{
			/* Extract the $_SERVER superglobal */
			pValue = VmExtractSuper(&(*pVm), "_SERVER", sizeof("_SERVER")-1);
		}
		if( pValue == 0 || (pValue->iFlags & MEMOBJ_HASHMAP) == 0 ){
			/* No such entry */
			rc = SXERR_NOTFOUND;
			break;
		}
		/* Point to the hashmap */
		pMap = (jx9_hashmap *)pValue->x.pOther;
		/* Perform the insertion */
		rc = VmHashmapInsert(pMap, zKey, -1, zValue, nLen);
		break;
								   }
	case JX9_VM_CONFIG_ARGV_ENTRY:{
		/* Script arguments */
		const char *zValue = va_arg(ap, const char *);
		jx9_hashmap *pMap;
		jx9_value *pValue;
		/* Extract the $argv array */
		pValue = VmExtractSuper(&(*pVm), "argv", sizeof("argv")-1);
		if( pValue == 0 || (pValue->iFlags & MEMOBJ_HASHMAP) == 0 ){
			/* No such entry */
			rc = SXERR_NOTFOUND;
			break;
		}
		/* Point to the hashmap */
		pMap = (jx9_hashmap *)pValue->x.pOther;
		/* Perform the insertion */
		rc = VmHashmapInsert(pMap, 0, 0, zValue,-1);
		if( rc == SXRET_OK && zValue && zValue[0] != 0 ){
			if( pMap->nEntry > 1 ){
				/* Append space separator first */
				SyBlobAppend(&pVm->sArgv, (const void *)" ", sizeof(char));
			}
			SyBlobAppend(&pVm->sArgv, (const void *)zValue,SyStrlen(zValue));
		}
		break;
								  }
	case JX9_VM_CONFIG_EXEC_VALUE: {
		/* Script return value */
		jx9_value **ppValue = va_arg(ap, jx9_value **);
#ifdef UNTRUST
		if( ppValue == 0 ){
			rc = SXERR_CORRUPT;
			break;
		}
#endif
		*ppValue = &pVm->sExec;
		break;
								   }
	case JX9_VM_CONFIG_IO_STREAM: {
		/* Register an IO stream device */
		const jx9_io_stream *pStream = va_arg(ap, const jx9_io_stream *);
		/* Make sure we are dealing with a valid IO stream */
		if( pStream == 0 || pStream->zName == 0 || pStream->zName[0] == 0 ||
			pStream->xOpen == 0 || pStream->xRead == 0 ){
				/* Invalid stream */
				rc = SXERR_INVALID;
				break;
		}
		if( pVm->pDefStream == 0 && SyStrnicmp(pStream->zName, "file", sizeof("file")-1) == 0 ){
			/* Make the 'file://' stream the defaut stream device */
			pVm->pDefStream = pStream;
		}
		/* Insert in the appropriate container */
		rc = SySetPut(&pVm->aIOstream, (const void *)&pStream);
		break;
								  }
	case JX9_VM_CONFIG_EXTRACT_OUTPUT: {
		/* Point to the VM internal output consumer buffer */
		const void **ppOut = va_arg(ap, const void **);
		unsigned int *pLen = va_arg(ap, unsigned int *);
#ifdef UNTRUST
		if( ppOut == 0 || pLen == 0 ){
			rc = SXERR_CORRUPT;
			break;
		}
#endif
		*ppOut = SyBlobData(&pVm->sConsumer);
		*pLen  = SyBlobLength(&pVm->sConsumer);
		break;
									   }
	case JX9_VM_CONFIG_HTTP_REQUEST:{
		/* Raw HTTP request*/
		const char *zRequest = va_arg(ap, const char *);
		int nByte = va_arg(ap, int);
		if( SX_EMPTY_STR(zRequest) ){
			rc = SXERR_EMPTY;
			break;
		}
		if( nByte < 0 ){
			/* Compute length automatically */
			nByte = (int)SyStrlen(zRequest);
		}
		/* Process the request */
		rc = VmHttpProcessRequest(&(*pVm), zRequest, nByte);
		break;
									}
	default:
		/* Unknown configuration option */
		rc = SXERR_UNKNOWN;
		break;
	}
	return rc;
}
/* Forward declaration */
static const char * VmInstrToString(sxi32 nOp);
/*
 * This routine is used to dump JX9 bytecode instructions to a human readable
 * format.
 * The dump is redirected to the given consumer callback which is responsible
 * of consuming the generated dump perhaps redirecting it to its standard output
 * (STDOUT).
 */
static sxi32 VmByteCodeDump(
	SySet *pByteCode,       /* Bytecode container */
	ProcConsumer xConsumer, /* Dump consumer callback */
	void *pUserData         /* Last argument to xConsumer() */
	)
{
	static const char zDump[] = {
		"====================================================\n"
		"JX9 VM Dump   Copyright (C) 2012-2013 Symisc Systems\n"
		"                              http://jx9.symisc.net/\n"
		"====================================================\n"
	};
	VmInstr *pInstr, *pEnd;
	sxi32 rc = SXRET_OK;
	sxu32 n;
	/* Point to the JX9 instructions */
	pInstr = (VmInstr *)SySetBasePtr(pByteCode);
	pEnd   = &pInstr[SySetUsed(pByteCode)];
	n = 0;
	xConsumer((const void *)zDump, sizeof(zDump)-1, pUserData);
	/* Dump instructions */
	for(;;){
		if( pInstr >= pEnd ){
			/* No more instructions */
			break;
		}
		/* Format and call the consumer callback */
		rc = SyProcFormat(xConsumer, pUserData, "%s %8d %8u %#8x [%u]\n", 
			VmInstrToString(pInstr->iOp), pInstr->iP1, pInstr->iP2, 
			SX_PTR_TO_INT(pInstr->p3), n);
		if( rc != SXRET_OK ){
			/* Consumer routine request an operation abort */
			return rc;
		}
		++n;
		pInstr++; /* Next instruction in the stream */
	}
	return rc;
}
/*
 * Consume a generated run-time error message by invoking the VM output
 * consumer callback.
 */
static sxi32 VmCallErrorHandler(jx9_vm *pVm, SyBlob *pMsg)
{
	jx9_output_consumer *pCons = &pVm->sVmConsumer;
	sxi32 rc = SXRET_OK;
	/* Append a new line */
#ifdef __WINNT__
	SyBlobAppend(pMsg, "\r\n", sizeof("\r\n")-1);
#else
	SyBlobAppend(pMsg, "\n", sizeof(char));
#endif
	/* Invoke the output consumer callback */
	rc = pCons->xConsumer(SyBlobData(pMsg), SyBlobLength(pMsg), pCons->pUserData);
	/* Increment output length */
	pVm->nOutputLen += SyBlobLength(pMsg);
	
	return rc;
}
/*
 * Throw a run-time error and invoke the supplied VM output consumer callback.
 * Refer to the implementation of [jx9_context_throw_error()] for additional
 * information.
 */
JX9_PRIVATE sxi32 jx9VmThrowError(
	jx9_vm *pVm,         /* Target VM */
	SyString *pFuncName, /* Function name. NULL otherwise */
	sxi32 iErr,          /* Severity level: [i.e: Error, Warning or Notice]*/
	const char *zMessage /* Null terminated error message */
	)
{
	SyBlob *pWorker = &pVm->sWorker;
	SyString *pFile;
	char *zErr;
	sxi32 rc;
	if( !pVm->bErrReport ){
		/* Don't bother reporting errors */
		return SXRET_OK;
	}
	/* Reset the working buffer */
	SyBlobReset(pWorker);
	/* Peek the processed file if available */
	pFile = (SyString *)SySetPeek(&pVm->aFiles);
	if( pFile ){
		/* Append file name */
		SyBlobAppend(pWorker, pFile->zString, pFile->nByte);
		SyBlobAppend(pWorker, (const void *)" ", sizeof(char));
	}
	zErr = "Error: ";
	switch(iErr){
	case JX9_CTX_WARNING: zErr = "Warning: "; break;
	case JX9_CTX_NOTICE:  zErr = "Notice: ";  break;
	default:
		iErr = JX9_CTX_ERR;
		break;
	}
	SyBlobAppend(pWorker, zErr, SyStrlen(zErr));
	if( pFuncName ){
		/* Append function name first */
		SyBlobAppend(pWorker, pFuncName->zString, pFuncName->nByte);
		SyBlobAppend(pWorker, "(): ", sizeof("(): ")-1);
	}
	SyBlobAppend(pWorker, zMessage, SyStrlen(zMessage));
	/* Consume the error message */
	rc = VmCallErrorHandler(&(*pVm), pWorker);
	return rc;
}
/*
 * Format and throw a run-time error and invoke the supplied VM output consumer callback.
 * Refer to the implementation of [jx9_context_throw_error_format()] for additional
 * information.
 */
static sxi32 VmThrowErrorAp(
	jx9_vm *pVm,         /* Target VM */
	SyString *pFuncName, /* Function name. NULL otherwise */
	sxi32 iErr,          /* Severity level: [i.e: Error, Warning or Notice] */
	const char *zFormat, /* Format message */
	va_list ap           /* Variable list of arguments */
	)
{
	SyBlob *pWorker = &pVm->sWorker;
	SyString *pFile;
	char *zErr;
	sxi32 rc;
	if( !pVm->bErrReport ){
		/* Don't bother reporting errors */
		return SXRET_OK;
	}
	/* Reset the working buffer */
	SyBlobReset(pWorker);
	/* Peek the processed file if available */
	pFile = (SyString *)SySetPeek(&pVm->aFiles);
	if( pFile ){
		/* Append file name */
		SyBlobAppend(pWorker, pFile->zString, pFile->nByte);
		SyBlobAppend(pWorker, (const void *)" ", sizeof(char));
	}
	zErr = "Error: ";
	switch(iErr){
	case JX9_CTX_WARNING: zErr = "Warning: "; break;
	case JX9_CTX_NOTICE:  zErr = "Notice: ";  break;
	default:
		iErr = JX9_CTX_ERR;
		break;
	}
	SyBlobAppend(pWorker, zErr, SyStrlen(zErr));
	if( pFuncName ){
		/* Append function name first */
		SyBlobAppend(pWorker, pFuncName->zString, pFuncName->nByte);
		SyBlobAppend(pWorker, "(): ", sizeof("(): ")-1);
	}
	SyBlobFormatAp(pWorker, zFormat, ap);
	/* Consume the error message */
	rc = VmCallErrorHandler(&(*pVm), pWorker);
	return rc;
}
/*
 * Format and throw a run-time error and invoke the supplied VM output consumer callback.
 * Refer to the implementation of [jx9_context_throw_error_format()] for additional
 * information.
 * ------------------------------------
 * Simple boring wrapper function.
 * ------------------------------------
 */
static sxi32 VmErrorFormat(jx9_vm *pVm, sxi32 iErr, const char *zFormat, ...)
{
	va_list ap;
	sxi32 rc;
	va_start(ap, zFormat);
	rc = VmThrowErrorAp(&(*pVm), 0, iErr, zFormat, ap);
	va_end(ap);
	return rc;
}
/*
 * Format and throw a run-time error and invoke the supplied VM output consumer callback.
 * Refer to the implementation of [jx9_context_throw_error_format()] for additional
 * information.
 * ------------------------------------
 * Simple boring wrapper function.
 * ------------------------------------
 */
JX9_PRIVATE sxi32 jx9VmThrowErrorAp(jx9_vm *pVm, SyString *pFuncName, sxi32 iErr, const char *zFormat, va_list ap)
{
	sxi32 rc;
	rc = VmThrowErrorAp(&(*pVm), &(*pFuncName), iErr, zFormat, ap);
	return rc;
}
/* Forward declaration */
static sxi32 VmLocalExec(jx9_vm *pVm,SySet *pByteCode,jx9_value *pResult);
/*
 * Execute as much of a JX9 bytecode program as we can then return.
 *
 * [jx9VmMakeReady()] must be called before this routine in order to
 * close the program with a final OP_DONE and to set up the default
 * consumer routines and other stuff. Refer to the implementation
 * of [jx9VmMakeReady()] for additional information.
 * If the installed VM output consumer callback ever returns JX9_ABORT
 * then the program execution is halted.
 * After this routine has finished, [jx9VmRelease()] or [jx9VmReset()]
 * should be used respectively to clean up the mess that was left behind
 * or to reset the VM to it's initial state.
 */
static sxi32 VmByteCodeExec(
	jx9_vm *pVm,         /* Target VM */
	VmInstr *aInstr,     /* JX9 bytecode program */
	jx9_value *pStack,   /* Operand stack */
	int nTos,            /* Top entry in the operand stack (usually -1) */
	jx9_value *pResult  /* Store program return value here. NULL otherwise */
	)
{
	VmInstr *pInstr;
	jx9_value *pTos;
	SySet aArg;
	sxi32 pc;
	sxi32 rc;
	/* Argument container */
	SySetInit(&aArg, &pVm->sAllocator, sizeof(jx9_value *));
	if( nTos < 0 ){
		pTos = &pStack[-1];
	}else{
		pTos = &pStack[nTos];
	}
	pc = 0;
	/* Execute as much as we can */
	for(;;){
		/* Fetch the instruction to execute */
		pInstr = &aInstr[pc];
		rc = SXRET_OK;
/*
 * What follows here is a massive switch statement where each case implements a
 * separate instruction in the virtual machine.  If we follow the usual
 * indentation convention each case should be indented by 6 spaces.  But
 * that is a lot of wasted space on the left margin.  So the code within
 * the switch statement will break with convention and be flush-left.
 */
		switch(pInstr->iOp){
/*
 * DONE: P1 * * 
 *
 * Program execution completed: Clean up the mess left behind
 * and return immediately.
 */
case JX9_OP_DONE:
	if( pInstr->iP1 ){
#ifdef UNTRUST
		if( pTos < pStack ){
			goto Abort;
		}
#endif
		if( pResult ){
			/* Execution result */
			jx9MemObjStore(pTos, pResult);
		}		
		VmPopOperand(&pTos, 1);
	}
	goto Done;
/*
 * HALT: P1 * *
 *
 * Program execution aborted: Clean up the mess left behind
 * and abort immediately.
 */
case JX9_OP_HALT:
	if( pInstr->iP1 ){
#ifdef UNTRUST
		if( pTos < pStack ){
			goto Abort;
		}
#endif
		if( pTos->iFlags & MEMOBJ_STRING ){
			if( SyBlobLength(&pTos->sBlob) > 0 ){
				/* Output the exit message */
				pVm->sVmConsumer.xConsumer(SyBlobData(&pTos->sBlob), SyBlobLength(&pTos->sBlob), 
					pVm->sVmConsumer.pUserData);
					/* Increment output length */
					pVm->nOutputLen += SyBlobLength(&pTos->sBlob);
			}
		}else if(pTos->iFlags & MEMOBJ_INT ){
			/* Record exit status */
			pVm->iExitStatus = (sxi32)pTos->x.iVal;
		}
		VmPopOperand(&pTos, 1);
	}
	goto Abort;
/*
 * JMP: * P2 *
 *
 * Unconditional jump: The next instruction executed will be 
 * the one at index P2 from the beginning of the program.
 */
case JX9_OP_JMP:
	pc = pInstr->iP2 - 1;
	break;
/*
 * JZ: P1 P2 *
 *
 * Take the jump if the top value is zero (FALSE jump).Pop the top most
 * entry in the stack if P1 is zero. 
 */
case JX9_OP_JZ:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	/* Get a boolean value */
	if((pTos->iFlags & MEMOBJ_BOOL) == 0 ){
		jx9MemObjToBool(pTos);
	}
	if( !pTos->x.iVal ){
		/* Take the jump */
		pc = pInstr->iP2 - 1;
	}
	if( !pInstr->iP1 ){
		VmPopOperand(&pTos, 1);
	}
	break;
/*
 * JNZ: P1 P2 *
 *
 * Take the jump if the top value is not zero (TRUE jump).Pop the top most
 * entry in the stack if P1 is zero.
 */
case JX9_OP_JNZ:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	/* Get a boolean value */
	if((pTos->iFlags & MEMOBJ_BOOL) == 0 ){
		jx9MemObjToBool(pTos);
	}
	if( pTos->x.iVal ){
		/* Take the jump */
		pc = pInstr->iP2 - 1;
	}
	if( !pInstr->iP1 ){
		VmPopOperand(&pTos, 1);
	}
	break;
/*
 * NOOP: * * *
 *
 * Do nothing. This instruction is often useful as a jump
 * destination.
 */
case JX9_OP_NOOP:
	break;
/*
 * POP: P1 * *
 *
 * Pop P1 elements from the operand stack.
 */
case JX9_OP_POP: {
	sxi32 n = pInstr->iP1;
	if( &pTos[-n+1] < pStack ){
		/* TICKET 1433-51 Stack underflow must be handled at run-time */
		n = (sxi32)(pTos - pStack);
	}
	VmPopOperand(&pTos, n);
	break;
				 }
/*
 * CVT_INT: * * *
 *
 * Force the top of the stack to be an integer.
 */
case JX9_OP_CVT_INT:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	if((pTos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pTos);
	}
	/* Invalidate any prior representation */
	MemObjSetType(pTos, MEMOBJ_INT);
	break;
/*
 * CVT_REAL: * * *
 *
 * Force the top of the stack to be a real.
 */
case JX9_OP_CVT_REAL:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	if((pTos->iFlags & MEMOBJ_REAL) == 0 ){
		jx9MemObjToReal(pTos);
	}
	/* Invalidate any prior representation */
	MemObjSetType(pTos, MEMOBJ_REAL);
	break;
/*
 * CVT_STR: * * *
 *
 * Force the top of the stack to be a string.
 */
case JX9_OP_CVT_STR:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	if( (pTos->iFlags & MEMOBJ_STRING) == 0 ){
		jx9MemObjToString(pTos);
	}
	break;
/*
 * CVT_BOOL: * * *
 *
 * Force the top of the stack to be a boolean.
 */
case JX9_OP_CVT_BOOL:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	if( (pTos->iFlags & MEMOBJ_BOOL) == 0 ){
		jx9MemObjToBool(pTos);
	}
	break;
/*
 * CVT_NULL: * * *
 *
 * Nullify the top of the stack.
 */
case JX9_OP_CVT_NULL:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	jx9MemObjRelease(pTos);
	break;
/*
 * CVT_NUMC: * * *
 *
 * Force the top of the stack to be a numeric type (integer, real or both).
 */
case JX9_OP_CVT_NUMC:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	/* Force a numeric cast */
	jx9MemObjToNumeric(pTos);
	break;
/*
 * CVT_ARRAY: * * *
 *
 * Force the top of the stack to be a hashmap aka 'array'.
 */
case JX9_OP_CVT_ARRAY:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	/* Force a hashmap cast */
	rc = jx9MemObjToHashmap(pTos);
	if( rc != SXRET_OK ){
		/* Not so fatal, emit a simple warning */
		jx9VmThrowError(&(*pVm), 0, JX9_CTX_WARNING, 
			"JX9 engine is running out of memory while performing an array cast");
	}
	break;
/*
 * LOADC P1 P2 *
 *
 * Load a constant [i.e: JX9_EOL, JX9_OS, __TIME__, ...] indexed at P2 in the constant pool.
 * If P1 is set, then this constant is candidate for expansion via user installable callbacks.
 */
case JX9_OP_LOADC: {
	jx9_value *pObj;
	/* Reserve a room */
	pTos++;
	if( (pObj = (jx9_value *)SySetAt(&pVm->aLitObj, pInstr->iP2)) != 0 ){
		if( pInstr->iP1 == 1 && SyBlobLength(&pObj->sBlob) <= 64 ){
			SyHashEntry *pEntry;
			/* Candidate for expansion via user defined callbacks */
			pEntry = SyHashGet(&pVm->hConstant, SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob));
			if( pEntry ){
				jx9_constant *pCons = (jx9_constant *)pEntry->pUserData;
				/* Set a NULL default value */
				MemObjSetType(pTos, MEMOBJ_NULL);
				SyBlobReset(&pTos->sBlob);
				/* Invoke the callback and deal with the expanded value */
				pCons->xExpand(pTos, pCons->pUserData);
				/* Mark as constant */
				pTos->nIdx = SXU32_HIGH;
				break;
			}
		}
		jx9MemObjLoad(pObj, pTos);
	}else{
		/* Set a NULL value */
		MemObjSetType(pTos, MEMOBJ_NULL);
	}
	/* Mark as constant */
	pTos->nIdx = SXU32_HIGH;
	break;
				  }
/*
 * LOAD: P1 * P3
 *
 * Load a variable where it's name is taken from the top of the stack or
 * from the P3 operand.
 * If P1 is set, then perform a lookup only.In other words do not create
 * the variable if non existent and push the NULL constant instead.
 */
case JX9_OP_LOAD:{
	jx9_value *pObj;
	SyString sName;
	if( pInstr->p3 == 0 ){
		/* Take the variable name from the top of the stack */
#ifdef UNTRUST
		if( pTos < pStack ){
			goto Abort;
		}
#endif
		/* Force a string cast */
		if( (pTos->iFlags & MEMOBJ_STRING) == 0 ){
			jx9MemObjToString(pTos);
		}
		SyStringInitFromBuf(&sName, SyBlobData(&pTos->sBlob), SyBlobLength(&pTos->sBlob));
	}else{
		SyStringInitFromBuf(&sName, pInstr->p3, SyStrlen((const char *)pInstr->p3));
		/* Reserve a room for the target object */
		pTos++;
	}
	/* Extract the requested memory object */
	pObj = VmExtractMemObj(&(*pVm), &sName, pInstr->p3 ? FALSE : TRUE, pInstr->iP1 != 1);
	if( pObj == 0 ){
		if( pInstr->iP1 ){
			/* Variable not found, load NULL */
			if( !pInstr->p3 ){
				jx9MemObjRelease(pTos);
			}else{
				MemObjSetType(pTos, MEMOBJ_NULL);
			}
			pTos->nIdx = SXU32_HIGH; /* Mark as constant */
			break;
		}else{
			/* Fatal error */
			VmErrorFormat(&(*pVm), JX9_CTX_ERR, "Fatal, JX9 engine is running out of memory while loading variable '%z'", &sName);
			goto Abort;
		}
	}
	/* Load variable contents */
	jx9MemObjLoad(pObj, pTos);
	pTos->nIdx = pObj->nIdx;
	break;
				   }
/*
 * LOAD_MAP P1 * *
 *
 * Allocate a new empty hashmap (array in the JX9 jargon) and push it on the stack.
 * If the P1 operand is greater than zero then pop P1 elements from the
 * stack and insert them (key => value pair) in the new hashmap.
 */
case JX9_OP_LOAD_MAP: {
	jx9_hashmap *pMap;
	int is_json_object; /* TRUE if we are dealing with a JSON object */
	int iIncr = 1;
	/* Allocate a new hashmap instance */
	pMap = jx9NewHashmap(&(*pVm), 0, 0);
	if( pMap == 0 ){
		VmErrorFormat(&(*pVm), JX9_CTX_ERR, 
			"Fatal, JX9 engine is running out of memory while loading JSON array/object at instruction #:%d", pc);
		goto Abort;
	}
	is_json_object = 0;
	if( pInstr->iP2 ){
		/* JSON object, record that */
		pMap->iFlags |= HASHMAP_JSON_OBJECT;
		is_json_object = 1;
		iIncr = 2;
	}
	if( pInstr->iP1 > 0 ){
		jx9_value *pEntry = &pTos[-pInstr->iP1+1]; /* Point to the first entry */
		/* Perform the insertion */
		while( pEntry <= pTos ){
			/* Standard insertion */
			jx9HashmapInsert(pMap, 
				is_json_object ? pEntry : 0 /* Automatic index assign */,
				is_json_object ? &pEntry[1] : pEntry
			);			
			/* Next pair on the stack */
			pEntry += iIncr;
		}
		/* Pop P1 elements */
		VmPopOperand(&pTos, pInstr->iP1);
	}
	/* Push the hashmap */
	pTos++;
	pTos->x.pOther = pMap;
	MemObjSetType(pTos, MEMOBJ_HASHMAP);
	break;
					  }
/*
 * LOAD_IDX: P1 P2 *
 *
 * Load a hasmap entry where it's index (either numeric or string) is taken
 * from the stack.
 * If the index does not refer to a valid element, then push the NULL constant
 * instead.
 */
case JX9_OP_LOAD_IDX: {
	jx9_hashmap_node *pNode = 0; /* cc warning */
	jx9_hashmap *pMap = 0;
	jx9_value *pIdx;
	pIdx = 0;
	if( pInstr->iP1 == 0 ){
		if( !pInstr->iP2){
			/* No available index, load NULL */
			if( pTos >= pStack ){
				jx9MemObjRelease(pTos);
			}else{
				/* TICKET 1433-020: Empty stack */
				pTos++;
				MemObjSetType(pTos, MEMOBJ_NULL);
				pTos->nIdx = SXU32_HIGH;
			}
			/* Emit a notice */
			jx9VmThrowError(&(*pVm), 0, JX9_CTX_NOTICE, 
				"JSON Array/Object: Attempt to access an undefined member, JX9 is loading NULL");
			break;
		}
	}else{
		pIdx = pTos;
		pTos--;
	}
	if( pTos->iFlags & MEMOBJ_STRING ){
		/* String access */
		if( pIdx ){
			sxu32 nOfft;
			if( (pIdx->iFlags & MEMOBJ_INT) == 0 ){
				/* Force an int cast */
				jx9MemObjToInteger(pIdx);
			}
			nOfft = (sxu32)pIdx->x.iVal;
			if( nOfft >= SyBlobLength(&pTos->sBlob) ){
				/* Invalid offset, load null */
				jx9MemObjRelease(pTos);
			}else{
				const char *zData = (const char *)SyBlobData(&pTos->sBlob);
				int c = zData[nOfft];
				jx9MemObjRelease(pTos);
				MemObjSetType(pTos, MEMOBJ_STRING);
				SyBlobAppend(&pTos->sBlob, (const void *)&c, sizeof(char));
			}
		}else{
			/* No available index, load NULL */
			MemObjSetType(pTos, MEMOBJ_NULL);
		}
		break;
	}
	if( pInstr->iP2 && (pTos->iFlags & MEMOBJ_HASHMAP) == 0 ){
		if( pTos->nIdx != SXU32_HIGH ){
			jx9_value *pObj;
			if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pTos->nIdx)) != 0 ){
				jx9MemObjToHashmap(pObj);
				jx9MemObjLoad(pObj, pTos);
			}
		}
	}
	rc = SXERR_NOTFOUND; /* Assume the index is invalid */
	if( pTos->iFlags & MEMOBJ_HASHMAP ){
		/* Point to the hashmap */
		pMap = (jx9_hashmap *)pTos->x.pOther;
		if( pIdx ){
			/* Load the desired entry */
			rc = jx9HashmapLookup(pMap, pIdx, &pNode);
		}
		if( rc != SXRET_OK && pInstr->iP2 ){
			/* Create a new empty entry */
			rc = jx9HashmapInsert(pMap, pIdx, 0);
			if( rc == SXRET_OK ){
				/* Point to the last inserted entry */
				pNode = pMap->pLast;
			}
		}
	}
	if( pIdx ){
		jx9MemObjRelease(pIdx);
	}
	if( rc == SXRET_OK ){
		/* Load entry contents */
		if( pMap->iRef < 2 ){
			/* TICKET 1433-42: Array will be deleted shortly, so we will make a copy
			 * of the entry value, rather than pointing to it.
			 */
			pTos->nIdx = SXU32_HIGH;
			jx9HashmapExtractNodeValue(pNode, pTos, TRUE);
		}else{
			pTos->nIdx = pNode->nValIdx;
			jx9HashmapExtractNodeValue(pNode, pTos, FALSE);
			jx9HashmapUnref(pMap);
		}
	}else{
		/* No such entry, load NULL */
		jx9MemObjRelease(pTos);
		pTos->nIdx = SXU32_HIGH;
	}
	break;
					  }
/*
 * STORE * P2 P3
 *
 * Perform a store (Assignment) operation.
 */
case JX9_OP_STORE: {
	jx9_value *pObj;
	SyString sName;
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	if( pInstr->iP2 ){
		sxu32 nIdx;
		/* Member store operation */
		nIdx = pTos->nIdx;
		VmPopOperand(&pTos, 1);
		if( nIdx == SXU32_HIGH ){
			jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, 
				"Cannot perform assignment on a constant object attribute, JX9 is loading NULL");
			pTos->nIdx = SXU32_HIGH;
		}else{
			/* Point to the desired memory object */
			pObj = (jx9_value *)SySetAt(&pVm->aMemObj, nIdx);
			if( pObj ){
				/* Perform the store operation */
				jx9MemObjStore(pTos, pObj);
			}
		}
		break;
	}else if( pInstr->p3 == 0 ){
		/* Take the variable name from the next on the stack */
		if( (pTos->iFlags & MEMOBJ_STRING) == 0 ){
			/* Force a string cast */
			jx9MemObjToString(pTos);
		}
		SyStringInitFromBuf(&sName, SyBlobData(&pTos->sBlob), SyBlobLength(&pTos->sBlob));
		pTos--;
#ifdef UNTRUST
		if( pTos < pStack  ){
			goto Abort;
		}
#endif
	}else{
		SyStringInitFromBuf(&sName, pInstr->p3, SyStrlen((const char *)pInstr->p3));
	}
	/* Extract the desired variable and if not available dynamically create it */
	pObj = VmExtractMemObj(&(*pVm), &sName, pInstr->p3 ? FALSE : TRUE, TRUE);
	if( pObj == 0 ){
		VmErrorFormat(&(*pVm), JX9_CTX_ERR, 
			"Fatal, JX9 engine is running out of memory while loading variable '%z'", &sName);
		goto Abort;
	}
	if( !pInstr->p3 ){
		jx9MemObjRelease(&pTos[1]);
	}
	/* Perform the store operation */
	jx9MemObjStore(pTos, pObj);
	break;
				   }
/*
 * STORE_IDX:   P1 * P3
 *
 * Perfrom a store operation an a hashmap entry.
 */
case JX9_OP_STORE_IDX: {
	jx9_hashmap *pMap = 0; /* cc  warning */
	jx9_value *pKey;
	sxu32 nIdx;
	if( pInstr->iP1 ){
		/* Key is next on stack */
		pKey = pTos;
		pTos--;
	}else{
		pKey = 0;
	}
	nIdx = pTos->nIdx;
	if( pTos->iFlags & MEMOBJ_HASHMAP ){
		/* Hashmap already loaded */
		pMap = (jx9_hashmap *)pTos->x.pOther;
		if( pMap->iRef < 2 ){
			/* TICKET 1433-48: Prevent garbage collection */
			pMap->iRef = 2;
		}
	}else{
		jx9_value *pObj;
		pObj = (jx9_value *)SySetAt(&pVm->aMemObj, nIdx);
		if( pObj == 0 ){
			if( pKey ){
			  jx9MemObjRelease(pKey);
			}
			VmPopOperand(&pTos, 1);
			break;
		}
		/* Phase#1: Load the array */
		if( (pObj->iFlags & MEMOBJ_STRING)  ){
			VmPopOperand(&pTos, 1);
			if( (pTos->iFlags&MEMOBJ_STRING) == 0 ){
				/* Force a string cast */
				jx9MemObjToString(pTos);
			}
			if( pKey == 0 ){
				/* Append string */
				if( SyBlobLength(&pTos->sBlob) > 0 ){
					SyBlobAppend(&pObj->sBlob, SyBlobData(&pTos->sBlob), SyBlobLength(&pTos->sBlob));
				}
			}else{
				sxu32 nOfft;
				if((pKey->iFlags & MEMOBJ_INT)){
					/* Force an int cast */
					jx9MemObjToInteger(pKey);
				}
				nOfft = (sxu32)pKey->x.iVal;
				if( nOfft < SyBlobLength(&pObj->sBlob) && SyBlobLength(&pTos->sBlob) > 0 ){
					const char *zBlob = (const char *)SyBlobData(&pTos->sBlob);
					char *zData = (char *)SyBlobData(&pObj->sBlob);
					zData[nOfft] = zBlob[0];
				}else{
					if( SyBlobLength(&pTos->sBlob) >= sizeof(char) ){
						/* Perform an append operation */
						SyBlobAppend(&pObj->sBlob, SyBlobData(&pTos->sBlob), sizeof(char));
					}
				}
			}
			if( pKey ){
			  jx9MemObjRelease(pKey);
			}
			break;
		}else if( (pObj->iFlags & MEMOBJ_HASHMAP) == 0 ){
			/* Force a hashmap cast  */
			rc = jx9MemObjToHashmap(pObj);
			if( rc != SXRET_OK ){
				VmErrorFormat(&(*pVm), JX9_CTX_ERR, "Fatal, JX9 engine is running out of memory while creating a new array");
				goto Abort;
			}
		}
		pMap = (jx9_hashmap *)pObj->x.pOther;
	}
	VmPopOperand(&pTos, 1);
	/* Phase#2: Perform the insertion */
	jx9HashmapInsert(pMap, pKey, pTos);	
	if( pKey ){
		jx9MemObjRelease(pKey);
	}
	break;
					   }
/*
 * INCR: P1 * *
 *
 * Force a numeric cast and increment the top of the stack by 1.
 * If the P1 operand is set then perform a duplication of the top of
 * the stack and increment after that.
 */
case JX9_OP_INCR:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	if( (pTos->iFlags & (MEMOBJ_HASHMAP|MEMOBJ_RES)) == 0 ){
		if( pTos->nIdx != SXU32_HIGH ){
			jx9_value *pObj;
			if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pTos->nIdx)) != 0 ){
				/* Force a numeric cast */
				jx9MemObjToNumeric(pObj);
				if( pObj->iFlags & MEMOBJ_REAL ){
					pObj->x.rVal++;
					/* Try to get an integer representation */
					jx9MemObjTryInteger(pTos);
				}else{
					pObj->x.iVal++;
					MemObjSetType(pTos, MEMOBJ_INT);
				}
				if( pInstr->iP1 ){
					/* Pre-icrement */
					jx9MemObjStore(pObj, pTos);
				}
			}
		}else{
			if( pInstr->iP1 ){
				/* Force a numeric cast */
				jx9MemObjToNumeric(pTos);
				/* Pre-increment */
				if( pTos->iFlags & MEMOBJ_REAL ){
					pTos->x.rVal++;
					/* Try to get an integer representation */
					jx9MemObjTryInteger(pTos);
				}else{
					pTos->x.iVal++;
					MemObjSetType(pTos, MEMOBJ_INT);
				}
			}
		}
	}
	break;
/*
 * DECR: P1 * *
 *
 * Force a numeric cast and decrement the top of the stack by 1.
 * If the P1 operand is set then perform a duplication of the top of the stack 
 * and decrement after that.
 */
case JX9_OP_DECR:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	if( (pTos->iFlags & (MEMOBJ_HASHMAP|MEMOBJ_RES|MEMOBJ_NULL)) == 0 ){
		/* Force a numeric cast */
		jx9MemObjToNumeric(pTos);
		if( pTos->nIdx != SXU32_HIGH ){
			jx9_value *pObj;
			if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pTos->nIdx)) != 0 ){
				/* Force a numeric cast */
				jx9MemObjToNumeric(pObj);
				if( pObj->iFlags & MEMOBJ_REAL ){
					pObj->x.rVal--;
					/* Try to get an integer representation */
					jx9MemObjTryInteger(pTos);
				}else{
					pObj->x.iVal--;
					MemObjSetType(pTos, MEMOBJ_INT);
				}
				if( pInstr->iP1 ){
					/* Pre-icrement */
					jx9MemObjStore(pObj, pTos);
				}
			}
		}else{
			if( pInstr->iP1 ){
				/* Pre-increment */
				if( pTos->iFlags & MEMOBJ_REAL ){
					pTos->x.rVal--;
					/* Try to get an integer representation */
					jx9MemObjTryInteger(pTos);
				}else{
					pTos->x.iVal--;
					MemObjSetType(pTos, MEMOBJ_INT);
				}
			}
		}
	}
	break;
/*
 * UMINUS: * * *
 *
 * Perform a unary minus operation.
 */
case JX9_OP_UMINUS:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	/* Force a numeric (integer, real or both) cast */
	jx9MemObjToNumeric(pTos);
	if( pTos->iFlags & MEMOBJ_REAL ){
		pTos->x.rVal = -pTos->x.rVal;
	}
	if( pTos->iFlags & MEMOBJ_INT ){
		pTos->x.iVal = -pTos->x.iVal;
	}
	break;				   
/*
 * UPLUS: * * *
 *
 * Perform a unary plus operation.
 */
case JX9_OP_UPLUS:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	/* Force a numeric (integer, real or both) cast */
	jx9MemObjToNumeric(pTos);
	if( pTos->iFlags & MEMOBJ_REAL ){
		pTos->x.rVal = +pTos->x.rVal;
	}
	if( pTos->iFlags & MEMOBJ_INT ){
		pTos->x.iVal = +pTos->x.iVal;
	}
	break;
/*
 * OP_LNOT: * * *
 *
 * Interpret the top of the stack as a boolean value.  Replace it
 * with its complement.
 */
case JX9_OP_LNOT:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	/* Force a boolean cast */
	if( (pTos->iFlags & MEMOBJ_BOOL) == 0 ){
		jx9MemObjToBool(pTos);
	}
	pTos->x.iVal = !pTos->x.iVal;
	break;
/*
 * OP_BITNOT: * * *
 *
 * Interpret the top of the stack as an value.Replace it
 * with its ones-complement.
 */
case JX9_OP_BITNOT:
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	/* Force an integer cast */
	if( (pTos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pTos);
	}
	pTos->x.iVal = ~pTos->x.iVal;
	break;
/* OP_MUL * * *
 * OP_MUL_STORE * * *
 *
 * Pop the top two elements from the stack, multiply them together, 
 * and push the result back onto the stack.
 */
case JX9_OP_MUL:
case JX9_OP_MUL_STORE: {
	jx9_value *pNos = &pTos[-1];
	/* Force the operand to be numeric */
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	jx9MemObjToNumeric(pTos);
	jx9MemObjToNumeric(pNos);
	/* Perform the requested operation */
	if( MEMOBJ_REAL & (pTos->iFlags|pNos->iFlags) ){
		/* Floating point arithemic */
		jx9_real a, b, r;
		if( (pTos->iFlags & MEMOBJ_REAL) == 0 ){
			jx9MemObjToReal(pTos);
		}
		if( (pNos->iFlags & MEMOBJ_REAL) == 0 ){
			jx9MemObjToReal(pNos);
		}
		a = pNos->x.rVal;
		b = pTos->x.rVal;
		r = a * b;
		/* Push the result */
		pNos->x.rVal = r;
		MemObjSetType(pNos, MEMOBJ_REAL);
		/* Try to get an integer representation */
		jx9MemObjTryInteger(pNos);
	}else{
		/* Integer arithmetic */
		sxi64 a, b, r;
		a = pNos->x.iVal;
		b = pTos->x.iVal;
		r = a * b;
		/* Push the result */
		pNos->x.iVal = r;
		MemObjSetType(pNos, MEMOBJ_INT);
	}
	if( pInstr->iOp == JX9_OP_MUL_STORE ){
		jx9_value *pObj;
		if( pTos->nIdx == SXU32_HIGH ){
			jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "Cannot perform assignment on a constant object attribute");
		}else if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pTos->nIdx)) != 0 ){
			jx9MemObjStore(pNos, pObj);
		}
	}
	VmPopOperand(&pTos, 1);
	break;
				 }
/* OP_ADD * * *
 *
 * Pop the top two elements from the stack, add them together, 
 * and push the result back onto the stack.
 */
case JX9_OP_ADD:{
	jx9_value *pNos = &pTos[-1];
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Perform the addition */
	jx9MemObjAdd(pNos, pTos, FALSE);
	VmPopOperand(&pTos, 1);
	break;
				}
/*
 * OP_ADD_STORE * * *
 *
 * Pop the top two elements from the stack, add them together, 
 * and push the result back onto the stack.
 */
case JX9_OP_ADD_STORE:{
	jx9_value *pNos = &pTos[-1];
	jx9_value *pObj;
	sxu32 nIdx;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Perform the addition */
	nIdx = pTos->nIdx;
	jx9MemObjAdd(pTos, pNos, TRUE);
	/* Peform the store operation */
	if( nIdx == SXU32_HIGH ){
		jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "Cannot perform assignment on a constant object attribute");
	}else if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, nIdx)) != 0 ){
		jx9MemObjStore(pTos, pObj);
	}
	/* Ticket 1433-35: Perform a stack dup */
	jx9MemObjStore(pTos, pNos);
	VmPopOperand(&pTos, 1);
	break;
				}
/* OP_SUB * * *
 *
 * Pop the top two elements from the stack, subtract the
 * first (what was next on the stack) from the second (the
 * top of the stack) and push the result back onto the stack.
 */
case JX9_OP_SUB: {
	jx9_value *pNos = &pTos[-1];
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	if( MEMOBJ_REAL & (pTos->iFlags|pNos->iFlags) ){
		/* Floating point arithemic */
		jx9_real a, b, r;
		if( (pTos->iFlags & MEMOBJ_REAL) == 0 ){
			jx9MemObjToReal(pTos);
		}
		if( (pNos->iFlags & MEMOBJ_REAL) == 0 ){
			jx9MemObjToReal(pNos);
		}
		a = pNos->x.rVal;
		b = pTos->x.rVal;
		r = a - b; 
		/* Push the result */
		pNos->x.rVal = r;
		MemObjSetType(pNos, MEMOBJ_REAL);
		/* Try to get an integer representation */
		jx9MemObjTryInteger(pNos);
	}else{
		/* Integer arithmetic */
		sxi64 a, b, r;
		a = pNos->x.iVal;
		b = pTos->x.iVal;
		r = a - b;
		/* Push the result */
		pNos->x.iVal = r;
		MemObjSetType(pNos, MEMOBJ_INT);
	}
	VmPopOperand(&pTos, 1);
	break;
				 }
/* OP_SUB_STORE * * *
 *
 * Pop the top two elements from the stack, subtract the
 * first (what was next on the stack) from the second (the
 * top of the stack) and push the result back onto the stack.
 */
case JX9_OP_SUB_STORE: {
	jx9_value *pNos = &pTos[-1];
	jx9_value *pObj;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	if( MEMOBJ_REAL & (pTos->iFlags|pNos->iFlags) ){
		/* Floating point arithemic */
		jx9_real a, b, r;
		if( (pTos->iFlags & MEMOBJ_REAL) == 0 ){
			jx9MemObjToReal(pTos);
		}
		if( (pNos->iFlags & MEMOBJ_REAL) == 0 ){
			jx9MemObjToReal(pNos);
		}
		a = pTos->x.rVal;
		b = pNos->x.rVal;
		r = a - b; 
		/* Push the result */
		pNos->x.rVal = r;
		MemObjSetType(pNos, MEMOBJ_REAL);
		/* Try to get an integer representation */
		jx9MemObjTryInteger(pNos);
	}else{
		/* Integer arithmetic */
		sxi64 a, b, r;
		a = pTos->x.iVal;
		b = pNos->x.iVal;
		r = a - b;
		/* Push the result */
		pNos->x.iVal = r;
		MemObjSetType(pNos, MEMOBJ_INT);
	}
	if( pTos->nIdx == SXU32_HIGH ){
		jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "Cannot perform assignment on a constant object attribute");
	}else if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pTos->nIdx)) != 0 ){
		jx9MemObjStore(pNos, pObj);
	}
	VmPopOperand(&pTos, 1);
	break;
				 }

/*
 * OP_MOD * * *
 *
 * Pop the top two elements from the stack, divide the
 * first (what was next on the stack) from the second (the
 * top of the stack) and push the remainder after division 
 * onto the stack.
 * Note: Only integer arithemtic is allowed.
 */
case JX9_OP_MOD:{
	jx9_value *pNos = &pTos[-1];
	sxi64 a, b, r;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force the operands to be integer */
	if( (pTos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pTos);
	}
	if( (pNos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pNos);
	}
	/* Perform the requested operation */
	a = pNos->x.iVal;
	b = pTos->x.iVal;
	if( b == 0 ){
		r = 0;
		VmErrorFormat(&(*pVm), JX9_CTX_ERR, "Division by zero %qd%%0", a);
		/* goto Abort; */
	}else{
		r = a%b;
	}
	/* Push the result */
	pNos->x.iVal = r;
	MemObjSetType(pNos, MEMOBJ_INT);
	VmPopOperand(&pTos, 1);
	break;
				}
/*
 * OP_MOD_STORE * * *
 *
 * Pop the top two elements from the stack, divide the
 * first (what was next on the stack) from the second (the
 * top of the stack) and push the remainder after division 
 * onto the stack.
 * Note: Only integer arithemtic is allowed.
 */
case JX9_OP_MOD_STORE: {
	jx9_value *pNos = &pTos[-1];
	jx9_value *pObj;
	sxi64 a, b, r;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force the operands to be integer */
	if( (pTos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pTos);
	}
	if( (pNos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pNos);
	}
	/* Perform the requested operation */
	a = pTos->x.iVal;
	b = pNos->x.iVal;
	if( b == 0 ){
		r = 0;
		VmErrorFormat(&(*pVm), JX9_CTX_ERR, "Division by zero %qd%%0", a);
		/* goto Abort; */
	}else{
		r = a%b;
	}
	/* Push the result */
	pNos->x.iVal = r;
	MemObjSetType(pNos, MEMOBJ_INT);
	if( pTos->nIdx == SXU32_HIGH ){
		jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "Cannot perform assignment on a constant object attribute");
	}else if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pTos->nIdx)) != 0 ){
		jx9MemObjStore(pNos, pObj);
	}
	VmPopOperand(&pTos, 1);
	break;
				}
/*
 * OP_DIV * * *
 * 
 * Pop the top two elements from the stack, divide the
 * first (what was next on the stack) from the second (the
 * top of the stack) and push the result onto the stack.
 * Note: Only floating point arithemtic is allowed.
 */
case JX9_OP_DIV:{
	jx9_value *pNos = &pTos[-1];
	jx9_real a, b, r;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force the operands to be real */
	if( (pTos->iFlags & MEMOBJ_REAL) == 0 ){
		jx9MemObjToReal(pTos);
	}
	if( (pNos->iFlags & MEMOBJ_REAL) == 0 ){
		jx9MemObjToReal(pNos);
	}
	/* Perform the requested operation */
	a = pNos->x.rVal;
	b = pTos->x.rVal;
	if( b == 0 ){
		/* Division by zero */
		r = 0;
		jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "Division by zero");
		/* goto Abort; */
	}else{
		r = a/b;
		/* Push the result */
		pNos->x.rVal = r;
		MemObjSetType(pNos, MEMOBJ_REAL);
		/* Try to get an integer representation */
		jx9MemObjTryInteger(pNos);
	}
	VmPopOperand(&pTos, 1);
	break;
				}
/*
 * OP_DIV_STORE * * *
 * 
 * Pop the top two elements from the stack, divide the
 * first (what was next on the stack) from the second (the
 * top of the stack) and push the result onto the stack.
 * Note: Only floating point arithemtic is allowed.
 */
case JX9_OP_DIV_STORE:{
	jx9_value *pNos = &pTos[-1];
	jx9_value *pObj;
	jx9_real a, b, r;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force the operands to be real */
	if( (pTos->iFlags & MEMOBJ_REAL) == 0 ){
		jx9MemObjToReal(pTos);
	}
	if( (pNos->iFlags & MEMOBJ_REAL) == 0 ){
		jx9MemObjToReal(pNos);
	}
	/* Perform the requested operation */
	a = pTos->x.rVal;
	b = pNos->x.rVal;
	if( b == 0 ){
		/* Division by zero */
		r = 0;
		VmErrorFormat(&(*pVm), JX9_CTX_ERR, "Division by zero %qd/0", a);
		/* goto Abort; */
	}else{
		r = a/b;
		/* Push the result */
		pNos->x.rVal = r;
		MemObjSetType(pNos, MEMOBJ_REAL);
		/* Try to get an integer representation */
		jx9MemObjTryInteger(pNos);
	}
	if( pTos->nIdx == SXU32_HIGH ){
		jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "Cannot perform assignment on a constant object attribute");
	}else if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pTos->nIdx)) != 0 ){
		jx9MemObjStore(pNos, pObj);
	}
	VmPopOperand(&pTos, 1);
	break;
				}
/* OP_BAND * * *
 *
 * Pop the top two elements from the stack.  Convert both elements
 * to integers.  Push back onto the stack the bit-wise AND of the
 * two elements.
*/
/* OP_BOR * * *
 *
 * Pop the top two elements from the stack.  Convert both elements
 * to integers.  Push back onto the stack the bit-wise OR of the
 * two elements.
 */
/* OP_BXOR * * *
 *
 * Pop the top two elements from the stack.  Convert both elements
 * to integers.  Push back onto the stack the bit-wise XOR of the
 * two elements.
 */
case JX9_OP_BAND:
case JX9_OP_BOR:
case JX9_OP_BXOR:{
	jx9_value *pNos = &pTos[-1];
	sxi64 a, b, r;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force the operands to be integer */
	if( (pTos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pTos);
	}
	if( (pNos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pNos);
	}
	/* Perform the requested operation */
	a = pNos->x.iVal;
	b = pTos->x.iVal;
	switch(pInstr->iOp){
	case JX9_OP_BOR_STORE:
	case JX9_OP_BOR:  r = a|b; break;
	case JX9_OP_BXOR_STORE:
	case JX9_OP_BXOR: r = a^b; break;
	case JX9_OP_BAND_STORE:
	case JX9_OP_BAND:
	default:          r = a&b; break;
	}
	/* Push the result */
	pNos->x.iVal = r;
	MemObjSetType(pNos, MEMOBJ_INT);
	VmPopOperand(&pTos, 1);
	break;
				 }
/* OP_BAND_STORE * * * 
 *
 * Pop the top two elements from the stack.  Convert both elements
 * to integers.  Push back onto the stack the bit-wise AND of the
 * two elements.
*/
/* OP_BOR_STORE * * *
 *
 * Pop the top two elements from the stack.  Convert both elements
 * to integers.  Push back onto the stack the bit-wise OR of the
 * two elements.
 */
/* OP_BXOR_STORE * * *
 *
 * Pop the top two elements from the stack.  Convert both elements
 * to integers.  Push back onto the stack the bit-wise XOR of the
 * two elements.
 */
case JX9_OP_BAND_STORE:
case JX9_OP_BOR_STORE:
case JX9_OP_BXOR_STORE:{
	jx9_value *pNos = &pTos[-1];
	jx9_value *pObj;
	sxi64 a, b, r;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force the operands to be integer */
	if( (pTos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pTos);
	}
	if( (pNos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pNos);
	}
	/* Perform the requested operation */
	a = pTos->x.iVal;
	b = pNos->x.iVal;
	switch(pInstr->iOp){
	case JX9_OP_BOR_STORE:
	case JX9_OP_BOR:  r = a|b; break;
	case JX9_OP_BXOR_STORE:
	case JX9_OP_BXOR: r = a^b; break;
	case JX9_OP_BAND_STORE:
	case JX9_OP_BAND:
	default:          r = a&b; break;
	}
	/* Push the result */
	pNos->x.iVal = r;
	MemObjSetType(pNos, MEMOBJ_INT);
	if( pTos->nIdx == SXU32_HIGH ){
		jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "Cannot perform assignment on a constant object attribute");
	}else if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pTos->nIdx)) != 0 ){
		jx9MemObjStore(pNos, pObj);
	}
	VmPopOperand(&pTos, 1);
	break;
				 }
/* OP_SHL * * *
 *
 * Pop the top two elements from the stack.  Convert both elements
 * to integers.  Push back onto the stack the second element shifted
 * left by N bits where N is the top element on the stack.
 * Note: Only integer arithmetic is allowed.
 */
/* OP_SHR * * *
 *
 * Pop the top two elements from the stack.  Convert both elements
 * to integers.  Push back onto the stack the second element shifted
 * right by N bits where N is the top element on the stack.
 * Note: Only integer arithmetic is allowed.
 */
case JX9_OP_SHL:
case JX9_OP_SHR: {
	jx9_value *pNos = &pTos[-1];
	sxi64 a, r;
	sxi32 b;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force the operands to be integer */
	if( (pTos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pTos);
	}
	if( (pNos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pNos);
	}
	/* Perform the requested operation */
	a = pNos->x.iVal;
	b = (sxi32)pTos->x.iVal;
	if( pInstr->iOp == JX9_OP_SHL ){
		r = a << b;
	}else{
		r = a >> b;
	}
	/* Push the result */
	pNos->x.iVal = r;
	MemObjSetType(pNos, MEMOBJ_INT);
	VmPopOperand(&pTos, 1);
	break;
				 }
/*  OP_SHL_STORE * * *
 *
 * Pop the top two elements from the stack.  Convert both elements
 * to integers.  Push back onto the stack the second element shifted
 * left by N bits where N is the top element on the stack.
 * Note: Only integer arithmetic is allowed.
 */
/* OP_SHR_STORE * * *
 *
 * Pop the top two elements from the stack.  Convert both elements
 * to integers.  Push back onto the stack the second element shifted
 * right by N bits where N is the top element on the stack.
 * Note: Only integer arithmetic is allowed.
 */
case JX9_OP_SHL_STORE:
case JX9_OP_SHR_STORE: {
	jx9_value *pNos = &pTos[-1];
	jx9_value *pObj;
	sxi64 a, r;
	sxi32 b;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force the operands to be integer */
	if( (pTos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pTos);
	}
	if( (pNos->iFlags & MEMOBJ_INT) == 0 ){
		jx9MemObjToInteger(pNos);
	}
	/* Perform the requested operation */
	a = pTos->x.iVal;
	b = (sxi32)pNos->x.iVal;
	if( pInstr->iOp == JX9_OP_SHL_STORE ){
		r = a << b;
	}else{
		r = a >> b;
	}
	/* Push the result */
	pNos->x.iVal = r;
	MemObjSetType(pNos, MEMOBJ_INT);
	if( pTos->nIdx == SXU32_HIGH ){
		jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "Cannot perform assignment on a constant object attribute");
	}else if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pTos->nIdx)) != 0 ){
		jx9MemObjStore(pNos, pObj);
	}
	VmPopOperand(&pTos, 1);
	break;
				 }
/* CAT:  P1 * *
 *
 * Pop P1 elements from the stack. Concatenate them togeher and push the result
 * back.
 */
case JX9_OP_CAT:{
	jx9_value *pNos, *pCur;
	if( pInstr->iP1 < 1 ){
		pNos = &pTos[-1];
	}else{
		pNos = &pTos[-pInstr->iP1+1];
	}
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force a string cast */
	if( (pNos->iFlags & MEMOBJ_STRING) == 0 ){
		jx9MemObjToString(pNos);
	}
	pCur = &pNos[1];
	while( pCur <= pTos ){
		if( (pCur->iFlags & MEMOBJ_STRING) == 0 ){
			jx9MemObjToString(pCur);
		}
		/* Perform the concatenation */
		if( SyBlobLength(&pCur->sBlob) > 0 ){
			jx9MemObjStringAppend(pNos, (const char *)SyBlobData(&pCur->sBlob), SyBlobLength(&pCur->sBlob));
		}
		SyBlobRelease(&pCur->sBlob);
		pCur++;
	}
	pTos = pNos;
	break;
				}
/*  CAT_STORE: * * *
 *
 * Pop two elements from the stack. Concatenate them togeher and push the result
 * back.
 */
case JX9_OP_CAT_STORE:{
	jx9_value *pNos = &pTos[-1];
	jx9_value *pObj;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	if((pTos->iFlags & MEMOBJ_STRING) == 0 ){
		/* Force a string cast */
		jx9MemObjToString(pTos);
	}
	if((pNos->iFlags & MEMOBJ_STRING) == 0 ){
		/* Force a string cast */
		jx9MemObjToString(pNos);
	}
	/* Perform the concatenation (Reverse order) */
	if( SyBlobLength(&pNos->sBlob) > 0 ){
		jx9MemObjStringAppend(pTos, (const char *)SyBlobData(&pNos->sBlob), SyBlobLength(&pNos->sBlob));
	}
	/* Perform the store operation */
	if( pTos->nIdx == SXU32_HIGH ){
		jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "Cannot perform assignment on a constant object attribute");
	}else if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pTos->nIdx)) != 0 ){
		jx9MemObjStore(pTos, pObj);
	}
	jx9MemObjStore(pTos, pNos);
	VmPopOperand(&pTos, 1);
	break;
				}
/* OP_AND: * * *
 *
 * Pop two values off the stack.  Take the logical AND of the
 * two values and push the resulting boolean value back onto the
 * stack. 
 */
/* OP_OR: * * *
 *
 * Pop two values off the stack.  Take the logical OR of the
 * two values and push the resulting boolean value back onto the
 * stack. 
 */
case JX9_OP_LAND:
case JX9_OP_LOR: {
	jx9_value *pNos = &pTos[-1];
	sxi32 v1, v2;    /* 0==TRUE, 1==FALSE, 2==UNKNOWN or NULL */
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force a boolean cast */
	if((pTos->iFlags & MEMOBJ_BOOL) == 0 ){
		jx9MemObjToBool(pTos);
	}
	if((pNos->iFlags & MEMOBJ_BOOL) == 0 ){
		jx9MemObjToBool(pNos);
	}
	v1 = pNos->x.iVal == 0 ? 1 : 0;
	v2 = pTos->x.iVal == 0 ? 1 : 0;
	if( pInstr->iOp == JX9_OP_LAND ){
		static const unsigned char and_logic[] = { 0, 1, 2, 1, 1, 1, 2, 1, 2 };
		v1 = and_logic[v1*3+v2];
	}else{
		static const unsigned char or_logic[] = { 0, 0, 0, 0, 1, 2, 0, 2, 2 };
		v1 = or_logic[v1*3+v2];
	}
	if( v1 == 2 ){
		v1 = 1;
	}
	VmPopOperand(&pTos, 1);
	pTos->x.iVal = v1 == 0 ? 1 : 0;
	MemObjSetType(pTos, MEMOBJ_BOOL);
	break;
				 }
/* OP_LXOR: * * *
 *
 * Pop two values off the stack. Take the logical XOR of the
 * two values and push the resulting boolean value back onto the
 * stack.
 * According to the JX9 language reference manual:
 *  $a xor $b is evaluated to TRUE if either $a or $b is 
 *  TRUE, but not both.
 */
case JX9_OP_LXOR:{
	jx9_value *pNos = &pTos[-1];
	sxi32 v = 0;
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	/* Force a boolean cast */
	if((pTos->iFlags & MEMOBJ_BOOL) == 0 ){
		jx9MemObjToBool(pTos);
	}
	if((pNos->iFlags & MEMOBJ_BOOL) == 0 ){
		jx9MemObjToBool(pNos);
	}
	if( (pNos->x.iVal && !pTos->x.iVal) || (pTos->x.iVal && !pNos->x.iVal) ){
		v = 1;
	}
	VmPopOperand(&pTos, 1);
	pTos->x.iVal = v;
	MemObjSetType(pTos, MEMOBJ_BOOL);
	break;
				 }
/* OP_EQ P1 P2 P3
 *
 * Pop the top two elements from the stack.  If they are equal, then
 * jump to instruction P2.  Otherwise, continue to the next instruction.
 * If P2 is zero, do not jump.  Instead, push a boolean 1 (TRUE) onto the
 * stack if the jump would have been taken, or a 0 (FALSE) if not. 
 */
/* OP_NEQ P1 P2 P3
 *
 * Pop the top two elements from the stack. If they are not equal, then
 * jump to instruction P2. Otherwise, continue to the next instruction.
 * If P2 is zero, do not jump.  Instead, push a boolean 1 (TRUE) onto the
 * stack if the jump would have been taken, or a 0 (FALSE) if not.
 */
case JX9_OP_EQ:
case JX9_OP_NEQ: {
	jx9_value *pNos = &pTos[-1];
	/* Perform the comparison and act accordingly */
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	rc = jx9MemObjCmp(pNos, pTos, FALSE, 0);
	if( pInstr->iOp == JX9_OP_EQ ){
		rc = rc == 0;
	}else{
		rc = rc != 0;
	}
	VmPopOperand(&pTos, 1);
	if( !pInstr->iP2 ){
		/* Push comparison result without taking the jump */
		jx9MemObjRelease(pTos);
		pTos->x.iVal = rc;
		/* Invalidate any prior representation */
		MemObjSetType(pTos, MEMOBJ_BOOL);
	}else{
		if( rc ){
			/* Jump to the desired location */
			pc = pInstr->iP2 - 1;
			VmPopOperand(&pTos, 1);
		}
	}
	break;
				 }
/* OP_TEQ P1 P2 *
 *
 * Pop the top two elements from the stack. If they have the same type and are equal
 * then jump to instruction P2. Otherwise, continue to the next instruction.
 * If P2 is zero, do not jump. Instead, push a boolean 1 (TRUE) onto the
 * stack if the jump would have been taken, or a 0 (FALSE) if not. 
 */
case JX9_OP_TEQ: {
	jx9_value *pNos = &pTos[-1];
	/* Perform the comparison and act accordingly */
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	rc = jx9MemObjCmp(pNos, pTos, TRUE, 0) == 0;
	VmPopOperand(&pTos, 1);
	if( !pInstr->iP2 ){
		/* Push comparison result without taking the jump */
		jx9MemObjRelease(pTos);
		pTos->x.iVal = rc;
		/* Invalidate any prior representation */
		MemObjSetType(pTos, MEMOBJ_BOOL);
	}else{
		if( rc ){
			/* Jump to the desired location */
			pc = pInstr->iP2 - 1;
			VmPopOperand(&pTos, 1);
		}
	}
	break;
				 }
/* OP_TNE P1 P2 *
 *
 * Pop the top two elements from the stack.If they are not equal an they are not 
 * of the same type, then jump to instruction P2. Otherwise, continue to the next 
 * instruction.
 * If P2 is zero, do not jump. Instead, push a boolean 1 (TRUE) onto the
 * stack if the jump would have been taken, or a 0 (FALSE) if not.
 * 
 */
case JX9_OP_TNE: {
	jx9_value *pNos = &pTos[-1];
	/* Perform the comparison and act accordingly */
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	rc = jx9MemObjCmp(pNos, pTos, TRUE, 0) != 0;
	VmPopOperand(&pTos, 1);
	if( !pInstr->iP2 ){
		/* Push comparison result without taking the jump */
		jx9MemObjRelease(pTos);
		pTos->x.iVal = rc;
		/* Invalidate any prior representation */
		MemObjSetType(pTos, MEMOBJ_BOOL);
	}else{
		if( rc ){
			/* Jump to the desired location */
			pc = pInstr->iP2 - 1;
			VmPopOperand(&pTos, 1);
		}
	}
	break;
				 }
/* OP_LT P1 P2 P3
 *
 * Pop the top two elements from the stack. If the second element (the top of stack)
 * is less than the first (next on stack), then jump to instruction P2.Otherwise
 * continue to the next instruction. In other words, jump if pNos<pTos.
 * If P2 is zero, do not jump.Instead, push a boolean 1 (TRUE) onto the
 * stack if the jump would have been taken, or a 0 (FALSE) if not.
 * 
 */
/* OP_LE P1 P2 P3
 *
 * Pop the top two elements from the stack. If the second element (the top of stack)
 * is less than or equal to the first (next on stack), then jump to instruction P2.
 * Otherwise continue to the next instruction. In other words, jump if pNos<pTos.
 * If P2 is zero, do not jump.Instead, push a boolean 1 (TRUE) onto the
 * stack if the jump would have been taken, or a 0 (FALSE) if not.
 * 
 */
case JX9_OP_LT:
case JX9_OP_LE: {
	jx9_value *pNos = &pTos[-1];
	/* Perform the comparison and act accordingly */
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	rc = jx9MemObjCmp(pNos, pTos, FALSE, 0);
	if( pInstr->iOp == JX9_OP_LE ){
		rc = rc < 1;
	}else{
		rc = rc < 0;
	}
	VmPopOperand(&pTos, 1);
	if( !pInstr->iP2 ){
		/* Push comparison result without taking the jump */
		jx9MemObjRelease(pTos);
		pTos->x.iVal = rc;
		/* Invalidate any prior representation */
		MemObjSetType(pTos, MEMOBJ_BOOL);
	}else{
		if( rc ){
			/* Jump to the desired location */
			pc = pInstr->iP2 - 1;
			VmPopOperand(&pTos, 1);
		}
	}
	break;
				}
/* OP_GT P1 P2 P3
 *
 * Pop the top two elements from the stack. If the second element (the top of stack)
 * is greater than the first (next on stack), then jump to instruction P2.Otherwise
 * continue to the next instruction. In other words, jump if pNos<pTos.
 * If P2 is zero, do not jump.Instead, push a boolean 1 (TRUE) onto the
 * stack if the jump would have been taken, or a 0 (FALSE) if not.
 * 
 */
/* OP_GE P1 P2 P3
 *
 * Pop the top two elements from the stack. If the second element (the top of stack)
 * is greater than or equal to the first (next on stack), then jump to instruction P2.
 * Otherwise continue to the next instruction. In other words, jump if pNos<pTos.
 * If P2 is zero, do not jump.Instead, push a boolean 1 (TRUE) onto the
 * stack if the jump would have been taken, or a 0 (FALSE) if not.
 * 
 */
case JX9_OP_GT:
case JX9_OP_GE: {
	jx9_value *pNos = &pTos[-1];
	/* Perform the comparison and act accordingly */
#ifdef UNTRUST
	if( pNos < pStack ){
		goto Abort;
	}
#endif
	rc = jx9MemObjCmp(pNos, pTos, FALSE, 0);
	if( pInstr->iOp == JX9_OP_GE ){
		rc = rc >= 0;
	}else{
		rc = rc > 0;
	}
	VmPopOperand(&pTos, 1);
	if( !pInstr->iP2 ){
		/* Push comparison result without taking the jump */
		jx9MemObjRelease(pTos);
		pTos->x.iVal = rc;
		/* Invalidate any prior representation */
		MemObjSetType(pTos, MEMOBJ_BOOL);
	}else{
		if( rc ){
			/* Jump to the desired location */
			pc = pInstr->iP2 - 1;
			VmPopOperand(&pTos, 1);
		}
	}
	break;
				}
/*
 * OP_FOREACH_INIT * P2 P3
 * Prepare a foreach step.
 */
case JX9_OP_FOREACH_INIT: {
	jx9_foreach_info *pInfo = (jx9_foreach_info *)pInstr->p3;
	void *pName;
#ifdef UNTRUST
	if( pTos < pStack ){
		goto Abort;
	}
#endif
	if( SyStringLength(&pInfo->sValue) < 1 ){
		/* Take the variable name from the top of the stack */
		if( (pTos->iFlags & MEMOBJ_STRING) == 0 ){
			/* Force a string cast */
			jx9MemObjToString(pTos);
		}
		/* Duplicate name */
		if( SyBlobLength(&pTos->sBlob) > 0 ){
			pName = SyMemBackendDup(&pVm->sAllocator, SyBlobData(&pTos->sBlob), SyBlobLength(&pTos->sBlob));
			SyStringInitFromBuf(&pInfo->sValue, pName, SyBlobLength(&pTos->sBlob));
		}
		VmPopOperand(&pTos, 1);
	}
	if( (pInfo->iFlags & JX9_4EACH_STEP_KEY) && SyStringLength(&pInfo->sKey) < 1 ){
		if( (pTos->iFlags & MEMOBJ_STRING) == 0 ){
			/* Force a string cast */
			jx9MemObjToString(pTos);
		}
		/* Duplicate name */
		if( SyBlobLength(&pTos->sBlob) > 0 ){
			pName = SyMemBackendDup(&pVm->sAllocator, SyBlobData(&pTos->sBlob), SyBlobLength(&pTos->sBlob));
			SyStringInitFromBuf(&pInfo->sKey, pName, SyBlobLength(&pTos->sBlob));
		}
		VmPopOperand(&pTos, 1);
	}
	/* Make sure we are dealing with a hashmap [i.e. JSON array or object ]*/
	if( (pTos->iFlags & (MEMOBJ_HASHMAP)) == 0 || SyStringLength(&pInfo->sValue) < 1 ){
		/* Jump out of the loop */
		if( (pTos->iFlags & MEMOBJ_NULL) == 0 ){
			jx9VmThrowError(&(*pVm), 0, JX9_CTX_WARNING,
				"Invalid argument supplied for the foreach statement, expecting JSON array or object instance");
		}
		pc = pInstr->iP2 - 1;
	}else{
		jx9_foreach_step *pStep;
		pStep = (jx9_foreach_step *)SyMemBackendPoolAlloc(&pVm->sAllocator, sizeof(jx9_foreach_step));
		if( pStep == 0 ){
			jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "JX9 is running out of memory while preparing the 'foreach' step");
			/* Jump out of the loop */
			pc = pInstr->iP2 - 1;
		}else{
			/* Zero the structure */
			SyZero(pStep, sizeof(jx9_foreach_step));
			/* Prepare the step */
			pStep->iFlags = pInfo->iFlags;
			if( pTos->iFlags & MEMOBJ_HASHMAP ){
				jx9_hashmap *pMap = (jx9_hashmap *)pTos->x.pOther;
				/* Reset the internal loop cursor */
				jx9HashmapResetLoopCursor(pMap);
				/* Mark the step */
				pStep->pMap = pMap;
				pMap->iRef++;
			}
		}
		if( SXRET_OK != SySetPut(&pInfo->aStep, (const void *)&pStep) ){
			jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, "JX9 is running out of memory while preparing the 'foreach' step");
			SyMemBackendPoolFree(&pVm->sAllocator, pStep);
			/* Jump out of the loop */
			pc = pInstr->iP2 - 1;
		}
	}
	VmPopOperand(&pTos, 1);
	break;
						  }
/*
 * OP_FOREACH_STEP * P2 P3
 * Perform a foreach step. Jump to P2 at the end of the step.
 */
case JX9_OP_FOREACH_STEP: {
	jx9_foreach_info *pInfo = (jx9_foreach_info *)pInstr->p3;
	jx9_foreach_step **apStep, *pStep;
	jx9_hashmap_node *pNode;
	jx9_hashmap *pMap;
	jx9_value *pValue;
	/* Peek the last step */
	apStep = (jx9_foreach_step **)SySetBasePtr(&pInfo->aStep);
	pStep = apStep[SySetUsed(&pInfo->aStep) - 1];
	pMap = pStep->pMap;
	/* Extract the current node value */
	pNode = jx9HashmapGetNextEntry(pMap);
	if( pNode == 0 ){
		/* No more entry to process */
		pc = pInstr->iP2 - 1; /* Jump to this destination */
		/* Automatically reset the loop cursor */
		jx9HashmapResetLoopCursor(pMap);
		/* Cleanup the mess left behind */
		SyMemBackendPoolFree(&pVm->sAllocator, pStep);
		SySetPop(&pInfo->aStep);
		jx9HashmapUnref(pMap);
	}else{
		if( (pStep->iFlags & JX9_4EACH_STEP_KEY) && SyStringLength(&pInfo->sKey) > 0 ){
			jx9_value *pKey = VmExtractMemObj(&(*pVm), &pInfo->sKey, FALSE, TRUE);
			if( pKey ){
				jx9HashmapExtractNodeKey(pNode, pKey);
			}
		}
		/* Make a copy of the entry value */
		pValue = VmExtractMemObj(&(*pVm), &pInfo->sValue, FALSE, TRUE);
		if( pValue ){
			jx9HashmapExtractNodeValue(pNode, pValue, TRUE);
		}
	}
	break;
						  }
/*
 * OP_MEMBER P1 P2
 * Load JSON object entry on the stack.
 */
case JX9_OP_MEMBER: {
	jx9_hashmap_node *pNode = 0; /* cc warning */
	jx9_hashmap *pMap = 0;
	jx9_value *pIdx;
	pIdx = pTos;
	pTos--;
	rc = SXERR_NOTFOUND; /* Assume the index is invalid */
	if( pTos->iFlags & MEMOBJ_HASHMAP ){
		/* Point to the hashmap */
		pMap = (jx9_hashmap *)pTos->x.pOther;
		/* Load the desired entry */
		rc = jx9HashmapLookup(pMap, pIdx, &pNode);
	}
	jx9MemObjRelease(pIdx);	
	if( rc == SXRET_OK ){
		/* Load entry contents */
		if( pMap->iRef < 2 ){
			/* TICKET 1433-42: Array will be deleted shortly, so we will make a copy
			 * of the entry value, rather than pointing to it.
			 */
			pTos->nIdx = SXU32_HIGH;
			jx9HashmapExtractNodeValue(pNode, pTos, TRUE);
		}else{
			pTos->nIdx = pNode->nValIdx;
			jx9HashmapExtractNodeValue(pNode, pTos, FALSE);
			jx9HashmapUnref(pMap);
		}
	}else{
		/* No such entry, load NULL */
		jx9MemObjRelease(pTos);
		pTos->nIdx = SXU32_HIGH;
	}
	break;
					}
/*
 * OP_SWITCH * * P3
 *  This is the bytecode implementation of the complex switch() JX9 construct.
 */
case JX9_OP_SWITCH: {
	jx9_switch *pSwitch = (jx9_switch *)pInstr->p3;
	jx9_case_expr *aCase, *pCase;
	jx9_value sValue, sCaseValue; 
	sxu32 n, nEntry;
#ifdef UNTRUST
	if( pSwitch == 0 || pTos < pStack ){
		goto Abort;
	}
#endif
	/* Point to the case table  */
	aCase = (jx9_case_expr *)SySetBasePtr(&pSwitch->aCaseExpr);
	nEntry = SySetUsed(&pSwitch->aCaseExpr);
	/* Select the appropriate case block to execute */
	jx9MemObjInit(pVm, &sValue);
	jx9MemObjInit(pVm, &sCaseValue);
	for( n = 0 ; n < nEntry ; ++n ){
		pCase = &aCase[n];
		jx9MemObjLoad(pTos, &sValue);
		/* Execute the case expression first */
		VmLocalExec(pVm,&pCase->aByteCode, &sCaseValue);
		/* Compare the two expression */
		rc = jx9MemObjCmp(&sValue, &sCaseValue, FALSE, 0);
		jx9MemObjRelease(&sValue);
		jx9MemObjRelease(&sCaseValue);
		if( rc == 0 ){
			/* Value match, jump to this block */
			pc = pCase->nStart - 1;
			break;
		}
	}
	VmPopOperand(&pTos, 1);
	if( n >= nEntry ){
		/* No approprite case to execute, jump to the default case */
		if( pSwitch->nDefault > 0 ){
			pc = pSwitch->nDefault - 1;
		}else{
			/* No default case, jump out of this switch */
			pc = pSwitch->nOut - 1;
		}
	}
	break;
					}
/*
 * OP_UPLINK P1 * *
 * Link a variable to the top active VM frame. 
 * This is used to implement the 'uplink' JX9 construct.
 */
case JX9_OP_UPLINK: {
	if( pVm->pFrame->pParent ){
		jx9_value *pLink = &pTos[-pInstr->iP1+1];
		SyString sName;
		/* Perform the link */
		while( pLink <= pTos ){
			if((pLink->iFlags & MEMOBJ_STRING) == 0 ){
				/* Force a string cast */
				jx9MemObjToString(pLink);
			}
			SyStringInitFromBuf(&sName, SyBlobData(&pLink->sBlob), SyBlobLength(&pLink->sBlob));
			if( sName.nByte > 0 ){
				VmFrameLink(&(*pVm), &sName);
			}
			pLink++;
		}
	}
	VmPopOperand(&pTos, pInstr->iP1);
	break;
					}
/*
 * OP_CALL P1 * *
 *  Call a JX9 or a foreign function and push the return value of the called
 *  function on the stack.
 */
case JX9_OP_CALL: {
	jx9_value *pArg = &pTos[-pInstr->iP1];
	SyHashEntry *pEntry;
	SyString sName;
	/* Extract function name */
	if( (pTos->iFlags & MEMOBJ_STRING) == 0 ){
		/* Raise exception: Invalid function name */
		VmErrorFormat(&(*pVm), JX9_CTX_WARNING, "Invalid function name, JX9 is returning NULL.");
		/* Pop given arguments */
		if( pInstr->iP1 > 0 ){
			VmPopOperand(&pTos, pInstr->iP1);
		}
		/* Assume a null return value so that the program continue it's execution normally */
		jx9MemObjRelease(pTos);
		break;
	}
	SyStringInitFromBuf(&sName, SyBlobData(&pTos->sBlob), SyBlobLength(&pTos->sBlob));
	/* Check for a compiled function first */
	pEntry = SyHashGet(&pVm->hFunction, (const void *)sName.zString, sName.nByte);
	if( pEntry ){
		jx9_vm_func_arg *aFormalArg;
		jx9_value *pFrameStack;
		jx9_vm_func *pVmFunc;
		VmFrame *pFrame;
		jx9_value *pObj;
		VmSlot sArg;
		sxu32 n;
		pVmFunc = (jx9_vm_func *)pEntry->pUserData;
		/* Check The recursion limit */
		if( pVm->nRecursionDepth > pVm->nMaxDepth ){
			VmErrorFormat(&(*pVm), JX9_CTX_ERR, 
				"Recursion limit reached while invoking user function '%z', JX9 will set a NULL return value", 
				&pVmFunc->sName);
			/* Pop given arguments */
			if( pInstr->iP1 > 0 ){
				VmPopOperand(&pTos, pInstr->iP1);
			}
			/* Assume a null return value so that the program continue it's execution normally */
			jx9MemObjRelease(pTos);
			break;
		}
		if( pVmFunc->pNextName ){
			/* Function is candidate for overloading, select the appropriate function to call */
			pVmFunc = VmOverload(&(*pVm), pVmFunc, pArg, (int)(pTos-pArg));
		}
		/* Extract the formal argument set */
		aFormalArg = (jx9_vm_func_arg *)SySetBasePtr(&pVmFunc->aArgs);
		/* Create a new VM frame  */
		rc = VmEnterFrame(&(*pVm),pVmFunc,&pFrame);
		if( rc != SXRET_OK ){
			/* Raise exception: Out of memory */
			VmErrorFormat(&(*pVm), JX9_CTX_ERR, 
				"JX9 is running out of memory while calling function '%z', JX9 is returning NULL.", 
				&pVmFunc->sName);
			/* Pop given arguments */
			if( pInstr->iP1 > 0 ){
				VmPopOperand(&pTos, pInstr->iP1);
			}
			/* Assume a null return value so that the program continue it's execution normally */
			jx9MemObjRelease(pTos);
			break;
		}
		if( SySetUsed(&pVmFunc->aStatic) > 0 ){
			jx9_vm_func_static_var *pStatic, *aStatic;
			/* Install static variables */
			aStatic = (jx9_vm_func_static_var *)SySetBasePtr(&pVmFunc->aStatic);
			for( n = 0 ; n < SySetUsed(&pVmFunc->aStatic) ; ++n ){
				pStatic = &aStatic[n];
				if( pStatic->nIdx == SXU32_HIGH ){
					/* Initialize the static variables */
					pObj = VmReserveMemObj(&(*pVm), &pStatic->nIdx);
					if( pObj ){
						/* Assume a NULL initialization value */
						jx9MemObjInit(&(*pVm), pObj);
						if( SySetUsed(&pStatic->aByteCode) > 0 ){
							/* Evaluate initialization expression (Any complex expression) */
							VmLocalExec(&(*pVm), &pStatic->aByteCode, pObj);
						}
						pObj->nIdx = pStatic->nIdx;
					}else{
						continue;
					}
				}
				/* Install in the current frame */
				SyHashInsert(&pFrame->hVar, SyStringData(&pStatic->sName), SyStringLength(&pStatic->sName), 
					SX_INT_TO_PTR(pStatic->nIdx));
			}
		}
		/* Push arguments in the local frame */
		n = 0;
		while( pArg < pTos ){
			if( n < SySetUsed(&pVmFunc->aArgs) ){
				if( (pArg->iFlags & MEMOBJ_NULL) && SySetUsed(&aFormalArg[n].aByteCode) > 0 ){
					/* NULL values are redirected to default arguments */
					rc = VmLocalExec(&(*pVm), &aFormalArg[n].aByteCode, pArg);
					if( rc == JX9_ABORT ){
						goto Abort;
					}
				}
				/* Make sure the given arguments are of the correct type */
				if( aFormalArg[n].nType > 0 ){
				 if( ((pArg->iFlags & aFormalArg[n].nType) == 0) ){
						ProcMemObjCast xCast = jx9MemObjCastMethod(aFormalArg[n].nType);
						/* Cast to the desired type */
						if( xCast ){
							xCast(pArg);
						}
					}
				}
				/* Pass by value, make a copy of the given argument */
				pObj = VmExtractMemObj(&(*pVm), &aFormalArg[n].sName, FALSE, TRUE);
			}else{
				char zName[32];
				SyString sName;
				/* Set a dummy name */
				sName.nByte = SyBufferFormat(zName, sizeof(zName), "[%u]apArg", n);
				sName.zString = zName;
				/* Annonymous argument */
				pObj = VmExtractMemObj(&(*pVm), &sName, TRUE, TRUE);
			}
			if( pObj ){
				jx9MemObjStore(pArg, pObj);
				/* Insert argument index  */
				sArg.nIdx = pObj->nIdx;
				sArg.pUserData = 0;
				SySetPut(&pFrame->sArg, (const void *)&sArg);
			}
			jx9MemObjRelease(pArg);
			pArg++;
			++n;
		}
		/* Process default values */
		while( n < SySetUsed(&pVmFunc->aArgs) ){
			if( SySetUsed(&aFormalArg[n].aByteCode) > 0 ){
				pObj = VmExtractMemObj(&(*pVm), &aFormalArg[n].sName, FALSE, TRUE);
				if( pObj ){
					/* Evaluate the default value and extract it's result */
					rc = VmLocalExec(&(*pVm), &aFormalArg[n].aByteCode, pObj);
					if( rc == JX9_ABORT ){
						goto Abort;
					}
					/* Insert argument index */
					sArg.nIdx = pObj->nIdx;
					sArg.pUserData = 0;
					SySetPut(&pFrame->sArg, (const void *)&sArg);
					/* Make sure the default argument is of the correct type */
					if( aFormalArg[n].nType > 0 && ((pObj->iFlags & aFormalArg[n].nType) == 0) ){
						ProcMemObjCast xCast = jx9MemObjCastMethod(aFormalArg[n].nType);
						/* Cast to the desired type */
						xCast(pObj);
					}
				}
			}
			++n;
		}
		/* Pop arguments, function name from the operand stack and assume the function 
		 * does not return anything.
		 */
		jx9MemObjRelease(pTos);
		pTos = &pTos[-pInstr->iP1];
		/* Allocate a new operand stack and evaluate the function body */
		pFrameStack = VmNewOperandStack(&(*pVm), SySetUsed(&pVmFunc->aByteCode));
		if( pFrameStack == 0 ){
			/* Raise exception: Out of memory */
			VmErrorFormat(&(*pVm), JX9_CTX_ERR, "JX9 is running out of memory while calling function '%z', JX9 is returning NULL.", 
				&pVmFunc->sName);
			if( pInstr->iP1 > 0 ){
				VmPopOperand(&pTos, pInstr->iP1);
			}
			break;
		}
		/* Increment nesting level */
		pVm->nRecursionDepth++;
		/* Execute function body */
		rc = VmByteCodeExec(&(*pVm), (VmInstr *)SySetBasePtr(&pVmFunc->aByteCode), pFrameStack, -1, pTos);
		/* Decrement nesting level */
		pVm->nRecursionDepth--;
		/* Free the operand stack */
		SyMemBackendFree(&pVm->sAllocator, pFrameStack);
		/* Leave the frame */
		VmLeaveFrame(&(*pVm));
		if( rc == JX9_ABORT ){
			/* Abort processing immeditaley */
			goto Abort;
		}
	}else{
		jx9_user_func *pFunc; 
		jx9_context sCtx;
		jx9_value sRet;
		/* Look for an installed foreign function */
		pEntry = SyHashGet(&pVm->hHostFunction, (const void *)sName.zString, sName.nByte);
		if( pEntry == 0 ){
			/* Call to undefined function */
			VmErrorFormat(&(*pVm), JX9_CTX_WARNING, "Call to undefined function '%z', JX9 is returning NULL.", &sName);
			/* Pop given arguments */
			if( pInstr->iP1 > 0 ){
				VmPopOperand(&pTos, pInstr->iP1);
			}
			/* Assume a null return value so that the program continue it's execution normally */
			jx9MemObjRelease(pTos);
			break;
		}
		pFunc = (jx9_user_func *)pEntry->pUserData;
		/* Start collecting function arguments */
		SySetReset(&aArg);
		while( pArg < pTos ){
			SySetPut(&aArg, (const void *)&pArg);
			pArg++;
		}
		/* Assume a null return value */
		jx9MemObjInit(&(*pVm), &sRet);
		/* Init the call context */
		VmInitCallContext(&sCtx, &(*pVm), pFunc, &sRet, 0);
		/* Call the foreign function */
		rc = pFunc->xFunc(&sCtx, (int)SySetUsed(&aArg), (jx9_value **)SySetBasePtr(&aArg));
		/* Release the call context */
		VmReleaseCallContext(&sCtx);
		if( rc == JX9_ABORT ){
			goto Abort;
		}
		if( pInstr->iP1 > 0 ){
			/* Pop function name and arguments */
			VmPopOperand(&pTos, pInstr->iP1);
		}
		/* Save foreign function return value */
		jx9MemObjStore(&sRet, pTos);
		jx9MemObjRelease(&sRet);
	}
	break;
				  }
/*
 * OP_CONSUME: P1 * *
 * Consume (Invoke the installed VM output consumer callback) and POP P1 elements from the stack.
 */
case JX9_OP_CONSUME: {
	jx9_output_consumer *pCons = &pVm->sVmConsumer;
	jx9_value *pCur, *pOut = pTos;

	pOut = &pTos[-pInstr->iP1 + 1];
	pCur = pOut;
	/* Start the consume process  */
	while( pOut <= pTos ){
		/* Force a string cast */
		if( (pOut->iFlags & MEMOBJ_STRING) == 0 ){
			jx9MemObjToString(pOut);
		}
		if( SyBlobLength(&pOut->sBlob) > 0 ){
			/*SyBlobNullAppend(&pOut->sBlob);*/
			/* Invoke the output consumer callback */
			rc = pCons->xConsumer(SyBlobData(&pOut->sBlob), SyBlobLength(&pOut->sBlob), pCons->pUserData);
			/* Increment output length */
			pVm->nOutputLen += SyBlobLength(&pOut->sBlob);
			SyBlobRelease(&pOut->sBlob);
			if( rc == SXERR_ABORT ){
				/* Output consumer callback request an operation abort. */
				goto Abort;
			}
		}
		pOut++;
	}
	pTos = &pCur[-1];
	break;
					 }

		} /* Switch() */
		pc++; /* Next instruction in the stream */
	} /* For(;;) */
Done:
	SySetRelease(&aArg);
	return SXRET_OK;
Abort:
	SySetRelease(&aArg);
	while( pTos >= pStack ){
		jx9MemObjRelease(pTos);
		pTos--;
	}
	return JX9_ABORT;
}
/*
 * Execute as much of a local JX9 bytecode program as we can then return.
 * This function is a wrapper around [VmByteCodeExec()].
 * See block-comment on that function for additional information.
 */
static sxi32 VmLocalExec(jx9_vm *pVm, SySet *pByteCode,jx9_value *pResult)
{
	jx9_value *pStack;
	sxi32 rc;
	/* Allocate a new operand stack */
	pStack = VmNewOperandStack(&(*pVm), SySetUsed(pByteCode));
	if( pStack == 0 ){
		return SXERR_MEM;
	}
	/* Execute the program */
	rc = VmByteCodeExec(&(*pVm), (VmInstr *)SySetBasePtr(pByteCode), pStack, -1, &(*pResult));
	/* Free the operand stack */
	SyMemBackendFree(&pVm->sAllocator, pStack);
	/* Execution result */
	return rc;
}
/*
 * Execute as much of a JX9 bytecode program as we can then return.
 * This function is a wrapper around [VmByteCodeExec()].
 * See block-comment on that function for additional information.
 */
JX9_PRIVATE sxi32 jx9VmByteCodeExec(jx9_vm *pVm)
{
	/* Make sure we are ready to execute this program */
	if( pVm->nMagic != JX9_VM_RUN ){
		return pVm->nMagic == JX9_VM_EXEC ? SXERR_LOCKED /* Locked VM */ : SXERR_CORRUPT; /* Stale VM */
	}
	/* Set the execution magic number  */
	pVm->nMagic = JX9_VM_EXEC;
	/* Execute the program */
	VmByteCodeExec(&(*pVm), (VmInstr *)SySetBasePtr(pVm->pByteContainer), pVm->aOps, -1, &pVm->sExec);
	/*
	 * TICKET 1433-100: Do not remove the JX9_VM_EXEC magic number
	 * so that any following call to [jx9_vm_exec()] without calling
	 * [jx9_vm_reset()] first would fail.
	 */
	return SXRET_OK;
}
/*
 * Extract a memory object (i.e. a variable) from the running script.
 * This function must be called after calling jx9_vm_exec(). Otherwise
 * NULL is returned.
 */
JX9_PRIVATE jx9_value * jx9VmExtractVariable(jx9_vm *pVm,SyString *pVar)
{
	jx9_value *pValue;
	if( pVm->nMagic != JX9_VM_EXEC ){
		/* call jx9_vm_exec() first */
		return 0;
	}
	/* Perform the lookup */
	pValue = VmExtractMemObj(pVm,pVar,FALSE,FALSE);
	/* Lookup result */
	return pValue;
}
/*
 * Invoke the installed VM output consumer callback to consume
 * the desired message.
 * Refer to the implementation of [jx9_context_output()] defined
 * in 'api.c' for additional information.
 */
JX9_PRIVATE sxi32 jx9VmOutputConsume(
	jx9_vm *pVm,      /* Target VM */
	SyString *pString /* Message to output */
	)
{
	jx9_output_consumer *pCons = &pVm->sVmConsumer;
	sxi32 rc = SXRET_OK;
	/* Call the output consumer */
	if( pString->nByte > 0 ){
		rc = pCons->xConsumer((const void *)pString->zString, pString->nByte, pCons->pUserData);
		/* Increment output length */
		pVm->nOutputLen += pString->nByte;
	}
	return rc;
}
/*
 * Format a message and invoke the installed VM output consumer
 * callback to consume the formatted message.
 * Refer to the implementation of [jx9_context_output_format()] defined
 * in 'api.c' for additional information.
 */
JX9_PRIVATE sxi32 jx9VmOutputConsumeAp(
	jx9_vm *pVm,         /* Target VM */
	const char *zFormat, /* Formatted message to output */
	va_list ap           /* Variable list of arguments */ 
	)
{
	jx9_output_consumer *pCons = &pVm->sVmConsumer;
	sxi32 rc = SXRET_OK;
	SyBlob sWorker;
	/* Format the message and call the output consumer */
	SyBlobInit(&sWorker, &pVm->sAllocator);
	SyBlobFormatAp(&sWorker, zFormat, ap);
	if( SyBlobLength(&sWorker) > 0 ){
		/* Consume the formatted message */
		rc = pCons->xConsumer(SyBlobData(&sWorker), SyBlobLength(&sWorker), pCons->pUserData);
	}
	/* Increment output length */
	pVm->nOutputLen += SyBlobLength(&sWorker);
	/* Release the working buffer */
	SyBlobRelease(&sWorker);
	return rc;
}
/*
 * Return a string representation of the given JX9 OP code.
 * This function never fail and always return a pointer
 * to a null terminated string.
 */
static const char * VmInstrToString(sxi32 nOp)
{
	const char *zOp = "Unknown     ";
	switch(nOp){
	case JX9_OP_DONE:       zOp = "DONE       "; break;
	case JX9_OP_HALT:       zOp = "HALT       "; break;
	case JX9_OP_LOAD:       zOp = "LOAD       "; break;
	case JX9_OP_LOADC:      zOp = "LOADC      "; break;
	case JX9_OP_LOAD_MAP:   zOp = "LOAD_MAP   "; break;
	case JX9_OP_LOAD_IDX:   zOp = "LOAD_IDX   "; break;
	case JX9_OP_NOOP:       zOp = "NOOP       "; break;
	case JX9_OP_JMP:        zOp = "JMP        "; break;
	case JX9_OP_JZ:         zOp = "JZ         "; break;
	case JX9_OP_JNZ:        zOp = "JNZ        "; break;
	case JX9_OP_POP:        zOp = "POP        "; break;
	case JX9_OP_CAT:        zOp = "CAT        "; break;
	case JX9_OP_CVT_INT:    zOp = "CVT_INT    "; break;
	case JX9_OP_CVT_STR:    zOp = "CVT_STR    "; break;
	case JX9_OP_CVT_REAL:   zOp = "CVT_REAL   "; break;
	case JX9_OP_CALL:       zOp = "CALL       "; break;
	case JX9_OP_UMINUS:     zOp = "UMINUS     "; break;
	case JX9_OP_UPLUS:      zOp = "UPLUS      "; break;
	case JX9_OP_BITNOT:     zOp = "BITNOT     "; break;
	case JX9_OP_LNOT:       zOp = "LOGNOT     "; break;
	case JX9_OP_MUL:        zOp = "MUL        "; break;
	case JX9_OP_DIV:        zOp = "DIV        "; break;
	case JX9_OP_MOD:        zOp = "MOD        "; break;
	case JX9_OP_ADD:        zOp = "ADD        "; break;
	case JX9_OP_SUB:        zOp = "SUB        "; break;
	case JX9_OP_SHL:        zOp = "SHL        "; break;
	case JX9_OP_SHR:        zOp = "SHR        "; break;
	case JX9_OP_LT:         zOp = "LT         "; break;
	case JX9_OP_LE:         zOp = "LE         "; break;
	case JX9_OP_GT:         zOp = "GT         "; break;
	case JX9_OP_GE:         zOp = "GE         "; break;
	case JX9_OP_EQ:         zOp = "EQ         "; break;
	case JX9_OP_NEQ:        zOp = "NEQ        "; break;
	case JX9_OP_TEQ:        zOp = "TEQ        "; break;
	case JX9_OP_TNE:        zOp = "TNE        "; break;
	case JX9_OP_BAND:       zOp = "BITAND     "; break;
	case JX9_OP_BXOR:       zOp = "BITXOR     "; break;
	case JX9_OP_BOR:        zOp = "BITOR      "; break;
	case JX9_OP_LAND:       zOp = "LOGAND     "; break;
	case JX9_OP_LOR:        zOp = "LOGOR      "; break;
	case JX9_OP_LXOR:       zOp = "LOGXOR     "; break;
	case JX9_OP_STORE:      zOp = "STORE      "; break;
	case JX9_OP_STORE_IDX:  zOp = "STORE_IDX  "; break;
	case JX9_OP_PULL:       zOp = "PULL       "; break;
	case JX9_OP_SWAP:       zOp = "SWAP       "; break;
	case JX9_OP_YIELD:      zOp = "YIELD      "; break;
	case JX9_OP_CVT_BOOL:   zOp = "CVT_BOOL   "; break;
	case JX9_OP_CVT_NULL:   zOp = "CVT_NULL   "; break;
	case JX9_OP_CVT_ARRAY:  zOp = "CVT_JSON   "; break;
	case JX9_OP_CVT_NUMC:   zOp = "CVT_NUMC   "; break;
	case JX9_OP_INCR:       zOp = "INCR       "; break;
	case JX9_OP_DECR:       zOp = "DECR       "; break;
	case JX9_OP_ADD_STORE:  zOp = "ADD_STORE  "; break;
	case JX9_OP_SUB_STORE:  zOp = "SUB_STORE  "; break;
	case JX9_OP_MUL_STORE:  zOp = "MUL_STORE  "; break;
	case JX9_OP_DIV_STORE:  zOp = "DIV_STORE  "; break;
	case JX9_OP_MOD_STORE:  zOp = "MOD_STORE  "; break;
	case JX9_OP_CAT_STORE:  zOp = "CAT_STORE  "; break;
	case JX9_OP_SHL_STORE:  zOp = "SHL_STORE  "; break;
	case JX9_OP_SHR_STORE:  zOp = "SHR_STORE  "; break;
	case JX9_OP_BAND_STORE: zOp = "BAND_STORE "; break;
	case JX9_OP_BOR_STORE:  zOp = "BOR_STORE  "; break;
	case JX9_OP_BXOR_STORE: zOp = "BXOR_STORE "; break;
	case JX9_OP_CONSUME:    zOp = "CONSUME    "; break;
	case JX9_OP_MEMBER:     zOp = "MEMBER     "; break;
	case JX9_OP_UPLINK:     zOp = "UPLINK     "; break;
	case JX9_OP_SWITCH:     zOp = "SWITCH     "; break;
	case JX9_OP_FOREACH_INIT:
		                    zOp = "4EACH_INIT "; break;
	case JX9_OP_FOREACH_STEP:
						    zOp = "4EACH_STEP "; break;
	default:
		break;
	}
	return zOp;
}
/*
 * Dump JX9 bytecodes instructions to a human readable format.
 * The xConsumer() callback which is an used defined function
 * is responsible of consuming the generated dump.
 */
JX9_PRIVATE sxi32 jx9VmDump(
	jx9_vm *pVm,            /* Target VM */
	ProcConsumer xConsumer, /* Output [i.e: dump] consumer callback */
	void *pUserData         /* Last argument to xConsumer() */
	)
{
	sxi32 rc;
	rc = VmByteCodeDump(pVm->pByteContainer, xConsumer, pUserData);
	return rc;
}
/*
 * Default constant expansion callback used by the 'const' statement if used
 * outside a object body [i.e: global or function scope].
 * Refer to the implementation of [JX9_CompileConstant()] defined
 * in 'compile.c' for additional information.
 */
JX9_PRIVATE void jx9VmExpandConstantValue(jx9_value *pVal, void *pUserData)
{
	SySet *pByteCode = (SySet *)pUserData;
	/* Evaluate and expand constant value */
	VmLocalExec((jx9_vm *)SySetGetUserData(pByteCode), pByteCode, (jx9_value *)pVal);
}
/*
 * Section:
 *  Function handling functions.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */
/*
 * int func_num_args(void)
 *   Returns the number of arguments passed to the function.
 * Parameters
 *   None.
 * Return
 *  Total number of arguments passed into the current user-defined function
 *  or -1 if called from the globe scope.
 */
static int vm_builtin_func_num_args(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	VmFrame *pFrame;
	jx9_vm *pVm;
	/* Point to the target VM */
	pVm = pCtx->pVm;
	/* Current frame */
	pFrame = pVm->pFrame;
	if( pFrame->pParent == 0 ){
		SXUNUSED(nArg);
		SXUNUSED(apArg);
		/* Global frame, return -1 */
		jx9_result_int(pCtx, -1);
		return SXRET_OK;
	}
	/* Total number of arguments passed to the enclosing function */
	nArg = (int)SySetUsed(&pFrame->sArg);
	jx9_result_int(pCtx, nArg);
	return SXRET_OK;
}
/*
 * value func_get_arg(int $arg_num)
 *   Return an item from the argument list.
 * Parameters
 *  Argument number(index start from zero).
 * Return
 *  Returns the specified argument or FALSE on error.
 */
static int vm_builtin_func_get_arg(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_value *pObj = 0;
	VmSlot *pSlot = 0;
	VmFrame *pFrame;
	jx9_vm *pVm;
	/* Point to the target VM */
	pVm = pCtx->pVm;
	/* Current frame */
	pFrame = pVm->pFrame;
	if( nArg < 1 || pFrame->pParent == 0 ){
		/* Global frame or Missing arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Called in the global scope");
		jx9_result_bool(pCtx, 0);
		return SXRET_OK;
	}
	/* Extract the desired index */
	nArg = jx9_value_to_int(apArg[0]);
	if( nArg < 0 || nArg >= (int)SySetUsed(&pFrame->sArg) ){
		/* Invalid index, return FALSE */
		jx9_result_bool(pCtx, 0);
		return SXRET_OK;
	}
	/* Extract the desired argument */
	if( (pSlot = (VmSlot *)SySetAt(&pFrame->sArg, (sxu32)nArg)) != 0 ){
		if( (pObj = (jx9_value *)SySetAt(&pVm->aMemObj, pSlot->nIdx)) != 0 ){
			/* Return the desired argument */
			jx9_result_value(pCtx, (jx9_value *)pObj);
		}else{
			/* No such argument, return false */
			jx9_result_bool(pCtx, 0);
		}
	}else{
		/* CAN'T HAPPEN */
		jx9_result_bool(pCtx, 0);
	}
	return SXRET_OK;
}
/*
 * array func_get_args(void)
 *   Returns an array comprising a copy of function's argument list.
 * Parameters
 *  None.
 * Return
 *  Returns an array in which each element is a copy of the corresponding
 *  member of the current user-defined function's argument list.
 *  Otherwise FALSE is returned on failure.
 */
static int vm_builtin_func_get_args(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_value *pObj = 0;
	jx9_value *pArray;
	VmFrame *pFrame;
	VmSlot *aSlot;
	sxu32 n;
	/* Point to the current frame */
	pFrame = pCtx->pVm->pFrame;
	if( pFrame->pParent == 0 ){
		/* Global frame, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Called in the global scope");
		jx9_result_bool(pCtx, 0);
		return SXRET_OK;
	}
	/* Create a new array */
	pArray = jx9_context_new_array(pCtx);
	if( pArray == 0 ){
		SXUNUSED(nArg); /* cc warning */
		SXUNUSED(apArg);
		jx9_result_bool(pCtx, 0);
		return SXRET_OK;
	}
	/* Start filling the array with the given arguments */
	aSlot = (VmSlot *)SySetBasePtr(&pFrame->sArg);
	for( n = 0;  n < SySetUsed(&pFrame->sArg) ; n++ ){
		pObj = (jx9_value *)SySetAt(&pCtx->pVm->aMemObj, aSlot[n].nIdx);
		if( pObj ){
			jx9_array_add_elem(pArray, 0/* Automatic index assign*/, pObj);
		}
	}
	/* Return the freshly created array */
	jx9_result_value(pCtx, pArray);
	return SXRET_OK;
}
/*
 * bool function_exists(string $name)
 *  Return TRUE if the given function has been defined.
 * Parameters
 *  The name of the desired function.
 * Return
 *  Return TRUE if the given function has been defined.False otherwise
 */
static int vm_builtin_func_exists(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zName;
	jx9_vm *pVm;
	int nLen;
	int res;
	if( nArg < 1 ){
		/* Missing argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return SXRET_OK;
	}
	/* Point to the target VM */
	pVm = pCtx->pVm;
	/* Extract the function name */
	zName = jx9_value_to_string(apArg[0], &nLen);
	/* Assume the function is not defined */
	res = 0;
	/* Perform the lookup */
	if( SyHashGet(&pVm->hFunction, (const void *)zName, (sxu32)nLen) != 0 ||
		SyHashGet(&pVm->hHostFunction, (const void *)zName, (sxu32)nLen) != 0 ){
			/* Function is defined */
			res = 1;
	}
	jx9_result_bool(pCtx, res);
	return SXRET_OK;
}
/*
 * Verify that the contents of a variable can be called as a function.
 * [i.e: Whether it is callable or not].
 * Return TRUE if callable.FALSE otherwise.
 */
JX9_PRIVATE int jx9VmIsCallable(jx9_vm *pVm, jx9_value *pValue)
{
	int res = 0;
	if( pValue->iFlags & MEMOBJ_STRING ){
		const char *zName;
		int nLen;
		/* Extract the name */
		zName = jx9_value_to_string(pValue, &nLen);
		/* Perform the lookup */
		if( SyHashGet(&pVm->hFunction, (const void *)zName, (sxu32)nLen) != 0 ||
			SyHashGet(&pVm->hHostFunction, (const void *)zName, (sxu32)nLen) != 0 ){
				/* Function is callable */
				res = 1;
		}
	}
	return res;
}
/*
 * bool is_callable(callable $name[, bool $syntax_only = false])
 * Verify that the contents of a variable can be called as a function.
 * Parameters
 * $name
 *    The callback function to check
 * $syntax_only
 *    If set to TRUE the function only verifies that name might be a function or method.
 *    It will only reject simple variables that are not strings, or an array that does
 *    not have a valid structure to be used as a callback. The valid ones are supposed
 *    to have only 2 entries, the first of which is an object or a string, and the second
 *    a string.
 * Return
 *  TRUE if name is callable, FALSE otherwise.
 */
static int vm_builtin_is_callable(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_vm *pVm;	
	int res;
	if( nArg < 1 ){
		/* Missing arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return SXRET_OK;
	}
	/* Point to the target VM */
	pVm = pCtx->pVm;
	/* Perform the requested operation */
	res = jx9VmIsCallable(pVm, apArg[0]);
	jx9_result_bool(pCtx, res);
	return SXRET_OK;
}
/*
 * Hash walker callback used by the [get_defined_functions()] function
 * defined below.
 */
static int VmHashFuncStep(SyHashEntry *pEntry, void *pUserData)
{
	jx9_value *pArray = (jx9_value *)pUserData;
	jx9_value sName;
	sxi32 rc;
	/* Prepare the function name for insertion */
	jx9MemObjInitFromString(pArray->pVm, &sName, 0);
	jx9MemObjStringAppend(&sName, (const char *)pEntry->pKey, pEntry->nKeyLen);
	/* Perform the insertion */
	rc = jx9_array_add_elem(pArray, 0/* Automatic index assign */, &sName); /* Will make it's own copy */
	jx9MemObjRelease(&sName);
	return rc;
}
/*
 * array get_defined_functions(void)
 *  Returns an array of all defined functions.
 * Parameter
 *  None.
 * Return
 *  Returns an multidimensional array containing a list of all defined functions
 *  both built-in (internal) and user-defined.
 *  The internal functions will be accessible via $arr["internal"], and the user 
 *  defined ones using $arr["user"]. 
 * Note:
 *  NULL is returned on failure.
 */
static int vm_builtin_get_defined_func(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_value *pArray;
	/* NOTE:
	 * Don't worry about freeing memory here, every allocated resource will be released
	 * automatically by the engine as soon we return from this foreign function.
	 */
	pArray = jx9_context_new_array(pCtx);
 	if( pArray == 0 ){
		SXUNUSED(nArg); /* cc warning */
		SXUNUSED(apArg);
		/* Return NULL */
		jx9_result_null(pCtx);
		return SXRET_OK;
	}
	/* Fill with the appropriate information */
	SyHashForEach(&pCtx->pVm->hHostFunction,VmHashFuncStep,pArray);
	/* Fill with the appropriate information */
	SyHashForEach(&pCtx->pVm->hFunction, VmHashFuncStep,pArray);
	/* Return a copy of the array array */
	jx9_result_value(pCtx, pArray);
	return SXRET_OK;
}
/*
 * Call a user defined or foreign function where the name of the function
 * is stored in the pFunc parameter and the given arguments are stored
 * in the apArg[] array.
 * Return SXRET_OK if the function was successfuly called.Any other
 * return value indicates failure.
 */
JX9_PRIVATE sxi32 jx9VmCallUserFunction(
	jx9_vm *pVm,       /* Target VM */
	jx9_value *pFunc,  /* Callback name */
	int nArg,          /* Total number of given arguments */
	jx9_value **apArg, /* Callback arguments */
	jx9_value *pResult /* Store callback return value here. NULL otherwise */
	)
{
	jx9_value *aStack;
	VmInstr aInstr[2];
	int i;
	if((pFunc->iFlags & (MEMOBJ_STRING)) == 0 ){
		/* Don't bother processing, it's invalid anyway */
		if( pResult ){
			/* Assume a null return value */
			jx9MemObjRelease(pResult);
		}
		return SXERR_INVALID;
	}
	/* Create a new operand stack */
	aStack = VmNewOperandStack(&(*pVm), 1+nArg);
	if( aStack == 0 ){
		jx9VmThrowError(&(*pVm), 0, JX9_CTX_ERR, 
			"JX9 is running out of memory while invoking user callback");
		if( pResult ){
			/* Assume a null return value */
			jx9MemObjRelease(pResult);
		}
		return SXERR_MEM;
	}
	/* Fill the operand stack with the given arguments */
	for( i = 0 ; i < nArg ; i++ ){
		jx9MemObjLoad(apArg[i], &aStack[i]);
		aStack[i].nIdx = apArg[i]->nIdx;
	}
	/* Push the function name */
	jx9MemObjLoad(pFunc, &aStack[i]);
	aStack[i].nIdx = SXU32_HIGH; /* Mark as constant */
	/* Emit the CALL istruction */
	aInstr[0].iOp = JX9_OP_CALL;
	aInstr[0].iP1 = nArg; /* Total number of given arguments */
	aInstr[0].iP2 = 0;
	aInstr[0].p3  = 0;
	/* Emit the DONE instruction */
	aInstr[1].iOp = JX9_OP_DONE;
	aInstr[1].iP1 = 1;   /* Extract function return value if available */
	aInstr[1].iP2 = 0;
	aInstr[1].p3  = 0;
	/* Execute the function body (if available) */
	VmByteCodeExec(&(*pVm), aInstr, aStack, nArg, pResult);
	/* Clean up the mess left behind */
	SyMemBackendFree(&pVm->sAllocator, aStack);
	return JX9_OK;
}
/*
 * Call a user defined or foreign function whith a varibale number
 * of arguments where the name of the function is stored in the pFunc
 * parameter.
 * Return SXRET_OK if the function was successfuly called.Any other
 * return value indicates failure.
 */
JX9_PRIVATE sxi32 jx9VmCallUserFunctionAp(
	jx9_vm *pVm,       /* Target VM */
	jx9_value *pFunc,  /* Callback name */
	jx9_value *pResult, /* Store callback return value here. NULL otherwise */
	...                /* 0 (Zero) or more Callback arguments */ 
	)
{
	jx9_value *pArg;
	SySet aArg;
	va_list ap;
	sxi32 rc;
	SySetInit(&aArg, &pVm->sAllocator, sizeof(jx9_value *));
	/* Copy arguments one after one */
	va_start(ap, pResult);
	for(;;){
		pArg = va_arg(ap, jx9_value *);
		if( pArg == 0 ){
			break;
		}
		SySetPut(&aArg, (const void *)&pArg);
	}
	/* Call the core routine */
	rc = jx9VmCallUserFunction(&(*pVm), pFunc, (int)SySetUsed(&aArg), (jx9_value **)SySetBasePtr(&aArg), pResult);
	/* Cleanup */
	va_end(ap);
	SySetRelease(&aArg);
	return rc;
}
/*
 * bool defined(string $name)
 *  Checks whether a given named constant exists.
 * Parameter:
 *  Name of the desired constant.
 * Return
 *  TRUE if the given constant exists.FALSE otherwise.
 */
static int vm_builtin_defined(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zName;
	int nLen = 0;
	int res = 0;
	if( nArg < 1 ){
		/* Missing constant name, return FALSE */
		jx9_context_throw_error(pCtx,JX9_CTX_NOTICE,"Missing constant name");
		jx9_result_bool(pCtx, 0);
		return SXRET_OK;
	}
	/* Extract constant name */
	zName = jx9_value_to_string(apArg[0], &nLen);
	/* Perform the lookup */
	if( nLen > 0 && SyHashGet(&pCtx->pVm->hConstant, (const void *)zName, (sxu32)nLen) != 0 ){
		/* Already defined */
		res = 1;
	}
	jx9_result_bool(pCtx, res);
	return SXRET_OK;
}
/*
 * Hash walker callback used by the [get_defined_constants()] function
 * defined below.
 */
static int VmHashConstStep(SyHashEntry *pEntry, void *pUserData)
{
	jx9_value *pArray = (jx9_value *)pUserData;
	jx9_value sName;
	sxi32 rc;
	/* Prepare the constant name for insertion */
	jx9MemObjInitFromString(pArray->pVm, &sName, 0);
	jx9MemObjStringAppend(&sName, (const char *)pEntry->pKey, pEntry->nKeyLen);
	/* Perform the insertion */
	rc = jx9_array_add_elem(pArray, 0, &sName); /* Will make it's own copy */
	jx9MemObjRelease(&sName);
	return rc;
}
/*
 * array get_defined_constants(void)
 *  Returns an associative array with the names of all defined
 *  constants.
 * Parameters
 *  NONE.
 * Returns
 *  Returns the names of all the constants currently defined.
 */
static int vm_builtin_get_defined_constants(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_value *pArray;
	/* Create the array first*/
	pArray = jx9_context_new_array(pCtx);
	if( pArray == 0 ){
		SXUNUSED(nArg); /* cc warning */
		SXUNUSED(apArg);
		/* Return NULL */
		jx9_result_null(pCtx);
		return SXRET_OK;
	}
	/* Fill the array with the defined constants */
	SyHashForEach(&pCtx->pVm->hConstant, VmHashConstStep, pArray);
	/* Return the created array */
	jx9_result_value(pCtx, pArray);
	return SXRET_OK;
}
/*
 * Section:
 *  Random numbers/string generators.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */
/*
 * Generate a random 32-bit unsigned integer.
 * JX9 use it's own private PRNG which is based on the one
 * used by te SQLite3 library.
 */
JX9_PRIVATE sxu32 jx9VmRandomNum(jx9_vm *pVm)
{
	sxu32 iNum;
	SyRandomness(&pVm->sPrng, (void *)&iNum, sizeof(sxu32));
	return iNum;
}
/*
 * Generate a random string (English Alphabet) of length nLen.
 * Note that the generated string is NOT null terminated.
 * JX9 use it's own private PRNG which is based on the one used
 * by te SQLite3 library.
 */
JX9_PRIVATE void jx9VmRandomString(jx9_vm *pVm, char *zBuf, int nLen)
{
	static const char zBase[] = {"abcdefghijklmnopqrstuvwxyz"}; /* English Alphabet */
	int i;
	/* Generate a binary string first */
	SyRandomness(&pVm->sPrng, zBuf, (sxu32)nLen);
	/* Turn the binary string into english based alphabet */
	for( i = 0 ; i < nLen ; ++i ){
		 zBuf[i] = zBase[zBuf[i] % (sizeof(zBase)-1)];
	 }
}
/*
 * int rand()
 *  Generate a random (unsigned 32-bit) integer.
 * Parameter
 *  $min
 *    The lowest value to return (default: 0)
 *  $max
 *   The highest value to return (default: getrandmax())
 * Return
 *   A pseudo random value between min (or 0) and max (or getrandmax(), inclusive).
 * Note:
 *  JX9 use it's own private PRNG which is based on the one used
 *  by te SQLite3 library.
 */
static int vm_builtin_rand(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	sxu32 iNum;
	/* Generate the random number */
	iNum = jx9VmRandomNum(pCtx->pVm);
	if( nArg > 1 ){
		sxu32 iMin, iMax;
		iMin = (sxu32)jx9_value_to_int(apArg[0]);
		iMax = (sxu32)jx9_value_to_int(apArg[1]);
		if( iMin < iMax ){
			sxu32 iDiv = iMax+1-iMin;
			if( iDiv > 0 ){
				iNum = (iNum % iDiv)+iMin;
			}
		}else if(iMax > 0 ){
			iNum %= iMax;
		}
	}
	/* Return the number */
	jx9_result_int64(pCtx, (jx9_int64)iNum);
	return SXRET_OK;
}
/*
 * int getrandmax(void)
 *   Show largest possible random value
 * Return
 *  The largest possible random value returned by rand() which is in
 *  this implementation 0xFFFFFFFF.
 * Note:
 *  JX9 use it's own private PRNG which is based on the one used
 *  by te SQLite3 library.
 */
static int vm_builtin_getrandmax(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SXUNUSED(nArg); /* cc warning */
	SXUNUSED(apArg);
	jx9_result_int64(pCtx, SXU32_HIGH);
	return SXRET_OK;
}
/*
 * string rand_str()
 * string rand_str(int $len)
 *  Generate a random string (English alphabet).
 * Parameter
 *  $len
 *    Length of the desired string (default: 16, Min: 1, Max: 1024)
 * Return
 *   A pseudo random string.
 * Note:
 *  JX9 use it's own private PRNG which is based on the one used
 *  by te SQLite3 library.
 */
static int vm_builtin_rand_str(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	char zString[1024];
	int iLen = 0x10;
	if( nArg > 0 ){
		/* Get the desired length */
		iLen = jx9_value_to_int(apArg[0]);
		if( iLen < 1 || iLen > 1024 ){
			/* Default length */
			iLen = 0x10;
		}
	}
	/* Generate the random string */
	jx9VmRandomString(pCtx->pVm, zString, iLen);
	/* Return the generated string */
	jx9_result_string(pCtx, zString, iLen); /* Will make it's own copy */
	return SXRET_OK;
}
/*
 * Section:
 *  Language construct implementation as foreign functions.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */
/*
 * void print($string...)
 *  Output one or more messages.
 * Parameters
 *  $string
 *   Message to output.
 * Return
 *  NULL.
 */
static int vm_builtin_print(jx9_context *pCtx, int nArg,jx9_value **apArg)
{
	const char *zData;
	int nDataLen = 0;
	jx9_vm *pVm;
	int i, rc;
	/* Point to the target VM */
	pVm = pCtx->pVm;
	/* Output */
	for( i = 0 ; i < nArg ; ++i ){
		zData = jx9_value_to_string(apArg[i], &nDataLen);
		if( nDataLen > 0 ){
			rc = pVm->sVmConsumer.xConsumer((const void *)zData, (unsigned int)nDataLen, pVm->sVmConsumer.pUserData);
			/* Increment output length */
			pVm->nOutputLen += nDataLen;
			if( rc == SXERR_ABORT ){
				/* Output consumer callback request an operation abort */
				return JX9_ABORT;
			}
		}
	}
	return SXRET_OK;
}
/*
 * void exit(string $msg)
 * void exit(int $status)
 * void die(string $ms)
 * void die(int $status)
 *   Output a message and terminate program execution.
 * Parameter
 *  If status is a string, this function prints the status just before exiting.
 *  If status is an integer, that value will be used as the exit status 
 *  and not printed
 * Return
 *  NULL
 */
static int vm_builtin_exit(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	if( nArg > 0 ){
		if( jx9_value_is_string(apArg[0]) ){
			const char *zData;
			int iLen = 0;
			/* Print exit message */
			zData = jx9_value_to_string(apArg[0], &iLen);
			jx9_context_output(pCtx, zData, iLen);
		}else if(jx9_value_is_int(apArg[0]) ){
			sxi32 iExitStatus;
			/* Record exit status code */
			iExitStatus = jx9_value_to_int(apArg[0]);
			pCtx->pVm->iExitStatus = iExitStatus;
		}
	}
	/* Abort processing immediately */
	return JX9_ABORT;
}
/*
 * Unset a memory object [i.e: a jx9_value].
 */
JX9_PRIVATE sxi32 jx9VmUnsetMemObj(jx9_vm *pVm,sxu32 nObjIdx)
{
	jx9_value *pObj;
	pObj = (jx9_value *)SySetAt(&pVm->aMemObj, nObjIdx);
	if( pObj ){
		VmSlot sFree;
		/* Release the object */
		jx9MemObjRelease(pObj);
		/* Restore to the free list */
		sFree.nIdx = nObjIdx;
		sFree.pUserData = 0;
		SySetPut(&pVm->aFreeObj, (const void *)&sFree);
	}				
	return SXRET_OK;
}
/*
 * string gettype($var)
 *  Get the type of a variable
 * Parameters
 *   $var
 *    The variable being type checked.
 * Return
 *   String representation of the given variable type.
 */
static int vm_builtin_gettype(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zType = "null";
	if( nArg > 0 ){
		zType = jx9MemObjTypeDump(apArg[0]);
	}
	/* Return the variable type */
	jx9_result_string(pCtx, zType, -1/*Compute length automatically*/);
	return SXRET_OK;
}
/*
 * string get_resource_type(resource $handle)
 *  This function gets the type of the given resource.
 * Parameters
 *  $handle
 *  The evaluated resource handle.
 * Return
 *  If the given handle is a resource, this function will return a string 
 *  representing its type. If the type is not identified by this function
 *  the return value will be the string Unknown.
 *  This function will return FALSE and generate an error if handle
 *  is not a resource.
 */
static int vm_builtin_get_resource_type(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE*/
		jx9_result_bool(pCtx, 0);
		return SXRET_OK;
	}
	jx9_result_string_format(pCtx, "resID_%#x", apArg[0]->x.pOther);
	return SXRET_OK;
}
/*
 * void dump(expression, ....)
 *   dump — Dumps information about a variable
 * Parameters
 *   One or more expression to dump.
 * Returns
 *  Nothing.
 */
static int vm_builtin_dump(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyBlob sDump; /* Generated dump is stored here */
	int i;
	SyBlobInit(&sDump,&pCtx->pVm->sAllocator);
	/* Dump one or more expressions */
	for( i = 0 ; i < nArg ; i++ ){
		jx9_value *pObj = apArg[i];
		/* Reset the working buffer */
		SyBlobReset(&sDump);
		/* Dump the given expression */
		jx9MemObjDump(&sDump,pObj);
		/* Output */
		if( SyBlobLength(&sDump) > 0 ){
			jx9_context_output(pCtx, (const char *)SyBlobData(&sDump), (int)SyBlobLength(&sDump));
		}
	}
	/* Release the working buffer */
	SyBlobRelease(&sDump);
	return SXRET_OK;
}
/*
 * Section:
 *  Version, Credits and Copyright related functions.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 *    Stable.
 */
/*
 * string jx9_version(void)
 * string jx9_credits(void)
 *  Returns the running version of the jx9 version.
 * Parameters
 *  None
 * Return
 * Current jx9 version.
 */
static int vm_builtin_jx9_version(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SXUNUSED(nArg);
	SXUNUSED(apArg); /* cc warning */
	/* Current engine version, signature and cipyright notice */
	jx9_result_string_format(pCtx,"%s %s, %s",JX9_VERSION,JX9_SIG,JX9_COPYRIGHT);
	return JX9_OK;
}
/*
 * Section:
 *    URL related routines.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */
/* Forward declaration */
static sxi32 VmHttpSplitURI(SyhttpUri *pOut, const char *zUri, sxu32 nLen);
/*
 * value parse_url(string $url [, int $component = -1 ])
 *  Parse a URL and return its fields.
 * Parameters
 *  $url
 *   The URL to parse.
 * $component
 *  Specify one of JX9_URL_SCHEME, JX9_URL_HOST, JX9_URL_PORT, JX9_URL_USER
 *  JX9_URL_PASS, JX9_URL_PATH, JX9_URL_QUERY or JX9_URL_FRAGMENT to retrieve
 *  just a specific URL component as a string (except when JX9_URL_PORT is given
 *  in which case the return value will be an integer).
 * Return
 *  If the component parameter is omitted, an associative array is returned.
 *  At least one element will be present within the array. Potential keys within
 *  this array are:
 *   scheme - e.g. http
 *   host
 *   port
 *   user
 *   pass
 *   path
 *   query - after the question mark ?
 *   fragment - after the hashmark #
 * Note:
 *  FALSE is returned on failure.
 *  This function work with relative URL unlike the one shipped
 *  with the standard JX9 engine.
 */
static int vm_builtin_parse_url(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zStr; /* Input string */
	SyString *pComp;  /* Pointer to the URI component */
	SyhttpUri sURI;   /* Parse of the given URI */
	int nLen;
	sxi32 rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the given URI */
	zStr = jx9_value_to_string(apArg[0], &nLen);
	if( nLen < 1 ){
		/* Nothing to process, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Get a parse */
	rc = VmHttpSplitURI(&sURI, zStr, (sxu32)nLen);
	if( rc != SXRET_OK ){
		/* Malformed input, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	if( nArg > 1 ){
		int nComponent = jx9_value_to_int(apArg[1]);
		/* Refer to constant.c for constants values */
		switch(nComponent){
		case 1: /* JX9_URL_SCHEME */
			pComp = &sURI.sScheme;
			if( pComp->nByte < 1 ){
				/* No available value, return NULL */
				jx9_result_null(pCtx);
			}else{
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}
			break;
		case 2: /* JX9_URL_HOST */
			pComp = &sURI.sHost;
			if( pComp->nByte < 1 ){
				/* No available value, return NULL */
				jx9_result_null(pCtx);
			}else{
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}
			break;
		case 3: /* JX9_URL_PORT */
			pComp = &sURI.sPort;
			if( pComp->nByte < 1 ){
				/* No available value, return NULL */
				jx9_result_null(pCtx);
			}else{
				int iPort = 0;
				/* Cast the value to integer */
				SyStrToInt32(pComp->zString, pComp->nByte, (void *)&iPort, 0);
				jx9_result_int(pCtx, iPort);
			}
			break;
		case 4: /* JX9_URL_USER */
			pComp = &sURI.sUser;
			if( pComp->nByte < 1 ){
				/* No available value, return NULL */
				jx9_result_null(pCtx);
			}else{
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}
			break;
		case 5: /* JX9_URL_PASS */
			pComp = &sURI.sPass;
			if( pComp->nByte < 1 ){
				/* No available value, return NULL */
				jx9_result_null(pCtx);
			}else{
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}
			break;
		case 7: /* JX9_URL_QUERY */
			pComp = &sURI.sQuery;
			if( pComp->nByte < 1 ){
				/* No available value, return NULL */
				jx9_result_null(pCtx);
			}else{
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}
			break;
		case 8: /* JX9_URL_FRAGMENT */
			pComp = &sURI.sFragment;
			if( pComp->nByte < 1 ){
				/* No available value, return NULL */
				jx9_result_null(pCtx);
			}else{
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}
			break;
		case 6: /*  JX9_URL_PATH */
			pComp = &sURI.sPath;
			if( pComp->nByte < 1 ){
				/* No available value, return NULL */
				jx9_result_null(pCtx);
			}else{
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}
			break;
		default:
			/* No such entry, return NULL */
			jx9_result_null(pCtx);
			break;
		}
	}else{
		jx9_value *pArray, *pValue;
		/* Return an associative array */
		pArray = jx9_context_new_array(pCtx);  /* Empty array */
		pValue = jx9_context_new_scalar(pCtx); /* Array value */
		if( pArray == 0 || pValue == 0 ){
			/* Out of memory */
			jx9_context_throw_error(pCtx, JX9_CTX_ERR, "jx9 engine is running out of memory");
			/* Return false */
			jx9_result_bool(pCtx, 0);
			return JX9_OK;
		}
		/* Fill the array */
		pComp = &sURI.sScheme;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			jx9_array_add_strkey_elem(pArray, "scheme", pValue); /* Will make it's own copy */
		}
		/* Reset the string cursor */
		jx9_value_reset_string_cursor(pValue);
		pComp = &sURI.sHost;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			jx9_array_add_strkey_elem(pArray, "host", pValue); /* Will make it's own copy */
		}
		/* Reset the string cursor */
		jx9_value_reset_string_cursor(pValue);
		pComp = &sURI.sPort;
		if( pComp->nByte > 0 ){
			int iPort = 0;/* cc warning */
			/* Convert to integer */
			SyStrToInt32(pComp->zString, pComp->nByte, (void *)&iPort, 0);
			jx9_value_int(pValue, iPort);
			jx9_array_add_strkey_elem(pArray, "port", pValue); /* Will make it's own copy */
		}
		/* Reset the string cursor */
		jx9_value_reset_string_cursor(pValue);
		pComp = &sURI.sUser;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			jx9_array_add_strkey_elem(pArray, "user", pValue); /* Will make it's own copy */
		}
		/* Reset the string cursor */
		jx9_value_reset_string_cursor(pValue);
		pComp = &sURI.sPass;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			jx9_array_add_strkey_elem(pArray, "pass", pValue); /* Will make it's own copy */
		}
		/* Reset the string cursor */
		jx9_value_reset_string_cursor(pValue);
		pComp = &sURI.sPath;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			jx9_array_add_strkey_elem(pArray, "path", pValue); /* Will make it's own copy */
		}
		/* Reset the string cursor */
		jx9_value_reset_string_cursor(pValue);
		pComp = &sURI.sQuery;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			jx9_array_add_strkey_elem(pArray, "query", pValue); /* Will make it's own copy */
		}
		/* Reset the string cursor */
		jx9_value_reset_string_cursor(pValue);
		pComp = &sURI.sFragment;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			jx9_array_add_strkey_elem(pArray, "fragment", pValue); /* Will make it's own copy */
		}
		/* Return the created array */
		jx9_result_value(pCtx, pArray);
		/* NOTE:
		 * Don't worry about freeing 'pValue', everything will be released
		 * automatically as soon we return from this function.
		 */
	}
	/* All done */
	return JX9_OK;
}
/*
 * Section:
 *   Array related routines.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 * Note 2012-5-21 01:04:15:
 *  Array related functions that need access to the underlying
 *  virtual machine are implemented here rather than 'hashmap.c'
 */
/*
 * The [extract()] function store it's state information in an instance
 * of the following structure.
 */
typedef struct extract_aux_data extract_aux_data;
struct extract_aux_data
{
	jx9_vm *pVm;          /* VM that own this instance */
	int iCount;           /* Number of variables successfully imported  */
	const char *zPrefix;  /* Prefix name */
	int Prefixlen;        /* Prefix  length */
	int iFlags;           /* Control flags */
	char zWorker[1024];   /* Working buffer */
};
/* Forward declaration */
static int VmExtractCallback(jx9_value *pKey, jx9_value *pValue, void *pUserData);
/*
 * int extract(array $var_array[, int $extract_type = EXTR_OVERWRITE[, string $prefix = NULL ]])
 *   Import variables into the current symbol table from an array.
 * Parameters
 * $var_array
 *  An associative array. This function treats keys as variable names and values
 *  as variable values. For each key/value pair it will create a variable in the current symbol
 *  table, subject to extract_type and prefix parameters.
 *  You must use an associative array; a numerically indexed array will not produce results
 *  unless you use EXTR_PREFIX_ALL or EXTR_PREFIX_INVALID.
 * $extract_type
 *  The way invalid/numeric keys and collisions are treated is determined by the extract_type.
 *  It can be one of the following values:
 *   EXTR_OVERWRITE
 *       If there is a collision, overwrite the existing variable. 
 *   EXTR_SKIP
 *       If there is a collision, don't overwrite the existing variable. 
 *   EXTR_PREFIX_SAME
 *       If there is a collision, prefix the variable name with prefix. 
 *   EXTR_PREFIX_ALL
 *       Prefix all variable names with prefix. 
 *   EXTR_PREFIX_INVALID
 *       Only prefix invalid/numeric variable names with prefix. 
 *   EXTR_IF_EXISTS
 *       Only overwrite the variable if it already exists in the current symbol table
 *       otherwise do nothing.
 *       This is useful for defining a list of valid variables and then extracting only those
 *       variables you have defined out of $_REQUEST, for example. 
 *   EXTR_PREFIX_IF_EXISTS
 *       Only create prefixed variable names if the non-prefixed version of the same variable exists in 
 *      the current symbol table.
 * $prefix
 *  Note that prefix is only required if extract_type is EXTR_PREFIX_SAME, EXTR_PREFIX_ALL
 *  EXTR_PREFIX_INVALID or EXTR_PREFIX_IF_EXISTS. If the prefixed result is not a valid variable name
 *  it is not imported into the symbol table. Prefixes are automatically separated from the array key by an
 *  underscore character.
 * Return
 *   Returns the number of variables successfully imported into the symbol table.
 */
static int vm_builtin_extract(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	extract_aux_data sAux;
	jx9_hashmap *pMap;
	if( nArg < 1 || !jx9_value_is_json_array(apArg[0]) ){
		/* Missing/Invalid arguments, return 0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target hashmap */
	pMap = (jx9_hashmap *)apArg[0]->x.pOther;
	if( pMap->nEntry < 1 ){
		/* Empty map, return  0 */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Prepare the aux data */
	SyZero(&sAux, sizeof(extract_aux_data)-sizeof(sAux.zWorker));
	if( nArg > 1 ){
		sAux.iFlags = jx9_value_to_int(apArg[1]);
		if( nArg > 2 ){
			sAux.zPrefix = jx9_value_to_string(apArg[2], &sAux.Prefixlen);
		}
	}
	sAux.pVm = pCtx->pVm;
	/* Invoke the worker callback */
	jx9HashmapWalk(pMap, VmExtractCallback, &sAux);
	/* Number of variables successfully imported */
	jx9_result_int(pCtx, sAux.iCount);
	return JX9_OK;
}
/*
 * Worker callback for the [extract()] function defined
 * below.
 */
static int VmExtractCallback(jx9_value *pKey, jx9_value *pValue, void *pUserData)
{
	extract_aux_data *pAux = (extract_aux_data *)pUserData;
	int iFlags = pAux->iFlags;
	jx9_vm *pVm = pAux->pVm;
	jx9_value *pObj;
	SyString sVar;
	if( (iFlags & 0x10/* EXTR_PREFIX_INVALID */) && (pKey->iFlags & (MEMOBJ_INT|MEMOBJ_BOOL|MEMOBJ_REAL))){
		iFlags |= 0x08; /*EXTR_PREFIX_ALL*/
	}
	/* Perform a string cast */
	jx9MemObjToString(pKey);
	if( SyBlobLength(&pKey->sBlob) < 1 ){
		/* Unavailable variable name */
		return SXRET_OK;
	}
	sVar.nByte = 0; /* cc warning */
	if( (iFlags & 0x08/*EXTR_PREFIX_ALL*/ ) && pAux->Prefixlen > 0 ){
		sVar.nByte = (sxu32)SyBufferFormat(pAux->zWorker, sizeof(pAux->zWorker), "%.*s_%.*s", 
			pAux->Prefixlen, pAux->zPrefix, 
			SyBlobLength(&pKey->sBlob), SyBlobData(&pKey->sBlob)
			);
	}else{
		sVar.nByte = (sxu32) SyMemcpy(SyBlobData(&pKey->sBlob), pAux->zWorker, 
			SXMIN(SyBlobLength(&pKey->sBlob), sizeof(pAux->zWorker)));
	}
	sVar.zString = pAux->zWorker;
	/* Try to extract the variable */
	pObj = VmExtractMemObj(pVm, &sVar, TRUE, FALSE);
	if( pObj ){
		/* Collision */
		if( iFlags & 0x02 /* EXTR_SKIP */ ){
			return SXRET_OK;
		}
		if( iFlags & 0x04 /* EXTR_PREFIX_SAME */ ){
			if( (iFlags & 0x08/*EXTR_PREFIX_ALL*/) || pAux->Prefixlen < 1){
				/* Already prefixed */
				return SXRET_OK;
			}
			sVar.nByte = SyBufferFormat(
				pAux->zWorker, sizeof(pAux->zWorker),
				"%.*s_%.*s", 
				pAux->Prefixlen, pAux->zPrefix, 
				SyBlobLength(&pKey->sBlob), SyBlobData(&pKey->sBlob)
				);
			pObj = VmExtractMemObj(pVm, &sVar, TRUE, TRUE);
		}
	}else{
		/* Create the variable */
		pObj = VmExtractMemObj(pVm, &sVar, TRUE, TRUE);
	}
	if( pObj ){
		/* Overwrite the old value */
		jx9MemObjStore(pValue, pObj);
		/* Increment counter */
		pAux->iCount++;
	}
	return SXRET_OK;
}
/*
 * Compile and evaluate a JX9 chunk at run-time.
 * Refer to the include language construct implementation for more
 * information.
 */
static sxi32 VmEvalChunk(
	jx9_vm *pVm,        /* Underlying Virtual Machine */
	jx9_context *pCtx,  /* Call Context */
	SyString *pChunk,   /* JX9 chunk to evaluate */ 
	int iFlags,         /* Compile flag */
	int bTrueReturn     /* TRUE to return execution result */
	)
{
	SySet *pByteCode, aByteCode;
	ProcConsumer xErr = 0;
	void *pErrData = 0;
	/* Initialize bytecode container */
	SySetInit(&aByteCode, &pVm->sAllocator, sizeof(VmInstr));
	SySetAlloc(&aByteCode, 0x20);
	/* Reset the code generator */
	if( bTrueReturn ){
		/* Included file, log compile-time errors */
		xErr = pVm->pEngine->xConf.xErr;
		pErrData = pVm->pEngine->xConf.pErrData;
	}
	jx9ResetCodeGenerator(pVm, xErr, pErrData);
	/* Swap bytecode container */
	pByteCode = pVm->pByteContainer;
	pVm->pByteContainer = &aByteCode;
	/* Compile the chunk */
	jx9CompileScript(pVm, pChunk, iFlags);
	if( pVm->sCodeGen.nErr > 0 ){
		/* Compilation error, return false */
		if( pCtx ){
			jx9_result_bool(pCtx, 0);
		}
	}else{
		jx9_value sResult; /* Return value */
		if( SXRET_OK != jx9VmEmitInstr(pVm, JX9_OP_DONE, 0, 0, 0, 0) ){
			/* Out of memory */
			if( pCtx ){
				jx9_result_bool(pCtx, 0);
			}
			goto Cleanup;
		}
		if( bTrueReturn ){
			/* Assume a boolean true return value */
			jx9MemObjInitFromBool(pVm, &sResult, 1);
		}else{
			/* Assume a null return value */
			jx9MemObjInit(pVm, &sResult);
		}
		/* Execute the compiled chunk */
		VmLocalExec(pVm, &aByteCode, &sResult);
		if( pCtx ){
			/* Set the execution result */
			jx9_result_value(pCtx, &sResult);
		}
		jx9MemObjRelease(&sResult);
	}
Cleanup:
	/* Cleanup the mess left behind */
	pVm->pByteContainer = pByteCode;
	SySetRelease(&aByteCode);
	return SXRET_OK;
}
/*
 * Check if a file path is already included.
 */
static int VmIsIncludedFile(jx9_vm *pVm, SyString *pFile)
{
	SyString *aEntries;
	sxu32 n;
	aEntries = (SyString *)SySetBasePtr(&pVm->aIncluded);
	/* Perform a linear search */
	for( n = 0 ; n < SySetUsed(&pVm->aIncluded) ; ++n ){
		if( SyStringCmp(pFile, &aEntries[n], SyMemcmp) == 0 ){
			/* Already included */
			return TRUE;
		}
	}
	return FALSE;
}
/*
 * Push a file path in the appropriate VM container.
 */
JX9_PRIVATE sxi32 jx9VmPushFilePath(jx9_vm *pVm, const char *zPath, int nLen, sxu8 bMain, sxi32 *pNew)
{
	SyString sPath;
	char *zDup;
#ifdef __WINNT__
	char *zCur;
#endif
	sxi32 rc;
	if( nLen < 0 ){
		nLen = SyStrlen(zPath);
	}
	/* Duplicate the file path first */
	zDup = SyMemBackendStrDup(&pVm->sAllocator, zPath, nLen);
	if( zDup == 0 ){
		return SXERR_MEM;
	}
#ifdef __WINNT__
	/* Normalize path on windows
	 * Example:
	 *    Path/To/File.jx9
	 * becomes
	 *   path\to\file.jx9
	 */
	zCur = zDup;
	while( zCur[0] != 0 ){
		if( zCur[0] == '/' ){
			zCur[0] = '\\';
		}else if( (unsigned char)zCur[0] < 0xc0 && SyisUpper(zCur[0]) ){
			int c = SyToLower(zCur[0]);
			zCur[0] = (char)c; /* MSVC stupidity */
		}
		zCur++;
	}
#endif
	/* Install the file path */
	SyStringInitFromBuf(&sPath, zDup, nLen);
	if( !bMain ){
		if( VmIsIncludedFile(&(*pVm), &sPath) ){
			/* Already included */
			*pNew = 0;
		}else{
			/* Insert in the corresponding container */
			rc = SySetPut(&pVm->aIncluded, (const void *)&sPath);
			if( rc != SXRET_OK ){
				SyMemBackendFree(&pVm->sAllocator, zDup);
				return rc;
			}
			*pNew = 1;
		}
	}
	SySetPut(&pVm->aFiles, (const void *)&sPath);
	return SXRET_OK;
}
/*
 * Compile and Execute a JX9 script at run-time.
 * SXRET_OK is returned on sucessful evaluation.Any other return values
 * indicates failure.
 * Note that the JX9 script to evaluate can be a local or remote file.In
 * either cases the [jx9StreamReadWholeFile()] function handle all the underlying
 * operations.
 * If the [jJX9_DISABLE_BUILTIN_FUNC] compile-time directive is defined, then
 * this function is a no-op.
 * Refer to the implementation of the include(), import() language
 * constructs for more information.
 */
static sxi32 VmExecIncludedFile(
	 jx9_context *pCtx, /* Call Context */
	 SyString *pPath,   /* Script path or URL*/
	 int IncludeOnce    /* TRUE if called from import() or require_once() */
	 )
{
	sxi32 rc;
#ifndef JX9_DISABLE_BUILTIN_FUNC
	const jx9_io_stream *pStream;
	SyBlob sContents;
	void *pHandle;
	jx9_vm *pVm;
	int isNew;
	/* Initialize fields */
	pVm = pCtx->pVm;
	SyBlobInit(&sContents, &pVm->sAllocator);
	isNew = 0;
	/* Extract the associated stream */
	pStream = jx9VmGetStreamDevice(pVm, &pPath->zString, pPath->nByte);
	/*
	 * Open the file or the URL [i.e: http://jx9.symisc.net/example/hello.jx9.txt"] 
	 * in a read-only mode.
	 */
	pHandle = jx9StreamOpenHandle(pVm, pStream,pPath->zString, JX9_IO_OPEN_RDONLY, TRUE, 0, TRUE, &isNew);
	if( pHandle == 0 ){
		return SXERR_IO;
	}
	rc = SXRET_OK; /* Stupid cc warning */
	if( IncludeOnce && !isNew ){
		/* Already included */
		rc = SXERR_EXISTS;
	}else{
		/* Read the whole file contents */
		rc = jx9StreamReadWholeFile(pHandle, pStream, &sContents);
		if( rc == SXRET_OK ){
			SyString sScript;
			/* Compile and execute the script */
			SyStringInitFromBuf(&sScript, SyBlobData(&sContents), SyBlobLength(&sContents));
			VmEvalChunk(pCtx->pVm, &(*pCtx), &sScript, 0, TRUE);
		}
	}
	/* Pop from the set of included file */
	(void)SySetPop(&pVm->aFiles);
	/* Close the handle */
	jx9StreamCloseHandle(pStream, pHandle);
	/* Release the working buffer */
	SyBlobRelease(&sContents);
#else
	pCtx = 0; /* cc warning */
	pPath = 0;
	IncludeOnce = 0;
	rc = SXERR_IO;
#endif /* JX9_DISABLE_BUILTIN_FUNC */
	return rc;
}
/* * include:
 * According to the JX9 reference manual.
 *  The include() function includes and evaluates the specified file.
 *  Files are included based on the file path given or, if none is given
 *  the include_path specified.If the file isn't found in the include_path
 *  include() will finally check in the calling script's own directory
 *  and the current working directory before failing. The include()
 *  construct will emit a warning if it cannot find a file; this is different
 *  behavior from require(), which will emit a fatal error.
 *  If a path is defined — whether absolute (starting with a drive letter
 *  or \ on Windows, or / on Unix/Linux systems) or relative to the current
 *  directory (starting with . or ..) — the include_path will be ignored altogether.
 *  For example, if a filename begins with ../, the parser will look in the parent
 *  directory to find the requested file.
 *  When a file is included, the code it contains inherits the variable scope
 *  of the line on which the include occurs. Any variables available at that line
 *  in the calling file will be available within the called file, from that point forward.
 *  However, all functions and objectes defined in the included file have the global scope. 
 */
static int vm_builtin_include(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyString sFile;
	sxi32 rc;
	if( nArg < 1 ){
		/* Nothing to evaluate, return NULL */
		jx9_result_null(pCtx);
		return SXRET_OK;
	}
	/* File to include */
	sFile.zString = jx9_value_to_string(apArg[0], (int *)&sFile.nByte);
	if( sFile.nByte < 1 ){
		/* Empty string, return NULL */
		jx9_result_null(pCtx);
		return SXRET_OK;
	}
	/* Open, compile and execute the desired script */
	rc = VmExecIncludedFile(&(*pCtx), &sFile, FALSE);
	if( rc != SXRET_OK ){
		/* Emit a warning and return false */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, "IO error while importing: '%z'", &sFile);
		jx9_result_bool(pCtx, 0);
	}
	return SXRET_OK;
}
/*
 * import:
 *  According to the JX9 reference manual.
 *   The import() statement includes and evaluates the specified file during
 *   the execution of the script. This is a behavior similar to the include() 
 *   statement, with the only difference being that if the code from a file has already
 *   been included, it will not be included again. As the name suggests, it will be included
 *   just once.
 */
static int vm_builtin_import(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyString sFile;
	sxi32 rc;
	if( nArg < 1 ){
		/* Nothing to evaluate, return NULL */
		jx9_result_null(pCtx);
		return SXRET_OK;
	}
	/* File to include */
	sFile.zString = jx9_value_to_string(apArg[0], (int *)&sFile.nByte);
	if( sFile.nByte < 1 ){
		/* Empty string, return NULL */
		jx9_result_null(pCtx);
		return SXRET_OK;
	}
	/* Open, compile and execute the desired script */
	rc = VmExecIncludedFile(&(*pCtx), &sFile, TRUE);
	if( rc == SXERR_EXISTS ){
		/* File already included, return TRUE */
		jx9_result_bool(pCtx, 1);
		return SXRET_OK;
	}
	if( rc != SXRET_OK ){
		/* Emit a warning and return false */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, "IO error while importing: '%z'", &sFile);
		jx9_result_bool(pCtx, 0);
 	}
	return SXRET_OK;
}
/*
 * Section:
 *  Command line arguments processing.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */
/*
 * Check if a short option argument [i.e: -c] is available in the command
 * line string. Return a pointer to the start of the stream on success.
 * NULL otherwise.
 */
static const char * VmFindShortOpt(int c, const char *zIn, const char *zEnd)
{
	while( zIn < zEnd ){
		if( zIn[0] == '-' && &zIn[1] < zEnd && (int)zIn[1] == c ){
			/* Got one */
			return &zIn[1];
		}
		/* Advance the cursor */
		zIn++;
	}
	/* No such option */
	return 0;
}
/*
 * Check if a long option argument [i.e: --opt] is available in the command
 * line string. Return a pointer to the start of the stream on success.
 * NULL otherwise.
 */
static const char * VmFindLongOpt(const char *zLong, int nByte, const char *zIn, const char *zEnd)
{
	const char *zOpt;
	while( zIn < zEnd ){
		if( zIn[0] == '-' && &zIn[1] < zEnd && (int)zIn[1] == '-' ){
			zIn += 2;
			zOpt = zIn;
			while( zIn < zEnd && !SyisSpace(zIn[0]) ){
				if( zIn[0] == '=' /* --opt=val */){
					break;
				}
				zIn++;
			}
			/* Test */
			if( (int)(zIn-zOpt) == nByte && SyMemcmp(zOpt, zLong, nByte) == 0 ){
				/* Got one, return it's value */
				return zIn;
			}

		}else{
			zIn++;
		}
	}
	/* No such option */
	return 0;
}
/*
 * Long option [i.e: --opt] arguments private data structure.
 */
struct getopt_long_opt
{
	const char *zArgIn, *zArgEnd; /* Command line arguments */
	jx9_value *pWorker;  /* Worker variable*/
	jx9_value *pArray;   /* getopt() return value */
	jx9_context *pCtx;   /* Call Context */
};
/* Forward declaration */
static int VmProcessLongOpt(jx9_value *pKey, jx9_value *pValue, void *pUserData);
/*
 * Extract short or long argument option values.
 */
static void VmExtractOptArgValue(
	jx9_value *pArray,  /* getopt() return value */
	jx9_value *pWorker, /* Worker variable */
	const char *zArg,   /* Argument stream */
	const char *zArgEnd, /* End of the argument stream  */
	int need_val,       /* TRUE to fetch option argument */
	jx9_context *pCtx,  /* Call Context */
	const char *zName   /* Option name */)
{
	jx9_value_bool(pWorker, 0);
	if( !need_val ){
		/* 
		 * Option does not need arguments.
		 * Insert the option name and a boolean FALSE.
		 */
		jx9_array_add_strkey_elem(pArray, (const char *)zName, pWorker); /* Will make it's own copy */
	}else{
		const char *zCur;
		/* Extract option argument */
		zArg++;
		if( zArg < zArgEnd && zArg[0] == '=' ){
			zArg++;
		}
		while( zArg < zArgEnd && (unsigned char)zArg[0] < 0xc0 && SyisSpace(zArg[0]) ){
			zArg++;
		}
		if( zArg >= zArgEnd || zArg[0] == '-' ){
			/*
			 * Argument not found.
			 * Insert the option name and a boolean FALSE.
			 */
			jx9_array_add_strkey_elem(pArray, (const char *)zName, pWorker); /* Will make it's own copy */
			return;
		}
		/* Delimit the value */
		zCur = zArg;
		if( zArg[0] == '\'' || zArg[0] == '"' ){
			int d = zArg[0];
			/* Delimt the argument */
			zArg++;
			zCur = zArg;
			while( zArg < zArgEnd ){
				if( zArg[0] == d && zArg[-1] != '\\' ){
					/* Delimiter found, exit the loop  */
					break;
				}
				zArg++;
			}
			/* Save the value */
			jx9_value_string(pWorker, zCur, (int)(zArg-zCur));
			if( zArg < zArgEnd ){ zArg++; }
		}else{
			while( zArg < zArgEnd && !SyisSpace(zArg[0]) ){
				zArg++;
			}
			/* Save the value */
			jx9_value_string(pWorker, zCur, (int)(zArg-zCur));
		}
		/*
		 * Check if we are dealing with multiple values.
		 * If so, create an array to hold them, rather than a scalar variable.
		 */
		while( zArg < zArgEnd && (unsigned char)zArg[0] < 0xc0 && SyisSpace(zArg[0]) ){
			zArg++;
		}
		if( zArg < zArgEnd && zArg[0] != '-' ){
			jx9_value *pOptArg; /* Array of option arguments */
			pOptArg = jx9_context_new_array(pCtx);
			if( pOptArg == 0 ){
				jx9_context_throw_error(pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
			}else{
				/* Insert the first value */
				jx9_array_add_elem(pOptArg, 0, pWorker); /* Will make it's own copy */
				for(;;){
					if( zArg >= zArgEnd || zArg[0] == '-' ){
						/* No more value */
						break;
					}
					/* Delimit the value */
					zCur = zArg;
					if( zArg < zArgEnd && zArg[0] == '\\' ){
						zArg++;
						zCur = zArg;
					}
					while( zArg < zArgEnd && !SyisSpace(zArg[0]) ){
						zArg++;
					}
					/* Reset the string cursor */
					jx9_value_reset_string_cursor(pWorker);
					/* Save the value */
					jx9_value_string(pWorker, zCur, (int)(zArg-zCur));
					/* Insert */
					jx9_array_add_elem(pOptArg, 0, pWorker); /* Will make it's own copy */
					/* Jump trailing white spaces */
					while( zArg < zArgEnd && (unsigned char)zArg[0] < 0xc0 && SyisSpace(zArg[0]) ){
						zArg++;
					}
				}
				/* Insert the option arg array */
				jx9_array_add_strkey_elem(pArray, (const char *)zName, pOptArg); /* Will make it's own copy */
				/* Safely release */
				jx9_context_release_value(pCtx, pOptArg);
			}
		}else{
			/* Single value */
			jx9_array_add_strkey_elem(pArray, (const char *)zName, pWorker); /* Will make it's own copy */
		}
	}
}
/*
 * array getopt(string $options[, array $longopts ])
 *   Gets options from the command line argument list.
 * Parameters
 *  $options
 *   Each character in this string will be used as option characters
 *   and matched against options passed to the script starting with
 *   a single hyphen (-). For example, an option string "x" recognizes
 *   an option -x. Only a-z, A-Z and 0-9 are allowed.
 *  $longopts
 *   An array of options. Each element in this array will be used as option
 *   strings and matched against options passed to the script starting with
 *   two hyphens (--). For example, an longopts element "opt" recognizes an
 *   option --opt. 
 * Return
 *  This function will return an array of option / argument pairs or FALSE
 *  on failure. 
 */
static int vm_builtin_getopt(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zIn, *zEnd, *zArg, *zArgIn, *zArgEnd;
	struct getopt_long_opt sLong;
	jx9_value *pArray, *pWorker;
	SyBlob *pArg;
	int nByte;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Missing/Invalid option arguments");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract option arguments */
	zIn  = jx9_value_to_string(apArg[0], &nByte);
	zEnd = &zIn[nByte];
	/* Point to the string representation of the $argv[] array */
	pArg = &pCtx->pVm->sArgv;
	/* Create a new empty array and a worker variable */
	pArray = jx9_context_new_array(pCtx);
	pWorker = jx9_context_new_scalar(pCtx);
	if( pArray == 0 || pWorker == 0 ){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR, "JX9 is running out of memory");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	if( SyBlobLength(pArg) < 1 ){
		/* Empty command line, return the empty array*/
		jx9_result_value(pCtx, pArray);
		/* Everything will be released automatically when we return 
		 * from this function.
		 */
		return JX9_OK;
	}
	zArgIn = (const char *)SyBlobData(pArg);
	zArgEnd = &zArgIn[SyBlobLength(pArg)];
	/* Fill the long option structure */
	sLong.pArray = pArray;
	sLong.pWorker = pWorker;
	sLong.zArgIn =  zArgIn;
	sLong.zArgEnd = zArgEnd;
	sLong.pCtx = pCtx;
	/* Start processing */
	while( zIn < zEnd ){
		int c = zIn[0];
		int need_val = 0;
		/* Advance the stream cursor */
		zIn++;
		/* Ignore non-alphanum characters */
		if( !SyisAlphaNum(c) ){
			continue;
		}
		if( zIn < zEnd && zIn[0] == ':' ){
			zIn++;
			need_val = 1;
			if( zIn < zEnd && zIn[0] == ':' ){
				zIn++;
			}
		}
		/* Find option */
		zArg = VmFindShortOpt(c, zArgIn, zArgEnd);
		if( zArg == 0 ){
			/* No such option */
			continue;
		}
		/* Extract option argument value */
		VmExtractOptArgValue(pArray, pWorker, zArg, zArgEnd, need_val, pCtx, (const char *)&c);	
	}
	if( nArg > 1 && jx9_value_is_json_array(apArg[1]) && jx9_array_count(apArg[1]) > 0 ){
		/* Process long options */
		jx9_array_walk(apArg[1], VmProcessLongOpt, &sLong);
	}
	/* Return the option array */
	jx9_result_value(pCtx, pArray);
	/* 
	 * Don't worry about freeing memory, everything will be released
	 * automatically as soon we return from this foreign function.
	 */
	return JX9_OK;
}
/*
 * Array walker callback used for processing long options values.
 */
static int VmProcessLongOpt(jx9_value *pKey, jx9_value *pValue, void *pUserData)
{
	struct getopt_long_opt *pOpt = (struct getopt_long_opt *)pUserData;
	const char *zArg, *zOpt, *zEnd;
	int need_value = 0;
	int nByte;
	/* Value must be of type string */
	if( !jx9_value_is_string(pValue) ){
		/* Simply ignore */
		return JX9_OK;
	}
	zOpt = jx9_value_to_string(pValue, &nByte);
	if( nByte < 1 ){
		/* Empty string, ignore */
		return JX9_OK;
	}
	zEnd = &zOpt[nByte - 1];
	if( zEnd[0] == ':' ){
		char *zTerm;
		/* Try to extract a value */
		need_value = 1;
		while( zEnd >= zOpt && zEnd[0] == ':' ){
			zEnd--;
		}
		if( zOpt >= zEnd ){
			/* Empty string, ignore */
			SXUNUSED(pKey);
			return JX9_OK;
		}
		zEnd++;
		zTerm = (char *)zEnd;
		zTerm[0] = 0;
	}else{
		zEnd = &zOpt[nByte];
	}
	/* Find the option */
	zArg = VmFindLongOpt(zOpt, (int)(zEnd-zOpt), pOpt->zArgIn, pOpt->zArgEnd);
	if( zArg == 0 ){
		/* No such option, return immediately */
		return JX9_OK;
	}
	/* Try to extract a value */
	VmExtractOptArgValue(pOpt->pArray, pOpt->pWorker, zArg, pOpt->zArgEnd, need_value, pOpt->pCtx, zOpt);
	return JX9_OK;
}
/*
 * int utf8_encode(string $input)
 *  UTF-8 encoding.
 *  This function encodes the string data to UTF-8, and returns the encoded version.
 *  UTF-8 is a standard mechanism used by Unicode for encoding wide character values
 * into a byte stream. UTF-8 is transparent to plain ASCII characters, is self-synchronized
 * (meaning it is possible for a program to figure out where in the bytestream characters start)
 * and can be used with normal string comparison functions for sorting and such.
 *  Notes on UTF-8 (According to SQLite3 authors):
 *  Byte-0    Byte-1    Byte-2    Byte-3    Value
 *  0xxxxxxx                                 00000000 00000000 0xxxxxxx
 *  110yyyyy  10xxxxxx                       00000000 00000yyy yyxxxxxx
 *  1110zzzz  10yyyyyy  10xxxxxx             00000000 zzzzyyyy yyxxxxxx
 *  11110uuu  10uuzzzz  10yyyyyy  10xxxxxx   000uuuuu zzzzyyyy yyxxxxxx
 * Parameters
 * $input
 *   String to encode or NULL on failure.
 * Return
 *  An UTF-8 encoded string.
 */
static int vm_builtin_utf8_encode(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const unsigned char *zIn, *zEnd;
	int nByte, c, e;
	if( nArg < 1 ){
		/* Missing arguments, return null */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Extract the target string */
	zIn = (const unsigned char *)jx9_value_to_string(apArg[0], &nByte);
	if( nByte < 1 ){
		/* Empty string, return null */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	zEnd = &zIn[nByte];
	/* Start the encoding process */
	for(;;){
		if( zIn >= zEnd ){
			/* End of input */
			break;
		}
		c = zIn[0];
		/* Advance the stream cursor */
		zIn++;
		/* Encode */
		if( c<0x00080 ){
			e = (c&0xFF);
			jx9_result_string(pCtx, (const char *)&e, (int)sizeof(char));
		}else if( c<0x00800 ){
			e = 0xC0 + ((c>>6)&0x1F);
			jx9_result_string(pCtx, (const char *)&e, (int)sizeof(char));
			e = 0x80 + (c & 0x3F);
			jx9_result_string(pCtx, (const char *)&e, (int)sizeof(char));
		}else if( c<0x10000 ){
			e = 0xE0 + ((c>>12)&0x0F);
			jx9_result_string(pCtx, (const char *)&e, (int)sizeof(char));
			e = 0x80 + ((c>>6) & 0x3F);
			jx9_result_string(pCtx, (const char *)&e, (int)sizeof(char));
			e = 0x80 + (c & 0x3F);
			jx9_result_string(pCtx, (const char *)&e, (int)sizeof(char));
		}else{
			e = 0xF0 + ((c>>18) & 0x07);
			jx9_result_string(pCtx, (const char *)&e, (int)sizeof(char));
			e = 0x80 + ((c>>12) & 0x3F);
			jx9_result_string(pCtx, (const char *)&e, (int)sizeof(char));
			e = 0x80 + ((c>>6) & 0x3F);
			jx9_result_string(pCtx, (const char *)&e, (int)sizeof(char));
			e = 0x80 + (c & 0x3F);
			jx9_result_string(pCtx, (const char *)&e, (int)sizeof(char));
		} 
	}
	/* All done */
	return JX9_OK;
}
/*
 * UTF-8 decoding routine extracted from the sqlite3 source tree.
 * Original author: D. Richard Hipp (http://www.sqlite.org)
 * Status: Public Domain
 */
/*
** This lookup table is used to help decode the first byte of
** a multi-byte UTF8 character.
*/
static const unsigned char UtfTrans1[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
  0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00, 
};
/*
** Translate a single UTF-8 character.  Return the unicode value.
**
** During translation, assume that the byte that zTerm points
** is a 0x00.
**
** Write a pointer to the next unread byte back into *pzNext.
**
** Notes On Invalid UTF-8:
**
**  *  This routine never allows a 7-bit character (0x00 through 0x7f) to
**     be encoded as a multi-byte character.  Any multi-byte character that
**     attempts to encode a value between 0x00 and 0x7f is rendered as 0xfffd.
**
**  *  This routine never allows a UTF16 surrogate value to be encoded.
**     If a multi-byte character attempts to encode a value between
**     0xd800 and 0xe000 then it is rendered as 0xfffd.
**
**  *  Bytes in the range of 0x80 through 0xbf which occur as the first
**     byte of a character are interpreted as single-byte characters
**     and rendered as themselves even though they are technically
**     invalid characters.
**
**  *  This routine accepts an infinite number of different UTF8 encodings
**     for unicode values 0x80 and greater.  It do not change over-length
**     encodings to 0xfffd as some systems recommend.
*/
#define READ_UTF8(zIn, zTerm, c)                           \
  c = *(zIn++);                                            \
  if( c>=0xc0 ){                                           \
    c = UtfTrans1[c-0xc0];                                 \
    while( zIn!=zTerm && (*zIn & 0xc0)==0x80 ){            \
      c = (c<<6) + (0x3f & *(zIn++));                      \
    }                                                      \
    if( c<0x80                                             \
        || (c&0xFFFFF800)==0xD800                          \
        || (c&0xFFFFFFFE)==0xFFFE ){  c = 0xFFFD; }        \
  }
JX9_PRIVATE int jx9Utf8Read(
  const unsigned char *z,         /* First byte of UTF-8 character */
  const unsigned char *zTerm,     /* Pretend this byte is 0x00 */
  const unsigned char **pzNext    /* Write first byte past UTF-8 char here */
){
  int c;
  READ_UTF8(z, zTerm, c);
  *pzNext = z;
  return c;
}
/*
 * string utf8_decode(string $data)
 *  This function decodes data, assumed to be UTF-8 encoded, to unicode.
 * Parameters
 * data
 *  An UTF-8 encoded string.
 * Return
 *  Unicode decoded string or NULL on failure.
 */
static int vm_builtin_utf8_decode(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const unsigned char *zIn, *zEnd;
	int nByte, c;
	if( nArg < 1 ){
		/* Missing arguments, return null */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Extract the target string */
	zIn = (const unsigned char *)jx9_value_to_string(apArg[0], &nByte);
	if( nByte < 1 ){
		/* Empty string, return null */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	zEnd = &zIn[nByte];
	/* Start the decoding process */
	while( zIn < zEnd ){
		c = jx9Utf8Read(zIn, zEnd, &zIn);
		if( c == 0x0 ){
			break;
		}
		jx9_result_string(pCtx, (const char *)&c, (int)sizeof(char));
	}
	return JX9_OK;
}
/*
 * string json_encode(mixed $value)
 *  Returns a string containing the JSON representation of value.
 * Parameters
 *  $value
 *  The value being encoded. Can be any type except a resource.
 * Return
 *  Returns a JSON encoded string on success. FALSE otherwise
 */
static int vm_builtin_json_encode(jx9_context *pCtx,int nArg,jx9_value **apArg)
{
	SyBlob sBlob;
	if( nArg < 1 ){
		/* Missing arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Init the working buffer */
	SyBlobInit(&sBlob,&pCtx->pVm->sAllocator);
	/* Perform the encoding operation */
	jx9JsonSerialize(apArg[0],&sBlob);
	/* Return the serialized value */
	jx9_result_string(pCtx,(const char *)SyBlobData(&sBlob),(int)SyBlobLength(&sBlob));
	/* Cleanup */
	SyBlobRelease(&sBlob);
	/* All done */
	return JX9_OK;
}
/*
 * mixed json_decode(string $json)
 *  Takes a JSON encoded string and converts it into a JX9 variable.
 * Parameters
 *  $json
 *    The json string being decoded.
 * Return
 *  The value encoded in json in appropriate JX9 type. Values true, false and null (case-insensitive)
 *  are returned as TRUE, FALSE and NULL respectively. NULL is returned if the json cannot be decoded
 *  or if the encoded data is deeper than the recursion limit.
 */
static int vm_builtin_json_decode(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zJSON;
	int nByte;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Extract the JSON string */
	zJSON = jx9_value_to_string(apArg[0], &nByte);
	if( nByte < 1 ){
		/* Empty string, return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Decode the raw JSON */
	jx9JsonDecode(pCtx,zJSON,nByte);
	return JX9_OK;
}
/* Table of built-in VM functions. */
static const jx9_builtin_func aVmFunc[] = {
	     /* JSON Encoding/Decoding */
	{ "json_encode",     vm_builtin_json_encode   },
	{ "json_decode",     vm_builtin_json_decode   },
	     /* Functions calls */
	{ "func_num_args"  , vm_builtin_func_num_args }, 
	{ "func_get_arg"   , vm_builtin_func_get_arg  }, 
	{ "func_get_args"  , vm_builtin_func_get_args }, 
	{ "function_exists", vm_builtin_func_exists   }, 
	{ "is_callable"    , vm_builtin_is_callable   }, 
	{ "get_defined_functions", vm_builtin_get_defined_func },  
	    /* Constants management */
	{ "defined",  vm_builtin_defined              }, 
	{ "get_defined_constants", vm_builtin_get_defined_constants }, 
	   /* Random numbers/strings generators */
	{ "rand",          vm_builtin_rand            }, 
	{ "rand_str",      vm_builtin_rand_str        }, 
	{ "getrandmax",    vm_builtin_getrandmax      }, 
	   /* Language constructs functions */
	{ "print", vm_builtin_print                   }, 
	{ "exit",  vm_builtin_exit                    }, 
	{ "die",   vm_builtin_exit                    },  
	  /* Variable handling functions */ 
	{ "gettype",   vm_builtin_gettype              }, 
	{ "get_resource_type", vm_builtin_get_resource_type},
	 /* Variable dumping */
	{ "dump",     vm_builtin_dump                 },
	  /* Release info */
	{"jx9_version",       vm_builtin_jx9_version  }, 
	{"jx9_credits",       vm_builtin_jx9_version  }, 
	{"jx9_info",          vm_builtin_jx9_version  },
	{"jx9_copyright",     vm_builtin_jx9_version  }, 
	  /* hashmap */
	{"extract",          vm_builtin_extract       }, 
	  /* URL related function */
	{"parse_url",        vm_builtin_parse_url     }, 
	   /* UTF-8 encoding/decoding */
	{"utf8_encode",    vm_builtin_utf8_encode}, 
	{"utf8_decode",    vm_builtin_utf8_decode}, 
	   /* Command line processing */
	{"getopt",         vm_builtin_getopt     }, 
	   /* Files/URI inclusion facility */
	{ "include",      vm_builtin_include          }, 
	{ "import", vm_builtin_import     }
};
/*
 * Register the built-in VM functions defined above.
 */
static sxi32 VmRegisterSpecialFunction(jx9_vm *pVm)
{
	sxi32 rc;
	sxu32 n;
	for( n = 0 ; n < SX_ARRAYSIZE(aVmFunc) ; ++n ){
		/* Note that these special functions have access
		 * to the underlying virtual machine as their
		 * private data.
		 */
		rc = jx9_create_function(&(*pVm), aVmFunc[n].zName, aVmFunc[n].xFunc, &(*pVm));
		if( rc != SXRET_OK ){
			return rc;
		}
	}
	return SXRET_OK;
}
#ifndef JX9_DISABLE_BUILTIN_FUNC
/*
 * Extract the IO stream device associated with a given scheme.
 * Return a pointer to an instance of jx9_io_stream when the scheme
 * have an associated IO stream registered with it. NULL otherwise.
 * If no scheme:// is avalilable then the file:// scheme is assumed.
 * For more information on how to register IO stream devices, please
 * refer to the official documentation.
 */
JX9_PRIVATE const jx9_io_stream * jx9VmGetStreamDevice(
	jx9_vm *pVm,           /* Target VM */
	const char **pzDevice, /* Full path, URI, ... */
	int nByte              /* *pzDevice length*/
	)
{
	const char *zIn, *zEnd, *zCur, *zNext;
	jx9_io_stream **apStream, *pStream;
	SyString sDev, sCur;
	sxu32 n, nEntry;
	int rc;
	/* Check if a scheme [i.e: file://, http://, zip://...] is available */
	zNext = zCur = zIn = *pzDevice;
	zEnd = &zIn[nByte];
	while( zIn < zEnd ){
		if( zIn < &zEnd[-3]/*://*/ && zIn[0] == ':' && zIn[1] == '/' && zIn[2] == '/' ){
			/* Got one */
			zNext = &zIn[sizeof("://")-1];
			break;
		}
		/* Advance the cursor */
		zIn++;
	}
	if( zIn >= zEnd ){
		/* No such scheme, return the default stream */
		return pVm->pDefStream;
	}
	SyStringInitFromBuf(&sDev, zCur, zIn-zCur);
	/* Remove leading and trailing white spaces */
	SyStringFullTrim(&sDev);
	/* Perform a linear lookup on the installed stream devices */
	apStream = (jx9_io_stream **)SySetBasePtr(&pVm->aIOstream);
	nEntry = SySetUsed(&pVm->aIOstream);
	for( n = 0 ; n < nEntry ; n++ ){
		pStream = apStream[n];
		SyStringInitFromBuf(&sCur, pStream->zName, SyStrlen(pStream->zName));
		/* Perfrom a case-insensitive comparison */
		rc = SyStringCmp(&sDev, &sCur, SyStrnicmp);
		if( rc == 0 ){
			/* Stream device found */
			*pzDevice = zNext;
			return pStream;
		}
	}
	/* No such stream, return NULL */
	return 0;
}
#endif /* JX9_DISABLE_BUILTIN_FUNC */
/*
 * Section:
 *    HTTP/URI related routines.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */ 
 /*
  * URI Parser: Split an URI into components [i.e: Host, Path, Query, ...].
  * URI syntax: [method:/][/[user[:pwd]@]host[:port]/][document]
  * This almost, but not quite, RFC1738 URI syntax.
  * This routine is not a validator, it does not check for validity
  * nor decode URI parts, the only thing this routine does is splitting
  * the input to its fields.
  * Upper layer are responsible of decoding and validating URI parts.
  * On success, this function populate the "SyhttpUri" structure passed
  * as the first argument. Otherwise SXERR_* is returned when a malformed
  * input is encountered.
  */
 static sxi32 VmHttpSplitURI(SyhttpUri *pOut, const char *zUri, sxu32 nLen)
 {
	 const char *zEnd = &zUri[nLen];
	 sxu8 bHostOnly = FALSE;
	 sxu8 bIPv6 = FALSE	; 
	 const char *zCur;
	 SyString *pComp;
	 sxu32 nPos = 0;
	 sxi32 rc;
	 /* Zero the structure first */
	 SyZero(pOut, sizeof(SyhttpUri));
	 /* Remove leading and trailing white spaces  */
	 SyStringInitFromBuf(&pOut->sRaw, zUri, nLen);
	 SyStringFullTrim(&pOut->sRaw);
	 /* Find the first '/' separator */
	 rc = SyByteFind(zUri, (sxu32)(zEnd - zUri), '/', &nPos);
	 if( rc != SXRET_OK ){
		 /* Assume a host name only */
		 zCur = zEnd;
		 bHostOnly = TRUE;
		 goto ProcessHost;
	 }
	 zCur = &zUri[nPos];
	 if( zUri != zCur && zCur[-1] == ':' ){
		 /* Extract a scheme:
		  * Not that we can get an invalid scheme here.
		  * Fortunately the caller can discard any URI by comparing this scheme with its 
		  * registered schemes and will report the error as soon as his comparison function
		  * fail.
		  */
	 	pComp = &pOut->sScheme;
		SyStringInitFromBuf(pComp, zUri, (sxu32)(zCur - zUri - 1));
		SyStringLeftTrim(pComp);		
	 }
	 if( zCur[1] != '/' ){
		 if( zCur == zUri || zCur[-1] == ':' ){
		  /* No authority */
		  goto PathSplit;
		}
		 /* There is something here , we will assume its an authority
		  * and someone has forgot the two prefix slashes "//", 
		  * sooner or later we will detect if we are dealing with a malicious
		  * user or not, but now assume we are dealing with an authority
		  * and let the caller handle all the validation process.
		  */
		 goto ProcessHost;
	 }	 
	 zUri = &zCur[2];
	 zCur = zEnd;
	 rc = SyByteFind(zUri, (sxu32)(zEnd - zUri), '/', &nPos);
	 if( rc == SXRET_OK ){
		 zCur = &zUri[nPos];
	 }
 ProcessHost:
	 /* Extract user information if present */
	 rc = SyByteFind(zUri, (sxu32)(zCur - zUri), '@', &nPos);
	 if( rc == SXRET_OK ){
		 if( nPos > 0 ){
			 sxu32 nPassOfft; /* Password offset */
			 pComp = &pOut->sUser;
			 SyStringInitFromBuf(pComp, zUri, nPos);
			 /* Extract the password if available */
			 rc = SyByteFind(zUri, (sxu32)(zCur - zUri), ':', &nPassOfft);
			 if( rc == SXRET_OK && nPassOfft < nPos){
				 pComp->nByte = nPassOfft;
				 pComp = &pOut->sPass;
				 pComp->zString = &zUri[nPassOfft+sizeof(char)];
				 pComp->nByte = nPos - nPassOfft - 1;
			 }
			 /* Update the cursor */
			 zUri = &zUri[nPos+1];
		 }else{
			 zUri++;
		 }
	 }
	 pComp = &pOut->sHost;
	 while( zUri < zCur && SyisSpace(zUri[0])){
		 zUri++;
	 }	
	 SyStringInitFromBuf(pComp, zUri, (sxu32)(zCur - zUri));
	 if( pComp->zString[0] == '[' ){
		 /* An IPv6 Address: Make a simple naive test
		  */
		 zUri++; pComp->zString++; pComp->nByte = 0;
		 while( ((unsigned char)zUri[0] < 0xc0 && SyisHex(zUri[0])) || zUri[0] == ':' ){
			 zUri++; pComp->nByte++;
		 }
		 if( zUri[0] != ']' ){
			 return SXERR_CORRUPT; /* Malformed IPv6 address */
		 }
		 zUri++;
		 bIPv6 = TRUE;
	 }
	 /* Extract a port number if available */
	 rc = SyByteFind(zUri, (sxu32)(zCur - zUri), ':', &nPos);
	 if( rc == SXRET_OK ){
		 if( bIPv6 == FALSE ){
			 pComp->nByte = (sxu32)(&zUri[nPos] - zUri);
		 }
		 pComp = &pOut->sPort;
		 SyStringInitFromBuf(pComp, &zUri[nPos+1], (sxu32)(zCur - &zUri[nPos+1]));	
	 }
	 if( bHostOnly == TRUE ){
		 return SXRET_OK;
	 }
PathSplit:
	 zUri = zCur;
	 pComp = &pOut->sPath;
	 SyStringInitFromBuf(pComp, zUri, (sxu32)(zEnd-zUri));
	 if( pComp->nByte == 0 ){
		 return SXRET_OK; /* Empty path */
	 }
	 if( SXRET_OK == SyByteFind(zUri, (sxu32)(zEnd-zUri), '?', &nPos) ){
		 pComp->nByte = nPos; /* Update path length */
		 pComp = &pOut->sQuery;
		 SyStringInitFromBuf(pComp, &zUri[nPos+1], (sxu32)(zEnd-&zUri[nPos+1]));
	 }
	 if( SXRET_OK == SyByteFind(zUri, (sxu32)(zEnd-zUri), '#', &nPos) ){
		 /* Update path or query length */
		 if( pComp == &pOut->sPath ){
			 pComp->nByte = nPos;
		 }else{
			 if( &zUri[nPos] < (char *)SyStringData(pComp) ){
				 /* Malformed syntax : Query must be present before fragment */
				 return SXERR_SYNTAX;
			 }
			 pComp->nByte -= (sxu32)(zEnd - &zUri[nPos]);
		 }
		 pComp = &pOut->sFragment;
		 SyStringInitFromBuf(pComp, &zUri[nPos+1], (sxu32)(zEnd-&zUri[nPos+1]))
	 }
	 return SXRET_OK;
 }
 /*
 * Extract a single line from a raw HTTP request.
 * Return SXRET_OK on success, SXERR_EOF when end of input
 * and SXERR_MORE when more input is needed.
 */
static sxi32 VmGetNextLine(SyString *pCursor, SyString *pCurrent)
{ 
  	const char *zIn;
  	sxu32 nPos; 
	/* Jump leading white spaces */
	SyStringLeftTrim(pCursor);
	if( pCursor->nByte < 1 ){
		SyStringInitFromBuf(pCurrent, 0, 0);
		return SXERR_EOF; /* End of input */
	}
	zIn = SyStringData(pCursor);
	if( SXRET_OK != SyByteListFind(pCursor->zString, pCursor->nByte, "\r\n", &nPos) ){
		/* Line not found, tell the caller to read more input from source */
		SyStringDupPtr(pCurrent, pCursor);
		return SXERR_MORE;
	}
  	pCurrent->zString = zIn;
  	pCurrent->nByte	= nPos;	
  	/* advance the cursor so we can call this routine again */
  	pCursor->zString = &zIn[nPos];
  	pCursor->nByte -= nPos;
  	return SXRET_OK;
 }
 /*
  * Split a single MIME header into a name value pair. 
  * This function return SXRET_OK, SXERR_CONTINUE on success.
  * Otherwise SXERR_NEXT is returned when a malformed header
  * is encountered.
  * Note: This function handle also mult-line headers.
  */
 static sxi32 VmHttpProcessOneHeader(SyhttpHeader *pHdr, SyhttpHeader *pLast, const char *zLine, sxu32 nLen)
 {
	 SyString *pName;
	 sxu32 nPos;
	 sxi32 rc;
	 if( nLen < 1 ){
		 return SXERR_NEXT;
	 }
	 /* Check for multi-line header */
	if( pLast && (zLine[-1] == ' ' || zLine[-1] == '\t') ){
		SyString *pTmp = &pLast->sValue;
		SyStringFullTrim(pTmp);
		if( pTmp->nByte == 0 ){
			SyStringInitFromBuf(pTmp, zLine, nLen);
		}else{
			/* Update header value length */
			pTmp->nByte = (sxu32)(&zLine[nLen] - pTmp->zString);
		}
		 /* Simply tell the caller to reset its states and get another line */
		 return SXERR_CONTINUE;
	 }
	/* Split the header */
	pName = &pHdr->sName;
	rc = SyByteFind(zLine, nLen, ':', &nPos);
	if(rc != SXRET_OK ){
		return SXERR_NEXT; /* Malformed header;Check the next entry */
	}
	SyStringInitFromBuf(pName, zLine, nPos);
	SyStringFullTrim(pName);
	/* Extract a header value */
	SyStringInitFromBuf(&pHdr->sValue, &zLine[nPos + 1], nLen - nPos - 1);
	/* Remove leading and trailing whitespaces */
	SyStringFullTrim(&pHdr->sValue);
	return SXRET_OK;
 }
 /*
  * Extract all MIME headers associated with a HTTP request.
  * After processing the first line of a HTTP request, the following
  * routine is called in order to extract MIME headers.
  * This function return SXRET_OK on success, SXERR_MORE when it needs
  * more inputs.
  * Note: Any malformed header is simply discarded.
  */
 static sxi32 VmHttpExtractHeaders(SyString *pRequest, SySet *pOut)
 {
	 SyhttpHeader *pLast = 0;
	 SyString sCurrent;
	 SyhttpHeader sHdr;
	 sxu8 bEol;
	 sxi32 rc;
	 if( SySetUsed(pOut) > 0 ){
		 pLast = (SyhttpHeader *)SySetAt(pOut, SySetUsed(pOut)-1);
	 }
	 bEol = FALSE;
	 for(;;){
		 SyZero(&sHdr, sizeof(SyhttpHeader));
		 /* Extract a single line from the raw HTTP request */
		 rc = VmGetNextLine(pRequest, &sCurrent);
		 if(rc != SXRET_OK ){
			 if( sCurrent.nByte < 1 ){
				 break;
			 }
			 bEol = TRUE;
		 }
		 /* Process the header */
		 if( SXRET_OK == VmHttpProcessOneHeader(&sHdr, pLast, sCurrent.zString, sCurrent.nByte)){
			 if( SXRET_OK != SySetPut(pOut, (const void *)&sHdr) ){
				 break;
			 }
			 /* Retrieve the last parsed header so we can handle multi-line header
			  * in case we face one of them.
			  */
			 pLast = (SyhttpHeader *)SySetPeek(pOut);
		 }
		 if( bEol ){
			 break;
		 }
	 } /* for(;;) */
	 return SXRET_OK;
 }
 /*
  * Process the first line of a HTTP request.
  * This routine perform the following operations
  *  1) Extract the HTTP method.
  *  2) Split the request URI to it's fields [ie: host, path, query, ...].
  *  3) Extract the HTTP protocol version.
  */
 static sxi32 VmHttpProcessFirstLine(
	 SyString *pRequest, /* Raw HTTP request */
	 sxi32 *pMethod,     /* OUT: HTTP method */
	 SyhttpUri *pUri,    /* OUT: Parse of the URI */
	 sxi32 *pProto       /* OUT: HTTP protocol */
	 )
 {
	 static const char *azMethods[] = { "get", "post", "head", "put"};
	 static const sxi32 aMethods[]  = { HTTP_METHOD_GET, HTTP_METHOD_POST, HTTP_METHOD_HEAD, HTTP_METHOD_PUT};
	 const char *zIn, *zEnd, *zPtr;
	 SyString sLine;
	 sxu32 nLen;
	 sxi32 rc;
	 /* Extract the first line and update the pointer */
	 rc = VmGetNextLine(pRequest, &sLine);
	 if( rc != SXRET_OK ){
		 return rc;
	 }
	 if ( sLine.nByte < 1 ){
		 /* Empty HTTP request */
		 return SXERR_EMPTY;
	 }
	 /* Delimit the line and ignore trailing and leading white spaces */
	 zIn = sLine.zString;
	 zEnd = &zIn[sLine.nByte];
	 while( zIn < zEnd && (unsigned char)zIn[0] < 0xc0 && SyisSpace(zIn[0]) ){
		 zIn++;
	 }
	 /* Extract the HTTP method */
	 zPtr = zIn;
	 while( zIn < zEnd && !SyisSpace(zIn[0]) ){
		 zIn++;
	 }
	 *pMethod = HTTP_METHOD_OTHR;
	 if( zIn > zPtr ){
		 sxu32 i;
		 nLen = (sxu32)(zIn-zPtr);
		 for( i = 0 ; i < SX_ARRAYSIZE(azMethods) ; ++i ){
			 if( SyStrnicmp(azMethods[i], zPtr, nLen) == 0 ){
				 *pMethod = aMethods[i];
				 break;
			 }
		 }
	 }
	 /* Jump trailing white spaces */
	 while( zIn < zEnd && (unsigned char)zIn[0] < 0xc0 && SyisSpace(zIn[0]) ){
		 zIn++;
	 }
	  /* Extract the request URI */
	 zPtr = zIn;
	 while( zIn < zEnd && !SyisSpace(zIn[0]) ){
		 zIn++;
	 } 
	 if( zIn > zPtr ){
		 nLen = (sxu32)(zIn-zPtr);
		 /* Split raw URI to it's fields */
		 VmHttpSplitURI(pUri, zPtr, nLen);
	 }
	 /* Jump trailing white spaces */
	 while( zIn < zEnd && (unsigned char)zIn[0] < 0xc0 && SyisSpace(zIn[0]) ){
		 zIn++;
	 }
	 /* Extract the HTTP version */
	 zPtr = zIn;
	 while( zIn < zEnd && !SyisSpace(zIn[0]) ){
		 zIn++;
	 }
	 *pProto = HTTP_PROTO_11; /* HTTP/1.1 */
	 rc = 1;
	 if( zIn > zPtr ){
		 rc = SyStrnicmp(zPtr, "http/1.0", (sxu32)(zIn-zPtr));
	 }
	 if( !rc ){
		 *pProto = HTTP_PROTO_10; /* HTTP/1.0 */
	 }
	 return SXRET_OK;
 }
 /*
  * Tokenize, decode and split a raw query encoded as: "x-www-form-urlencoded" 
  * into a name value pair.
  * Note that this encoding is implicit in GET based requests.
  * After the tokenization process, register the decoded queries
  * in the $_GET/$_POST/$_REQUEST superglobals arrays.
  */
 static sxi32 VmHttpSplitEncodedQuery(
	 jx9_vm *pVm,       /* Target VM */
	 SyString *pQuery,  /* Raw query to decode */
	 SyBlob *pWorker,   /* Working buffer */
	 int is_post        /* TRUE if we are dealing with a POST request */
	 )
 {
	 const char *zEnd = &pQuery->zString[pQuery->nByte];
	 const char *zIn = pQuery->zString;
	 jx9_value *pGet, *pRequest;
	 SyString sName, sValue;
	 const char *zPtr;
	 sxu32 nBlobOfft;
	 /* Extract superglobals */
	 if( is_post ){
		 /* $_POST superglobal */
		 pGet = VmExtractSuper(&(*pVm), "_POST", sizeof("_POST")-1);
	 }else{
		 /* $_GET superglobal */
		 pGet = VmExtractSuper(&(*pVm), "_GET", sizeof("_GET")-1);
	 }
	 pRequest = VmExtractSuper(&(*pVm), "_REQUEST", sizeof("_REQUEST")-1);
	 /* Split up the raw query */
	 for(;;){
		 /* Jump leading white spaces */
		 while(zIn < zEnd  && SyisSpace(zIn[0]) ){
			 zIn++;
		 }
		 if( zIn >= zEnd ){
			 break;
		 }
		 zPtr = zIn;
		 while( zPtr < zEnd && zPtr[0] != '=' && zPtr[0] != '&' && zPtr[0] != ';' ){
			 zPtr++;
		 }
		 /* Reset the working buffer */
		 SyBlobReset(pWorker);
		 /* Decode the entry */
		 SyUriDecode(zIn, (sxu32)(zPtr-zIn), jx9VmBlobConsumer, pWorker, TRUE);
		 /* Save the entry */
		 sName.nByte = SyBlobLength(pWorker);
		 sValue.zString = 0;
		 sValue.nByte = 0;
		 if( zPtr < zEnd && zPtr[0] == '=' ){
			 zPtr++;
			 zIn = zPtr;
			 /* Store field value */
			 while( zPtr < zEnd && zPtr[0] != '&' && zPtr[0] != ';' ){
				 zPtr++;
			 }
			 if( zPtr > zIn ){
				 /* Decode the value */
				  nBlobOfft = SyBlobLength(pWorker);
				  SyUriDecode(zIn, (sxu32)(zPtr-zIn), jx9VmBlobConsumer, pWorker, TRUE);
				  sValue.zString = (const char *)SyBlobDataAt(pWorker, nBlobOfft);
				  sValue.nByte = SyBlobLength(pWorker) - nBlobOfft;
				 
			 }
			 /* Synchronize pointers */
			 zIn = zPtr;
		 }
		 sName.zString = (const char *)SyBlobData(pWorker);
		 /* Install the decoded query in the $_GET/$_REQUEST array */
		 if( pGet && (pGet->iFlags & MEMOBJ_HASHMAP) ){
			 VmHashmapInsert((jx9_hashmap *)pGet->x.pOther, 
				 sName.zString, (int)sName.nByte, 
				 sValue.zString, (int)sValue.nByte
				 );
		 }
		 if( pRequest && (pRequest->iFlags & MEMOBJ_HASHMAP) ){
			 VmHashmapInsert((jx9_hashmap *)pRequest->x.pOther, 
				 sName.zString, (int)sName.nByte, 
				 sValue.zString, (int)sValue.nByte
					 );
		 }
		 /* Advance the pointer */
		 zIn = &zPtr[1];
	 }
	/* All done*/
	return SXRET_OK;
 }
 /*
  * Extract MIME header value from the given set.
  * Return header value on success. NULL otherwise.
  */
 static SyString * VmHttpExtractHeaderValue(SySet *pSet, const char *zMime, sxu32 nByte)
 {
	 SyhttpHeader *aMime, *pMime;
	 SyString sMime;
	 sxu32 n;
	 SyStringInitFromBuf(&sMime, zMime, nByte);
	 /* Point to the MIME entries */
	 aMime = (SyhttpHeader *)SySetBasePtr(pSet);
	 /* Perform the lookup */
	 for( n = 0 ; n < SySetUsed(pSet) ; ++n ){
		 pMime = &aMime[n];
		 if( SyStringCmp(&sMime, &pMime->sName, SyStrnicmp) == 0 ){
			 /* Header found, return it's associated value */
			 return &pMime->sValue;
		 }
	 }
	 /* No such MIME header */
	 return 0;
 }
 /*
  * Tokenize and decode a raw "Cookie:" MIME header into a name value pair
  * and insert it's fields [i.e name, value] in the $_COOKIE superglobal.
  */
 static sxi32 VmHttpPorcessCookie(jx9_vm *pVm, SyBlob *pWorker, const char *zIn, sxu32 nByte)
 {
	 const char *zPtr, *zDelimiter, *zEnd = &zIn[nByte];
	 SyString sName, sValue;
	 jx9_value *pCookie;
	 sxu32 nOfft;
	 /* Make sure the $_COOKIE superglobal is available */
	 pCookie = VmExtractSuper(&(*pVm), "_COOKIE", sizeof("_COOKIE")-1);
	 if( pCookie == 0 || (pCookie->iFlags & MEMOBJ_HASHMAP) == 0 ){
		 /* $_COOKIE superglobal not available */
		 return SXERR_NOTFOUND;
	 }	
	 for(;;){
		  /* Jump leading white spaces */
		 while( zIn < zEnd && SyisSpace(zIn[0]) ){
			 zIn++;
		 }
		 if( zIn >= zEnd ){
			 break;
		 }
		  /* Reset the working buffer */
		 SyBlobReset(pWorker);
		 zDelimiter = zIn;
		 /* Delimit the name[=value]; pair */ 
		 while( zDelimiter < zEnd && zDelimiter[0] != ';' ){
			 zDelimiter++;
		 }
		 zPtr = zIn;
		 while( zPtr < zDelimiter && zPtr[0] != '=' ){
			 zPtr++;
		 }
		 /* Decode the cookie */
		 SyUriDecode(zIn, (sxu32)(zPtr-zIn), jx9VmBlobConsumer, pWorker, TRUE);
		 sName.nByte = SyBlobLength(pWorker);
		 zPtr++;
		 sValue.zString = 0;
		 sValue.nByte = 0;
		 if( zPtr < zDelimiter ){
			 /* Got a Cookie value */
			 nOfft = SyBlobLength(pWorker);
			 SyUriDecode(zPtr, (sxu32)(zDelimiter-zPtr), jx9VmBlobConsumer, pWorker, TRUE);
			 SyStringInitFromBuf(&sValue, SyBlobDataAt(pWorker, nOfft), SyBlobLength(pWorker)-nOfft);
		 }
		 /* Synchronize pointers */
		 zIn = &zDelimiter[1];
		 /* Perform the insertion */
		 sName.zString = (const char *)SyBlobData(pWorker);
		 VmHashmapInsert((jx9_hashmap *)pCookie->x.pOther, 
			 sName.zString, (int)sName.nByte, 
			 sValue.zString, (int)sValue.nByte
			 );
	 }
	 return SXRET_OK;
 }
 /*
  * Process a full HTTP request and populate the appropriate arrays
  * such as $_SERVER, $_GET, $_POST, $_COOKIE, $_REQUEST, ... with the information
  * extracted from the raw HTTP request. As an extension Symisc introduced 
  * the $_HEADER array which hold a copy of the processed HTTP MIME headers
  * and their associated values. [i.e: $_HEADER['Server'], $_HEADER['User-Agent'], ...].
  * This function return SXRET_OK on success. Any other return value indicates
  * a malformed HTTP request.
  */
 static sxi32 VmHttpProcessRequest(jx9_vm *pVm, const char *zRequest, int nByte)
 {
	 SyString *pName, *pValue, sRequest; /* Raw HTTP request */
	 jx9_value *pHeaderArray;          /* $_HEADER superglobal (Symisc eXtension to the JX9 specification)*/
	 SyhttpHeader *pHeader;            /* MIME header */
	 SyhttpUri sUri;     /* Parse of the raw URI*/
	 SyBlob sWorker;     /* General purpose working buffer */
	 SySet sHeader;      /* MIME headers set */
	 sxi32 iMethod;      /* HTTP method [i.e: GET, POST, HEAD...]*/
	 sxi32 iVer;         /* HTTP protocol version */
	 sxi32 rc;
	 SyStringInitFromBuf(&sRequest, zRequest, nByte);
	 SySetInit(&sHeader, &pVm->sAllocator, sizeof(SyhttpHeader));
	 SyBlobInit(&sWorker, &pVm->sAllocator);
	 /* Ignore leading and trailing white spaces*/
	 SyStringFullTrim(&sRequest);
	 /* Process the first line */
	 rc = VmHttpProcessFirstLine(&sRequest, &iMethod, &sUri, &iVer);
	 if( rc != SXRET_OK ){
		 return rc;
	 }
	 /* Process MIME headers */
	 VmHttpExtractHeaders(&sRequest, &sHeader);
	 /*
	  * Setup $_SERVER environments 
	  */
	 /* 'SERVER_PROTOCOL': Name and revision of the information protocol via which the page was requested */
	 jx9_vm_config(pVm, 
		 JX9_VM_CONFIG_SERVER_ATTR, 
		 "SERVER_PROTOCOL", 
		 iVer == HTTP_PROTO_10 ? "HTTP/1.0" : "HTTP/1.1", 
		 sizeof("HTTP/1.1")-1
		 );
	 /* 'REQUEST_METHOD':  Which request method was used to access the page */
	 jx9_vm_config(pVm, 
		 JX9_VM_CONFIG_SERVER_ATTR, 
		 "REQUEST_METHOD", 
		 iMethod == HTTP_METHOD_GET ?   "GET" : 
		 (iMethod == HTTP_METHOD_POST ? "POST":
		 (iMethod == HTTP_METHOD_PUT  ? "PUT" :
		 (iMethod == HTTP_METHOD_HEAD ?  "HEAD" : "OTHER"))), 
		 -1 /* Compute attribute length automatically */
		 );
	 if( SyStringLength(&sUri.sQuery) > 0 && iMethod == HTTP_METHOD_GET ){
		 pValue = &sUri.sQuery;
		 /* 'QUERY_STRING': The query string, if any, via which the page was accessed */
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "QUERY_STRING", 
			 pValue->zString, 
			 pValue->nByte
			 );
		 /* Decoded the raw query */
		 VmHttpSplitEncodedQuery(&(*pVm), pValue, &sWorker, FALSE);
	 }
	 /* REQUEST_URI: The URI which was given in order to access this page; for instance, '/index.html' */
	 pValue = &sUri.sRaw;
	 jx9_vm_config(pVm, 
		 JX9_VM_CONFIG_SERVER_ATTR, 
		 "REQUEST_URI", 
		 pValue->zString, 
		 pValue->nByte
		 );
	 /*
	  * 'PATH_INFO'
	  * 'ORIG_PATH_INFO' 
      * Contains any client-provided pathname information trailing the actual script filename but preceding
	  * the query string, if available. For instance, if the current script was accessed via the URL
	  * http://www.example.com/jx9/path_info.jx9/some/stuff?foo=bar, then $_SERVER['PATH_INFO'] would contain
	  * /some/stuff. 
	  */
	 pValue = &sUri.sPath;
	 jx9_vm_config(pVm, 
		 JX9_VM_CONFIG_SERVER_ATTR, 
		 "PATH_INFO", 
		 pValue->zString, 
		 pValue->nByte
		 );
	 jx9_vm_config(pVm, 
		 JX9_VM_CONFIG_SERVER_ATTR, 
		 "ORIG_PATH_INFO", 
		 pValue->zString, 
		 pValue->nByte
		 );
	 /* 'HTTP_ACCEPT': Contents of the Accept: header from the current request, if there is one */
	 pValue = VmHttpExtractHeaderValue(&sHeader, "Accept", sizeof("Accept")-1);
	 if( pValue ){
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "HTTP_ACCEPT", 
			 pValue->zString, 
			 pValue->nByte
		 );
	 }
	 /* 'HTTP_ACCEPT_CHARSET': Contents of the Accept-Charset: header from the current request, if there is one. */
	 pValue = VmHttpExtractHeaderValue(&sHeader, "Accept-Charset", sizeof("Accept-Charset")-1);
	 if( pValue ){
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "HTTP_ACCEPT_CHARSET", 
			 pValue->zString, 
			 pValue->nByte
		 );
	 }
	 /* 'HTTP_ACCEPT_ENCODING': Contents of the Accept-Encoding: header from the current request, if there is one. */
	 pValue = VmHttpExtractHeaderValue(&sHeader, "Accept-Encoding", sizeof("Accept-Encoding")-1);
	 if( pValue ){
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "HTTP_ACCEPT_ENCODING", 
			 pValue->zString, 
			 pValue->nByte
		 );
	 }
	  /* 'HTTP_ACCEPT_LANGUAGE': Contents of the Accept-Language: header from the current request, if there is one */
	 pValue = VmHttpExtractHeaderValue(&sHeader, "Accept-Language", sizeof("Accept-Language")-1);
	 if( pValue ){
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "HTTP_ACCEPT_LANGUAGE", 
			 pValue->zString, 
			 pValue->nByte
		 );
	 }
	 /* 'HTTP_CONNECTION': Contents of the Connection: header from the current request, if there is one. */
	 pValue = VmHttpExtractHeaderValue(&sHeader, "Connection", sizeof("Connection")-1);
	 if( pValue ){
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "HTTP_CONNECTION", 
			 pValue->zString, 
			 pValue->nByte
		 );
	 }
	 /* 'HTTP_HOST': Contents of the Host: header from the current request, if there is one. */
	 pValue = VmHttpExtractHeaderValue(&sHeader, "Host", sizeof("Host")-1);
	 if( pValue ){
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "HTTP_HOST", 
			 pValue->zString, 
			 pValue->nByte
		 );
	 }
	 /* 'HTTP_REFERER': Contents of the Referer: header from the current request, if there is one. */
	 pValue = VmHttpExtractHeaderValue(&sHeader, "Referer", sizeof("Referer")-1);
	 if( pValue ){
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "HTTP_REFERER", 
			 pValue->zString, 
			 pValue->nByte
		 );
	 }
	 /* 'HTTP_USER_AGENT': Contents of the Referer: header from the current request, if there is one. */
	 pValue = VmHttpExtractHeaderValue(&sHeader, "User-Agent", sizeof("User-Agent")-1);
	 if( pValue ){
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "HTTP_USER_AGENT", 
			 pValue->zString, 
			 pValue->nByte
		 );
	 }
	  /* 'JX9_AUTH_DIGEST': When doing Digest HTTP authentication this variable is set to the 'Authorization'
	   * header sent by the client (which you should then use to make the appropriate validation).
	   */
	 pValue = VmHttpExtractHeaderValue(&sHeader, "Authorization", sizeof("Authorization")-1);
	 if( pValue ){
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "JX9_AUTH_DIGEST", 
			 pValue->zString, 
			 pValue->nByte
		 );
		 jx9_vm_config(pVm, 
			 JX9_VM_CONFIG_SERVER_ATTR, 
			 "JX9_AUTH", 
			 pValue->zString, 
			 pValue->nByte
		 );
	 }
	 /* Install all clients HTTP headers in the $_HEADER superglobal */
	 pHeaderArray = VmExtractSuper(&(*pVm), "_HEADER", sizeof("_HEADER")-1);
	 /* Iterate throw the available MIME headers*/
	 SySetResetCursor(&sHeader);
	 pHeader = 0; /* stupid cc warning */
	 while( SXRET_OK == SySetGetNextEntry(&sHeader, (void **)&pHeader) ){
		 pName  = &pHeader->sName;
		 pValue = &pHeader->sValue;
		 if( pHeaderArray && (pHeaderArray->iFlags & MEMOBJ_HASHMAP)){
			 /* Insert the MIME header and it's associated value */
			 VmHashmapInsert((jx9_hashmap *)pHeaderArray->x.pOther, 
				 pName->zString, (int)pName->nByte, 
				 pValue->zString, (int)pValue->nByte
				 );
		 }
		 if( pName->nByte == sizeof("Cookie")-1 && SyStrnicmp(pName->zString, "Cookie", sizeof("Cookie")-1) == 0 
			 && pValue->nByte > 0){
				 /* Process the name=value pair and insert them in the $_COOKIE superglobal array */
				 VmHttpPorcessCookie(&(*pVm), &sWorker, pValue->zString, pValue->nByte);
		 }
	 }
	 if( iMethod == HTTP_METHOD_POST ){
		 /* Extract raw POST data */
		 pValue = VmHttpExtractHeaderValue(&sHeader, "Content-Type", sizeof("Content-Type") - 1);
		 if( pValue && pValue->nByte >= sizeof("application/x-www-form-urlencoded") - 1 &&
			 SyMemcmp("application/x-www-form-urlencoded", pValue->zString, pValue->nByte) == 0 ){
				 /* Extract POST data length */
				 pValue = VmHttpExtractHeaderValue(&sHeader, "Content-Length", sizeof("Content-Length") - 1);
				 if( pValue ){
					 sxi32 iLen = 0; /* POST data length */
					 SyStrToInt32(pValue->zString, pValue->nByte, (void *)&iLen, 0);
					 if( iLen > 0 ){
						 /* Remove leading and trailing white spaces */
						 SyStringFullTrim(&sRequest);
						 if( (int)sRequest.nByte > iLen ){
							 sRequest.nByte = (sxu32)iLen;
						 }
						 /* Decode POST data now */
						 VmHttpSplitEncodedQuery(&(*pVm), &sRequest, &sWorker, TRUE);
					 }
				 }
		 }
	 }
	 /* All done, clean-up the mess left behind */
	 SySetRelease(&sHeader);
	 SyBlobRelease(&sWorker);
	 return SXRET_OK;
 }
