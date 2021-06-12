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
 /* $SymiscID: json.c v1.0 FreeBSD 2012-12-16 00:28 stable <chm@symisc.net> $ */
#ifndef JX9_AMALGAMATION
#include "jx9Int.h"
#endif
/* This file deals with JSON serialization, decoding and stuff like that. */
/*
 * Section: 
 *  JSON encoding/decoding routines.
 * Authors:
 *  Symisc Systems, devel@symisc.net.
 *  Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Devel.
 */
/* Forward reference */
static int VmJsonArrayEncode(jx9_value *pKey, jx9_value *pValue, void *pUserData);
static int VmJsonObjectEncode(jx9_value *pKey, jx9_value *pValue, void *pUserData);
/* 
 * JSON encoder state is stored in an instance 
 * of the following structure.
 */
typedef struct json_private_data json_private_data;
struct json_private_data
{
	SyBlob *pOut;      /* Output consumer buffer */
	int isFirst;       /* True if first encoded entry */
	int iFlags;        /* JSON encoding flags */
	int nRecCount;     /* Recursion count */
};
/*
 * Returns the JSON representation of a value.In other word perform a JSON encoding operation.
 * According to wikipedia
 * JSON's basic types are:
 *   Number (double precision floating-point format in JavaScript, generally depends on implementation)
 *   String (double-quoted Unicode, with backslash escaping)
 *   Boolean (true or false)
 *   Array (an ordered sequence of values, comma-separated and enclosed in square brackets; the values
 *    do not need to be of the same type)
 *   Object (an unordered collection of key:value pairs with the ':' character separating the key 
 *     and the value, comma-separated and enclosed in curly braces; the keys must be strings and should
 *     be distinct from each other)
 *   null (empty)
 * Non-significant white space may be added freely around the "structural characters"
 * (i.e. the brackets "[{]}", colon ":" and comma ",").
 */
static sxi32 VmJsonEncode(
	jx9_value *pIn,          /* Encode this value */
	json_private_data *pData /* Context data */
	){
		SyBlob *pOut = pData->pOut;
		int nByte;
		if( jx9_value_is_null(pIn) || jx9_value_is_resource(pIn)){
			/* null */
			SyBlobAppend(pOut, "null", sizeof("null")-1);
		}else if( jx9_value_is_bool(pIn) ){
			int iBool = jx9_value_to_bool(pIn);
			sxu32 iLen;
			/* true/false */
			iLen = iBool ? sizeof("true") : sizeof("false");
			SyBlobAppend(pOut, iBool ? "true" : "false", iLen-1);
		}else if(  jx9_value_is_numeric(pIn) && !jx9_value_is_string(pIn) ){
			const char *zNum;
			/* Get a string representation of the number */
			zNum = jx9_value_to_string(pIn, &nByte);
			SyBlobAppend(pOut,zNum,nByte);
		}else if( jx9_value_is_string(pIn) ){
				const char *zIn, *zEnd;
				int c;
				/* Encode the string */
				zIn = jx9_value_to_string(pIn, &nByte);
				zEnd = &zIn[nByte];
				/* Append the double quote */
				SyBlobAppend(pOut,"\"", sizeof(char));
				for(;;){
					if( zIn >= zEnd ){
						/* No more input to process */
						break;
					}
					c = zIn[0];
					/* Advance the stream cursor */
					zIn++;
					if( c == '"' || c == '\\' ){
						/* Unescape the character */
						SyBlobAppend(pOut,"\\", sizeof(char));
					}
					/* Append character verbatim */
					SyBlobAppend(pOut,(const char *)&c,sizeof(char));
				}
				/* Append the double quote */
				SyBlobAppend(pOut,"\"",sizeof(char));
		}else if( jx9_value_is_json_array(pIn) ){
			/* Encode the array/object */
			pData->isFirst = 1;
			if( jx9_value_is_json_object(pIn) ){
				/* Encode the object instance */
				pData->isFirst = 1;
				/* Append the curly braces */
				SyBlobAppend(pOut, "{",sizeof(char));
				/* Iterate throw object attribute */
				jx9_array_walk(pIn,VmJsonObjectEncode, pData);
				/* Append the closing curly braces  */
				SyBlobAppend(pOut, "}",sizeof(char));
			}else{
				/* Append the square bracket or curly braces */
				SyBlobAppend(pOut,"[",sizeof(char));
				/* Iterate throw array entries */
				jx9_array_walk(pIn, VmJsonArrayEncode, pData);
				/* Append the closing square bracket or curly braces */
				SyBlobAppend(pOut,"]",sizeof(char));
			}
		}else{
			/* Can't happen */
			SyBlobAppend(pOut,"null",sizeof("null")-1);
		}
		/* All done */
		return JX9_OK;
}
/*
 * The following walker callback is invoked each time we need
 * to encode an array to JSON.
 */
static int VmJsonArrayEncode(jx9_value *pKey, jx9_value *pValue, void *pUserData)
{
	json_private_data *pJson = (json_private_data *)pUserData;
	if( pJson->nRecCount > 31 ){
		/* Recursion limit reached, return immediately */
		SXUNUSED(pKey); /* cc warning */
		return JX9_OK;
	}
	if( !pJson->isFirst ){
		/* Append the colon first */
		SyBlobAppend(pJson->pOut,",",(int)sizeof(char));
	}
	/* Encode the value */
	pJson->nRecCount++;
	VmJsonEncode(pValue, pJson);
	pJson->nRecCount--;
	pJson->isFirst = 0;
	return JX9_OK;
}
/*
 * The following walker callback is invoked each time we need to encode
 * a object instance [i.e: Object in the JX9 jargon] to JSON.
 */
static int VmJsonObjectEncode(jx9_value *pKey,jx9_value *pValue,void *pUserData)
{
	json_private_data *pJson = (json_private_data *)pUserData;
	const char *zKey;
	int nByte;
	if( pJson->nRecCount > 31 ){
		/* Recursion limit reached, return immediately */
		return JX9_OK;
	}
	if( !pJson->isFirst ){
		/* Append the colon first */
		SyBlobAppend(pJson->pOut,",",sizeof(char));
	}
	/* Extract a string representation of the key */
	zKey = jx9_value_to_string(pKey, &nByte);
	/* Append the key and the double colon */
	if( nByte > 0 ){
		SyBlobAppend(pJson->pOut,"\"",sizeof(char));
		SyBlobAppend(pJson->pOut,zKey,(sxu32)nByte);
		SyBlobAppend(pJson->pOut,"\"",sizeof(char));
	}else{
		/* Can't happen */
		SyBlobAppend(pJson->pOut,"null",sizeof("null")-1);
	}
	SyBlobAppend(pJson->pOut,":",sizeof(char));
	/* Encode the value */
	pJson->nRecCount++;
	VmJsonEncode(pValue, pJson);
	pJson->nRecCount--;
	pJson->isFirst = 0;
	return JX9_OK;
}
/*
 *  Returns a string containing the JSON representation of value. 
 *  In other words, perform the serialization of the given JSON object.
 */
JX9_PRIVATE int jx9JsonSerialize(jx9_value *pValue,SyBlob *pOut)
{
	json_private_data sJson;
	/* Prepare the JSON data */
	sJson.nRecCount = 0;
	sJson.pOut = pOut;
	sJson.isFirst = 1;
	sJson.iFlags = 0;
	/* Perform the encoding operation */
	VmJsonEncode(pValue, &sJson);
	/* All done */
	return JX9_OK;
}
/* Possible tokens from the JSON tokenization process */
#define JSON_TK_TRUE    0x001 /* Boolean true */
#define JSON_TK_FALSE   0x002 /* Boolean false */
#define JSON_TK_STR     0x004 /* String enclosed in double quotes */
#define JSON_TK_NULL    0x008 /* null */
#define JSON_TK_NUM     0x010 /* Numeric */
#define JSON_TK_OCB     0x020 /* Open curly braces '{' */
#define JSON_TK_CCB     0x040 /* Closing curly braces '}' */
#define JSON_TK_OSB     0x080 /* Open square bracke '[' */
#define JSON_TK_CSB     0x100 /* Closing square bracket ']' */
#define JSON_TK_COLON   0x200 /* Single colon ':' */
#define JSON_TK_COMMA   0x400 /* Single comma ',' */
#define JSON_TK_ID      0x800 /* ID */
#define JSON_TK_INVALID 0x1000 /* Unexpected token */
/* 
 * Tokenize an entire JSON input.
 * Get a single low-level token from the input file.
 * Update the stream pointer so that it points to the first
 * character beyond the extracted token.
 */
static sxi32 VmJsonTokenize(SyStream *pStream, SyToken *pToken, void *pUserData, void *pCtxData)
{
	int *pJsonErr = (int *)pUserData;
	SyString *pStr;
	int c;
	/* Ignore leading white spaces */
	while( pStream->zText < pStream->zEnd && pStream->zText[0] < 0xc0 && SyisSpace(pStream->zText[0]) ){
		/* Advance the stream cursor */
		if( pStream->zText[0] == '\n' ){
			/* Update line counter */
			pStream->nLine++;
		}
		pStream->zText++;
	}
	if( pStream->zText >= pStream->zEnd ){
		/* End of input reached */
		SXUNUSED(pCtxData); /* cc warning */
		return SXERR_EOF;
	}
	/* Record token starting position and line */
	pToken->nLine = pStream->nLine;
	pToken->pUserData = 0;
	pStr = &pToken->sData;
	SyStringInitFromBuf(pStr, pStream->zText, 0);
	if( pStream->zText[0] >= 0xc0 || SyisAlpha(pStream->zText[0]) || pStream->zText[0] == '_' ){
		/* The following code fragment is taken verbatim from the xPP source tree.
		 * xPP is a modern embeddable macro processor with advanced features useful for
		 * application seeking for a production quality, ready to use macro processor.
		 * xPP is a widely used library developed and maintened by Symisc Systems.
		 * You can reach the xPP home page by following this link:
		 * http://xpp.symisc.net/
		 */
		const unsigned char *zIn;
		/* Isolate UTF-8 or alphanumeric stream */
		if( pStream->zText[0] < 0xc0 ){
			pStream->zText++;
		}
		for(;;){
			zIn = pStream->zText;
			if( zIn[0] >= 0xc0 ){
				zIn++;
				/* UTF-8 stream */
				while( zIn < pStream->zEnd && ((zIn[0] & 0xc0) == 0x80) ){
					zIn++;
				}
			}
			/* Skip alphanumeric stream */
			while( zIn < pStream->zEnd && zIn[0] < 0xc0 && (SyisAlphaNum(zIn[0]) || zIn[0] == '_') ){
				zIn++;
			}
			if( zIn == pStream->zText ){
				/* Not an UTF-8 or alphanumeric stream */
				break;
			}
			/* Synchronize pointers */
			pStream->zText = zIn;
		}
		/* Record token length */
		pStr->nByte = (sxu32)((const char *)pStream->zText-pStr->zString);
		/* A simple identifier */
		pToken->nType = JSON_TK_ID;
		if( pStr->nByte == sizeof("true") -1 && SyStrnicmp(pStr->zString, "true", sizeof("true")-1) == 0 ){
			/* boolean true */
			pToken->nType = JSON_TK_TRUE;
		}else if( pStr->nByte == sizeof("false") -1 && SyStrnicmp(pStr->zString,"false", sizeof("false")-1) == 0 ){
			/* boolean false */
			pToken->nType = JSON_TK_FALSE;
		}else if( pStr->nByte == sizeof("null") -1 && SyStrnicmp(pStr->zString,"null", sizeof("null")-1) == 0 ){
			/* NULL */
			pToken->nType = JSON_TK_NULL;
		}
		return SXRET_OK;
	}
	if( pStream->zText[0] == '{' || pStream->zText[0] == '[' || pStream->zText[0] == '}' || pStream->zText[0] == ']'
		|| pStream->zText[0] == ':' || pStream->zText[0] == ',' ){
			/* Single character */
			c = pStream->zText[0];
			/* Set token type */
			switch(c){
			case '[': pToken->nType = JSON_TK_OSB;   break;
			case '{': pToken->nType = JSON_TK_OCB;   break;
			case '}': pToken->nType = JSON_TK_CCB;   break;
			case ']': pToken->nType = JSON_TK_CSB;   break;
			case ':': pToken->nType = JSON_TK_COLON; break;
			case ',': pToken->nType = JSON_TK_COMMA; break;
			default:
				break;
			}
			/* Advance the stream cursor */
			pStream->zText++;
	}else if( pStream->zText[0] == '"') {
		/* JSON string */
		pStream->zText++;
		pStr->zString++;
		/* Delimit the string */
		while( pStream->zText < pStream->zEnd ){
			if( pStream->zText[0] == '"' && pStream->zText[-1] != '\\' ){
				break;
			}
			if( pStream->zText[0] == '\n' ){
				/* Update line counter */
				pStream->nLine++;
			}
			pStream->zText++;
		}
		if( pStream->zText >= pStream->zEnd ){
			/* Missing closing '"' */
			pToken->nType = JSON_TK_INVALID;
			*pJsonErr = SXERR_SYNTAX;
		}else{
			pToken->nType = JSON_TK_STR;
			pStream->zText++; /* Jump the closing double quotes */
		}
  }else if( (pStream->zText[0] < 0xc0 && SyisDigit(pStream->zText[0]))
          || pStream->zText[0] == '-' || pStream->zText[0] == '+' ){
		/* Number */
		pStream->zText++;
		pToken->nType = JSON_TK_NUM;
		while( pStream->zText < pStream->zEnd && pStream->zText[0] < 0xc0 && SyisDigit(pStream->zText[0]) ){
			pStream->zText++;
		}
		if( pStream->zText < pStream->zEnd ){
			c = pStream->zText[0];
			if( c == '.' ){
					/* Real number */
					pStream->zText++;
					while( pStream->zText < pStream->zEnd && pStream->zText[0] < 0xc0 && SyisDigit(pStream->zText[0]) ){
						pStream->zText++;
					}
					if( pStream->zText < pStream->zEnd ){
						c = pStream->zText[0];
						if( c=='e' || c=='E' ){
							pStream->zText++;
							if( pStream->zText < pStream->zEnd ){
								c = pStream->zText[0];
								if( c =='+' || c=='-' ){
									pStream->zText++;
								}
								while( pStream->zText < pStream->zEnd && pStream->zText[0] < 0xc0 && SyisDigit(pStream->zText[0]) ){
									pStream->zText++;
								}
							}
						}
					}					
				}else if( c=='e' || c=='E' ){
					/* Real number */
					pStream->zText++;
					if( pStream->zText < pStream->zEnd ){
						c = pStream->zText[0];
						if( c =='+' || c=='-' ){
							pStream->zText++;
						}
						while( pStream->zText < pStream->zEnd && pStream->zText[0] < 0xc0 && SyisDigit(pStream->zText[0]) ){
							pStream->zText++;
						}
					}					
				}
			}
	}else{
		/* Unexpected token */
		pToken->nType = JSON_TK_INVALID;
		/* Advance the stream cursor */
		pStream->zText++;
		*pJsonErr = SXERR_SYNTAX;
		/* Abort processing immediatley */
		return SXERR_ABORT;
	}
	/* record token length */
	pStr->nByte = (sxu32)((const char *)pStream->zText-pStr->zString);
	if( pToken->nType == JSON_TK_STR ){
		pStr->nByte--;
	}
	/* Return to the lexer */
	return SXRET_OK;
}
/*
 * JSON decoded input consumer callback signature.
 */
typedef int (*ProcJSONConsumer)(jx9_context *, jx9_value *, jx9_value *, void *); 
/* 
 * JSON decoder state is kept in the following structure.
 */
typedef struct json_decoder json_decoder;
struct json_decoder
{
	jx9_context *pCtx; /* Call context */
	ProcJSONConsumer xConsumer; /* Consumer callback */ 
	void *pUserData;   /* Last argument to xConsumer() */
	int iFlags;        /* Configuration flags */
	SyToken *pIn;      /* Token stream */
	SyToken *pEnd;     /* End of the token stream */
	int rec_count;     /* Current nesting level */
	int *pErr;         /* JSON decoding error if any */
};
/* Forward declaration */
static int VmJsonArrayDecoder(jx9_context *pCtx, jx9_value *pKey, jx9_value *pWorker, void *pUserData);
/*
 * Dequote [i.e: Resolve all backslash escapes ] a JSON string and store
 * the result in the given jx9_value.
 */
static void VmJsonDequoteString(const SyString *pStr, jx9_value *pWorker)
{
	const char *zIn = pStr->zString;
	const char *zEnd = &pStr->zString[pStr->nByte];
	const char *zCur;
	int c;
	/* Mark the value as a string */
	jx9_value_string(pWorker, "", 0); /* Empty string */
	for(;;){
		zCur = zIn;
		while( zIn < zEnd && zIn[0] != '\\' ){
			zIn++;
		}
		if( zIn > zCur ){
			/* Append chunk verbatim */
			jx9_value_string(pWorker, zCur, (int)(zIn-zCur));
		}
		zIn++;
		if( zIn >= zEnd ){
			/* End of the input reached */
			break;
		}
		c = zIn[0];
		/* Unescape the character */
		switch(c){
		case '"':  jx9_value_string(pWorker, (const char *)&c, (int)sizeof(char)); break;
		case '\\': jx9_value_string(pWorker, (const char *)&c, (int)sizeof(char)); break;
		case 'n':  jx9_value_string(pWorker, "\n", (int)sizeof(char)); break;
		case 'r':  jx9_value_string(pWorker, "\r", (int)sizeof(char)); break;
		case 't':  jx9_value_string(pWorker, "\t", (int)sizeof(char)); break;
		case 'f':  jx9_value_string(pWorker, "\f", (int)sizeof(char)); break;
		default:
			jx9_value_string(pWorker, (const char *)&c, (int)sizeof(char));
			break;
		}
		/* Advance the stream cursor */
		zIn++;
	}
}
/*
 * Returns a jx9_value holding the image of a JSON string. In other word perform a JSON decoding operation.
 * According to wikipedia
 * JSON's basic types are:
 *   Number (double precision floating-point format in JavaScript, generally depends on implementation)
 *   String (double-quoted Unicode, with backslash escaping)
 *   Boolean (true or false)
 *   Array (an ordered sequence of values, comma-separated and enclosed in square brackets; the values
 *    do not need to be of the same type)
 *   Object (an unordered collection of key:value pairs with the ':' character separating the key 
 *     and the value, comma-separated and enclosed in curly braces; the keys must be strings and should
 *     be distinct from each other)
 *   null (empty)
 * Non-significant white space may be added freely around the "structural characters" (i.e. the brackets "[{]}", colon ":" and comma ", ").
 */
static sxi32 VmJsonDecode(
	json_decoder *pDecoder, /* JSON decoder */      
	jx9_value *pArrayKey    /* Key for the decoded array */
	){
	jx9_value *pWorker; /* Worker variable */
	sxi32 rc;
	/* Check if we do not nest to much */
	if( pDecoder->rec_count > 31 ){
		/* Nesting limit reached, abort decoding immediately */
		return SXERR_ABORT;
	}
	if( pDecoder->pIn->nType & (JSON_TK_STR|JSON_TK_ID|JSON_TK_TRUE|JSON_TK_FALSE|JSON_TK_NULL|JSON_TK_NUM) ){
		/* Scalar value */
		pWorker = jx9_context_new_scalar(pDecoder->pCtx);
		if( pWorker == 0 ){
			jx9_context_throw_error(pDecoder->pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
			/* Abort the decoding operation immediately */
			return SXERR_ABORT;
		}
		/* Reflect the JSON image */
		if( pDecoder->pIn->nType & JSON_TK_NULL ){
			/* Nullify the value.*/
			jx9_value_null(pWorker);
		}else if( pDecoder->pIn->nType & (JSON_TK_TRUE|JSON_TK_FALSE) ){
			/* Boolean value */
			jx9_value_bool(pWorker, (pDecoder->pIn->nType & JSON_TK_TRUE) ? 1 : 0 );
		}else if( pDecoder->pIn->nType & JSON_TK_NUM ){
			SyString *pStr = &pDecoder->pIn->sData;
			/* 
			 * Numeric value.
			 * Get a string representation first then try to get a numeric
			 * value.
			 */
			jx9_value_string(pWorker, pStr->zString, (int)pStr->nByte);
			/* Obtain a numeric representation */
			jx9MemObjToNumeric(pWorker);
		}else if( pDecoder->pIn->nType & JSON_TK_ID ){
			SyString *pStr = &pDecoder->pIn->sData;
			jx9_value_string(pWorker, pStr->zString, (int)pStr->nByte);
		}else{
			/* Dequote the string */
			VmJsonDequoteString(&pDecoder->pIn->sData, pWorker);
		}
		/* Invoke the consumer callback */
		rc = pDecoder->xConsumer(pDecoder->pCtx, pArrayKey, pWorker, pDecoder->pUserData);
		if( rc == SXERR_ABORT ){
			return SXERR_ABORT;
		}
		/* All done, advance the stream cursor */
		pDecoder->pIn++;
	}else if( pDecoder->pIn->nType & JSON_TK_OSB /*'[' */) {
		ProcJSONConsumer xOld;
		void *pOld;
		/* Array representation*/
		pDecoder->pIn++;
		/* Create a working array */
		pWorker = jx9_context_new_array(pDecoder->pCtx);
		if( pWorker == 0 ){
			jx9_context_throw_error(pDecoder->pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
			/* Abort the decoding operation immediately */
			return SXERR_ABORT;
		}
		/* Save the old consumer */
		xOld = pDecoder->xConsumer;
		pOld = pDecoder->pUserData;
		/* Set the new consumer */
		pDecoder->xConsumer = VmJsonArrayDecoder;
		pDecoder->pUserData = pWorker;
		/* Decode the array */
		for(;;){
			/* Jump trailing comma. Note that the standard JX9 engine will not let you
			 * do this.
			 */
			while( (pDecoder->pIn < pDecoder->pEnd) && (pDecoder->pIn->nType & JSON_TK_COMMA) ){
				pDecoder->pIn++;
			}
			if( pDecoder->pIn >= pDecoder->pEnd || (pDecoder->pIn->nType & JSON_TK_CSB) /*']'*/ ){
				if( pDecoder->pIn < pDecoder->pEnd ){
					pDecoder->pIn++; /* Jump the trailing ']' */
				}
				break;
			}
			/* Recurse and decode the entry */
			pDecoder->rec_count++;
			rc = VmJsonDecode(pDecoder, 0);
			pDecoder->rec_count--;
			if( rc == SXERR_ABORT ){
				/* Abort processing immediately */
				return SXERR_ABORT;
			}
			/*The cursor is automatically advanced by the VmJsonDecode() function */
			if( (pDecoder->pIn < pDecoder->pEnd) &&
				((pDecoder->pIn->nType & (JSON_TK_CSB/*']'*/|JSON_TK_COMMA/*','*/))==0) ){
					/* Unexpected token, abort immediatley */
					*pDecoder->pErr = SXERR_SYNTAX;
					return SXERR_ABORT;
			}
		}
		/* Restore the old consumer */
		pDecoder->xConsumer = xOld;
		pDecoder->pUserData = pOld;
		/* Invoke the old consumer on the decoded array */
		xOld(pDecoder->pCtx, pArrayKey, pWorker, pOld);
	}else if( pDecoder->pIn->nType & JSON_TK_OCB /*'{' */) {
		ProcJSONConsumer xOld;
		jx9_value *pKey;
		void *pOld;
		/* Object representation*/
		pDecoder->pIn++;
		/* Create a working array */
		pWorker = jx9_context_new_array(pDecoder->pCtx);
		pKey = jx9_context_new_scalar(pDecoder->pCtx);
		if( pWorker == 0 || pKey == 0){
			jx9_context_throw_error(pDecoder->pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
			/* Abort the decoding operation immediately */
			return SXERR_ABORT;
		}
		/* Save the old consumer */
		xOld = pDecoder->xConsumer;
		pOld = pDecoder->pUserData;
		/* Set the new consumer */
		pDecoder->xConsumer = VmJsonArrayDecoder;
		pDecoder->pUserData = pWorker;
		/* Decode the object */
		for(;;){
			/* Jump trailing comma. Note that the standard JX9 engine will not let you
			 * do this.
			 */
			while( (pDecoder->pIn < pDecoder->pEnd) && (pDecoder->pIn->nType & JSON_TK_COMMA) ){
				pDecoder->pIn++;
			}
			if( pDecoder->pIn >= pDecoder->pEnd || (pDecoder->pIn->nType & JSON_TK_CCB) /*'}'*/ ){
				if( pDecoder->pIn < pDecoder->pEnd ){
					pDecoder->pIn++; /* Jump the trailing ']' */
				}
				break;
			}
			if( (pDecoder->pIn->nType & (JSON_TK_ID|JSON_TK_STR)) == 0 || &pDecoder->pIn[1] >= pDecoder->pEnd
				|| (pDecoder->pIn[1].nType & JSON_TK_COLON) == 0){
					/* Syntax error, return immediately */
					*pDecoder->pErr = SXERR_SYNTAX;
					return SXERR_ABORT;
			}
			if( pDecoder->pIn->nType & JSON_TK_ID ){
				SyString *pStr = &pDecoder->pIn->sData;
				jx9_value_string(pKey, pStr->zString, (int)pStr->nByte);
			}else{
				/* Dequote the key */
				VmJsonDequoteString(&pDecoder->pIn->sData, pKey);
			}
			/* Jump the key and the colon */
			pDecoder->pIn += 2; 
			/* Recurse and decode the value */
			pDecoder->rec_count++;
			rc = VmJsonDecode(pDecoder, pKey);
			pDecoder->rec_count--;
			if( rc == SXERR_ABORT ){
				/* Abort processing immediately */
				return SXERR_ABORT;
			}
			/* Reset the internal buffer of the key */
			jx9_value_reset_string_cursor(pKey);
			/*The cursor is automatically advanced by the VmJsonDecode() function */
		}
		/* Restore the old consumer */
		pDecoder->xConsumer = xOld;
		pDecoder->pUserData = pOld;
		/* Invoke the old consumer on the decoded object*/
		xOld(pDecoder->pCtx, pArrayKey, pWorker, pOld);
		/* Release the key */
		jx9_context_release_value(pDecoder->pCtx, pKey);
	}else{
		/* Unexpected token */
		return SXERR_ABORT; /* Abort immediately */
	}
	/* Release the worker variable */
	jx9_context_release_value(pDecoder->pCtx, pWorker);
	return SXRET_OK;
}
/*
 * The following JSON decoder callback is invoked each time
 * a JSON array representation [i.e: [15, "hello", FALSE] ]
 * is being decoded.
 */
static int VmJsonArrayDecoder(jx9_context *pCtx, jx9_value *pKey, jx9_value *pWorker, void *pUserData)
{
	jx9_value *pArray = (jx9_value *)pUserData;
	/* Insert the entry */
	jx9_array_add_elem(pArray, pKey, pWorker); /* Will make it's own copy */
	SXUNUSED(pCtx); /* cc warning */
	/* All done */
	return SXRET_OK;
}
/*
 * Standard JSON decoder callback.
 */
static int VmJsonDefaultDecoder(jx9_context *pCtx, jx9_value *pKey, jx9_value *pWorker, void *pUserData)
{
	/* Return the value directly */
	jx9_result_value(pCtx, pWorker); /* Will make it's own copy */
	SXUNUSED(pKey); /* cc warning */
	SXUNUSED(pUserData);
	/* All done */
	return SXRET_OK;
}
/*
 * Exported JSON decoding interface
 */
JX9_PRIVATE int jx9JsonDecode(jx9_context *pCtx,const char *zJSON,int nByte)
{
	jx9_vm *pVm = pCtx->pVm;
	json_decoder sDecoder;
	SySet sToken;
	SyLex sLex;
	sxi32 rc;
	/* Tokenize the input */
	SySetInit(&sToken, &pVm->sAllocator, sizeof(SyToken));
	rc = SXRET_OK;
	SyLexInit(&sLex, &sToken, VmJsonTokenize, &rc);
	SyLexTokenizeInput(&sLex,zJSON,(sxu32)nByte, 0, 0, 0);
	if( rc != SXRET_OK ){
		/* Something goes wrong while tokenizing input. [i.e: Unexpected token] */
		SyLexRelease(&sLex);
		SySetRelease(&sToken);
		/* return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Fill the decoder */
	sDecoder.pCtx = pCtx;
	sDecoder.pErr = &rc;
	sDecoder.pIn = (SyToken *)SySetBasePtr(&sToken);
	sDecoder.pEnd = &sDecoder.pIn[SySetUsed(&sToken)];
	sDecoder.iFlags = 0;
	sDecoder.rec_count = 0;
	/* Set a default consumer */
	sDecoder.xConsumer = VmJsonDefaultDecoder;
	sDecoder.pUserData = 0;
	/* Decode the raw JSON input */
	rc = VmJsonDecode(&sDecoder, 0);
	if( rc == SXERR_ABORT ){
		/* 
		 * Something goes wrong while decoding JSON input.Return NULL.
		 */
		jx9_result_null(pCtx);
	}
	/* Clean-up the mess left behind */
	SyLexRelease(&sLex);
	SySetRelease(&sToken);
	/* All done */
	return JX9_OK;
}
