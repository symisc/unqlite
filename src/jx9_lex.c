/*
 * Symisc JX9: A Highly Efficient Embeddable Scripting Engine Based on JSON.
 * Copyright (C) 2012-2026, Symisc Systems https://jx9.symisc.net/
 * Version 1.7.2
 * For information on licensing, redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES
 * please contact Symisc Systems via:
 *       legal@symisc.net
 *       licensing@symisc.net
 *       contact@symisc.net
 * or visit:
 *      https://jx9.symisc.net/
 */
 /* $SymiscID: lex.c v1.0 FreeBSD 2012-12-09 00:19 stable <chm@symisc.net> $ */
#ifndef JX9_AMALGAMATION
#include "jx9Int.h"
#endif
/* This file implements a thread-safe and full reentrant lexical analyzer for the Jx9 programming language */
/* Forward declarations */
static sxu32 keywordCode(const char *z,int n);
static sxi32 LexExtractNowdoc(SyStream *pStream,SyToken *pToken);
/*
 * Tokenize a raw jx9 input.
 * Get a single low-level token from the input file. Update the stream pointer so that
 * it points to the first character beyond the extracted token.
 */
static sxi32 jx9TokenizeInput(SyStream *pStream,SyToken *pToken,void *pUserData,void *pCtxData)
{
	SyString *pStr;
	sxi32 rc;
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
		 * https://xpp.symisc.net/
		 */
		const unsigned char *zIn;
		sxu32 nKeyword;
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
		nKeyword = keywordCode(pStr->zString, (int)pStr->nByte);
		if( nKeyword != JX9_TK_ID ){
			/* We are dealing with a keyword [i.e: if, function, CREATE, ...], save the keyword ID */
			pToken->nType = JX9_TK_KEYWORD;
			pToken->pUserData = SX_INT_TO_PTR(nKeyword);
		}else{
			/* A simple identifier */
			pToken->nType = JX9_TK_ID;
		}
	}else{
		sxi32 c;
		/* Non-alpha stream */
		if( pStream->zText[0] == '#' || 
			( pStream->zText[0] == '/' &&  &pStream->zText[1] < pStream->zEnd && pStream->zText[1] == '/') ){
				pStream->zText++;
				/* Inline comments */
				while( pStream->zText < pStream->zEnd && pStream->zText[0] != '\n' ){
					pStream->zText++;
				}
				/* Tell the upper-layer to ignore this token */ 
				return SXERR_CONTINUE;
		}else if( pStream->zText[0] == '/' && &pStream->zText[1] < pStream->zEnd && pStream->zText[1] == '*' ){
			pStream->zText += 2;
			/* Block comment */
			while( pStream->zText < pStream->zEnd ){
				if( pStream->zText[0] == '*' ){
					if( &pStream->zText[1] >= pStream->zEnd || pStream->zText[1] == '/'  ){
						break;
					}
				}
				if( pStream->zText[0] == '\n' ){
					pStream->nLine++;
				}
				pStream->zText++;
			}
			pStream->zText += 2;
			/* Tell the upper-layer to ignore this token */
			return SXERR_CONTINUE;
		}else if( SyisDigit(pStream->zText[0]) ){
			pStream->zText++;
			/* Decimal digit stream */
			while( pStream->zText < pStream->zEnd && pStream->zText[0] < 0xc0 && SyisDigit(pStream->zText[0]) ){
				pStream->zText++;
			}
			/* Mark the token as integer until we encounter a real number */
			pToken->nType = JX9_TK_INTEGER;
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
								if( (c =='+' || c=='-') && &pStream->zText[1] < pStream->zEnd  &&
									pStream->zText[1] < 0xc0 && SyisDigit(pStream->zText[1]) ){
										pStream->zText++;
								}
								while( pStream->zText < pStream->zEnd && pStream->zText[0] < 0xc0 && SyisDigit(pStream->zText[0]) ){
									pStream->zText++;
								}
							}
						}
					}
					pToken->nType = JX9_TK_REAL;
				}else if( c=='e' || c=='E' ){
					SXUNUSED(pUserData); /* Prevent compiler warning */
					SXUNUSED(pCtxData);
					pStream->zText++;
					if( pStream->zText < pStream->zEnd ){
						c = pStream->zText[0];
						if( (c =='+' || c=='-') && &pStream->zText[1] < pStream->zEnd  &&
							pStream->zText[1] < 0xc0 && SyisDigit(pStream->zText[1]) ){
								pStream->zText++;
						}
						while( pStream->zText < pStream->zEnd && pStream->zText[0] < 0xc0 && SyisDigit(pStream->zText[0]) ){
							pStream->zText++;
						}
					}
					pToken->nType = JX9_TK_REAL;
				}else if( c == 'x' || c == 'X' ){
					/* Hex digit stream */
					pStream->zText++;
					while( pStream->zText < pStream->zEnd && pStream->zText[0] < 0xc0 && SyisHex(pStream->zText[0]) ){
						pStream->zText++;
					}
				}else if(c  == 'b' || c == 'B' ){
					/* Binary digit stream */
					pStream->zText++;
					while( pStream->zText < pStream->zEnd && (pStream->zText[0] == '0' || pStream->zText[0] == '1') ){
						pStream->zText++;
					}
				}
			}
			/* Record token length */
			pStr->nByte = (sxu32)((const char *)pStream->zText-pStr->zString);
			return SXRET_OK;
		}
		c = pStream->zText[0];
		pStream->zText++; /* Advance the stream cursor */
		/* Assume we are dealing with an operator*/
		pToken->nType = JX9_TK_OP;
		switch(c){
		case '$': pToken->nType = JX9_TK_DOLLAR; break;
		case '{': pToken->nType = JX9_TK_OCB;   break; 
		case '}': pToken->nType = JX9_TK_CCB;    break;
		case '(': pToken->nType = JX9_TK_LPAREN; break; 
		case '[': pToken->nType |= JX9_TK_OSB;   break; /* Bitwise operation here, since the square bracket token '[' 
														 * is a potential operator [i.e: subscripting] */
		case ']': pToken->nType = JX9_TK_CSB;    break;
		case ')': {
			SySet *pTokSet = pStream->pSet;
			/* Assemble type cast operators [i.e: (int), (float), (bool)...] */ 
			if( pTokSet->nUsed >= 2 ){
				SyToken *pTmp;
				/* Peek the last recongnized token */
				pTmp = (SyToken *)SySetPeek(pTokSet);
				if( pTmp->nType & JX9_TK_KEYWORD ){
					sxi32 nID = SX_PTR_TO_INT(pTmp->pUserData);
					if( (sxu32)nID & (JX9_TKWRD_INT|JX9_TKWRD_FLOAT|JX9_TKWRD_STRING|JX9_TKWRD_BOOL) ){
						pTmp = (SyToken *)SySetAt(pTokSet, pTokSet->nUsed - 2);
						if( pTmp->nType & JX9_TK_LPAREN ){
							/* Merge the three tokens '(' 'TYPE' ')' into a single one */
							const char * zTypeCast = "(int)";
							if( nID & JX9_TKWRD_FLOAT ){
								zTypeCast = "(float)";
							}else if( nID & JX9_TKWRD_BOOL ){
								zTypeCast = "(bool)";
							}else if( nID & JX9_TKWRD_STRING ){
								zTypeCast = "(string)";
							}
							/* Reflect the change */
							pToken->nType = JX9_TK_OP;
							SyStringInitFromBuf(&pToken->sData, zTypeCast, SyStrlen(zTypeCast));
							/* Save the instance associated with the type cast operator */
							pToken->pUserData = (void *)jx9ExprExtractOperator(&pToken->sData, 0);
							/* Remove the two previous tokens */
							pTokSet->nUsed -= 2;
							return SXRET_OK;
						}
					}
				}
			}
			pToken->nType = JX9_TK_RPAREN;
			break;
				  }
		case '\'':{
			/* Single quoted string */
			pStr->zString++;
			while( pStream->zText < pStream->zEnd ){
				if( pStream->zText[0] == '\''  ){
					if( pStream->zText[-1] != '\\' ){
						break;
					}else{
						const unsigned char *zPtr = &pStream->zText[-2];
						sxi32 i = 1;
						while( zPtr > pStream->zInput && zPtr[0] == '\\' ){
							zPtr--;
							i++;
						}
						if((i&1)==0){
							break;
						}
					}
				}
				if( pStream->zText[0] == '\n' ){
					pStream->nLine++;
				}
				pStream->zText++;
			}
			/* Record token length and type */
			pStr->nByte = (sxu32)((const char *)pStream->zText-pStr->zString);
			pToken->nType = JX9_TK_SSTR;
			/* Jump the trailing single quote */
			pStream->zText++;
			return SXRET_OK;
				  }
		case '"':{
			sxi32 iNest;
			/* Double quoted string */
			pStr->zString++;
			while( pStream->zText < pStream->zEnd ){
				if( pStream->zText[0] == '{' && &pStream->zText[1] < pStream->zEnd && pStream->zText[1] == '$'){
					iNest = 1;
					pStream->zText++;
					/* TICKET 1433-40: Hnadle braces'{}' in double quoted string where everything is allowed */
					while(pStream->zText < pStream->zEnd ){
						if( pStream->zText[0] == '{' ){
							iNest++;
						}else if (pStream->zText[0] == '}' ){
							iNest--;
							if( iNest <= 0 ){
								pStream->zText++;
								break;
							}
						}else if( pStream->zText[0] == '\n' ){
							pStream->nLine++;
						}
						pStream->zText++;
					}
					if( pStream->zText >= pStream->zEnd ){
						break;
					}
				}
				if( pStream->zText[0] == '"' ){
					if( pStream->zText[-1] != '\\' ){
						break;
					}else{
						const unsigned char *zPtr = &pStream->zText[-2];
						sxi32 i = 1;
						while( zPtr > pStream->zInput && zPtr[0] == '\\' ){
							zPtr--;
							i++;
						}
						if((i&1)==0){
							break;
						}
					}
				}
				if( pStream->zText[0] == '\n' ){
					pStream->nLine++;
				}
				pStream->zText++;
			}
			/* Record token length and type */
			pStr->nByte = (sxu32)((const char *)pStream->zText-pStr->zString);
			pToken->nType = JX9_TK_DSTR;
			/* Jump the trailing quote */
			pStream->zText++;
			return SXRET_OK;
				  }
		case ':':
			pToken->nType = JX9_TK_COLON; /* Single colon */
			break;
		case ',': pToken->nType |= JX9_TK_COMMA;  break; /* Comma is also an operator */
		case ';': pToken->nType = JX9_TK_SEMI;   break;
			/* Handle combined operators [i.e: +=, ===, !=== ...] */
		case '=':
			pToken->nType |= JX9_TK_EQUAL;
			if( pStream->zText < pStream->zEnd ){
				if( pStream->zText[0] == '=' ){
					pToken->nType &= ~JX9_TK_EQUAL;
					/* Current operator: == */
					pStream->zText++;
					if( pStream->zText < pStream->zEnd && pStream->zText[0] == '=' ){
						/* Current operator: === */
						pStream->zText++;
					}
				}
			}
			break;
		case '!':
			if( pStream->zText < pStream->zEnd && pStream->zText[0] == '=' ){
				/* Current operator: != */
				pStream->zText++;
				if( pStream->zText < pStream->zEnd && pStream->zText[0] == '=' ){
					/* Current operator: !== */
					pStream->zText++;
				}
			}
			break;
		case '&':
			pToken->nType |= JX9_TK_AMPER;
			if( pStream->zText < pStream->zEnd ){
				if( pStream->zText[0] == '&' ){
					pToken->nType &= ~JX9_TK_AMPER;
					/* Current operator: && */
					pStream->zText++;
				}else if( pStream->zText[0] == '=' ){
					pToken->nType &= ~JX9_TK_AMPER;
					/* Current operator: &= */
					pStream->zText++;
				}
			}
		case '.':
			if( pStream->zText < pStream->zEnd && (pStream->zText[0] == '.' || pStream->zText[0] == '=') ){
				/* Concatenation operator: '..' or '.='  */
				pStream->zText++;
			}
			break;
		case '|':
			if( pStream->zText < pStream->zEnd ){
				if( pStream->zText[0] == '|' ){
					/* Current operator: || */
					pStream->zText++;
				}else if( pStream->zText[0] == '=' ){
					/* Current operator: |= */
					pStream->zText++;
				}
			}
			break;
		case '+':
			if( pStream->zText < pStream->zEnd ){
				if( pStream->zText[0] == '+' ){
					/* Current operator: ++ */
					pStream->zText++;
				}else if( pStream->zText[0] == '=' ){
					/* Current operator: += */
					pStream->zText++;
				}
			}
			break;
		case '-':
			if( pStream->zText < pStream->zEnd ){
				if( pStream->zText[0] == '-' ){
					/* Current operator: -- */
					pStream->zText++;
				}else if( pStream->zText[0] == '=' ){
					/* Current operator: -= */
					pStream->zText++;
				}else if( pStream->zText[0] == '>' ){
					/* Current operator: -> */
					pStream->zText++;
				}
			}
			break;
		case '*':
			if( pStream->zText < pStream->zEnd && pStream->zText[0] == '=' ){
				/* Current operator: *= */
				pStream->zText++;
			}
			break;
		case '/':
			if( pStream->zText < pStream->zEnd && pStream->zText[0] == '=' ){
				/* Current operator: /= */
				pStream->zText++;
			}
			break;
		case '%':
			if( pStream->zText < pStream->zEnd && pStream->zText[0] == '=' ){
				/* Current operator: %= */
				pStream->zText++;
			}
			break;
		case '^':
			if( pStream->zText < pStream->zEnd && pStream->zText[0] == '=' ){
				/* Current operator: ^= */
				pStream->zText++;
			}
			break;
		case '<':
			if( pStream->zText < pStream->zEnd ){
				if( pStream->zText[0] == '<' ){
					/* Current operator: << */
					pStream->zText++;
					if( pStream->zText < pStream->zEnd ){
						if( pStream->zText[0] == '=' ){
							/* Current operator: <<= */
							pStream->zText++;
						}else if( pStream->zText[0] == '<' ){
							/* Current Token: <<<  */
							pStream->zText++;
							/* This may be the beginning of a Heredoc/Nowdoc string, try to delimit it */
							rc = LexExtractNowdoc(&(*pStream), &(*pToken));
							if( rc == SXRET_OK ){
								/* Here/Now doc successfuly extracted */
								return SXRET_OK;
							}
						}
					}
				}else if( pStream->zText[0] == '>' ){
					/* Current operator: <> */
					pStream->zText++;
				}else if( pStream->zText[0] == '=' ){
					/* Current operator: <= */
					pStream->zText++;
				}
			}
			break;
		case '>':
			if( pStream->zText < pStream->zEnd ){
				if( pStream->zText[0] == '>' ){
					/* Current operator: >> */
					pStream->zText++;
					if( pStream->zText < pStream->zEnd && pStream->zText[0] == '=' ){
						/* Current operator: >>= */
						pStream->zText++;
					}
				}else if( pStream->zText[0] == '=' ){
					/* Current operator: >= */
					pStream->zText++;
				}
			}
			break;
		default:
			break;
		}
		if( pStr->nByte <= 0 ){
			/* Record token length */
			pStr->nByte = (sxu32)((const char *)pStream->zText-pStr->zString);
		}
		if( pToken->nType & JX9_TK_OP ){
			const jx9_expr_op *pOp;
			/* Check if the extracted token is an operator */
			pOp = jx9ExprExtractOperator(pStr, (SyToken *)SySetPeek(pStream->pSet));
			if( pOp == 0 ){
				/* Not an operator */
				pToken->nType &= ~JX9_TK_OP;
				if( pToken->nType <= 0 ){
					pToken->nType = JX9_TK_OTHER;
				}
			}else{
				/* Save the instance associated with this operator for later processing */
				pToken->pUserData = (void *)pOp;
			}
		}
	}
	/* Tell the upper-layer to save the extracted token for later processing */
	return SXRET_OK;
}
/***** This file contains automatically generated code ******
**
** The code in this file has been automatically generated by
**
**     $Header: /sqlite/sqlite/tool/mkkeywordhash.c,v 1.38 2011/12/21 01:00:46 <chm@symisc.net> $
**
** The code in this file implements a function that determines whether
** or not a given identifier is really a JX9 keyword.  The same thing
** might be implemented more directly using a hand-written hash table.
** But by using this automatically generated code, the size of the code
** is substantially reduced.  This is important for embedded applications
** on platforms with limited memory.
*/
/* Hash score: 35 */
static sxu32 keywordCode(const char *z, int n)
{
  /* zText[] encodes 188 bytes of keywords in 128 bytes */
  /*   printegereturnconstaticaselseifloatincludefaultDIEXITcontinue      */
  /*   diewhileASPRINTbooleanbreakforeachfunctionimportstringswitch       */
  /*   uplink                                                             */
  static const char zText[127] = {
    'p','r','i','n','t','e','g','e','r','e','t','u','r','n','c','o','n','s',
    't','a','t','i','c','a','s','e','l','s','e','i','f','l','o','a','t','i',
    'n','c','l','u','d','e','f','a','u','l','t','D','I','E','X','I','T','c',
    'o','n','t','i','n','u','e','d','i','e','w','h','i','l','e','A','S','P',
    'R','I','N','T','b','o','o','l','e','a','n','b','r','e','a','k','f','o',
    'r','e','a','c','h','f','u','n','c','t','i','o','n','i','m','p','o','r',
    't','s','t','r','i','n','g','s','w','i','t','c','h','u','p','l','i','n',
    'k',
  };
  static const unsigned char aHash[59] = {
       0,   0,   0,   0,  15,   0,  30,   0,   0,   2,  19,  18,   0,
       0,  10,   3,  12,   0,  28,  29,  23,   0,  13,  22,   0,   0,
      14,  24,  25,  31,  11,   0,   0,   0,   0,   1,   5,   0,   0,
      20,   0,  27,   9,   0,   0,   0,   8,   0,   0,  26,   6,   0,
       0,  17,   0,   0,   0,   0,   0,
  };
  static const unsigned char aNext[31] = {
       0,   0,   0,   0,   0,   0,   0,   0,   0,   4,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,   0,  16,   0,  21,   7,
       0,   0,   0,   0,   0,
  };
  static const unsigned char aLen[31] = {
       5,   7,   3,   6,   5,   6,   4,   2,   6,   4,   2,   5,   7,
       7,   3,   4,   8,   3,   5,   2,   5,   4,   7,   5,   3,   7,
       8,   6,   6,   6,   6,
  };
  static const sxu16 aOffset[31] = {
       0,   2,   2,   8,  14,  17,  22,  23,  25,  25,  29,  30,  35,
      40,  47,  49,  53,  61,  64,  69,  71,  76,  76,  83,  88,  88,
      95, 103, 109, 115, 121,
  };
  static const sxu32 aCode[31] = {
    JX9_TKWRD_PRINT,   JX9_TKWRD_INT,      JX9_TKWRD_INT,     JX9_TKWRD_RETURN,   JX9_TKWRD_CONST, 
    JX9_TKWRD_STATIC,  JX9_TKWRD_CASE,     JX9_TKWRD_AS,      JX9_TKWRD_ELIF,     JX9_TKWRD_ELSE,
    JX9_TKWRD_IF,      JX9_TKWRD_FLOAT,    JX9_TKWRD_INCLUDE, JX9_TKWRD_DEFAULT,  JX9_TKWRD_DIE, 
    JX9_TKWRD_EXIT,    JX9_TKWRD_CONTINUE, JX9_TKWRD_DIE,     JX9_TKWRD_WHILE,    JX9_TKWRD_AS,  
    JX9_TKWRD_PRINT,   JX9_TKWRD_BOOL,     JX9_TKWRD_BOOL,    JX9_TKWRD_BREAK,    JX9_TKWRD_FOR, 
    JX9_TKWRD_FOREACH, JX9_TKWRD_FUNCTION, JX9_TKWRD_IMPORT,  JX9_TKWRD_STRING,  JX9_TKWRD_SWITCH,  
    JX9_TKWRD_UPLINK,  
  };
  int h, i;
  if( n<2 ) return JX9_TK_ID;
  h = (((int)z[0]*4) ^ ((int)z[n-1]*3) ^ n) % 59;
  for(i=((int)aHash[h])-1; i>=0; i=((int)aNext[i])-1){
    if( (int)aLen[i]==n && SyMemcmp(&zText[aOffset[i]],z,n)==0 ){
       /* JX9_TKWRD_PRINT */
       /* JX9_TKWRD_INT */
       /* JX9_TKWRD_INT */
       /* JX9_TKWRD_RETURN */
       /* JX9_TKWRD_CONST */
       /* JX9_TKWRD_STATIC */
       /* JX9_TKWRD_CASE */
       /* JX9_TKWRD_AS */
       /* JX9_TKWRD_ELIF */
       /* JX9_TKWRD_ELSE */
       /* JX9_TKWRD_IF */
       /* JX9_TKWRD_FLOAT */
       /* JX9_TKWRD_INCLUDE */
       /* JX9_TKWRD_DEFAULT */
       /* JX9_TKWRD_DIE */
       /* JX9_TKWRD_EXIT */
       /* JX9_TKWRD_CONTINUE */
       /* JX9_TKWRD_DIE */
       /* JX9_TKWRD_WHILE */
       /* JX9_TKWRD_AS */
       /* JX9_TKWRD_PRINT */
       /* JX9_TKWRD_BOOL */
       /* JX9_TKWRD_BOOL */
       /* JX9_TKWRD_BREAK */
       /* JX9_TKWRD_FOR */
       /* JX9_TKWRD_FOREACH */
       /* JX9_TKWRD_FUNCTION */
       /* JX9_TKWRD_IMPORT */
       /* JX9_TKWRD_STRING */
       /* JX9_TKWRD_SWITCH */
       /* JX9_TKWRD_UPLINK */
      return aCode[i];
    }
  }
  return JX9_TK_ID;
}
/*
 * Extract a heredoc/nowdoc text from a raw JX9 input.
 * According to the JX9 language reference manual:
 *  A third way to delimit strings is the heredoc syntax: <<<. After this operator, an identifier
 *  is provided, then a newline. The string itself follows, and then the same identifier again
 *  to close the quotation.
 *  The closing identifier must begin in the first column of the line. Also, the identifier must 
 *  follow the same naming rules as any other label in JX9: it must contain only alphanumeric 
 *  characters and underscores, and must start with a non-digit character or underscore. 
 *  Heredoc text behaves just like a double-quoted string, without the double quotes.
 *  This means that quotes in a heredoc do not need to be escaped, but the escape codes listed
 *  above can still be used. Variables are expanded, but the same care must be taken when expressing
 *  complex variables inside a heredoc as with strings. 
 *  Nowdocs are to single-quoted strings what heredocs are to double-quoted strings.
 *  A nowdoc is specified similarly to a heredoc, but no parsing is done inside a nowdoc.
 *  The construct is ideal for embedding JX9 code or other large blocks of text without the need
 *  for escaping. It shares some features in common with the SGML <![CDATA[ ]]> construct, in that
 *  it declares a block of text which is not for parsing.
 *  A nowdoc is identified with the same <<< sequence used for heredocs, but the identifier which follows
 *  is enclosed in single quotes, e.g. <<<'EOT'. All the rules for heredoc identifiers also apply to nowdoc
 *  identifiers, especially those regarding the appearance of the closing identifier.
 */
static sxi32 LexExtractNowdoc(SyStream *pStream, SyToken *pToken)
{
	const unsigned char *zIn  = pStream->zText;
	const unsigned char *zEnd = pStream->zEnd;
	const unsigned char *zPtr;
	SyString sDelim;
	SyString sStr;
	/* Jump leading white spaces */
	while( zIn < zEnd && zIn[0] < 0xc0 && SyisSpace(zIn[0]) && zIn[0] != '\n' ){
		zIn++;
	}
	if( zIn >= zEnd ){
		/* A simple symbol, return immediately */
		return SXERR_CONTINUE;
	}
	if( zIn[0] == '\'' || zIn[0] == '"' ){
		zIn++;
	}
	if( zIn[0] < 0xc0 && !SyisAlphaNum(zIn[0]) && zIn[0] != '_' ){
		/* Invalid delimiter, return immediately */
		return SXERR_CONTINUE;
	}
	/* Isolate the identifier */
	sDelim.zString = (const char *)zIn;
	for(;;){
		zPtr = zIn;
		/* Skip alphanumeric stream */
		while( zPtr < zEnd && zPtr[0] < 0xc0 && (SyisAlphaNum(zPtr[0]) || zPtr[0] == '_') ){
			zPtr++;
		}
		if( zPtr < zEnd && zPtr[0] >= 0xc0 ){
			zPtr++;
			/* UTF-8 stream */
			while( zPtr < zEnd && ((zPtr[0] & 0xc0) == 0x80) ){
				zPtr++;
			}
		}
		if( zPtr == zIn ){
			/* Not an UTF-8 or alphanumeric stream */
			break;
		}
		/* Synchronize pointers */
		zIn = zPtr;
	}
	/* Get the identifier length */
	sDelim.nByte = (sxu32)((const char *)zIn-sDelim.zString);
	if( zIn[0] == '"' || zIn[0] == '\'' ){
		/* Jump the trailing single quote */
		zIn++;
	}
	/* Jump trailing white spaces */
	while( zIn < zEnd && zIn[0] < 0xc0 && SyisSpace(zIn[0]) && zIn[0] != '\n' ){
		zIn++;
	}
	if( sDelim.nByte <= 0 || zIn >= zEnd || zIn[0] != '\n' ){
		/* Invalid syntax */
		return SXERR_CONTINUE;
	}
	pStream->nLine++; /* Increment line counter */
	zIn++;
	/* Isolate the delimited string */
	sStr.zString = (const char *)zIn;
	/* Go and found the closing delimiter */
	for(;;){
		/* Synchronize with the next line */
		while( zIn < zEnd && zIn[0] != '\n' ){
			zIn++;
		}
		if( zIn >= zEnd ){
			/* End of the input reached, break immediately */
			pStream->zText = pStream->zEnd;
			break;
		}
		pStream->nLine++; /* Increment line counter */
		zIn++;
		if( (sxu32)(zEnd - zIn) >= sDelim.nByte && SyMemcmp((const void *)sDelim.zString, (const void *)zIn, sDelim.nByte) == 0 ){
			zPtr = &zIn[sDelim.nByte];
			while( zPtr < zEnd && zPtr[0] < 0xc0 && SyisSpace(zPtr[0]) && zPtr[0] != '\n' ){
				zPtr++;
			}
			if( zPtr >= zEnd ){
				/* End of input */
				pStream->zText = zPtr;
				break;
			}
			if( zPtr[0] == ';' ){
				const unsigned char *zCur = zPtr;
				zPtr++;
				while( zPtr < zEnd && zPtr[0] < 0xc0 && SyisSpace(zPtr[0]) && zPtr[0] != '\n' ){
					zPtr++;
				}
				if( zPtr >= zEnd || zPtr[0] == '\n' ){
					/* Closing delimiter found, break immediately */
					pStream->zText = zCur; /* Keep the semi-colon */
					break;
				}
			}else if( zPtr[0] == '\n' ){
				/* Closing delimiter found, break immediately */
				pStream->zText = zPtr; /* Synchronize with the stream cursor */
				break;
			}
			/* Synchronize pointers and continue searching */
			zIn = zPtr;
		}
	} /* For(;;) */
	/* Get the delimited string length */
	sStr.nByte = (sxu32)((const char *)zIn-sStr.zString);
	/* Record token type and length */
	pToken->nType = JX9_TK_NOWDOC;
	SyStringDupPtr(&pToken->sData, &sStr);
	/* Remove trailing white spaces */
	SyStringRightTrim(&pToken->sData);
	/* All done */
	return SXRET_OK;
}
/*
 * Tokenize a raw jx9 input.
 * This is the public tokenizer called by most code generator routines. 
 */
JX9_PRIVATE sxi32 jx9Tokenize(const char *zInput,sxu32 nLen,SySet *pOut)
{
	SyLex sLexer;
	sxi32 rc;
	/* Initialize the lexer */
	rc = SyLexInit(&sLexer, &(*pOut),jx9TokenizeInput,0);
	if( rc != SXRET_OK ){
		return rc;
	}
	/* Tokenize input */
	rc = SyLexTokenizeInput(&sLexer, zInput, nLen, 0, 0, 0);
	/* Release the lexer */
	SyLexRelease(&sLexer);
	/* Tokenization result */
	return rc;
}
