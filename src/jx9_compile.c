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
 /* $SymiscID: compile.c v1.7 FreeBSD 2012-12-11 21:46 stable <chm@symisc.net> $ */
#ifndef JX9_AMALGAMATION
#include "jx9Int.h"
#endif
/*
 * This file implement a thread-safe and full-reentrant compiler for the JX9 engine.
 * That is, routines defined in this file takes a stream of tokens and output
 * JX9 bytecode instructions.
 */
/* Forward declaration */
typedef struct LangConstruct LangConstruct;
typedef struct JumpFixup     JumpFixup;
/* Block [i.e: set of statements] control flags */
#define GEN_BLOCK_LOOP        0x001    /* Loop block [i.e: for, while, ...] */
#define GEN_BLOCK_PROTECTED   0x002    /* Protected block */
#define GEN_BLOCK_COND        0x004    /* Conditional block [i.e: if(condition){} ]*/
#define GEN_BLOCK_FUNC        0x008    /* Function body */
#define GEN_BLOCK_GLOBAL      0x010    /* Global block (always set)*/
#define GEN_BLOC_NESTED_FUNC  0x020    /* Nested function body */
#define GEN_BLOCK_EXPR        0x040    /* Expression */
#define GEN_BLOCK_STD         0x080    /* Standard block */
#define GEN_BLOCK_SWITCH      0x100    /* Switch statement */
/*
 * Compilation of some JX9 constructs such as if, for, while, the logical or
 * (||) and logical and (&&) operators in expressions requires the
 * generation of forward jumps.
 * Since the destination PC target of these jumps isn't known when the jumps
 * are emitted, we record each forward jump in an instance of the following
 * structure. Those jumps are fixed later when the jump destination is resolved.
 */
struct JumpFixup
{
	sxi32 nJumpType;     /* Jump type. Either TRUE jump, FALSE jump or Unconditional jump */
	sxu32 nInstrIdx;     /* Instruction index to fix later when the jump destination is resolved. */
};
/*
 * Each language construct is represented by an instance
 * of the following structure.
 */
struct LangConstruct
{
	sxu32 nID;                     /* Language construct ID [i.e: JX9_TKWRD_WHILE, JX9_TKWRD_FOR, JX9_TKWRD_IF...] */
	ProcLangConstruct xConstruct;  /* C function implementing the language construct */
};
/* Compilation flags */
#define JX9_COMPILE_SINGLE_STMT 0x001 /* Compile a single statement */
/* Token stream synchronization macros */
#define SWAP_TOKEN_STREAM(GEN, START, END)\
	pTmp  = GEN->pEnd;\
	pGen->pIn  = START;\
	pGen->pEnd = END
#define UPDATE_TOKEN_STREAM(GEN)\
	if( GEN->pIn < pTmp ){\
	    GEN->pIn++;\
	}\
	GEN->pEnd = pTmp
#define SWAP_DELIMITER(GEN, START, END)\
	pTmpIn  = GEN->pIn;\
	pTmpEnd = GEN->pEnd;\
	GEN->pIn = START;\
	GEN->pEnd = END
#define RE_SWAP_DELIMITER(GEN)\
	GEN->pIn  = pTmpIn;\
	GEN->pEnd = pTmpEnd
/* Flags related to expression compilation */
#define EXPR_FLAG_LOAD_IDX_STORE    0x001 /* Set the iP2 flag when dealing with the LOAD_IDX instruction */
#define EXPR_FLAG_RDONLY_LOAD       0x002 /* Read-only load, refer to the 'JX9_OP_LOAD' VM instruction for more information */
#define EXPR_FLAG_COMMA_STATEMENT   0x004 /* Treat comma expression as a single statement (used by object attributes) */
/* Forward declaration */
static sxi32 jx9CompileExpr(
	jx9_gen_state *pGen, /* Code generator state */
	sxi32 iFlags,        /* Control flags */
	sxi32 (*xTreeValidator)(jx9_gen_state *, jx9_expr_node *) /* Node validator callback.NULL otherwise */
	);

/*
 * Recover from a compile-time error. In other words synchronize
 * the token stream cursor with the first semi-colon seen.
 */
static sxi32 jx9ErrorRecover(jx9_gen_state *pGen)
{
	/* Synchronize with the next-semi-colon and avoid compiling this erroneous statement */
	while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_SEMI /*';'*/) == 0){
		pGen->pIn++;
	}
	return SXRET_OK;
}
/*
 * Check if the given identifier name is reserved or not.
 * Return TRUE if reserved.FALSE otherwise.
 */
static int GenStateIsReservedID(SyString *pName)
{
	if( pName->nByte == sizeof("null") - 1 ){
		if( SyStrnicmp(pName->zString, "null", sizeof("null")-1) == 0 ){
			return TRUE;
		}else if( SyStrnicmp(pName->zString, "true", sizeof("true")-1) == 0 ){
			return TRUE;
		}
	}else if( pName->nByte == sizeof("false") - 1 ){
		if( SyStrnicmp(pName->zString, "false", sizeof("false")-1) == 0 ){
			return TRUE;
		}
	}
	/* Not a reserved constant */
	return FALSE;
}
/*
 * Check if a given token value is installed in the literal table.
 */
static sxi32 GenStateFindLiteral(jx9_gen_state *pGen, const SyString *pValue, sxu32 *pIdx)
{
	SyHashEntry *pEntry;
	pEntry = SyHashGet(&pGen->hLiteral, (const void *)pValue->zString, pValue->nByte);
	if( pEntry == 0 ){
		return SXERR_NOTFOUND;
	}
	*pIdx = (sxu32)SX_PTR_TO_INT(pEntry->pUserData);
	return SXRET_OK;
}
/*
 * Install a given constant index in the literal table.
 * In order to be installed, the jx9_value must be of type string.
 */
static sxi32 GenStateInstallLiteral(jx9_gen_state *pGen,jx9_value *pObj, sxu32 nIdx)
{
	if( SyBlobLength(&pObj->sBlob) > 0 ){
		SyHashInsert(&pGen->hLiteral, SyBlobData(&pObj->sBlob), SyBlobLength(&pObj->sBlob), SX_INT_TO_PTR(nIdx));
	}
	return SXRET_OK;
}
/*
 * Generate a fatal error.
 */
static sxi32 GenStateOutOfMem(jx9_gen_state *pGen)
{
	jx9GenCompileError(pGen,E_ERROR,1,"Fatal, Jx9 compiler is running out of memory");
	/* Abort compilation immediately */
	return SXERR_ABORT;
}
/*
 * Fetch a block that correspond to the given criteria from the stack of
 * compiled blocks.
 * Return a pointer to that block on success. NULL otherwise.
 */
static GenBlock * GenStateFetchBlock(GenBlock *pCurrent, sxi32 iBlockType, sxi32 iCount)
{
	GenBlock *pBlock = pCurrent;
	for(;;){
		if( pBlock->iFlags & iBlockType ){
			iCount--; /* Decrement nesting level */
			if( iCount < 1 ){
				/* Block meet with the desired criteria */
				return pBlock;
			}
		}
		/* Point to the upper block */
		pBlock = pBlock->pParent;
		if( pBlock == 0 || (pBlock->iFlags & (GEN_BLOCK_PROTECTED|GEN_BLOCK_FUNC)) ){
			/* Forbidden */
			break;
		}
	}
	/* No such block */
	return 0;
}
/*
 * Initialize a freshly allocated block instance.
 */
static void GenStateInitBlock(
	jx9_gen_state *pGen, /* Code generator state */
	GenBlock *pBlock,    /* Target block */
	sxi32 iType,         /* Block type [i.e: loop, conditional, function body, etc.]*/
	sxu32 nFirstInstr,   /* First instruction to compile */
	void *pUserData      /* Upper layer private data */
	)
{
	/* Initialize block fields */
	pBlock->nFirstInstr = nFirstInstr;
	pBlock->pUserData   = pUserData;
	pBlock->pGen        = pGen;
	pBlock->iFlags      = iType;
	pBlock->pParent     = 0;
	pBlock->bPostContinue = 0;
	SySetInit(&pBlock->aJumpFix, &pGen->pVm->sAllocator, sizeof(JumpFixup));
	SySetInit(&pBlock->aPostContFix, &pGen->pVm->sAllocator, sizeof(JumpFixup));
}
/*
 * Allocate a new block instance.
 * Return SXRET_OK and write a pointer to the new instantiated block
 * on success.Otherwise generate a compile-time error and abort
 * processing on failure.
 */
static sxi32 GenStateEnterBlock(
	jx9_gen_state *pGen,  /* Code generator state */
	sxi32 iType,          /* Block type [i.e: loop, conditional, function body, etc.]*/
	sxu32 nFirstInstr,    /* First instruction to compile */
	void *pUserData,      /* Upper layer private data */
	GenBlock **ppBlock    /* OUT: instantiated block */
	)
{
	GenBlock *pBlock;
	/* Allocate a new block instance */
	pBlock = (GenBlock *)SyMemBackendPoolAlloc(&pGen->pVm->sAllocator, sizeof(GenBlock));
	if( pBlock == 0 ){
		/* If the supplied memory subsystem is so sick that we are unable to allocate
		 * a tiny chunk of memory, there is no much we can do here.
		 */
		return GenStateOutOfMem(pGen);
	}
	/* Zero the structure */
	SyZero(pBlock, sizeof(GenBlock));
	GenStateInitBlock(&(*pGen), pBlock, iType, nFirstInstr, pUserData);
	/* Link to the parent block */
	pBlock->pParent = pGen->pCurrent;
	/* Mark as the current block */
	pGen->pCurrent = pBlock;
	if( ppBlock ){
		/* Write a pointer to the new instance */
		*ppBlock = pBlock;
	}
	return SXRET_OK;
}
/*
 * Release block fields without freeing the whole instance.
 */
static void GenStateReleaseBlock(GenBlock *pBlock)
{
	SySetRelease(&pBlock->aPostContFix);
	SySetRelease(&pBlock->aJumpFix);
}
/*
 * Release a block.
 */
static void GenStateFreeBlock(GenBlock *pBlock)
{
	jx9_gen_state *pGen = pBlock->pGen;
	GenStateReleaseBlock(&(*pBlock));
	/* Free the instance */
	SyMemBackendPoolFree(&pGen->pVm->sAllocator, pBlock);
}
/*
 * POP and release a block from the stack of compiled blocks.
 */
static sxi32 GenStateLeaveBlock(jx9_gen_state *pGen, GenBlock **ppBlock)
{
	GenBlock *pBlock = pGen->pCurrent;
	if( pBlock == 0 ){
		/* No more block to pop */
		return SXERR_EMPTY;
	}
	/* Point to the upper block */
	pGen->pCurrent = pBlock->pParent;
	if( ppBlock ){
		/* Write a pointer to the popped block */
		*ppBlock = pBlock;
	}else{
		/* Safely release the block */
		GenStateFreeBlock(&(*pBlock));	
	}
	return SXRET_OK;
}
/*
 * Emit a forward jump.
 * Notes on forward jumps
 *  Compilation of some JX9 constructs such as if, for, while and the logical or
 *  (||) and logical and (&&) operators in expressions requires the
 *  generation of forward jumps.
 *  Since the destination PC target of these jumps isn't known when the jumps
 *  are emitted, we record each forward jump in an instance of the following
 *  structure. Those jumps are fixed later when the jump destination is resolved.
 */
static sxi32 GenStateNewJumpFixup(GenBlock *pBlock, sxi32 nJumpType, sxu32 nInstrIdx)
{
	JumpFixup sJumpFix;
	sxi32 rc;
	/* Init the JumpFixup structure */
	sJumpFix.nJumpType = nJumpType;
	sJumpFix.nInstrIdx = nInstrIdx;
	/* Insert in the jump fixup table */
	rc = SySetPut(&pBlock->aJumpFix,(const void *)&sJumpFix);
	return rc;
}
/*
 * Fix a forward jump now the jump destination is resolved.
 * Return the total number of fixed jumps.
 * Notes on forward jumps:
 *  Compilation of some JX9 constructs such as if, for, while and the logical or
 *  (||) and logical and (&&) operators in expressions requires the
 *  generation of forward jumps.
 *  Since the destination PC target of these jumps isn't known when the jumps
 *  are emitted, we record each forward jump in an instance of the following
 *  structure.Those jumps are fixed later when the jump destination is resolved.
 */
static sxu32 GenStateFixJumps(GenBlock *pBlock, sxi32 nJumpType, sxu32 nJumpDest)
{
	JumpFixup *aFix;
	VmInstr *pInstr;
	sxu32 nFixed; 
	sxu32 n;
	/* Point to the jump fixup table */
	aFix = (JumpFixup *)SySetBasePtr(&pBlock->aJumpFix);
	/* Fix the desired jumps */
	for( nFixed = n = 0 ; n < SySetUsed(&pBlock->aJumpFix) ; ++n ){
		if( aFix[n].nJumpType < 0 ){
			/* Already fixed */
			continue;
		}
		if( nJumpType > 0 && aFix[n].nJumpType != nJumpType ){
			/* Not of our interest */
			continue;
		}
		/* Point to the instruction to fix */
		pInstr = jx9VmGetInstr(pBlock->pGen->pVm, aFix[n].nInstrIdx);
		if( pInstr ){
			pInstr->iP2 = nJumpDest;
			nFixed++;
			/* Mark as fixed */
			aFix[n].nJumpType = -1;
		}
	}
	/* Total number of fixed jumps */
	return nFixed;
}
/*
 * Reserve a room for a numeric constant [i.e: 64-bit integer or real number]
 * in the constant table.
 */
static jx9_value * GenStateInstallNumLiteral(jx9_gen_state *pGen, sxu32 *pIdx)
{
	jx9_value *pObj;
	sxu32 nIdx = 0; /* cc warning */
	/* Reserve a new constant */
	pObj = jx9VmReserveConstObj(pGen->pVm, &nIdx);
	if( pObj == 0 ){
		GenStateOutOfMem(pGen);
		return 0;
	}
	*pIdx = nIdx;
	/* TODO(chems): Create a numeric table (64bit int keys) same as 
	 * the constant string iterals table [optimization purposes].
	 */
	return pObj;
}
/*
 * Compile a numeric [i.e: integer or real] literal.
 * Notes on the integer type.
 *  According to the JX9 language reference manual
 *  Integers can be specified in decimal (base 10), hexadecimal (base 16), octal (base 8)
 *  or binary (base 2) notation, optionally preceded by a sign (- or +). 
 *  To use octal notation, precede the number with a 0 (zero). To use hexadecimal 
 *  notation precede the number with 0x. To use binary notation precede the number with 0b.
 */
static sxi32 jx9CompileNumLiteral(jx9_gen_state *pGen,sxi32 iCompileFlag)
{
	SyToken *pToken = pGen->pIn; /* Raw token */
	sxu32 nIdx = 0;
	if( pToken->nType & JX9_TK_INTEGER ){
		jx9_value *pObj;
		sxi64 iValue;
		iValue = jx9TokenValueToInt64(&pToken->sData);
		pObj = GenStateInstallNumLiteral(&(*pGen), &nIdx);
		if( pObj == 0 ){
			SXUNUSED(iCompileFlag); /* cc warning */
			return SXERR_ABORT;
		}
		jx9MemObjInitFromInt(pGen->pVm, pObj, iValue);
	}else{
		/* Real number */
		jx9_value *pObj;
		/* Reserve a new constant */
		pObj = jx9VmReserveConstObj(pGen->pVm, &nIdx);
		if( pObj == 0 ){
			return GenStateOutOfMem(pGen);
		}
		jx9MemObjInitFromString(pGen->pVm, pObj, &pToken->sData);
		jx9MemObjToReal(pObj);
	}
	/* Emit the load constant instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, nIdx, 0, 0);
	/* Node successfully compiled */
	return SXRET_OK;
}
/*
 * Compile a nowdoc string.
 * According to the JX9 language reference manual:
 *
 *  Nowdocs are to single-quoted strings what heredocs are to double-quoted strings.
 *  A nowdoc is specified similarly to a heredoc, but no parsing is done inside a nowdoc.
 *  The construct is ideal for embedding JX9 code or other large blocks of text without the
 *  need for escaping. It shares some features in common with the SGML <![CDATA[ ]]> 
 *  construct, in that it declares a block of text which is not for parsing.
 *  A nowdoc is identified with the same <<< sequence used for heredocs, but the identifier
 *  which follows is enclosed in single quotes, e.g. <<<'EOT'. All the rules for heredoc 
 *  identifiers also apply to nowdoc identifiers, especially those regarding the appearance
 *  of the closing identifier. 
 */
static sxi32 jx9CompileNowdoc(jx9_gen_state *pGen,sxi32 iCompileFlag)
{
	SyString *pStr = &pGen->pIn->sData; /* Constant string literal */
	jx9_value *pObj;
	sxu32 nIdx;
	nIdx = 0; /* Prevent compiler warning */
	if( pStr->nByte <= 0 ){
		/* Empty string, load NULL */
		jx9VmEmitInstr(pGen->pVm,JX9_OP_LOADC, 0, 0, 0, 0);
		return SXRET_OK;
	}
	/* Reserve a new constant */
	pObj = jx9VmReserveConstObj(pGen->pVm, &nIdx);
	if( pObj == 0 ){
		jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "JX9 engine is running out of memory");
		SXUNUSED(iCompileFlag); /* cc warning */
		return SXERR_ABORT;
	}
	/* No processing is done here, simply a memcpy() operation */
	jx9MemObjInitFromString(pGen->pVm, pObj, pStr);
	/* Emit the load constant instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, nIdx, 0, 0);
	/* Node successfully compiled */
	return SXRET_OK;
}
/*
 * Compile a single quoted string.
 * According to the JX9 language reference manual:
 *
 *   The simplest way to specify a string is to enclose it in single quotes (the character ' ).
 *   To specify a literal single quote, escape it with a backslash (\). To specify a literal
 *   backslash, double it (\\). All other instances of backslash will be treated as a literal
 *   backslash: this means that the other escape sequences you might be used to, such as \r 
 *   or \n, will be output literally as specified rather than having any special meaning.
 * 
 */
JX9_PRIVATE sxi32 jx9CompileSimpleString(jx9_gen_state *pGen, sxi32 iCompileFlag)
{
	SyString *pStr = &pGen->pIn->sData; /* Constant string literal */
	const char *zIn, *zCur, *zEnd;
	jx9_value *pObj;
	sxu32 nIdx;
	nIdx = 0; /* Prevent compiler warning */
	/* Delimit the string */
	zIn  = pStr->zString;
	zEnd = &zIn[pStr->nByte];
	if( zIn > zEnd ){
		/* Empty string, load NULL */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, 0, 0, 0);
		return SXRET_OK;
	}
	if( SXRET_OK == GenStateFindLiteral(&(*pGen), pStr, &nIdx) ){
		/* Already processed, emit the load constant instruction
		 * and return.
		 */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, nIdx, 0, 0);
		return SXRET_OK;
	}
	/* Reserve a new constant */
	pObj = jx9VmReserveConstObj(pGen->pVm, &nIdx);
	if( pObj == 0 ){
		jx9GenCompileError(&(*pGen), E_ERROR, 1, "JX9 engine is running out of memory");
		SXUNUSED(iCompileFlag); /* cc warning */
		return SXERR_ABORT;
	}
	jx9MemObjInitFromString(pGen->pVm, pObj, 0);
	/* Compile the node */
	for(;;){
		if( zIn >= zEnd ){
			/* End of input */
			break;
		}
		zCur = zIn;
		while( zIn < zEnd && zIn[0] != '\\' ){
			zIn++;
		}
		if( zIn > zCur ){
			/* Append raw contents*/
			jx9MemObjStringAppend(pObj, zCur, (sxu32)(zIn-zCur));
		}
        else
        {
            jx9MemObjStringAppend(pObj, "", 0);
        }
		zIn++;
		if( zIn < zEnd ){
			if( zIn[0] == '\\' ){
				/* A literal backslash */
				jx9MemObjStringAppend(pObj, "\\", sizeof(char));
			}else if( zIn[0] == '\'' ){
				/* A single quote */
				jx9MemObjStringAppend(pObj, "'", sizeof(char));
			}else{
				/* verbatim copy */
				zIn--;
				jx9MemObjStringAppend(pObj, zIn, sizeof(char)*2);
				zIn++;
			}
		}
		/* Advance the stream cursor */
		zIn++;
	}
	/* Emit the load constant instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, nIdx, 0, 0);
	if( pStr->nByte < 1024 ){
		/* Install in the literal table */
		GenStateInstallLiteral(pGen, pObj, nIdx);
	}
	/* Node successfully compiled */
	return SXRET_OK;
}
/*
 * Process variable expression [i.e: "$var", "${var}"] embedded in a double quoted/heredoc string.
 * According to the JX9 language reference manual
 *   When a string is specified in double quotes or with heredoc, variables are parsed within it.
 *  There are two types of syntax: a simple one and a complex one. The simple syntax is the most
 *  common and convenient. It provides a way to embed a variable, an array value, or an object
 *  property in a string with a minimum of effort.
 *  Simple syntax
 *   If a dollar sign ($) is encountered, the parser will greedily take as many tokens as possible
 *   to form a valid variable name. Enclose the variable name in curly braces to explicitly specify
 *   the end of the name.
 *   Similarly, an array index or an object property can be parsed. With array indices, the closing
 *   square bracket (]) marks the end of the index. The same rules apply to object properties
 *   as to simple variables. 
 *  Complex (curly) syntax
 *   This isn't called complex because the syntax is complex, but because it allows for the use 
 *   of complex expressions.
 *   Any scalar variable, array element or object property with a string representation can be
 *   included via this syntax. Simply write the expression the same way as it would appear outside
 *   the string, and then wrap it in { and }. Since { can not be escaped, this syntax will only
 *   be recognised when the $ immediately follows the {. Use {\$ to get a literal {$
 */
static sxi32 GenStateProcessStringExpression(
	jx9_gen_state *pGen, /* Code generator state */
	const char *zIn,     /* Raw expression */
	const char *zEnd     /* End of the expression */
	)
{
	SyToken *pTmpIn, *pTmpEnd;
	SySet sToken;
	sxi32 rc;
	/* Initialize the token set */
	SySetInit(&sToken, &pGen->pVm->sAllocator, sizeof(SyToken));
	/* Preallocate some slots */
	SySetAlloc(&sToken, 0x08);
	/* Tokenize the text */
	jx9Tokenize(zIn,(sxu32)(zEnd-zIn),&sToken);
	/* Swap delimiter */
	pTmpIn  = pGen->pIn;
	pTmpEnd = pGen->pEnd;
	pGen->pIn = (SyToken *)SySetBasePtr(&sToken);
	pGen->pEnd = &pGen->pIn[SySetUsed(&sToken)];
	/* Compile the expression */
	rc = jx9CompileExpr(&(*pGen), 0, 0);
	/* Restore token stream */
	pGen->pIn  = pTmpIn;
	pGen->pEnd = pTmpEnd;
	/* Release the token set */
	SySetRelease(&sToken);
	/* Compilation result */
	return rc;
}
/*
 * Reserve a new constant for a double quoted/heredoc string.
 */
static jx9_value * GenStateNewStrObj(jx9_gen_state *pGen,sxi32 *pCount)
{
	jx9_value *pConstObj;
	sxu32 nIdx = 0;
	/* Reserve a new constant */
	pConstObj = jx9VmReserveConstObj(pGen->pVm, &nIdx);
	if( pConstObj == 0 ){
		GenStateOutOfMem(&(*pGen));
		return 0;
	}
	(*pCount)++;
	jx9MemObjInitFromString(pGen->pVm, pConstObj, 0);
	/* Emit the load constant instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, nIdx, 0, 0);
	return pConstObj;
}
/*
 * Compile a double quoted/heredoc string.
 * According to the JX9 language reference manual
 * Heredoc
 *  A third way to delimit strings is the heredoc syntax: <<<. After this operator, an identifier
 *  is provided, then a newline. The string itself follows, and then the same identifier again
 *  to close the quotation.
 *  The closing identifier must begin in the first column of the line. Also, the identifier must
 *  follow the same naming rules as any other label in JX9: it must contain only alphanumeric
 *  characters and underscores, and must start with a non-digit character or underscore.
 *  Warning
 *  It is very important to note that the line with the closing identifier must contain
 *  no other characters, except possibly a semicolon (;). That means especially that the identifier
 *  may not be indented, and there may not be any spaces or tabs before or after the semicolon.
 *  It's also important to realize that the first character before the closing identifier must
 *  be a newline as defined by the local operating system. This is \n on UNIX systems, including Mac OS X.
 *  The closing delimiter (possibly followed by a semicolon) must also be followed by a newline.
 *  If this rule is broken and the closing identifier is not "clean", it will not be considered a closing
 *  identifier, and JX9 will continue looking for one. If a proper closing identifier is not found before
 *  the end of the current file, a parse error will result at the last line.
 *  Heredocs can not be used for initializing object properties. 
 * Double quoted
 *  If the string is enclosed in double-quotes ("), JX9 will interpret more escape sequences for special characters:
 *  Escaped characters Sequence 	Meaning
 *  \n linefeed (LF or 0x0A (10) in ASCII)
 *  \r carriage return (CR or 0x0D (13) in ASCII)
 *  \t horizontal tab (HT or 0x09 (9) in ASCII)
 *  \v vertical tab (VT or 0x0B (11) in ASCII)
 *  \f form feed (FF or 0x0C (12) in ASCII)
 *  \\ backslash
 *  \$ dollar sign
 *  \" double-quote
 *  \[0-7]{1, 3} 	the sequence of characters matching the regular expression is a character in octal notation
 *  \x[0-9A-Fa-f]{1, 2} 	the sequence of characters matching the regular expression is a character in hexadecimal notation
 * As in single quoted strings, escaping any other character will result in the backslash being printed too.
 * The most important feature of double-quoted strings is the fact that variable names will be expanded.
 * See string parsing for details.
 */
static sxi32 GenStateCompileString(jx9_gen_state *pGen)
{
	SyString *pStr = &pGen->pIn->sData; /* Raw token value */
	const char *zIn, *zCur, *zEnd;
	jx9_value *pObj = 0;
	sxi32 iCons;	
	sxi32 rc;
	/* Delimit the string */
	zIn  = pStr->zString;
	zEnd = &zIn[pStr->nByte];
	if( zIn > zEnd ){
		/* Empty string, load NULL */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, 0, 0, 0);
		return SXRET_OK;
	}
	zCur = 0;
	/* Compile the node */
	iCons = 0;
	for(;;){
		zCur = zIn;
		while( zIn < zEnd && zIn[0] != '\\'  ){
			if(zIn[0] == '$' && &zIn[1] < zEnd &&
				(((unsigned char)zIn[1] >= 0xc0 || SyisAlpha(zIn[1]) || zIn[1] == '_')) ){
					break;
			}
			zIn++;
		}
		if( zIn > zCur ){
			if( pObj == 0 ){
				pObj = GenStateNewStrObj(&(*pGen), &iCons);
				if( pObj == 0 ){
					return SXERR_ABORT;
				}
			}
			jx9MemObjStringAppend(pObj, zCur, (sxu32)(zIn-zCur));
		}
        else
        {
            if( pObj == 0 ){
                pObj = GenStateNewStrObj(&(*pGen), &iCons);
                if( pObj == 0 ){
                    return SXERR_ABORT;
                }
            }
            jx9MemObjStringAppend(pObj, "", 0);
        }
		if( zIn >= zEnd ){
			break;
		}
		if( zIn[0] == '\\' ){
			const char *zPtr = 0;
			sxu32 n;
			zIn++;
			if( zIn >= zEnd ){
				break;
			}
			if( pObj == 0 ){
				pObj = GenStateNewStrObj(&(*pGen), &iCons);
				if( pObj == 0 ){
					return SXERR_ABORT;
				}
			}
			n = sizeof(char); /* size of conversion */
			switch( zIn[0] ){
			case '$':
				/* Dollar sign */
				jx9MemObjStringAppend(pObj, "$", sizeof(char));
				break;
			case '\\':
				/* A literal backslash */
				jx9MemObjStringAppend(pObj, "\\", sizeof(char));
				break;
			case 'a':
				/* The "alert" character (BEL)[ctrl+g] ASCII code 7 */
				jx9MemObjStringAppend(pObj, "\a", sizeof(char));
				break;
			case 'b':
				/* Backspace (BS)[ctrl+h] ASCII code 8 */
				jx9MemObjStringAppend(pObj, "\b", sizeof(char));
				break;
			case 'f':
				/* Form-feed (FF)[ctrl+l] ASCII code 12 */
				jx9MemObjStringAppend(pObj, "\f", sizeof(char));
				break;
			case 'n':
				/* Line feed(new line) (LF)[ctrl+j] ASCII code 10 */
				jx9MemObjStringAppend(pObj, "\n", sizeof(char));
				break;
			case 'r':
				/* Carriage return (CR)[ctrl+m] ASCII code 13 */
				jx9MemObjStringAppend(pObj, "\r", sizeof(char));
				break;
			case 't':
				/* Horizontal tab (HT)[ctrl+i] ASCII code 9 */
				jx9MemObjStringAppend(pObj, "\t", sizeof(char));
				break;
			case 'v':
				/* Vertical tab(VT)[ctrl+k] ASCII code 11 */
				jx9MemObjStringAppend(pObj, "\v", sizeof(char));
				break;
			case '\'':
				/* Single quote */
				jx9MemObjStringAppend(pObj, "'", sizeof(char));
				break;
			case '"':
				/* Double quote */
				jx9MemObjStringAppend(pObj, "\"", sizeof(char));
				break;
			case '0':
				/* NUL byte */
				jx9MemObjStringAppend(pObj, "\0", sizeof(char));
				break;
			case 'x':
				if((unsigned char)zIn[1] < 0xc0 && SyisHex(zIn[1]) ){
					int c;
					/* Hex digit */
					c = SyHexToint(zIn[1]) << 4;
					if( &zIn[2] < zEnd ){
						c +=  SyHexToint(zIn[2]);
					}
					/* Output char */
					jx9MemObjStringAppend(pObj, (const char *)&c, sizeof(char));
					n += sizeof(char) * 2;
				}else{
					/* Output literal character  */
					jx9MemObjStringAppend(pObj, "x", sizeof(char));
				}
				break;
			case 'o':
				if( &zIn[1] < zEnd && (unsigned char)zIn[1] < 0xc0 && SyisDigit(zIn[1]) && (zIn[1] - '0') < 8 ){
					/* Octal digit stream */
					int c;
					c = 0;
					zIn++;
					for( zPtr = zIn ; zPtr < &zIn[3*sizeof(char)] ; zPtr++ ){
						if( zPtr >= zEnd || (unsigned char)zPtr[0] >= 0xc0 || !SyisDigit(zPtr[0]) || (zPtr[0] - '0') > 7 ){
							break;
						}
						c = c * 8 + (zPtr[0] - '0');
					}
					if ( c > 0 ){
						jx9MemObjStringAppend(pObj, (const char *)&c, sizeof(char));
					}
					n = (sxu32)(zPtr-zIn);
				}else{
					/* Output literal character  */
					jx9MemObjStringAppend(pObj, "o", sizeof(char));
				}
				break;
			default:
				/* Output without a slash */
				jx9MemObjStringAppend(pObj, zIn, sizeof(char));
				break;
			}
			/* Advance the stream cursor */
			zIn += n;
			continue;
		}
		if( zIn[0] == '{' ){
			/* Curly syntax */
			const char *zExpr;
			sxi32 iNest = 1;
			zIn++;
			zExpr = zIn;
			/* Synchronize with the next closing curly braces */
			while( zIn < zEnd ){
				if( zIn[0] == '{' ){
					/* Increment nesting level */
					iNest++;
				}else if(zIn[0] == '}' ){
					/* Decrement nesting level */
					iNest--;
					if( iNest <= 0 ){
						break;
					}
				}
				zIn++;
			}
			/* Process the expression */
			rc = GenStateProcessStringExpression(&(*pGen),zExpr,zIn);
			if( rc == SXERR_ABORT ){
				return SXERR_ABORT;
			}
			if( rc != SXERR_EMPTY ){
				++iCons;
			}
			if( zIn < zEnd ){
				/* Jump the trailing curly */
				zIn++;
			}
		}else{
			/* Simple syntax */
			const char *zExpr = zIn;
			/* Assemble variable name */
			for(;;){
				/* Jump leading dollars */
				while( zIn < zEnd && zIn[0] == '$' ){
					zIn++;
				}
				for(;;){
					while( zIn < zEnd && (unsigned char)zIn[0] < 0xc0 && (SyisAlphaNum(zIn[0]) || zIn[0] == '_' ) ){
						zIn++;
					}
					if((unsigned char)zIn[0] >= 0xc0 ){
						/* UTF-8 stream */
						zIn++;
						while( zIn < zEnd && (((unsigned char)zIn[0] & 0xc0) == 0x80) ){
							zIn++;
						}
						continue;
					}
					break;
				}
				if( zIn >= zEnd ){
					break;
				}
				if( zIn[0] == '[' ){
					sxi32 iSquare = 1;
					zIn++;
					while( zIn < zEnd ){
						if( zIn[0] == '[' ){
							iSquare++;
						}else if (zIn[0] == ']' ){
							iSquare--;
							if( iSquare <= 0 ){
								break;
							}
						}
						zIn++;
					}
					if( zIn < zEnd ){
						zIn++;
					}
					break;
				}else if( zIn[0] == '.' ){
					/* Member access operator '.' */
					zIn++;
				}else{
					break;
				}
			}
			/* Process the expression */
			rc = GenStateProcessStringExpression(&(*pGen),zExpr, zIn);
			if( rc == SXERR_ABORT ){
				return SXERR_ABORT;
			}
			if( rc != SXERR_EMPTY ){
				++iCons;
			}
		}
		/* Invalidate the previously used constant */
		pObj = 0;
	}/*for(;;)*/
	if( iCons > 1 ){
		/* Concatenate all compiled constants */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_CAT, iCons, 0, 0, 0);
	}
	/* Node successfully compiled */
	return SXRET_OK;
}
/*
 * Compile a double quoted string.
 *  See the block-comment above for more information.
 */
JX9_PRIVATE sxi32 jx9CompileString(jx9_gen_state *pGen, sxi32 iCompileFlag)
{
	sxi32 rc;
	rc = GenStateCompileString(&(*pGen));
	SXUNUSED(iCompileFlag); /* cc warning */
	/* Compilation result */
	return rc;
}
/*
 * Compile a literal which is an identifier(name) for simple values.
 */
JX9_PRIVATE sxi32 jx9CompileLiteral(jx9_gen_state *pGen,sxi32 iCompileFlag)
{
	SyToken *pToken = pGen->pIn;
	jx9_value *pObj;
	SyString *pStr;	
	sxu32 nIdx;
	/* Extract token value */
	pStr = &pToken->sData;
	/* Deal with the reserved literals [i.e: null, false, true, ...] first */
	if( pStr->nByte == sizeof("NULL") - 1 ){
		if( SyStrnicmp(pStr->zString, "null", sizeof("NULL")-1) == 0 ){
			/* NULL constant are always indexed at 0 */
			jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, 0, 0, 0);
			return SXRET_OK;
		}else if( SyStrnicmp(pStr->zString, "true", sizeof("TRUE")-1) == 0 ){
			/* TRUE constant are always indexed at 1 */
			jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, 1, 0, 0);
			return SXRET_OK;
		}
	}else if (pStr->nByte == sizeof("FALSE") - 1 &&
		SyStrnicmp(pStr->zString, "false", sizeof("FALSE")-1) == 0 ){
			/* FALSE constant are always indexed at 2 */
			jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, 2, 0, 0);
			return SXRET_OK;
	}else if(pStr->nByte == sizeof("__LINE__") - 1 &&
		SyMemcmp(pStr->zString, "__LINE__", sizeof("__LINE__")-1) == 0 ){
			/* TICKET 1433-004: __LINE__ constant must be resolved at compile time, not run time */
			pObj = jx9VmReserveConstObj(pGen->pVm, &nIdx);
			if( pObj == 0 ){
				SXUNUSED(iCompileFlag); /* cc warning */
				return GenStateOutOfMem(pGen);
			}
			jx9MemObjInitFromInt(pGen->pVm, pObj, pToken->nLine);
			/* Emit the load constant instruction */
			jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, nIdx, 0, 0);
			return SXRET_OK;
	}else if( pStr->nByte == sizeof("__FUNCTION__") - 1 &&
		SyMemcmp(pStr->zString, "__FUNCTION__", sizeof("__FUNCTION__")-1) == 0 ){
			GenBlock *pBlock = pGen->pCurrent;
			/* TICKET 1433-004: __FUNCTION__/__METHOD__ constants must be resolved at compile time, not run time */
			while( pBlock && (pBlock->iFlags & GEN_BLOCK_FUNC) == 0 ){
				/* Point to the upper block */
				pBlock = pBlock->pParent;
			}
			if( pBlock == 0 ){
				/* Called in the global scope, load NULL */
				jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, 0, 0, 0);
			}else{
				/* Extract the target function/method */
				jx9_vm_func *pFunc = (jx9_vm_func *)pBlock->pUserData;
				pObj = jx9VmReserveConstObj(pGen->pVm, &nIdx);
				if( pObj == 0 ){
					return GenStateOutOfMem(pGen);
				}
				jx9MemObjInitFromString(pGen->pVm, pObj, &pFunc->sName);
				/* Emit the load constant instruction */
				jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, nIdx, 0, 0);
			}
			return SXRET_OK;
	}
	/* Query literal table */
	if( SXRET_OK != GenStateFindLiteral(&(*pGen), &pToken->sData, &nIdx) ){
		jx9_value *pObj;
		/* Unknown literal, install it in the literal table */
		pObj = jx9VmReserveConstObj(pGen->pVm, &nIdx);
		if( pObj == 0 ){
			return GenStateOutOfMem(pGen);
		}
		jx9MemObjInitFromString(pGen->pVm, pObj, &pToken->sData);
		GenStateInstallLiteral(&(*pGen), pObj, nIdx);
	}
	/* Emit the load constant instruction */
	jx9VmEmitInstr(pGen->pVm,JX9_OP_LOADC,1,nIdx, 0, 0);
	/* Node successfully compiled */
	return SXRET_OK;
}
/*
 * Compile an array entry whether it is a key or a value.
 */
static sxi32 GenStateCompileJSONEntry(
	jx9_gen_state *pGen, /* Code generator state */
	SyToken *pIn,        /* Token stream */
	SyToken *pEnd,       /* End of the token stream */
	sxi32 iFlags,        /* Compilation flags */
	sxi32 (*xValidator)(jx9_gen_state *,jx9_expr_node *) /* Expression tree validator callback */
	)
{
	SyToken *pTmpIn, *pTmpEnd;
	sxi32 rc;
	/* Swap token stream */
	SWAP_DELIMITER(pGen, pIn, pEnd);
	/* Compile the expression*/
	rc = jx9CompileExpr(&(*pGen), iFlags, xValidator);
	/* Restore token stream */
	RE_SWAP_DELIMITER(pGen);
	return rc;
}
/* 
 * Compile a Jx9 JSON Array.
 */
JX9_PRIVATE sxi32 jx9CompileJsonArray(jx9_gen_state *pGen, sxi32 iCompileFlag)
{
	sxi32 nPair = 0;
	SyToken *pCur;
	sxi32 rc;

	pGen->pIn++; /* Jump the open square bracket '['*/
	pGen->pEnd--;
	SXUNUSED(iCompileFlag); /* cc warning */
	for(;;){
		/* Jump leading commas */
		while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_COMMA) ){
			pGen->pIn++;
		}
		pCur = pGen->pIn;
		if( SXRET_OK != jx9GetNextExpr(pGen->pIn, pGen->pEnd, &pGen->pIn) ){
			/* No more entry to process */
			break;
		}
		/* Compile entry */
		rc = GenStateCompileJSONEntry(&(*pGen),pCur,pGen->pIn,EXPR_FLAG_RDONLY_LOAD/*Do not create the variable if inexistant*/,0);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
		nPair++;
	}
	/* Emit the load map instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_LOAD_MAP,nPair,0,0,0);
	/* Node successfully compiled */
	return SXRET_OK;
}
/*
 * Node validator for a given JSON key.
 */
static sxi32 GenStateJSONObjectKeyNodeValidator(jx9_gen_state *pGen,jx9_expr_node *pRoot)
{
	sxi32 rc = SXRET_OK;
	if( pRoot->xCode != jx9CompileVariable && pRoot->xCode != jx9CompileString 
		&& pRoot->xCode != jx9CompileSimpleString && pRoot->xCode != jx9CompileLiteral ){
		/* Unexpected expression */
		rc = jx9GenCompileError(&(*pGen), E_ERROR, pRoot->pStart? pRoot->pStart->nLine : 0, 
			"JSON Object: Unexpected expression, key must be of type string, literal or simple variable");
		if( rc != SXERR_ABORT ){
			rc = SXERR_INVALID;
		}
	}
	return rc;
}
/* 
 * Compile a Jx9 JSON Object
 */
JX9_PRIVATE sxi32 jx9CompileJsonObject(jx9_gen_state *pGen, sxi32 iCompileFlag)
{
	SyToken *pKey, *pCur;
	sxi32 nPair = 0;
	sxi32 rc;

	pGen->pIn++; /* Jump the open querly braces '{'*/
	pGen->pEnd--;
	SXUNUSED(iCompileFlag); /* cc warning */
	for(;;){
		/* Jump leading commas */
		while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_COMMA) ){
			pGen->pIn++;
		}
		pCur = pGen->pIn;
		if( SXRET_OK != jx9GetNextExpr(pGen->pIn, pGen->pEnd, &pGen->pIn) ){
			/* No more entry to process */
			break;
		}
		/* Compile the key */
		pKey = pCur;
		while( pCur < pGen->pIn ){
			if( pCur->nType & JX9_TK_COLON /*':'*/  ){
				break;
			}
			pCur++;
		}
		rc = SXERR_EMPTY;
        if( (pCur->nType & JX9_TK_COLON) == 0 ){
            rc = jx9GenCompileError(&(*pGen), E_ABORT, pCur->nLine, "JSON Object: Missing colon string \":\"");
            if( rc == SXERR_ABORT ){
                return SXERR_ABORT;
            }
            return SXRET_OK;
        }

		if( pCur < pGen->pIn ){
			if( &pCur[1] >= pGen->pIn ){
				/* Missing value */
				rc = jx9GenCompileError(&(*pGen), E_ERROR, pCur->nLine, "JSON Object: Missing entry value");
				if( rc == SXERR_ABORT ){
					return SXERR_ABORT;
				}
				return SXRET_OK;
			}
			/* Compile the expression holding the key */
			rc = GenStateCompileJSONEntry(&(*pGen), pKey, pCur,
				EXPR_FLAG_RDONLY_LOAD                /* Do not create the variable if inexistant */, 
				GenStateJSONObjectKeyNodeValidator   /* Node validator callback */
				);
			if( rc == SXERR_ABORT ){
				return SXERR_ABORT;
			}
			pCur++; /* Jump the double colon ':'  */
		}else if( pKey == pCur ){
			/* Key is omitted, emit an error */
			jx9GenCompileError(&(*pGen),E_ERROR, pCur->nLine, "JSON Object: Missing entry key");
			pCur++; /* Jump the double colon ':'  */
		}else{
			/* Reset back the cursor and point to the entry value */
			pCur = pKey;
		}
		/* Compile indice value */
		rc = GenStateCompileJSONEntry(&(*pGen), pCur, pGen->pIn, EXPR_FLAG_RDONLY_LOAD/*Do not create the variable if inexistant*/,0);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
		nPair++;
	}
	/* Emit the load map instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_LOAD_MAP, nPair * 2, 1, 0, 0);
	/* Node successfully compiled */
	return SXRET_OK;
}
/*
 * Compile a function [i.e: print, exit(), include(), ...] which is a langauge
 * construct.
 */
JX9_PRIVATE sxi32 jx9CompileLangConstruct(jx9_gen_state *pGen,sxi32 iCompileFlag)
{
	SyString *pName;
	sxu32 nKeyID;
	sxi32 rc;
	/* Name of the language construct [i.e: print, die...]*/
	pName = &pGen->pIn->sData;
	nKeyID = (sxu32)SX_PTR_TO_INT(pGen->pIn->pUserData);
	pGen->pIn++; /* Jump the language construct keyword */
	if( nKeyID == JX9_TKWRD_PRINT ){
		SyToken *pTmp, *pNext = 0;
		/* Compile arguments one after one */
		pTmp = pGen->pEnd;
		jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, 1 /* Boolean true index */, 0, 0);
		while( SXRET_OK == jx9GetNextExpr(pGen->pIn, pTmp, &pNext) ){
			if( pGen->pIn < pNext ){
				pGen->pEnd = pNext;
				rc = jx9CompileExpr(&(*pGen), EXPR_FLAG_RDONLY_LOAD/* Do not create variable if inexistant */, 0);
				if( rc == SXERR_ABORT ){
					return SXERR_ABORT;
				}
				if( rc != SXERR_EMPTY ){
					/* Ticket 1433-008: Optimization #1: Consume input directly 
					 * without the overhead of a function call.
					 * This is a very powerful optimization that improve
					 * performance greatly.
					 */
					jx9VmEmitInstr(pGen->pVm,JX9_OP_CONSUME,1,0,0,0);
				}
			}
			/* Jump trailing commas */
			while( pNext < pTmp && (pNext->nType & JX9_TK_COMMA) ){
				pNext++;
			}
			pGen->pIn = pNext;
		}
		/* Restore token stream */
		pGen->pEnd = pTmp;	
	}else{
		sxi32 nArg = 0;
		sxu32 nIdx = 0;
		rc = jx9CompileExpr(&(*pGen), EXPR_FLAG_RDONLY_LOAD/* Do not create variable if inexistant */, 0);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}else if(rc != SXERR_EMPTY ){
			nArg = 1;
		}
		if( SXRET_OK != GenStateFindLiteral(&(*pGen), pName, &nIdx) ){
			jx9_value *pObj;
			/* Emit the call instruction */
			pObj = jx9VmReserveConstObj(pGen->pVm, &nIdx);
			if( pObj == 0 ){
				SXUNUSED(iCompileFlag); /* cc warning */
				return GenStateOutOfMem(pGen);
			}
			jx9MemObjInitFromString(pGen->pVm, pObj, pName);
			/* Install in the literal table */
			GenStateInstallLiteral(&(*pGen), pObj, nIdx);
		}
		/* Emit the call instruction */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, nIdx, 0, 0);
		jx9VmEmitInstr(pGen->pVm, JX9_OP_CALL, nArg, 0, 0, 0);
	}
	/* Node successfully compiled */
	return SXRET_OK;
}
/*
 * Compile a node holding a variable declaration.
 * According to the J9X language reference
 *  Variables in JX9 are represented by a dollar sign followed by the name of the variable.
 *  The variable name is case-sensitive.
 *  Variable names follow the same rules as other labels in JX9. A valid variable name
 *  starts with a letter, underscore or any UTF-8 stream, followed by any number of letters
 *  numbers, or underscores.
 *  By default, variables are always assigned by value unless the target value is a JSON
 *  array or a JSON object which is passed by reference.
 */
JX9_PRIVATE sxi32 jx9CompileVariable(jx9_gen_state *pGen,sxi32 iCompileFlag)
{
	sxu32 nLine = pGen->pIn->nLine;
	SyHashEntry *pEntry;
	SyString *pName;
	char *zName = 0;
	sxi32 iP1;
	void *p3;
	sxi32 rc;
	
	pGen->pIn++; /* Jump the dollar sign '$' */
	if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & (JX9_TK_ID|JX9_TK_KEYWORD)) == 0 ){
		/* Invalid variable name */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "Invalid variable name");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		return SXRET_OK;
	}
	/* Extract variable name */
	pName = &pGen->pIn->sData;
	/* Advance the stream cursor */
	pGen->pIn++;
	pEntry = SyHashGet(&pGen->hVar, (const void *)pName->zString, pName->nByte);
	if( pEntry == 0 ){
		/* Duplicate name */
		zName = SyMemBackendStrDup(&pGen->pVm->sAllocator, pName->zString, pName->nByte);
		if( zName == 0 ){
			return GenStateOutOfMem(pGen);
		}
		/* Install in the hashtable */
		SyHashInsert(&pGen->hVar, zName, pName->nByte, zName);
	}else{
		/* Name already available */
		zName = (char *)pEntry->pUserData;
	}
	p3 = (void *)zName;	
	iP1 = 0;
	if( iCompileFlag & EXPR_FLAG_RDONLY_LOAD ){
		if( (iCompileFlag & EXPR_FLAG_LOAD_IDX_STORE) == 0 ){
			/* Read-only load.In other words do not create the variable if inexistant */
			iP1 = 1;
		}
	}
	/* Emit the load instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_LOAD, iP1, 0, p3, 0);
	/* Node successfully compiled */
	return SXRET_OK;
}
/* Forward declaration */
static sxi32 GenStateCompileFunc(jx9_gen_state *pGen,SyString *pName,sxi32 iFlags,jx9_vm_func **ppFunc);
/*
 * Compile an annoynmous function or a closure.
 * According to the JX9 language reference
 *  Anonymous functions, also known as closures, allow the creation of functions
 *  which have no specified name. They are most useful as the value of callback
 *  parameters, but they have many other uses. Closures can also be used as
 *  the values of variables; Assigning a closure to a variable uses the same
 *  syntax as any other assignment, including the trailing semicolon:
 *  Example Anonymous function variable assignment example
 * $greet = function($name)
 * {
 *    printf("Hello %s\r\n", $name);
 * };
 * $greet('World');
 * $greet('JX9');
 * Note that the implementation of annoynmous function and closure under
 * JX9 is completely different from the one used by the  engine.
 */
JX9_PRIVATE sxi32 jx9CompileAnnonFunc(jx9_gen_state *pGen,sxi32 iCompileFlag)
{
	jx9_vm_func *pAnnonFunc; /* Annonymous function body */
	char zName[512];         /* Unique lambda name */
	static int iCnt = 1;     /* There is no worry about thread-safety here, because only
							  * one thread is allowed to compile the script.
						      */
	jx9_value *pObj;
	SyString sName;
	sxu32 nIdx;
	sxu32 nLen;
	sxi32 rc;

	pGen->pIn++; /* Jump the 'function' keyword */
	if( pGen->pIn->nType & (JX9_TK_ID|JX9_TK_KEYWORD) ){
		pGen->pIn++;
	}
	/* Reserve a constant for the lambda */
	pObj = jx9VmReserveConstObj(pGen->pVm, &nIdx);
	if( pObj == 0 ){
		GenStateOutOfMem(pGen);
		SXUNUSED(iCompileFlag); /* cc warning */
		return SXERR_ABORT;
	}
	/* Generate a unique name */
	nLen = SyBufferFormat(zName, sizeof(zName), "[lambda_%d]", iCnt++);
	/* Make sure the generated name is unique */
	while( SyHashGet(&pGen->pVm->hFunction, zName, nLen) != 0 && nLen < sizeof(zName) - 2 ){
		nLen = SyBufferFormat(zName, sizeof(zName), "[lambda_%d]", iCnt++);
	}
	SyStringInitFromBuf(&sName, zName, nLen);
	jx9MemObjInitFromString(pGen->pVm, pObj, &sName);
	/* Compile the lambda body */
	rc = GenStateCompileFunc(&(*pGen),&sName,0,&pAnnonFunc);
	if( rc == SXERR_ABORT ){
		return SXERR_ABORT;
	}
	/* Emit the load constant instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_LOADC, 0, nIdx, 0, 0);
	/* Node successfully compiled */
	return SXRET_OK;
}
/*
 * Compile the 'continue' statement.
 * According to the JX9 language reference
 *  continue is used within looping structures to skip the rest of the current loop iteration
 *  and continue execution at the condition evaluation and then the beginning of the next
 *  iteration.
 *  Note: Note that in JX9 the switch statement is considered a looping structure for
 *  the purposes of continue. 
 *  continue accepts an optional numeric argument which tells it how many levels
 *  of enclosing loops it should skip to the end of.
 *  Note:
 *   continue 0; and continue 1; is the same as running continue;. 
 */
static sxi32 jx9CompileContinue(jx9_gen_state *pGen)
{
	GenBlock *pLoop; /* Target loop */
	sxi32 iLevel;    /* How many nesting loop to skip */
	sxu32 nLine;
	sxi32 rc;
	nLine = pGen->pIn->nLine;
	iLevel = 0;
	/* Jump the 'continue' keyword */
	pGen->pIn++;
	if( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_NUM) ){
		/* optional numeric argument which tells us how many levels
		 * of enclosing loops we should skip to the end of. 
		 */
		iLevel = (sxi32)jx9TokenValueToInt64(&pGen->pIn->sData);
		if( iLevel < 2 ){
			iLevel = 0;
		}
		pGen->pIn++; /* Jump the optional numeric argument */
	}
	/* Point to the target loop */
	pLoop = GenStateFetchBlock(pGen->pCurrent, GEN_BLOCK_LOOP, iLevel);
	if( pLoop == 0 ){
		/* Illegal continue */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "A 'continue' statement may only be used within a loop or switch");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
	}else{
		sxu32 nInstrIdx = 0;
		/* Emit the unconditional jump to the beginning of the target loop */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_JMP, 0, pLoop->nFirstInstr, 0, &nInstrIdx);
		if( pLoop->bPostContinue == TRUE ){
			JumpFixup sJumpFix;
			/* Post-continue */
			sJumpFix.nJumpType = JX9_OP_JMP;
			sJumpFix.nInstrIdx = nInstrIdx;
			SySetPut(&pLoop->aPostContFix, (const void *)&sJumpFix);
		}
	}
	if( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_SEMI) == 0 ){
		/* Not so fatal, emit a warning only */
		jx9GenCompileError(&(*pGen), E_WARNING, pGen->pIn->nLine, "Expected semi-colon ';' after 'continue' statement");
	}
	/* Statement successfully compiled */
	return SXRET_OK;
}
/*
 * Compile the 'break' statement.
 * According to the JX9 language reference
 *  break ends execution of the current for, foreach, while, do-while or switch
 *  structure.
 *  break accepts an optional numeric argument which tells it how many nested
 *  enclosing structures are to be broken out of. 
 */
static sxi32 jx9CompileBreak(jx9_gen_state *pGen)
{
	GenBlock *pLoop; /* Target loop */
	sxi32 iLevel;    /* How many nesting loop to skip */
	sxu32 nLine;
	sxi32 rc;
	nLine = pGen->pIn->nLine;
	iLevel = 0;
	/* Jump the 'break' keyword */
	pGen->pIn++;
	if( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_NUM) ){
		/* optional numeric argument which tells us how many levels
		 * of enclosing loops we should skip to the end of. 
		 */
		iLevel = (sxi32)jx9TokenValueToInt64(&pGen->pIn->sData);
		if( iLevel < 2 ){
			iLevel = 0;
		}
		pGen->pIn++; /* Jump the optional numeric argument */
	}
	/* Extract the target loop */
	pLoop = GenStateFetchBlock(pGen->pCurrent, GEN_BLOCK_LOOP, iLevel);
	if( pLoop == 0 ){
		/* Illegal break */
		rc = jx9GenCompileError(pGen, E_ERROR, pGen->pIn->nLine, "A 'break' statement may only be used within a loop or switch");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
	}else{
		sxu32 nInstrIdx; 
		rc = jx9VmEmitInstr(pGen->pVm, JX9_OP_JMP, 0, 0, 0, &nInstrIdx);
		if( rc == SXRET_OK ){
			/* Fix the jump later when the jump destination is resolved */
			GenStateNewJumpFixup(pLoop, JX9_OP_JMP, nInstrIdx);
		}
	}
	if( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_SEMI) == 0 ){
		/* Not so fatal, emit a warning only */
		jx9GenCompileError(&(*pGen), E_WARNING, pGen->pIn->nLine, "Expected semi-colon ';' after 'break' statement");
	}
	/* Statement successfully compiled */
	return SXRET_OK;
}
/* Forward declaration */
static sxi32 GenStateCompileChunk(jx9_gen_state *pGen,sxi32 iFlags);
/*
 * Compile a JX9 block.
 * A block is simply one or more JX9 statements and expressions to compile
 * optionally delimited by braces {}.
 * Return SXRET_OK on success. Any other return value indicates failure
 * and this function takes care of generating the appropriate error
 * message.
 */
static sxi32 jx9CompileBlock(
	jx9_gen_state *pGen /* Code generator state */
	)
{
	sxi32 rc;
	if( pGen->pIn->nType & JX9_TK_OCB /* '{' */ ){
		sxu32 nLine = pGen->pIn->nLine;
		rc = GenStateEnterBlock(&(*pGen), GEN_BLOCK_STD, jx9VmInstrLength(pGen->pVm), 0, 0);
		if( rc != SXRET_OK ){
			return SXERR_ABORT;
		}
		pGen->pIn++;
		/* Compile until we hit the closing braces '}' */
		for(;;){
			if( pGen->pIn >= pGen->pEnd ){
				/* No more token to process. Missing closing braces */
				jx9GenCompileError(&(*pGen), E_ERROR, nLine, "Missing closing braces '}'");
				break;
			}
			if( pGen->pIn->nType & JX9_TK_CCB/*'}'*/ ){
				/* Closing braces found, break immediately*/
				pGen->pIn++;
				break;
			}
			/* Compile a single statement */
			rc = GenStateCompileChunk(&(*pGen),JX9_COMPILE_SINGLE_STMT);
			if( rc == SXERR_ABORT ){
				return SXERR_ABORT;
			}
		}
		GenStateLeaveBlock(&(*pGen), 0);			
	}else{
		/* Compile a single statement */
		rc = GenStateCompileChunk(&(*pGen),JX9_COMPILE_SINGLE_STMT);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
	}
	/* Jump trailing semi-colons ';' */
	while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_SEMI) ){
		pGen->pIn++;
	}
	return SXRET_OK;
}
/*
 * Compile the gentle 'while' statement.
 * According to the JX9 language reference
 *  while loops are the simplest type of loop in JX9.They behave just like their C counterparts.
 *  The basic form of a while statement is:
 *  while (expr)
 *   statement
 *  The meaning of a while statement is simple. It tells JX9 to execute the nested statement(s)
 *  repeatedly, as long as the while expression evaluates to TRUE. The value of the expression
 *  is checked each time at the beginning of the loop, so even if this value changes during
 *  the execution of the nested statement(s), execution will not stop until the end of the iteration
 *  (each time JX9 runs the statements in the loop is one iteration). Sometimes, if the while
 *  expression evaluates to FALSE from the very beginning, the nested statement(s) won't even be run once.
 *  Like with the if statement, you can group multiple statements within the same while loop by surrounding
 *  a group of statements with curly braces, or by using the alternate syntax:
 *  while (expr):
 *    statement
 *   endwhile;
 */
static sxi32 jx9CompileWhile(jx9_gen_state *pGen)
{ 
	GenBlock *pWhileBlock = 0;
	SyToken *pTmp, *pEnd = 0;
	sxu32 nFalseJump;
	sxu32 nLine;
	sxi32 rc;
	nLine = pGen->pIn->nLine;
	/* Jump the 'while' keyword */
	pGen->pIn++;    
	if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & JX9_TK_LPAREN) == 0 ){
		/* Syntax error */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "Expected '(' after 'while' keyword");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		goto Synchronize;
	}
	/* Jump the left parenthesis '(' */
	pGen->pIn++; 
	/* Create the loop block */
	rc = GenStateEnterBlock(&(*pGen), GEN_BLOCK_LOOP, jx9VmInstrLength(pGen->pVm), 0, &pWhileBlock);
	if( rc != SXRET_OK ){
		return SXERR_ABORT;
	}
	/* Delimit the condition */
	jx9DelimitNestedTokens(pGen->pIn, pGen->pEnd, JX9_TK_LPAREN /* '(' */, JX9_TK_RPAREN /* ')' */, &pEnd);
	if( pGen->pIn == pEnd || pEnd >= pGen->pEnd ){
		/* Empty expression */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "Expected expression after 'while' keyword");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
	}
	/* Swap token streams */
	pTmp = pGen->pEnd;
	pGen->pEnd = pEnd;
	/* Compile the expression */
	rc = jx9CompileExpr(&(*pGen), 0, 0);
	if( rc == SXERR_ABORT ){
		/* Expression handler request an operation abort [i.e: Out-of-memory] */
		return SXERR_ABORT;
	}
	/* Update token stream */
	while(pGen->pIn < pEnd ){
		rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "Unexpected token '%z'", &pGen->pIn->sData);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
		pGen->pIn++;
	}
	/* Synchronize pointers */
	pGen->pIn  = &pEnd[1];
	pGen->pEnd = pTmp;
	/* Emit the false jump */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_JZ, 0, 0, 0, &nFalseJump);
	/* Save the instruction index so we can fix it later when the jump destination is resolved */
	GenStateNewJumpFixup(pWhileBlock, JX9_OP_JZ, nFalseJump);
	/* Compile the loop body */
	rc = jx9CompileBlock(&(*pGen));
	if( rc == SXERR_ABORT ){
		return SXERR_ABORT;
	}
	/* Emit the unconditional jump to the start of the loop */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_JMP, 0, pWhileBlock->nFirstInstr, 0, 0);
	/* Fix all jumps now the destination is resolved */
	GenStateFixJumps(pWhileBlock, -1, jx9VmInstrLength(pGen->pVm));
	/* Release the loop block */
	GenStateLeaveBlock(pGen, 0);
	/* Statement successfully compiled */
	return SXRET_OK;
Synchronize:
	/* Synchronize with the first semi-colon ';' so we can avoid 
	 * compiling this erroneous block.
	 */
	while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & (JX9_TK_SEMI|JX9_TK_OCB)) == 0 ){
		pGen->pIn++;
	}
	return SXRET_OK;
}
/*
 * Compile the complex and powerful 'for' statement.
 * According to the JX9 language reference
 *  for loops are the most complex loops in JX9. They behave like their C counterparts.
 *  The syntax of a for loop is:
 *  for (expr1; expr2; expr3)
 *   statement
 *  The first expression (expr1) is evaluated (executed) once unconditionally at
 *  the beginning of the loop.
 *  In the beginning of each iteration, expr2 is evaluated. If it evaluates to
 *  TRUE, the loop continues and the nested statement(s) are executed. If it evaluates
 *  to FALSE, the execution of the loop ends.
 *  At the end of each iteration, expr3 is evaluated (executed).
 *  Each of the expressions can be empty or contain multiple expressions separated by commas.
 *  In expr2, all expressions separated by a comma are evaluated but the result is taken
 *  from the last part. expr2 being empty means the loop should be run indefinitely
 *  (JX9 implicitly considers it as TRUE, like C). This may not be as useless as you might
 *  think, since often you'd want to end the loop using a conditional break statement instead
 *  of using the for truth expression.
 */
static sxi32 jx9CompileFor(jx9_gen_state *pGen)
{
	SyToken *pTmp, *pPostStart, *pEnd = 0;
	GenBlock *pForBlock = 0;
	sxu32 nFalseJump;
	sxu32 nLine;
	sxi32 rc;
	nLine = pGen->pIn->nLine;
	/* Jump the 'for' keyword */
	pGen->pIn++;    
	if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & JX9_TK_LPAREN) == 0 ){
		/* Syntax error */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "Expected '(' after 'for' keyword");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		return SXRET_OK;
	}
	/* Jump the left parenthesis '(' */
	pGen->pIn++; 
	/* Delimit the init-expr;condition;post-expr */
	jx9DelimitNestedTokens(pGen->pIn, pGen->pEnd, JX9_TK_LPAREN /* '(' */, JX9_TK_RPAREN /* ')' */, &pEnd);
	if( pGen->pIn == pEnd || pEnd >= pGen->pEnd ){
		/* Empty expression */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "for: Invalid expression");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		/* Synchronize */
		pGen->pIn = pEnd;
		if( pGen->pIn < pGen->pEnd ){
			pGen->pIn++;
		}
		return SXRET_OK;
	}
	/* Swap token streams */
	pTmp = pGen->pEnd;
	pGen->pEnd = pEnd;
	/* Compile initialization expressions if available */
	rc = jx9CompileExpr(&(*pGen), 0, 0);
	/* Pop operand lvalues */
	if( rc == SXERR_ABORT ){
		/* Expression handler request an operation abort [i.e: Out-of-memory] */
		return SXERR_ABORT;
	}else if( rc != SXERR_EMPTY ){
		jx9VmEmitInstr(pGen->pVm, JX9_OP_POP, 1, 0, 0, 0);
	}
	if( (pGen->pIn->nType & JX9_TK_SEMI) == 0 ){
		/* Syntax error */
		rc = jx9GenCompileError(pGen, E_ERROR, pGen->pIn->nLine, 
			"for: Expected ';' after initialization expressions");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		return SXRET_OK;
	}
	/* Jump the trailing ';' */
	pGen->pIn++;
	/* Create the loop block */
	rc = GenStateEnterBlock(&(*pGen), GEN_BLOCK_LOOP, jx9VmInstrLength(pGen->pVm), 0, &pForBlock);
	if( rc != SXRET_OK ){
		return SXERR_ABORT;
	}
	/* Deffer continue jumps */
	pForBlock->bPostContinue = TRUE;
	/* Compile the condition */
	rc = jx9CompileExpr(&(*pGen), 0, 0);
	if( rc == SXERR_ABORT ){
		/* Expression handler request an operation abort [i.e: Out-of-memory] */
		return SXERR_ABORT;
	}else if( rc != SXERR_EMPTY ){
		/* Emit the false jump */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_JZ, 0, 0, 0, &nFalseJump);
		/* Save the instruction index so we can fix it later when the jump destination is resolved */
		GenStateNewJumpFixup(pForBlock, JX9_OP_JZ, nFalseJump);
	}
	if( (pGen->pIn->nType & JX9_TK_SEMI) == 0 ){
		/* Syntax error */
		rc = jx9GenCompileError(pGen, E_ERROR, pGen->pIn->nLine, 
			"for: Expected ';' after conditionals expressions");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		return SXRET_OK;
	}
	/* Jump the trailing ';' */
	pGen->pIn++;
	/* Save the post condition stream */
	pPostStart = pGen->pIn;
	/* Compile the loop body */
	pGen->pIn  = &pEnd[1]; /* Jump the trailing parenthesis ')' */
	pGen->pEnd = pTmp;
	rc = jx9CompileBlock(&(*pGen));
	if( rc == SXERR_ABORT ){
		return SXERR_ABORT;
	}
	/* Fix post-continue jumps */
	if( SySetUsed(&pForBlock->aPostContFix) > 0 ){
		JumpFixup *aPost;
		VmInstr *pInstr;
		sxu32 nJumpDest;
		sxu32 n;
		aPost = (JumpFixup *)SySetBasePtr(&pForBlock->aPostContFix);
		nJumpDest = jx9VmInstrLength(pGen->pVm);
		for( n = 0 ; n < SySetUsed(&pForBlock->aPostContFix) ; ++n ){
			pInstr = jx9VmGetInstr(pGen->pVm, aPost[n].nInstrIdx);
			if( pInstr ){
				/* Fix jump */
				pInstr->iP2 = nJumpDest;
			}
		}
	}
	/* compile the post-expressions if available */
	while( pPostStart < pEnd && (pPostStart->nType & JX9_TK_SEMI) ){
		pPostStart++;
	}
	if( pPostStart < pEnd ){
		SyToken *pTmpIn, *pTmpEnd;
		SWAP_DELIMITER(pGen, pPostStart, pEnd);
		rc = jx9CompileExpr(&(*pGen), 0, 0);
		if( pGen->pIn < pGen->pEnd ){
			/* Syntax error */
			rc = jx9GenCompileError(pGen, E_ERROR, pGen->pIn->nLine, "for: Expected ')' after post-expressions");
			if( rc == SXERR_ABORT ){
				/* Error count limit reached, abort immediately */
				return SXERR_ABORT;
			}
			return SXRET_OK;
		}
		RE_SWAP_DELIMITER(pGen);
		if( rc == SXERR_ABORT ){
			/* Expression handler request an operation abort [i.e: Out-of-memory] */
			return SXERR_ABORT;
		}else if( rc != SXERR_EMPTY){
			/* Pop operand lvalue */
			jx9VmEmitInstr(pGen->pVm, JX9_OP_POP, 1, 0, 0, 0);
		}
	}
	/* Emit the unconditional jump to the start of the loop */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_JMP, 0, pForBlock->nFirstInstr, 0, 0);
	/* Fix all jumps now the destination is resolved */
	GenStateFixJumps(pForBlock, -1, jx9VmInstrLength(pGen->pVm));
	/* Release the loop block */
	GenStateLeaveBlock(pGen, 0);
	/* Statement successfully compiled */
	return SXRET_OK;
}
/* Expression tree validator callback used by the 'foreach' statement.
 * Note that only variable expression [i.e: $x; ${'My'.'Var'}; ${$a['key]};...]
 * are allowed.
 */
static sxi32 GenStateForEachNodeValidator(jx9_gen_state *pGen,jx9_expr_node *pRoot)
{
	sxi32 rc = SXRET_OK; /* Assume a valid expression tree */
	if( pRoot->xCode != jx9CompileVariable ){
		/* Unexpected expression */
		rc = jx9GenCompileError(&(*pGen),
			E_ERROR,
			pRoot->pStart? pRoot->pStart->nLine : 0, 
			"foreach: Expecting a variable name"
			);
		if( rc != SXERR_ABORT ){
			rc = SXERR_INVALID;
		}
	}
	return rc;
}
/*
 * Compile the 'foreach' statement.
 * According to the JX9 language reference
 *  The foreach construct simply gives an easy way to iterate over arrays. foreach works
 *  only on arrays (and objects), and will issue an error when you try to use it on a variable
 *  with a different data type or an uninitialized variable. There are two syntaxes; the second
 *  is a minor but useful extension of the first:
 *  foreach (json_array_json_object as $value)
 *    statement
 *  foreach (json_array_json_objec as $key,$value)
 *   statement
 *  The first form loops over the array given by array_expression. On each loop, the value 
 *  of the current element is assigned to $value and the internal array pointer is advanced
 *  by one (so on the next loop, you'll be looking at the next element).
 *  The second form does the same thing, except that the current element's key will be assigned
 *  to the variable $key on each loop.
 *  Note:
 *  When foreach first starts executing, the internal array pointer is automatically reset to the
 *  first element of the array. This means that you do not need to call reset() before a foreach loop. 
 */
static sxi32 jx9CompileForeach(jx9_gen_state *pGen)
{ 
	SyToken *pCur, *pTmp, *pEnd = 0;
	GenBlock *pForeachBlock = 0;
	jx9_foreach_info *pInfo;
	sxu32 nFalseJump;
	VmInstr *pInstr;
	sxu32 nLine;
	sxi32 rc;
	nLine = pGen->pIn->nLine;
	/* Jump the 'foreach' keyword */
	pGen->pIn++;    
	if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & JX9_TK_LPAREN) == 0 ){
		/* Syntax error */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "foreach: Expected '('");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		goto Synchronize;
	}
	/* Jump the left parenthesis '(' */
	pGen->pIn++; 
	/* Create the loop block */
	rc = GenStateEnterBlock(&(*pGen), GEN_BLOCK_LOOP, jx9VmInstrLength(pGen->pVm), 0, &pForeachBlock);
	if( rc != SXRET_OK ){
		return SXERR_ABORT;
	}
	/* Delimit the expression */
	jx9DelimitNestedTokens(pGen->pIn, pGen->pEnd, JX9_TK_LPAREN /* '(' */, JX9_TK_RPAREN /* ')' */, &pEnd);
	if( pGen->pIn == pEnd || pEnd >= pGen->pEnd ){
		/* Empty expression */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "foreach: Missing expression");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		/* Synchronize */
		pGen->pIn = pEnd;
		if( pGen->pIn < pGen->pEnd ){
			pGen->pIn++;
		}
		return SXRET_OK;
	}
	/* Compile the array expression */
	pCur = pGen->pIn;
	while( pCur < pEnd ){
		if( pCur->nType & JX9_TK_KEYWORD ){
			sxi32 nKeywrd = SX_PTR_TO_INT(pCur->pUserData);
			if( nKeywrd == JX9_TKWRD_AS ){
				/* Break with the first 'as' found */
				break;
			}
		}
		/* Advance the stream cursor */
		pCur++;
	}
	if( pCur <= pGen->pIn ){
		rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, 
			"foreach: Missing array/object expression");
		if( rc == SXERR_ABORT ){
			/* Don't worry about freeing memory, everything will be released shortly */
			return SXERR_ABORT;
		}
		goto Synchronize;
	}
	/* Swap token streams */
	pTmp = pGen->pEnd;
	pGen->pEnd = pCur;
	rc = jx9CompileExpr(&(*pGen), 0, 0);
	if( rc == SXERR_ABORT ){
		/* Expression handler request an operation abort [i.e: Out-of-memory] */
		return SXERR_ABORT;
	}
	/* Update token stream */
	while(pGen->pIn < pCur ){
		rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "foreach: Unexpected token '%z'", &pGen->pIn->sData);
		if( rc == SXERR_ABORT ){
			/* Don't worry about freeing memory, everything will be released shortly */
			return SXERR_ABORT;
		}
		pGen->pIn++;
	}
	pCur++; /* Jump the 'as' keyword */
	pGen->pIn = pCur; 
	if( pGen->pIn >= pEnd ){
		rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "foreach: Missing $key => $value pair");
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
	}
	/* Create the foreach context */
	pInfo = (jx9_foreach_info *)SyMemBackendAlloc(&pGen->pVm->sAllocator, sizeof(jx9_foreach_info));
	if( pInfo == 0 ){
		jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "Fatal, JX9 engine is running out-of-memory");
		return SXERR_ABORT;
	}
	/* Zero the structure */
	SyZero(pInfo, sizeof(jx9_foreach_info));
	/* Initialize structure fields */
	SySetInit(&pInfo->aStep, &pGen->pVm->sAllocator, sizeof(jx9_foreach_step *));
	/* Check if we have a key field */
	while( pCur < pEnd && (pCur->nType & JX9_TK_COMMA) == 0 ){
		pCur++;
	}
	if( pCur < pEnd ){
		/* Compile the expression holding the key name */
		if( pGen->pIn >= pCur ){
			rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "foreach: Missing $key");
			if( rc == SXERR_ABORT ){
				/* Don't worry about freeing memory, everything will be released shortly */
				return SXERR_ABORT;
			}
		}else{
			pGen->pEnd = pCur;
			rc = jx9CompileExpr(&(*pGen), 0, GenStateForEachNodeValidator);
			if( rc == SXERR_ABORT ){
				/* Don't worry about freeing memory, everything will be released shortly */
				return SXERR_ABORT;
			}
			pInstr = jx9VmPopInstr(pGen->pVm);
			if( pInstr->p3 ){
				/* Record key name */
				SyStringInitFromBuf(&pInfo->sKey, pInstr->p3, SyStrlen((const char *)pInstr->p3));
			}
			pInfo->iFlags |= JX9_4EACH_STEP_KEY;
		}
		pGen->pIn = &pCur[1]; /* Jump the arrow */
	}
	pGen->pEnd = pEnd;
	if( pGen->pIn >= pEnd ){
		rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "foreach: Missing $value");
		if( rc == SXERR_ABORT ){
			/* Don't worry about freeing memory, everything will be released shortly */
			return SXERR_ABORT;
		}
		goto Synchronize;
	}
	/* Compile the expression holding the value name */
	rc = jx9CompileExpr(&(*pGen), 0, GenStateForEachNodeValidator);
	if( rc == SXERR_ABORT ){
		/* Don't worry about freeing memory, everything will be released shortly */
		return SXERR_ABORT;
	}
	pInstr = jx9VmPopInstr(pGen->pVm);
	if( pInstr->p3 ){
		/* Record value name */
		SyStringInitFromBuf(&pInfo->sValue, pInstr->p3, SyStrlen((const char *)pInstr->p3));
	}
	/* Emit the 'FOREACH_INIT' instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_FOREACH_INIT, 0, 0, pInfo, &nFalseJump);
	/* Save the instruction index so we can fix it later when the jump destination is resolved */
	GenStateNewJumpFixup(pForeachBlock, JX9_OP_FOREACH_INIT, nFalseJump);
	/* Record the first instruction to execute */
	pForeachBlock->nFirstInstr = jx9VmInstrLength(pGen->pVm);
	/* Emit the FOREACH_STEP instruction */
    jx9VmEmitInstr(pGen->pVm, JX9_OP_FOREACH_STEP, 0, 0, pInfo, &nFalseJump);
	/* Save the instruction index so we can fix it later when the jump destination is resolved */
	GenStateNewJumpFixup(pForeachBlock, JX9_OP_FOREACH_STEP, nFalseJump);
	/* Compile the loop body */
	pGen->pIn = &pEnd[1];
	pGen->pEnd = pTmp;
	rc = jx9CompileBlock(&(*pGen));
	if( rc == SXERR_ABORT ){
		/* Don't worry about freeing memory, everything will be released shortly */
		return SXERR_ABORT;
	}
	/* Emit the unconditional jump to the start of the loop */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_JMP, 0, pForeachBlock->nFirstInstr, 0, 0);
	/* Fix all jumps now the destination is resolved */
	GenStateFixJumps(pForeachBlock, -1,jx9VmInstrLength(pGen->pVm));
	/* Release the loop block */
	GenStateLeaveBlock(pGen, 0);
	/* Statement successfully compiled */
	return SXRET_OK;
Synchronize:
	/* Synchronize with the first semi-colon ';' so we can avoid 
	 * compiling this erroneous block.
	 */
	while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & (JX9_TK_SEMI|JX9_TK_OCB)) == 0 ){
		pGen->pIn++;
	}
	return SXRET_OK;
}
/*
 * Compile the infamous if/elseif/else if/else statements.
 * According to the JX9 language reference
 *  The if construct is one of the most important features of many languages JX9 included.
 *  It allows for conditional execution of code fragments. JX9 features an if structure 
 *  that is similar to that of C:
 *  if (expr)
 *   statement
 *  else construct:
 *   Often you'd want to execute a statement if a certain condition is met, and a different
 *   statement if the condition is not met. This is what else is for. else extends an if statement
 *   to execute a statement in case the expression in the if statement evaluates to FALSE.
 *   For example, the following code would display a is greater than b if $a is greater than
 *   $b, and a is NOT greater than b otherwise.
 *   The else statement is only executed if the if expression evaluated to FALSE, and if there
 *   were any elseif expressions - only if they evaluated to FALSE as well
 *  elseif
 *   elseif, as its name suggests, is a combination of if and else. Like else, it extends
 *   an if statement to execute a different statement in case the original if expression evaluates
 *   to FALSE. However, unlike else, it will execute that alternative expression only if the elseif
 *   conditional expression evaluates to TRUE. For example, the following code would display a is bigger
 *   than b, a equal to b or a is smaller than b:
 *    if ($a > $b) {
 *     print "a is bigger than b";
 *    } elseif ($a == $b) {
 *     print "a is equal to b";
 *    } else {
 *     print "a is smaller than b";
 *    }
 */
static sxi32 jx9CompileIf(jx9_gen_state *pGen)
{
	SyToken *pToken, *pTmp, *pEnd = 0;
	GenBlock *pCondBlock = 0;
	sxu32 nJumpIdx;
	sxu32 nKeyID;
	sxi32 rc;
	/* Jump the 'if' keyword */
	pGen->pIn++;
	pToken = pGen->pIn; 
	/* Create the conditional block */
	rc = GenStateEnterBlock(&(*pGen), GEN_BLOCK_COND, jx9VmInstrLength(pGen->pVm), 0, &pCondBlock);
	if( rc != SXRET_OK ){
		return SXERR_ABORT;
	}
	/* Process as many [if/else if/elseif/else] blocks as we can */
	for(;;){
		if( pToken >= pGen->pEnd || (pToken->nType & JX9_TK_LPAREN) == 0 ){
			/* Syntax error */
			if( pToken >= pGen->pEnd ){
				pToken--;
			}
			rc = jx9GenCompileError(pGen, E_ERROR, pToken->nLine, "if/else/elseif: Missing '('");
			if( rc == SXERR_ABORT ){
				/* Error count limit reached, abort immediately */
				return SXERR_ABORT;
			}
			goto Synchronize;
		}
		/* Jump the left parenthesis '(' */
		pToken++; 
		/* Delimit the condition */
		jx9DelimitNestedTokens(pToken, pGen->pEnd, JX9_TK_LPAREN /* '(' */, JX9_TK_RPAREN /* ')' */, &pEnd);
		if( pToken >= pEnd || (pEnd->nType & JX9_TK_RPAREN) == 0 ){
			/* Syntax error */
			if( pToken >= pGen->pEnd ){
				pToken--;
			}
			rc = jx9GenCompileError(pGen, E_ERROR, pToken->nLine, "if/else/elseif: Missing ')'");
			if( rc == SXERR_ABORT ){
				/* Error count limit reached, abort immediately */
				return SXERR_ABORT;
			}
			goto Synchronize;
		}
		/* Swap token streams */
		SWAP_TOKEN_STREAM(pGen, pToken, pEnd);
		/* Compile the condition */
		rc = jx9CompileExpr(&(*pGen), 0, 0);
		/* Update token stream */
		while(pGen->pIn < pEnd ){
			jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "Unexpected token '%z'", &pGen->pIn->sData);
			pGen->pIn++;
		}
		pGen->pIn  = &pEnd[1];
		pGen->pEnd = pTmp;
		if( rc == SXERR_ABORT ){
			/* Expression handler request an operation abort [i.e: Out-of-memory] */
			return SXERR_ABORT;
		}
		/* Emit the false jump */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_JZ, 0, 0, 0, &nJumpIdx);
		/* Save the instruction index so we can fix it later when the jump destination is resolved */
		GenStateNewJumpFixup(pCondBlock, JX9_OP_JZ, nJumpIdx);
		/* Compile the body */
		rc = jx9CompileBlock(&(*pGen));
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
		if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & JX9_TK_KEYWORD) == 0 ){
			break;
		}
		/* Ensure that the keyword ID is 'else if' or 'else' */
		nKeyID = (sxu32)SX_PTR_TO_INT(pGen->pIn->pUserData);
		if( (nKeyID & (JX9_TKWRD_ELSE|JX9_TKWRD_ELIF)) == 0 ){
			break;
		}
		/* Emit the unconditional jump */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_JMP, 0, 0, 0, &nJumpIdx);
		/* Save the instruction index so we can fix it later when the jump destination is resolved */
		GenStateNewJumpFixup(pCondBlock, JX9_OP_JMP, nJumpIdx);
		if( nKeyID & JX9_TKWRD_ELSE ){
			pToken = &pGen->pIn[1];
			if( pToken >= pGen->pEnd || (pToken->nType & JX9_TK_KEYWORD) == 0 ||
				SX_PTR_TO_INT(pToken->pUserData) != JX9_TKWRD_IF ){
					break;
			}
			pGen->pIn++; /* Jump the 'else' keyword */
		}
		pGen->pIn++; /* Jump the 'elseif/if' keyword */
		/* Synchronize cursors */
		pToken = pGen->pIn;
		/* Fix the false jump */
		GenStateFixJumps(pCondBlock, JX9_OP_JZ, jx9VmInstrLength(pGen->pVm));
	} /* For(;;) */
	/* Fix the false jump */
	GenStateFixJumps(pCondBlock, JX9_OP_JZ, jx9VmInstrLength(pGen->pVm));
	if( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_KEYWORD) &&
		(SX_PTR_TO_INT(pGen->pIn->pUserData) & JX9_TKWRD_ELSE) ){
			/* Compile the else block */
			pGen->pIn++;
			rc = jx9CompileBlock(&(*pGen));
			if( rc == SXERR_ABORT ){
				
				return SXERR_ABORT;
			}
	}
	nJumpIdx = jx9VmInstrLength(pGen->pVm);
	/* Fix all unconditional jumps now the destination is resolved */
	GenStateFixJumps(pCondBlock, JX9_OP_JMP, nJumpIdx);
	/* Release the conditional block */
	GenStateLeaveBlock(pGen, 0);
	/* Statement successfully compiled */
	return SXRET_OK;
Synchronize:
	/* Synchronize with the first semi-colon ';' so we can avoid compiling this erroneous block.
	 */
	while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & (JX9_TK_SEMI|JX9_TK_OCB)) == 0 ){
		pGen->pIn++;
	}
	return SXRET_OK;
}
/*
 * Compile the return statement.
 * According to the JX9 language reference
 *  If called from within a function, the return() statement immediately ends execution
 *  of the current function, and returns its argument as the value of the function call.
 *  return() will also end the execution of an eval() statement or script file.
 *  If called from the global scope, then execution of the current script file is ended.
 *  If the current script file was include()ed or require()ed, then control is passed back
 *  to the calling file. Furthermore, if the current script file was include()ed, then the value
 *  given to return() will be returned as the value of the include() call. If return() is called
 *  from within the main script file, then script execution end.
 *  Note that since return() is a language construct and not a function, the parentheses
 *  surrounding its arguments are not required. It is common to leave them out, and you actually
 *  should do so as JX9 has less work to do in this case. 
 *  Note: If no parameter is supplied, then the parentheses must be omitted and JX9 is returning NULL instead..
 */
static sxi32 jx9CompileReturn(jx9_gen_state *pGen)
{
	sxi32 nRet = 0; /* TRUE if there is a return value */
	sxi32 rc;
	/* Jump the 'return' keyword */
	pGen->pIn++;
	if( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_SEMI) == 0 ){
		/* Compile the expression */
		rc = jx9CompileExpr(&(*pGen), 0, 0);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}else if(rc != SXERR_EMPTY ){
			nRet = 1;
		}
	}
	/* Emit the done instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_DONE, nRet, 0, 0, 0);
	return SXRET_OK;
}
/*
 * Compile the die/exit language construct.
 * The role of these constructs is to terminate execution of the script.
 * Shutdown functions will always be executed even if exit() is called.
 */
static sxi32 jx9CompileHalt(jx9_gen_state *pGen)
{
	sxi32 nExpr = 0;
	sxi32 rc;
	/* Jump the die/exit keyword */
	pGen->pIn++;
	if( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_SEMI) == 0 ){
		/* Compile the expression */
		rc = jx9CompileExpr(&(*pGen), 0, 0);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}else if(rc != SXERR_EMPTY ){
			nExpr = 1;
		}
	}
	/* Emit the HALT instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_HALT, nExpr, 0, 0, 0);
	return SXRET_OK;
}
/*
 * Compile the static statement.
 * According to the JX9 language reference
 *  Another important feature of variable scoping is the static variable.
 *  A static variable exists only in a local function scope, but it does not lose its value
 *  when program execution leaves this scope.
 *  Static variables also provide one way to deal with recursive functions.
 */
static sxi32 jx9CompileStatic(jx9_gen_state *pGen)
{
	jx9_vm_func_static_var sStatic; /* Structure describing the static variable */
	jx9_vm_func *pFunc;             /* Enclosing function */
	GenBlock *pBlock;
	SyString *pName;
	char *zDup;
	sxu32 nLine;
	sxi32 rc;
	/* Jump the static keyword */
	nLine = pGen->pIn->nLine;
	pGen->pIn++;
	/* Extract the enclosing function if any */
	pBlock = pGen->pCurrent;
	while( pBlock ){
		if( pBlock->iFlags & GEN_BLOCK_FUNC){
			break;
		}
		/* Point to the upper block */
		pBlock = pBlock->pParent;
	}
	if( pBlock == 0 ){
		/* Static statement, called outside of a function body, treat it as a simple variable. */
		if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & JX9_TK_DOLLAR) == 0 ){
			rc = jx9GenCompileError(&(*pGen), E_ERROR, nLine, "Expected variable after 'static' keyword");
			if( rc == SXERR_ABORT ){
				return SXERR_ABORT;
			}
			goto Synchronize;
		}
		/* Compile the expression holding the variable */
		rc = jx9CompileExpr(&(*pGen), 0, 0);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}else if( rc != SXERR_EMPTY ){
			/* Emit the POP instruction */
			jx9VmEmitInstr(pGen->pVm, JX9_OP_POP, 1, 0, 0, 0);
		}
		return SXRET_OK;
	}
	pFunc = (jx9_vm_func *)pBlock->pUserData;
	/* Make sure we are dealing with a valid statement */
	if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & JX9_TK_DOLLAR) == 0 || &pGen->pIn[1] >= pGen->pEnd ||
		(pGen->pIn[1].nType & (JX9_TK_ID|JX9_TK_KEYWORD)) == 0 ){
			rc = jx9GenCompileError(&(*pGen), E_ERROR, nLine, "Expected variable after 'static' keyword");
			if( rc == SXERR_ABORT ){
				return SXERR_ABORT;
			}
			goto Synchronize;
	}
	pGen->pIn++;
	/* Extract variable name */
	pName = &pGen->pIn->sData;
	pGen->pIn++; /* Jump the var name */
	if( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & (JX9_TK_SEMI/*';'*/|JX9_TK_EQUAL/*'='*/)) == 0 ){
		rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "static: Unexpected token '%z'", &pGen->pIn->sData);
		goto Synchronize;
	}
	/* Initialize the structure describing the static variable */
	SySetInit(&sStatic.aByteCode, &pGen->pVm->sAllocator, sizeof(VmInstr));
	sStatic.nIdx = SXU32_HIGH; /* Not yet created */
	/* Duplicate variable name */
	zDup = SyMemBackendStrDup(&pGen->pVm->sAllocator, pName->zString, pName->nByte);
	if( zDup == 0 ){
		jx9GenCompileError(&(*pGen), E_ERROR, nLine, "Fatal, JX9 engine is running out of memory");
		return SXERR_ABORT;
	}
	SyStringInitFromBuf(&sStatic.sName, zDup, pName->nByte);
	/* Check if we have an expression to compile */
	if( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_EQUAL) ){
		SySet *pInstrContainer;
		pGen->pIn++; /* Jump the equal '=' sign */
		/* Swap bytecode container */
		pInstrContainer = jx9VmGetByteCodeContainer(pGen->pVm);
		jx9VmSetByteCodeContainer(pGen->pVm, &sStatic.aByteCode);
		/* Compile the expression */
		rc = jx9CompileExpr(&(*pGen), 0, 0);
		/* Emit the done instruction */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_DONE, (rc != SXERR_EMPTY ? 1 : 0), 0, 0, 0);
		/* Restore default bytecode container */
		jx9VmSetByteCodeContainer(pGen->pVm, pInstrContainer);
	}
	/* Finally save the compiled static variable in the appropriate container */
	SySetPut(&pFunc->aStatic, (const void *)&sStatic);
	return SXRET_OK;
Synchronize:
	/* Synchronize with the first semi-colon ';', so we can avoid compiling this erroneous
	 * statement. 
	 */
	while(pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_SEMI) ==  0 ){
		pGen->pIn++;
	}
	return SXRET_OK;
}
/*
 * Compile the 'const' statement.
 * According to the JX9 language reference
 *  A constant is an identifier (name) for a simple value. As the name suggests, that value
 *  cannot change during the execution of the script (except for magic constants, which aren't actually constants).
 *  A constant is case-sensitive by default. By convention, constant identifiers are always uppercase.
 *  The name of a constant follows the same rules as any label in JX9. A valid constant name starts
 *  with a letter or underscore, followed by any number of letters, numbers, or underscores.
 *  As a regular expression it would be expressed thusly: [a-zA-Z_\x7f-\xff][a-zA-Z0-9_\x7f-\xff]* 
 *  Syntax
 *  You can define a constant by using the define()-function or by using the const keyword outside
 *  a object definition. Once a constant is defined, it can never be changed or undefined.
 *  You can get the value of a constant by simply specifying its name. Unlike with variables
 *  you should not prepend a constant with a $. You can also use the function constant() to read
 *  a constant's value if you wish to obtain the constant's name dynamically. Use get_defined_constants()
 *  to get a list of all defined constants.
 */
static sxi32 jx9CompileConstant(jx9_gen_state *pGen)
{
	SySet *pConsCode, *pInstrContainer;
	sxu32 nLine = pGen->pIn->nLine;
	SyString *pName;
	sxi32 rc;
	pGen->pIn++; /* Jump the 'const' keyword */
	if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & (JX9_TK_SSTR|JX9_TK_DSTR|JX9_TK_ID|JX9_TK_KEYWORD)) == 0 ){
		/* Invalid constant name */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "const: Invalid constant name");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		goto Synchronize;
	}
	/* Peek constant name */
	pName = &pGen->pIn->sData;
	/* Make sure the constant name isn't reserved */
	if( GenStateIsReservedID(pName) ){
		/* Reserved constant */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "const: Cannot redeclare a reserved constant '%z'", pName);
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		goto Synchronize;
	}
	pGen->pIn++;
	if(pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & JX9_TK_EQUAL /* '=' */) == 0 ){
		/* Invalid statement*/
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "const: Expected '=' after constant name");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		goto Synchronize;
	}
	pGen->pIn++; /*Jump the equal sign */
	/* Allocate a new constant value container */
	pConsCode = (SySet *)SyMemBackendPoolAlloc(&pGen->pVm->sAllocator, sizeof(SySet));
	if( pConsCode == 0 ){
		return GenStateOutOfMem(pGen);
	}
	SySetInit(pConsCode, &pGen->pVm->sAllocator, sizeof(VmInstr));
	/* Swap bytecode container */
	pInstrContainer = jx9VmGetByteCodeContainer(pGen->pVm);
	jx9VmSetByteCodeContainer(pGen->pVm, pConsCode);
	/* Compile constant value */
	rc = jx9CompileExpr(&(*pGen), 0, 0);
	/* Emit the done instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_DONE, (rc != SXERR_EMPTY ? 1 : 0), 0, 0, 0);
	jx9VmSetByteCodeContainer(pGen->pVm, pInstrContainer); 
	if( rc == SXERR_ABORT ){
		/* Don't worry about freeing memory, everything will be released shortly */
		return SXERR_ABORT;
	}
	SySetSetUserData(pConsCode, pGen->pVm);
	/* Register the constant */
	rc = jx9VmRegisterConstant(pGen->pVm, pName, jx9VmExpandConstantValue, pConsCode);
	if( rc != SXRET_OK ){
		SySetRelease(pConsCode);
		SyMemBackendPoolFree(&pGen->pVm->sAllocator, pConsCode);
	}
	return SXRET_OK;
Synchronize:
	/* Synchronize with the next-semi-colon and avoid compiling this erroneous statement */
	while(pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_SEMI) == 0 ){
		pGen->pIn++;
	}
	return SXRET_OK;
}
/*
 * Compile the uplink construct.
 * According to the JX9 language reference
 *  In JX9 global variables must be declared uplink inside a function if they are going
 *  to be used in that function.
 *  Example #1 Using global
 *   $a = 1;
 *   $b = 2;
 *   function Sum()
 *   {
 *    uplink $a, $b;
 *    $b = $a + $b;
 *   } 
 *   Sum();
 *   print $b;
 *  ?>
 *  The above script will output 3. By declaring $a and $b global within the function
 *  all references to either variable will refer to the global version. There is no limit
 *  to the number of global variables that can be manipulated by a function.
 */
static sxi32 jx9CompileUplink(jx9_gen_state *pGen)
{
	SyToken *pTmp, *pNext = 0;
	sxi32 nExpr;
	sxi32 rc;
	/* Jump the 'uplink' keyword */
	pGen->pIn++;
	if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & JX9_TK_SEMI) ){
		/* Nothing to process */
		return SXRET_OK;
	}
	pTmp = pGen->pEnd;
	nExpr = 0;
	while( SXRET_OK == jx9GetNextExpr(pGen->pIn, pTmp, &pNext) ){
		if( pGen->pIn < pNext ){
			pGen->pEnd = pNext;
			if( (pGen->pIn->nType & JX9_TK_DOLLAR) == 0 ){
				rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "uplink: Expected variable name");
				if( rc == SXERR_ABORT ){
					return SXERR_ABORT;
				}
			}else{
				pGen->pIn++;
				if( pGen->pIn >= pGen->pEnd ){
					/* Emit a warning */
					jx9GenCompileError(&(*pGen), E_WARNING, pGen->pIn[-1].nLine, "uplink: Empty variable name");
				}else{
					rc = jx9CompileExpr(&(*pGen), 0, 0);
					if( rc == SXERR_ABORT ){
						return SXERR_ABORT;
					}else if(rc != SXERR_EMPTY ){
						nExpr++;
					}
				}
			}
		}
		/* Next expression in the stream */
		pGen->pIn = pNext;
		/* Jump trailing commas */
		while( pGen->pIn < pTmp && (pGen->pIn->nType & JX9_TK_COMMA) ){
			pGen->pIn++;
		}
	}
	/* Restore token stream */
	pGen->pEnd = pTmp;
	if( nExpr > 0 ){
		/* Emit the uplink instruction */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_UPLINK, nExpr, 0, 0, 0);
	}
	return SXRET_OK;
}
/*
 * Compile a switch block.
 *  (See block-comment below for more information)
 */
static sxi32 GenStateCompileSwitchBlock(jx9_gen_state *pGen,sxu32 *pBlockStart)
{
	sxi32 rc = SXRET_OK;
	while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & (JX9_TK_SEMI/*';'*/|JX9_TK_COLON/*':'*/)) == 0 ){
		/* Unexpected token */
		rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "Unexpected token '%z'", &pGen->pIn->sData);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
		pGen->pIn++;
	}
	pGen->pIn++;
	/* First instruction to execute in this block. */
	*pBlockStart = jx9VmInstrLength(pGen->pVm);
	/* Compile the block until we hit a case/default/endswitch keyword
	 * or the '}' token */
	for(;;){
		if( pGen->pIn >= pGen->pEnd ){
			/* No more input to process */
			break;
		}
		rc = SXRET_OK;
		if( (pGen->pIn->nType & JX9_TK_KEYWORD) == 0 ){
			if( pGen->pIn->nType & JX9_TK_CCB /*'}' */ ){
				rc = SXERR_EOF;
				break;
			}
		}else{
			sxi32 nKwrd;
			/* Extract the keyword */
			nKwrd = SX_PTR_TO_INT(pGen->pIn->pUserData);
			if( nKwrd == JX9_TKWRD_CASE || nKwrd == JX9_TKWRD_DEFAULT ){
				break;
			}
		}
		/* Compile block */
		rc = jx9CompileBlock(&(*pGen));
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
	}
	return rc;
}
/*
 * Compile a case eXpression.
 *  (See block-comment below for more information)
 */
static sxi32 GenStateCompileCaseExpr(jx9_gen_state *pGen, jx9_case_expr *pExpr)
{
	SySet *pInstrContainer;
	SyToken *pEnd, *pTmp;
	sxi32 iNest = 0;
	sxi32 rc;
	/* Delimit the expression */
	pEnd = pGen->pIn;
	while( pEnd < pGen->pEnd ){
		if( pEnd->nType & JX9_TK_LPAREN /*(*/ ){
			/* Increment nesting level */
			iNest++;
		}else if( pEnd->nType & JX9_TK_RPAREN /*)*/ ){
			/* Decrement nesting level */
			iNest--;
		}else if( pEnd->nType & (JX9_TK_SEMI/*';'*/|JX9_TK_COLON/*;'*/) && iNest < 1 ){
			break;
		}
		pEnd++;
	}
	if( pGen->pIn >= pEnd ){
		rc = jx9GenCompileError(pGen, E_ERROR, pGen->pIn->nLine, "Empty case expression");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
	}
	/* Swap token stream */
	pTmp = pGen->pEnd;
	pGen->pEnd = pEnd;
	pInstrContainer = jx9VmGetByteCodeContainer(pGen->pVm);
	jx9VmSetByteCodeContainer(pGen->pVm, &pExpr->aByteCode);
	rc = jx9CompileExpr(&(*pGen), 0, 0);
	/* Emit the done instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_DONE, (rc != SXERR_EMPTY ? 1 : 0), 0, 0, 0);
	jx9VmSetByteCodeContainer(pGen->pVm, pInstrContainer); 
	/* Update token stream */
	pGen->pIn  = pEnd;
	pGen->pEnd = pTmp;
	if( rc == SXERR_ABORT ){
		return SXERR_ABORT;
	}
	return SXRET_OK;
}
/*
 * Compile the smart switch statement.
 * According to the JX9 language reference manual
 *  The switch statement is similar to a series of IF statements on the same expression.
 *  In many occasions, you may want to compare the same variable (or expression) with many
 *  different values, and execute a different piece of code depending on which value it equals to.
 *  This is exactly what the switch statement is for.
 *  Note: Note that unlike some other languages, the continue statement applies to switch and acts
 *  similar to break. If you have a switch inside a loop and wish to continue to the next iteration
 *  of the outer loop, use continue 2. 
 *  Note that switch/case does loose comparision. 
 *  It is important to understand how the switch statement is executed in order to avoid mistakes.
 *  The switch statement executes line by line (actually, statement by statement).
 *  In the beginning, no code is executed. Only when a case statement is found with a value that
 *  matches the value of the switch expression does JX9 begin to execute the statements.
 *  JX9 continues to execute the statements until the end of the switch block, or the first time
 *  it sees a break statement. If you don't write a break statement at the end of a case's statement list.
 *  In a switch statement, the condition is evaluated only once and the result is compared to each
 *  case statement. In an elseif statement, the condition is evaluated again. If your condition
 *  is more complicated than a simple compare and/or is in a tight loop, a switch may be faster.
 *  The statement list for a case can also be empty, which simply passes control into the statement
 *  list for the next case. 
 *  The case expression may be any expression that evaluates to a simple type, that is, integer
 *  or floating-point numbers and strings.
 */
static sxi32 jx9CompileSwitch(jx9_gen_state *pGen)
{
	GenBlock *pSwitchBlock;
	SyToken *pTmp, *pEnd;
	jx9_switch *pSwitch;
	sxu32 nLine;
	sxi32 rc;
	nLine = pGen->pIn->nLine;
	/* Jump the 'switch' keyword */
	pGen->pIn++;    
	if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & JX9_TK_LPAREN) == 0 ){
		/* Syntax error */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "Expected '(' after 'switch' keyword");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		goto Synchronize;
	}
	/* Jump the left parenthesis '(' */
	pGen->pIn++; 
	pEnd = 0; /* cc warning */
	/* Create the loop block */
	rc = GenStateEnterBlock(&(*pGen), GEN_BLOCK_LOOP|GEN_BLOCK_SWITCH, 
		jx9VmInstrLength(pGen->pVm), 0, &pSwitchBlock);
	if( rc != SXRET_OK ){
		return SXERR_ABORT;
	}
	/* Delimit the condition */
	jx9DelimitNestedTokens(pGen->pIn, pGen->pEnd, JX9_TK_LPAREN /* '(' */, JX9_TK_RPAREN /* ')' */, &pEnd);
	if( pGen->pIn == pEnd || pEnd >= pGen->pEnd ){
		/* Empty expression */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "Expected expression after 'switch' keyword");
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
	}
	/* Swap token streams */
	pTmp = pGen->pEnd;
	pGen->pEnd = pEnd;
	/* Compile the expression */
	rc = jx9CompileExpr(&(*pGen), 0, 0);
	if( rc == SXERR_ABORT ){
		/* Expression handler request an operation abort [i.e: Out-of-memory] */
		return SXERR_ABORT;
	}
	/* Update token stream */
	while(pGen->pIn < pEnd ){
		rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, 
			"Switch: Unexpected token '%z'", &pGen->pIn->sData);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
		pGen->pIn++;
	}
	pGen->pIn  = &pEnd[1];
	pGen->pEnd = pTmp;
	if( pGen->pIn >= pGen->pEnd || &pGen->pIn[1] >= pGen->pEnd ||
		(pGen->pIn->nType & (JX9_TK_OCB/*'{'*/|JX9_TK_COLON/*:*/)) == 0 ){
			pTmp = pGen->pIn;
			if( pTmp >= pGen->pEnd ){
				pTmp--;
			}
			/* Unexpected token */
			rc = jx9GenCompileError(&(*pGen), E_ERROR, pTmp->nLine, "Switch: Unexpected token '%z'", &pTmp->sData);
			if( rc == SXERR_ABORT ){
				return SXERR_ABORT;
			}
			goto Synchronize;
	}
	pGen->pIn++; /* Jump the leading curly braces/colons */
	/* Create the switch blocks container */
	pSwitch = (jx9_switch *)SyMemBackendAlloc(&pGen->pVm->sAllocator, sizeof(jx9_switch));
	if( pSwitch == 0 ){
		/* Abort compilation */
		return GenStateOutOfMem(pGen);
	}
	/* Zero the structure */
	SyZero(pSwitch, sizeof(jx9_switch));
	/* Initialize fields */
	SySetInit(&pSwitch->aCaseExpr, &pGen->pVm->sAllocator, sizeof(jx9_case_expr));
	/* Emit the switch instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_SWITCH, 0, 0, pSwitch, 0);
	/* Compile case blocks */
	for(;;){
		sxu32 nKwrd;
		if( pGen->pIn >= pGen->pEnd ){
			/* No more input to process */
			break;
		}
		if( (pGen->pIn->nType & JX9_TK_KEYWORD) == 0 ){
			if(  (pGen->pIn->nType & JX9_TK_CCB /*}*/) == 0 ){
				/* Unexpected token */
				rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "Switch: Unexpected token '%z'", 
					&pGen->pIn->sData);
				if( rc == SXERR_ABORT ){
					return SXERR_ABORT;
				}
				/* FALL THROUGH */
			}
			/* Block compiled */
			break;
		}
		/* Extract the keyword */
		nKwrd = SX_PTR_TO_INT(pGen->pIn->pUserData);
		if( nKwrd == JX9_TKWRD_DEFAULT ){
			/*
			 * Accroding to the JX9 language reference manual
			 *  A special case is the default case. This case matches anything
			 *  that wasn't matched by the other cases.
			 */
			if( pSwitch->nDefault > 0 ){
				/* Default case already compiled */ 
				rc = jx9GenCompileError(&(*pGen), E_WARNING, pGen->pIn->nLine, "Switch: 'default' case already compiled");
				if( rc == SXERR_ABORT ){
					return SXERR_ABORT;
				}
			}
			pGen->pIn++; /* Jump the 'default' keyword */
			/* Compile the default block */
			rc = GenStateCompileSwitchBlock(pGen,&pSwitch->nDefault);
			if( rc == SXERR_ABORT){
				return SXERR_ABORT;
			}else if( rc == SXERR_EOF ){
				break;
			}
		}else if( nKwrd == JX9_TKWRD_CASE ){
			jx9_case_expr sCase;
			/* Standard case block */
			pGen->pIn++; /* Jump the 'case' keyword */
			/* initialize the structure */
			SySetInit(&sCase.aByteCode, &pGen->pVm->sAllocator, sizeof(VmInstr));
			/* Compile the case expression */
			rc = GenStateCompileCaseExpr(pGen, &sCase);
			if( rc == SXERR_ABORT ){
				return SXERR_ABORT;
			}
			/* Compile the case block */
			rc = GenStateCompileSwitchBlock(pGen,&sCase.nStart);
			/* Insert in the switch container */
			SySetPut(&pSwitch->aCaseExpr, (const void *)&sCase);
			if( rc == SXERR_ABORT){
				return SXERR_ABORT;
			}else if( rc == SXERR_EOF ){
				break;
			}
		}else{
			/* Unexpected token */
			rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "Switch: Unexpected token '%z'", 
				&pGen->pIn->sData);
			if( rc == SXERR_ABORT ){
				return SXERR_ABORT;
			}
			break;
		}
	}
	/* Fix all jumps now the destination is resolved */
	pSwitch->nOut = jx9VmInstrLength(pGen->pVm);
	GenStateFixJumps(pSwitchBlock, -1, jx9VmInstrLength(pGen->pVm));
	/* Release the loop block */
	GenStateLeaveBlock(pGen, 0);
	if( pGen->pIn < pGen->pEnd ){
		/* Jump the trailing curly braces */
		pGen->pIn++;
	}
	/* Statement successfully compiled */
	return SXRET_OK;
Synchronize:
	/* Synchronize with the first semi-colon */
	while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_SEMI) == 0 ){
		pGen->pIn++;
	}
	return SXRET_OK;
}
/*
 * Process default argument values. That is, a function may define C++-style default value
 * as follows:
 * function makecoffee($type = "cappuccino")
 * {
 *   return "Making a cup of $type.\n";
 * }
 * Some features:
 *  1 -) Default arguments value can be any complex expression [i.e: function call, annynoymous
 *      functions, array member, ..]
 * 2 -) Full type hinting: (Arguments are automatically casted to the desired type)
 *      Example:
 *           function a(string $a){} function b(int $a, string $c, float $d){}
 * 3 -) Function overloading!!
 *      Example:
 *      function foo($a) {
 *   	  return $a.JX9_EOL;
 *	    }
 *	    function foo($a, $b) {
 *   	  return $a + $b;
 *	    }
 *	    print foo(5); // Prints "5"
 *	    print foo(5, 2); // Prints "7"
 *      // Same arg
 *	   function foo(string $a)
 *	   {
 *	     print "a is a string\n";
 *	     dump($a);
 *	   }
 *	  function foo(int $a)
 *	  {
 *	    print "a is integer\n";
 *	    dump($a);
 *	  }
 *	  function foo(array $a)
 *	  {
 * 	    print "a is an array\n";
 * 	    dump($a);
 *	  }
 *	  foo('This is a great feature'); // a is a string [first foo]
 *	  foo(52); // a is integer [second foo] 
 *    foo(array(14, __TIME__, __DATE__)); // a is an array [third foo]
 * Please refer to the official documentation for more information on the powerful extension
 * introduced by the JX9 engine.
 */
static sxi32 GenStateProcessArgValue(jx9_gen_state *pGen, jx9_vm_func_arg *pArg, SyToken *pIn, SyToken *pEnd)
{
	SyToken *pTmpIn, *pTmpEnd;
	SySet *pInstrContainer;
	sxi32 rc;
	/* Swap token stream */
	SWAP_DELIMITER(pGen, pIn, pEnd);
	pInstrContainer = jx9VmGetByteCodeContainer(pGen->pVm);
	jx9VmSetByteCodeContainer(pGen->pVm, &pArg->aByteCode);
	/* Compile the expression holding the argument value */
	rc = jx9CompileExpr(&(*pGen), 0, 0);
	/* Emit the done instruction */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_DONE, (rc != SXERR_EMPTY ? 1 : 0), 0, 0, 0);
	jx9VmSetByteCodeContainer(pGen->pVm, pInstrContainer); 
	RE_SWAP_DELIMITER(pGen);
	if( rc == SXERR_ABORT ){
		return SXERR_ABORT;
	}
	return SXRET_OK;
}
/*
 * Collect function arguments one after one.
 * According to the JX9 language reference manual.
 * Information may be passed to functions via the argument list, which is a comma-delimited
 * list of expressions.
 * JX9 supports passing arguments by value (the default), passing by reference
 * and default argument values. Variable-length argument lists are also supported, 
 * see also the function references for func_num_args(), func_get_arg(), and func_get_args()
 * for more information.
 * Example #1 Passing arrays to functions
 * <?jx9
 * function takes_array($input)
 * {
 *    print "$input[0] + $input[1] = ", $input[0]+$input[1];
 * }
 * ?>
 * Making arguments be passed by reference
 * By default, function arguments are passed by value (so that if the value of the argument
 * within the function is changed, it does not get changed outside of the function).
 * To allow a function to modify its arguments, they must be passed by reference.
 * To have an argument to a function always passed by reference, prepend an ampersand (&)
 * to the argument name in the function definition:
 * Example #2 Passing function parameters by reference
 * <?jx9
 * function add_some_extra(&$string)
 * {
 *   $string .= 'and something extra.';
 * }
 * $str = 'This is a string, ';
 * add_some_extra($str);
 * print $str;    // outputs 'This is a string, and something extra.'
 * ?>
 *
 * JX9 have introduced powerful extension including full type hinting, function overloading
 * complex agrument values.Please refer to the official documentation for more information
 * on these extension.
 */
static sxi32 GenStateCollectFuncArgs(jx9_vm_func *pFunc, jx9_gen_state *pGen, SyToken *pEnd)
{
	jx9_vm_func_arg sArg; /* Current processed argument */
	SyToken *pCur, *pIn;  /* Token stream */
	SyBlob sSig;         /* Function signature */
	char *zDup;          /* Copy of argument name */
	sxi32 rc;

	pIn = pGen->pIn;
	pCur = 0;
	SyBlobInit(&sSig, &pGen->pVm->sAllocator);
	/* Process arguments one after one */
	for(;;){
		if( pIn >= pEnd ){
			/* No more arguments to process */
			break;
		}
		SyZero(&sArg, sizeof(jx9_vm_func_arg));
		SySetInit(&sArg.aByteCode, &pGen->pVm->sAllocator, sizeof(VmInstr));
		if( pIn->nType & (JX9_TK_ID|JX9_TK_KEYWORD) ){
			if( pIn->nType & JX9_TK_KEYWORD ){
				sxu32 nKey = (sxu32)(SX_PTR_TO_INT(pIn->pUserData));
				if( nKey & JX9_TKWRD_BOOL ){
					sArg.nType = MEMOBJ_BOOL;
				}else if( nKey & JX9_TKWRD_INT ){
					sArg.nType = MEMOBJ_INT;
				}else if( nKey & JX9_TKWRD_STRING ){
					sArg.nType = MEMOBJ_STRING;
				}else if( nKey & JX9_TKWRD_FLOAT ){
					sArg.nType = MEMOBJ_REAL;
				}else{
					jx9GenCompileError(&(*pGen), E_WARNING, pGen->pIn->nLine, 
						"Invalid argument type '%z', Automatic cast will not be performed", 
						&pIn->sData);
				}
			}
			pIn++;
		}
		if( pIn >= pEnd ){
			rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "Missing argument name");
			return rc;
		}
		if( pIn >= pEnd || (pIn->nType & JX9_TK_DOLLAR) == 0 || &pIn[1] >= pEnd || (pIn[1].nType & (JX9_TK_ID|JX9_TK_KEYWORD)) == 0 ){
			/* Invalid argument */ 
			rc = jx9GenCompileError(&(*pGen), E_ERROR, pGen->pIn->nLine, "Invalid argument name");
			return rc;
		}
		pIn++; /* Jump the dollar sign */
		/* Copy argument name */
		zDup = SyMemBackendStrDup(&pGen->pVm->sAllocator, SyStringData(&pIn->sData), SyStringLength(&pIn->sData));
		if( zDup == 0 ){
			return GenStateOutOfMem(pGen);
		}
		SyStringInitFromBuf(&sArg.sName, zDup, SyStringLength(&pIn->sData));
		pIn++;
		if( pIn < pEnd ){
			if( pIn->nType & JX9_TK_EQUAL ){
				SyToken *pDefend;
				sxi32 iNest = 0;
				pIn++; /* Jump the equal sign */
				pDefend = pIn;
				/* Process the default value associated with this argument */
				while( pDefend < pEnd ){
					if( (pDefend->nType & JX9_TK_COMMA) && iNest <= 0 ){
						break;
					}
					if( pDefend->nType & (JX9_TK_LPAREN/*'('*/|JX9_TK_OCB/*'{'*/|JX9_TK_OSB/*[*/) ){
						/* Increment nesting level */
						iNest++;
					}else if( pDefend->nType & (JX9_TK_RPAREN/*')'*/|JX9_TK_CCB/*'}'*/|JX9_TK_CSB/*]*/) ){
						/* Decrement nesting level */
						iNest--;
					}
					pDefend++;
				}
				if( pIn >= pDefend ){
					rc = jx9GenCompileError(&(*pGen), E_ERROR, pIn->nLine, "Missing argument default value");
					return rc;
				}
				/* Process default value */
				rc = GenStateProcessArgValue(&(*pGen), &sArg, pIn, pDefend);
				if( rc != SXRET_OK ){
					return rc;
				}
				/* Point beyond the default value */
				pIn = pDefend;
			}
			if( pIn < pEnd && (pIn->nType & JX9_TK_COMMA) == 0 ){
				rc = jx9GenCompileError(&(*pGen), E_ERROR, pIn->nLine, "Unexpected token '%z'", &pIn->sData);
				return rc;
			}
			pIn++; /* Jump the trailing comma */
		}
		/* Append argument signature */
		if( sArg.nType > 0 ){
			int c;
			c = 'n'; /* cc warning */
			/* Type leading character */
			switch(sArg.nType){
				case MEMOBJ_HASHMAP:
					/* Hashmap aka 'array' */
					c = 'h';
					break;
				case MEMOBJ_INT:
					/* Integer */
					c = 'i';
					break;
				case MEMOBJ_BOOL:
					/* Bool */
					c = 'b';
					break;
				case MEMOBJ_REAL:
					/* Float */
					c = 'f';
					break;
				case MEMOBJ_STRING:
					/* String */
					c = 's';
					break;
				default:
					break;
				}
				SyBlobAppend(&sSig, (const void *)&c, sizeof(char));
		}
		/* Save in the argument set */
		SySetPut(&pFunc->aArgs, (const void *)&sArg);
	}
	if( SyBlobLength(&sSig) > 0 ){
		/* Save function signature */
		SyStringInitFromBuf(&pFunc->sSignature, SyBlobData(&sSig), SyBlobLength(&sSig));
	}
	return SXRET_OK;
}
/*
 * Compile function [i.e: standard function, annonymous function or closure ] body.
 * Return SXRET_OK on success. Any other return value indicates failure
 * and this routine takes care of generating the appropriate error message.
 */
static sxi32 GenStateCompileFuncBody(
	jx9_gen_state *pGen,  /* Code generator state */
	jx9_vm_func *pFunc    /* Function state */
	)
{
	SySet *pInstrContainer; /* Instruction container */
	GenBlock *pBlock;
	sxi32 rc;
	/* Attach the new function */
	rc = GenStateEnterBlock(&(*pGen), GEN_BLOCK_PROTECTED|GEN_BLOCK_FUNC,jx9VmInstrLength(pGen->pVm), pFunc, &pBlock);
	if( rc != SXRET_OK ){
		return GenStateOutOfMem(pGen);
	}
	/* Swap bytecode containers */
	pInstrContainer = jx9VmGetByteCodeContainer(pGen->pVm);
	jx9VmSetByteCodeContainer(pGen->pVm, &pFunc->aByteCode);
	/* Compile the body */
	jx9CompileBlock(&(*pGen));
	/* Emit the final return if not yet done */
	jx9VmEmitInstr(pGen->pVm, JX9_OP_DONE, 0, 0, 0, 0);
	/* Restore the default container */
	jx9VmSetByteCodeContainer(pGen->pVm, pInstrContainer);
	/* Leave function block */
	GenStateLeaveBlock(&(*pGen), 0);
	if( rc == SXERR_ABORT ){
		/* Don't worry about freeing memory, everything will be released shortly */
		return SXERR_ABORT;
	}
	/* All done, function body compiled */
	return SXRET_OK;
}
/*
 * Compile a JX9 function whether is a Standard or Annonymous function.
 * According to the JX9 language reference manual.
 *  Function names follow the same rules as other labels in JX9. A valid function name
 *  starts with a letter or underscore, followed by any number of letters, numbers, or
 *  underscores. As a regular expression, it would be expressed thus:
 *     [a-zA-Z_\x7f-\xff][a-zA-Z0-9_\x7f-\xff]*. 
 *  Functions need not be defined before they are referenced.
 *  All functions and objectes in JX9 have the global scope - they can be called outside
 *  a function even if they were defined inside and vice versa.
 *  It is possible to call recursive functions in JX9. However avoid recursive function/method
 *  calls with over 32-64 recursion levels. 
 * 
 * JX9 have introduced powerful extension including full type hinting, function overloading, 
 * complex agrument values and more. Please refer to the official documentation for more information
 * on these extension.
 */
static sxi32 GenStateCompileFunc(
	jx9_gen_state *pGen, /* Code generator state */
	SyString *pName,     /* Function name. NULL otherwise */
	sxi32 iFlags,        /* Control flags */
	jx9_vm_func **ppFunc /* OUT: function state */
	)
{
	jx9_vm_func *pFunc;
	SyToken *pEnd;
	sxu32 nLine;
	char *zName;
	sxi32 rc;
	/* Extract line number */
	nLine = pGen->pIn->nLine;
	/* Jump the left parenthesis '(' */
	pGen->pIn++;
	/* Delimit the function signature */
	jx9DelimitNestedTokens(pGen->pIn, pGen->pEnd, JX9_TK_LPAREN /* '(' */, JX9_TK_RPAREN /* ')' */, &pEnd);
	if( pEnd >= pGen->pEnd ){
		/* Syntax error */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "Missing ')' after function '%z' signature", pName);
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		pGen->pIn = pGen->pEnd;
		return SXRET_OK;
	}
	/* Create the function state */
	pFunc = (jx9_vm_func *)SyMemBackendPoolAlloc(&pGen->pVm->sAllocator, sizeof(jx9_vm_func));
	if( pFunc == 0 ){
		goto OutOfMem;
	}
	/* function ID */
	zName = SyMemBackendStrDup(&pGen->pVm->sAllocator, pName->zString, pName->nByte);
	if( zName == 0 ){
		/* Don't worry about freeing memory, everything will be released shortly */
		goto OutOfMem;
	}
	/* Initialize the function state */
	jx9VmInitFuncState(pGen->pVm, pFunc, zName, pName->nByte, iFlags, 0);
	if( pGen->pIn < pEnd ){
		/* Collect function arguments */
		rc = GenStateCollectFuncArgs(pFunc, &(*pGen), pEnd);
		if( rc == SXERR_ABORT ){
			/* Don't worry about freeing memory, everything will be released shortly */
			return SXERR_ABORT;
		}
	}
	/* Compile function body */
	pGen->pIn = &pEnd[1];
	/* Compile the body */
	rc = GenStateCompileFuncBody(&(*pGen), pFunc);
	if( rc == SXERR_ABORT ){
		return SXERR_ABORT;
	}
	if( ppFunc ){
		*ppFunc = pFunc;
	}
	/* Finally register the function */
	rc = jx9VmInstallUserFunction(pGen->pVm, pFunc, 0);
	return rc;
	/* Fall through if something goes wrong */
OutOfMem:
	/* If the supplied memory subsystem is so sick that we are unable to allocate
	 * a tiny chunk of memory, there is no much we can do here.
	 */
	return GenStateOutOfMem(pGen);
}
/*
 * Compile a standard JX9 function.
 *  Refer to the block-comment above for more information.
 */
static sxi32 jx9CompileFunction(jx9_gen_state *pGen)
{
	SyString *pName;
	sxi32 iFlags;
	sxu32 nLine;
	sxi32 rc;

	nLine = pGen->pIn->nLine;
	pGen->pIn++; /* Jump the 'function' keyword */
	iFlags = 0;
	if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & (JX9_TK_ID|JX9_TK_KEYWORD)) == 0 ){
		/* Invalid function name */
		rc = jx9GenCompileError(&(*pGen), E_ERROR, nLine, "Invalid function name");
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
		/* Sychronize with the next semi-colon or braces*/
		while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & (JX9_TK_SEMI|JX9_TK_OCB)) == 0 ){
			pGen->pIn++;
		}
		return SXRET_OK;
	}
	pName = &pGen->pIn->sData;
	nLine = pGen->pIn->nLine;
	/* Jump the function name */
	pGen->pIn++;
	if( pGen->pIn >= pGen->pEnd || (pGen->pIn->nType & JX9_TK_LPAREN) == 0 ){
		/* Syntax error */
		rc = jx9GenCompileError(pGen, E_ERROR, nLine, "Expected '(' after function name '%z'", pName);
		if( rc == SXERR_ABORT ){
			/* Error count limit reached, abort immediately */
			return SXERR_ABORT;
		}
		/* Sychronize with the next semi-colon or '{' */
		while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & (JX9_TK_SEMI|JX9_TK_OCB)) == 0 ){
			pGen->pIn++;
		}
		return SXRET_OK;
	}
	/* Compile function body */
	rc = GenStateCompileFunc(&(*pGen),pName,iFlags,0);
	return rc;
}
/*
 * Generate bytecode for a given expression tree.
 * If something goes wrong while generating bytecode
 * for the expression tree (A very unlikely scenario)
 * this function takes care of generating the appropriate
 * error message.
 */
static sxi32 GenStateEmitExprCode(
	jx9_gen_state *pGen,  /* Code generator state */
	jx9_expr_node *pNode, /* Root of the expression tree */
	sxi32 iFlags /* Control flags */
	)
{
	VmInstr *pInstr;
	sxu32 nJmpIdx;
	sxi32 iP1 = 0;
	sxu32 iP2 = 0;
	void *p3  = 0;
	sxi32 iVmOp;
	sxi32 rc;
	if( pNode->xCode ){
		SyToken *pTmpIn, *pTmpEnd;
		/* Compile node */
		SWAP_DELIMITER(pGen, pNode->pStart, pNode->pEnd);
		rc = pNode->xCode(&(*pGen), iFlags);
		RE_SWAP_DELIMITER(pGen);
		return rc;
	}
	if( pNode->pOp == 0 ){
		jx9GenCompileError(&(*pGen), E_ERROR, pNode->pStart->nLine, 
			"Invalid expression node, JX9 is aborting compilation");
		return SXERR_ABORT;
	}
	iVmOp = pNode->pOp->iVmOp;
	if( pNode->pOp->iOp == EXPR_OP_QUESTY ){
		sxu32 nJz, nJmp;
		/* Ternary operator require special handling */
		/* Phase#1: Compile the condition */
		rc = GenStateEmitExprCode(&(*pGen), pNode->pCond, iFlags);
		if( rc != SXRET_OK ){
			return rc;
		}
		nJz = nJmp = 0; /* cc -O6 warning */
		/* Phase#2: Emit the false jump */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_JZ, 0, 0, 0, &nJz);
		if( pNode->pLeft ){
			/* Phase#3: Compile the 'then' expression  */
			rc = GenStateEmitExprCode(&(*pGen), pNode->pLeft, iFlags);
			if( rc != SXRET_OK ){
				return rc;
			}
		}
		/* Phase#4: Emit the unconditional jump */
		jx9VmEmitInstr(pGen->pVm, JX9_OP_JMP, 0, 0, 0, &nJmp);
		/* Phase#5: Fix the false jump now the jump destination is resolved. */
		pInstr = jx9VmGetInstr(pGen->pVm, nJz);
		if( pInstr ){
			pInstr->iP2 = jx9VmInstrLength(pGen->pVm);
		}
		/* Phase#6: Compile the 'else' expression */
		if( pNode->pRight ){
			rc = GenStateEmitExprCode(&(*pGen), pNode->pRight, iFlags);
			if( rc != SXRET_OK ){
				return rc;
			}
		}
		if( nJmp > 0 ){
			/* Phase#7: Fix the unconditional jump */
			pInstr = jx9VmGetInstr(pGen->pVm, nJmp);
			if( pInstr ){
				pInstr->iP2 = jx9VmInstrLength(pGen->pVm);
			}
		}
		/* All done */
		return SXRET_OK;
	}
	/* Generate code for the left tree */
	if( pNode->pLeft ){
		if( iVmOp == JX9_OP_CALL ){
			jx9_expr_node **apNode;
			sxi32 n;
			/* Recurse and generate bytecodes for function arguments */
			apNode = (jx9_expr_node **)SySetBasePtr(&pNode->aNodeArgs);
			/* Read-only load */
			iFlags |= EXPR_FLAG_RDONLY_LOAD;
			for( n = 0 ; n < (sxi32)SySetUsed(&pNode->aNodeArgs) ; ++n ){
				rc = GenStateEmitExprCode(&(*pGen), apNode[n], iFlags&~EXPR_FLAG_LOAD_IDX_STORE);
				if( rc != SXRET_OK ){
					return rc;
				}
			}
			/* Total number of given arguments */
			iP1 = (sxi32)SySetUsed(&pNode->aNodeArgs);
			/* Remove stale flags now */
			iFlags &= ~EXPR_FLAG_RDONLY_LOAD;
		}
		rc = GenStateEmitExprCode(&(*pGen), pNode->pLeft, iFlags);
		if( rc != SXRET_OK ){
			return rc;
		}
		if( iVmOp == JX9_OP_CALL ){
			pInstr = jx9VmPeekInstr(pGen->pVm);
			if( pInstr ){
				if ( pInstr->iOp == JX9_OP_LOADC ){
					/* Prevent constant expansion */
					pInstr->iP1 = 0;
				}else if( pInstr->iOp == JX9_OP_MEMBER /* $a.b(1, 2, 3) */  ){
					/* Annonymous function call, flag that */
					pInstr->iP2 = 1;
				}
			}
		}else if( iVmOp == JX9_OP_LOAD_IDX ){
			jx9_expr_node **apNode;
			sxi32 n;
			/* Recurse and generate bytecodes for array index */
			apNode = (jx9_expr_node **)SySetBasePtr(&pNode->aNodeArgs);
			for( n = 0 ; n < (sxi32)SySetUsed(&pNode->aNodeArgs) ; ++n ){
				rc = GenStateEmitExprCode(&(*pGen), apNode[n], iFlags&~EXPR_FLAG_LOAD_IDX_STORE);
				if( rc != SXRET_OK ){
					return rc;
				}
			}
			if( SySetUsed(&pNode->aNodeArgs) > 0 ){
				iP1 = 1; /* Node have an index associated with it */
			}
			if( iFlags & EXPR_FLAG_LOAD_IDX_STORE ){
				/* Create an empty entry when the desired index is not found */
				iP2 = 1;
			}
		}else if( pNode->pOp->iOp == EXPR_OP_COMMA ){
			/* POP the left node */
			jx9VmEmitInstr(pGen->pVm, JX9_OP_POP, 1, 0, 0, 0);
		}
	}
	rc = SXRET_OK;
	nJmpIdx = 0;
	/* Generate code for the right tree */
	if( pNode->pRight ){
		if( iVmOp == JX9_OP_LAND ){
			/* Emit the false jump so we can short-circuit the logical and */
			jx9VmEmitInstr(pGen->pVm, JX9_OP_JZ, 1/* Keep the value on the stack */, 0, 0, &nJmpIdx);
		}else if (iVmOp == JX9_OP_LOR ){
			/* Emit the true jump so we can short-circuit the logical or*/
			jx9VmEmitInstr(pGen->pVm, JX9_OP_JNZ, 1/* Keep the value on the stack */, 0, 0, &nJmpIdx);
		}else if( pNode->pOp->iPrec == 18 /* Combined binary operators [i.e: =, '.=', '+=', *=' ...] precedence */ ){
			iFlags |= EXPR_FLAG_LOAD_IDX_STORE;
		}
		rc = GenStateEmitExprCode(&(*pGen), pNode->pRight, iFlags);
		if( iVmOp == JX9_OP_STORE ){
			pInstr = jx9VmPeekInstr(pGen->pVm);
			if( pInstr ){
				if(pInstr->iOp == JX9_OP_MEMBER ){
					/* Perform a member store operation [i.e: $this.x = 50] */
					iP2 = 1;
				}else{
					if( pInstr->iOp == JX9_OP_LOAD_IDX ){
						/* Transform the STORE instruction to STORE_IDX instruction */
						iVmOp = JX9_OP_STORE_IDX;
						iP1 = pInstr->iP1;
					}else{
						p3 = pInstr->p3;
					}
					/* POP the last dynamic load instruction */
					(void)jx9VmPopInstr(pGen->pVm);
				}
			}
		}
	}
	if( iVmOp > 0 ){
		if( iVmOp == JX9_OP_INCR || iVmOp == JX9_OP_DECR ){
			if( pNode->iFlags & EXPR_NODE_PRE_INCR ){
				/* Pre-increment/decrement operator [i.e: ++$i, --$j ] */
				iP1 = 1;
			}
		}
		/* Finally, emit the VM instruction associated with this operator */
		jx9VmEmitInstr(pGen->pVm, iVmOp, iP1, iP2, p3, 0);
		if( nJmpIdx > 0 ){
			/* Fix short-circuited jumps now the destination is resolved */
			pInstr = jx9VmGetInstr(pGen->pVm, nJmpIdx);
			if( pInstr ){
				pInstr->iP2 = jx9VmInstrLength(pGen->pVm);
			}
		}
	}
	return rc;
}
/*
 * Compile a JX9 expression.
 * According to the JX9 language reference manual:
 *  Expressions are the most important building stones of JX9.
 *  In JX9, almost anything you write is an expression.
 *  The simplest yet most accurate way to define an expression
 *  is "anything that has a value". 
 * If something goes wrong while compiling the expression, this
 * function takes care of generating the appropriate error
 * message.
 */
static sxi32 jx9CompileExpr(
	jx9_gen_state *pGen, /* Code generator state */
	sxi32 iFlags,        /* Control flags */
	sxi32 (*xTreeValidator)(jx9_gen_state *, jx9_expr_node *) /* Node validator callback.NULL otherwise */
	)
{
	jx9_expr_node *pRoot;
	SySet sExprNode;
	SyToken *pEnd;
	sxi32 nExpr;
	sxi32 iNest;
	sxi32 rc;
	/* Initialize worker variables */
	nExpr = 0;
	pRoot = 0;
	SySetInit(&sExprNode, &pGen->pVm->sAllocator, sizeof(jx9_expr_node *));
	SySetAlloc(&sExprNode, 0x10);
	rc = SXRET_OK;
	/* Delimit the expression */
	pEnd = pGen->pIn;
	iNest = 0;
	while( pEnd < pGen->pEnd ){
		if( pEnd->nType & JX9_TK_OCB /* '{' */ ){
			/* Ticket 1433-30: Annonymous/Closure functions body */
			iNest++;
		}else if(pEnd->nType & JX9_TK_CCB /* '}' */ ){
			iNest--;
		}else if( pEnd->nType & JX9_TK_SEMI /* ';' */ ){
			if( iNest <= 0 ){
				break;
			}
		}
		pEnd++;
	}
	if( iFlags & EXPR_FLAG_COMMA_STATEMENT ){
		SyToken *pEnd2 = pGen->pIn;
		iNest = 0;
		/* Stop at the first comma */
		while( pEnd2 < pEnd ){
			if( pEnd2->nType & (JX9_TK_OCB/*'{'*/|JX9_TK_OSB/*'['*/|JX9_TK_LPAREN/*'('*/) ){
				iNest++;
			}else if(pEnd2->nType & (JX9_TK_CCB/*'}'*/|JX9_TK_CSB/*']'*/|JX9_TK_RPAREN/*')'*/)){
				iNest--;
			}else if( pEnd2->nType & JX9_TK_COMMA /*','*/ ){
				if( iNest <= 0 ){
					break;
				}
			}
			pEnd2++;
		}
		if( pEnd2 <pEnd ){
			pEnd = pEnd2;
		}
	}
	if( pEnd > pGen->pIn ){
		SyToken *pTmp = pGen->pEnd;
		/* Swap delimiter */
		pGen->pEnd = pEnd;
		/* Try to get an expression tree */
		rc = jx9ExprMakeTree(&(*pGen), &sExprNode, &pRoot);
		if( rc == SXRET_OK && pRoot ){
			rc = SXRET_OK;
			if( xTreeValidator ){
				/* Call the upper layer validator callback */
				rc = xTreeValidator(&(*pGen), pRoot);
			}
			if( rc != SXERR_ABORT ){
				/* Generate code for the given tree */
				rc = GenStateEmitExprCode(&(*pGen), pRoot, iFlags);
			}
			nExpr = 1;
		}
		/* Release the whole tree */
		jx9ExprFreeTree(&(*pGen), &sExprNode);
		/* Synchronize token stream */
		pGen->pEnd = pTmp;
		pGen->pIn  = pEnd;
		if( rc == SXERR_ABORT ){
			SySetRelease(&sExprNode);
			return SXERR_ABORT;
		}
	}
	SySetRelease(&sExprNode);
	return nExpr > 0 ? SXRET_OK : SXERR_EMPTY;
}
/*
 * Return a pointer to the node construct handler associated
 * with a given node type [i.e: string, integer, float, ...].
 */
JX9_PRIVATE ProcNodeConstruct jx9GetNodeHandler(sxu32 nNodeType)
{
	if( nNodeType & JX9_TK_NUM ){
		/* Numeric literal: Either real or integer */
		return jx9CompileNumLiteral;
	}else if( nNodeType & JX9_TK_DSTR ){
		/* Double quoted string */
		return jx9CompileString;
	}else if( nNodeType & JX9_TK_SSTR ){
		/* Single quoted string */
		return jx9CompileSimpleString;
	}else if( nNodeType & JX9_TK_NOWDOC ){
		/* Nowdoc */
		return jx9CompileNowdoc;
	}
	return 0;
}
/*
 * Jx9 Language construct table.
 */
static const LangConstruct aLangConstruct[] = {
	{ JX9_TKWRD_IF,       jx9CompileIf     },
	{ JX9_TKWRD_FUNCTION, jx9CompileFunction  },
	{ JX9_TKWRD_FOREACH,  jx9CompileForeach },
	{ JX9_TKWRD_WHILE,    jx9CompileWhile  },
	{ JX9_TKWRD_FOR,      jx9CompileFor    },
	{ JX9_TKWRD_SWITCH,   jx9CompileSwitch },
	{ JX9_TKWRD_DIE,      jx9CompileHalt   },
	{ JX9_TKWRD_EXIT,     jx9CompileHalt   },
	{ JX9_TKWRD_RETURN,   jx9CompileReturn },
	{ JX9_TKWRD_BREAK,    jx9CompileBreak  },
	{ JX9_TKWRD_CONTINUE, jx9CompileContinue  },
	{ JX9_TKWRD_STATIC,   jx9CompileStatic    },
	{ JX9_TKWRD_UPLINK,   jx9CompileUplink  },
	{ JX9_TKWRD_CONST,    jx9CompileConstant  },
};
/*
 * Return a pointer to the statement handler routine associated
 * with a given JX9 keyword [i.e: if, for, while, ...].
 */
static ProcLangConstruct GenStateGetStatementHandler(
	sxu32 nKeywordID   /* Keyword  ID*/
	)
{
	sxu32 n = 0;
	for(;;){
		if( n >= SX_ARRAYSIZE(aLangConstruct) ){
			break;
		}
		if( aLangConstruct[n].nID == nKeywordID ){
			/* Return a pointer to the handler.
			*/
			return aLangConstruct[n].xConstruct;
		}
		n++;
	}
	/* Not a language construct */
	return 0;
}
/*
 * Compile a jx9 program.
 * If something goes wrong while compiling the Jx9 chunk, this function
 * takes care of generating the appropriate error message.
 */
static sxi32 GenStateCompileChunk(
	jx9_gen_state *pGen, /* Code generator state */
	sxi32 iFlags             /* Compile flags */
	)
{
	ProcLangConstruct xCons;
	sxi32 rc;
	rc = SXRET_OK; /* Prevent compiler warning */
	for(;;){
		if( pGen->pIn >= pGen->pEnd ){
			/* No more input to process */
			break;
		}
		xCons = 0;
		if( pGen->pIn->nType & JX9_TK_KEYWORD ){
			sxu32 nKeyword = (sxu32)SX_PTR_TO_INT(pGen->pIn->pUserData);
			/* Try to extract a language construct handler */
			xCons = GenStateGetStatementHandler(nKeyword);
			if( xCons == 0 && !jx9IsLangConstruct(nKeyword) ){
				rc = jx9GenCompileError(pGen, E_ERROR, pGen->pIn->nLine,
					"Syntax error: Unexpected keyword '%z'",
					&pGen->pIn->sData);
				if( rc == SXERR_ABORT ){
					break;
				}
				/* Synchronize with the first semi-colon and avoid compiling
				 * this erroneous statement.
				 */
				xCons = jx9ErrorRecover;
			}
		}
		if( xCons == 0 ){
			/* Assume an expression an try to compile it */
			rc = jx9CompileExpr(&(*pGen), 0, 0);
			if(  rc != SXERR_EMPTY ){
				/* Pop l-value */
				jx9VmEmitInstr(pGen->pVm, JX9_OP_POP, 1, 0, 0, 0);
			}
		}else{
			/* Go compile the sucker */
			rc = xCons(&(*pGen));
		}
		if( rc == SXERR_ABORT ){
			/* Request to abort compilation */
			break;
		}
		/* Ignore trailing semi-colons ';' */
		while( pGen->pIn < pGen->pEnd && (pGen->pIn->nType & JX9_TK_SEMI) ){
			pGen->pIn++;
		}
		if( iFlags & JX9_COMPILE_SINGLE_STMT ){
			/* Compile a single statement and return */
			break;
		}
		/* LOOP ONE */
		/* LOOP TWO */
		/* LOOP THREE */
		/* LOOP FOUR */
	}
	/* Return compilation status */
	return rc;
}
/*
 * Compile a raw chunk. The raw chunk can contain JX9 code embedded
 * in HTML, XML and so on. This function handle all the stuff.
 * This is the only compile interface exported from this file.
 */
JX9_PRIVATE sxi32 jx9CompileScript(
	jx9_vm *pVm,        /* Generate JX9 bytecodes for this Virtual Machine */
	SyString *pScript,  /* Script to compile */
	sxi32 iFlags        /* Compile flags */
	)
{
	jx9_gen_state *pGen;
	SySet aToken;
	sxi32 rc;
	if( pScript->nByte < 1 ){
		/* Nothing to compile */
		return JX9_OK;
	}
	/* Initialize the tokens containers */
	SySetInit(&aToken, &pVm->sAllocator, sizeof(SyToken));
	SySetAlloc(&aToken, 0xc0);
	pGen = &pVm->sCodeGen;
	rc = JX9_OK;
	/* Tokenize the JX9 chunk first */
	jx9Tokenize(pScript->zString,pScript->nByte,&aToken);
	if( SySetUsed(&aToken) < 1 ){
		return SXERR_EMPTY;
	}
	/* Point to the head and tail of the token stream. */
	pGen->pIn  = (SyToken *)SySetBasePtr(&aToken);
	pGen->pEnd = &pGen->pIn[SySetUsed(&aToken)];
	/* Compile the chunk */
	rc = GenStateCompileChunk(pGen,iFlags);
	/* Cleanup */
	SySetRelease(&aToken);
	return rc;
}
/*
 * Utility routines.Initialize the code generator.
 */
JX9_PRIVATE sxi32 jx9InitCodeGenerator(
	jx9_vm *pVm,       /* Target VM */
	ProcConsumer xErr, /* Error log consumer callabck  */
	void *pErrData     /* Last argument to xErr() */
	)
{
	jx9_gen_state *pGen = &pVm->sCodeGen;
	/* Zero the structure */
	SyZero(pGen, sizeof(jx9_gen_state));
	/* Initial state */
	pGen->pVm  = &(*pVm);
	pGen->xErr = xErr;
	pGen->pErrData = pErrData;
	SyHashInit(&pGen->hLiteral, &pVm->sAllocator, 0, 0);
	SyHashInit(&pGen->hVar, &pVm->sAllocator, 0, 0);
	/* Create the global scope */
	GenStateInitBlock(pGen, &pGen->sGlobal,GEN_BLOCK_GLOBAL,jx9VmInstrLength(&(*pVm)), 0);
	/* Point to the global scope */
	pGen->pCurrent = &pGen->sGlobal;
	return SXRET_OK;
}
/*
 * Utility routines. Reset the code generator to it's initial state.
 */
JX9_PRIVATE sxi32 jx9ResetCodeGenerator(
	jx9_vm *pVm,       /* Target VM */
	ProcConsumer xErr, /* Error log consumer callabck  */
	void *pErrData     /* Last argument to xErr() */
	)
{
	jx9_gen_state *pGen = &pVm->sCodeGen;
	GenBlock *pBlock, *pParent;
	/* Point to the global scope */
	pBlock = pGen->pCurrent;
	while( pBlock->pParent != 0 ){
		pParent = pBlock->pParent;
		GenStateFreeBlock(pBlock);
		pBlock = pParent;
	}
	pGen->xErr = xErr;
	pGen->pErrData = pErrData;
	pGen->pCurrent = &pGen->sGlobal;
	pGen->pIn = pGen->pEnd = 0;
	pGen->nErr = 0;
	return SXRET_OK;
}
/*
 * Generate a compile-time error message.
 * If the error count limit is reached (usually 15 error message)
 * this function return SXERR_ABORT.In that case upper-layers must
 * abort compilation immediately.
 */
JX9_PRIVATE sxi32 jx9GenCompileError(jx9_gen_state *pGen,sxi32 nErrType,sxu32 nLine,const char *zFormat,...)
{
	SyBlob *pWorker = &pGen->pVm->pEngine->xConf.sErrConsumer;
	const char *zErr = "Error";
	va_list ap;
	if( nErrType == E_ERROR ){
		/* Increment the error counter */
		pGen->nErr++;
		if( pGen->nErr > 15 ){
			/* Error count limit reached */
			SyBlobFormat(pWorker, "%u Error count limit reached, JX9 is aborting compilation\n", nLine);	
			/* Abort immediately */
			return SXERR_ABORT;
		}
	}
	switch(nErrType){
	case E_WARNING: zErr = "Warning";     break;
	case E_PARSE:   zErr = "Parse error"; break;
	case E_NOTICE:  zErr = "Notice";      break;
	default:
		break;
	}
	/* Format the error message */
	SyBlobFormat(pWorker, "%u %s: ", nLine, zErr);
	va_start(ap, zFormat);
	SyBlobFormatAp(pWorker, zFormat, ap);
	va_end(ap);
	/* Append a new line */
	SyBlobAppend(pWorker, (const void *)"\n", sizeof(char));
	return JX9_OK;
}
