/*
 * Compile this file together with the UnQLite database engine source code
 * to generate the executable. For example: 
 *  gcc -W -Wall -O6 unqlite_kv_intro.c unqlite.c -o unqlite_kv
*/
/*
 * This simple program is a quick introduction on how to embed and start
 * experimenting with UnQLite without having to do a lot of tedious
 * reading and configuration.
 *
 * Introduction to the Key/Value Store Interfaces:
 *
 * UnQLite is a standard key/value store similar to BerkeleyDB, Tokyo Cabinet, LevelDB, etc.
 * But, with a rich feature set including support for transactions (ACID), concurrent reader, etc.
 * Under the KV store, both keys and values are treated as simple arrays of bytes, so content
 * can be anything from ASCII strings, binary blob and even disk files.
 * The KV store layer is presented to host applications via a set of interfaces, these includes:
 * unqlite_kv_store(), unqlite_kv_append(), unqlite_kv_fetch_callback(), unqlite_kv_append_fmt(),
 * unqlite_kv_delete(), unqlite_kv_fetch(), etc.
 *
 * For an introduction to the UnQLite C/C++ interface, please refer to:
 *        http://unqlite.org/api_intro.html
 * For the full C/C++ API reference guide, please refer to:
 *        http://unqlite.org/c_api.html
 * UnQLite in 5 Minutes or Less:
 *        http://unqlite.org/intro.html
 * The Architecture of the UnQLite Database Engine:
 *        http://unqlite.org/arch.html
 * For an introduction to the UnQLite cursor interface, please refer to:
 *        http://unqlite.org/c_api/unqlite_kv_cursor.html
 * For an introduction to Jx9 which is the scripting language which power
 * the Document-Store interface to UnQLite, please refer to:
 *        http://unqlite.org/jx9.html
 */
/* $SymiscID: unqlite_kv_intro.c v1.0 FreeBSD 2013-05-14 10:17 stable <chm@symisc.net> $ */
/* 
 * Make sure you have the latest release of UnQLite from:
 *  http://unqlite.org/downloads.html
 */
#include <stdio.h>  /* puts() */
#include <stdlib.h> /* exit() */
/* Make sure this header file is available.*/
#include "unqlite.h"
/*
 * Banner.
 */
static const char zBanner[] = {
	"============================================================\n"
	"UnQLite Key/Value Store Intro                              \n"
	"                                         http://unqlite.org/\n"
	"============================================================\n"
};
/*
 * Extract the database error log and exit.
 */
static void Fatal(unqlite *pDb,const char *zMsg)
{
	if( pDb ){
		const char *zErr;
		int iLen = 0; /* Stupid cc warning */

		/* Extract the database error log */
		unqlite_config(pDb,UNQLITE_CONFIG_ERR_LOG,&zErr,&iLen);
		if( iLen > 0 ){
			/* Output the DB error log */
			puts(zErr); /* Always null terminated */
		}
	}else{
		if( zMsg ){
			puts(zMsg);
		}
	}
	/* Manually shutdown the library */
	unqlite_lib_shutdown();
	/* Exit immediately */
	exit(0);
}
/* Forward declaration: Data consumer callback */
static int DataConsumerCallback(const void *pData,unsigned int nDatalen,void *pUserData /* Unused */);

int main(int argc,char *argv[])
{
	unqlite *pDb;               /* Database handle */
	unqlite_kv_cursor *pCur;    /* Cursor handle */
	int i,rc;

	puts(zBanner);

	/* Open our database */
	rc = unqlite_open(&pDb,argc > 1 ? argv[1] /* On-disk DB */ : ":mem:" /* In-mem DB */,UNQLITE_OPEN_CREATE);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Out of memory");
	}
	
	/* Store some records */
	rc = unqlite_kv_store(pDb,"test",-1,"Hello World",11); /* test => 'Hello World' */
	if( rc != UNQLITE_OK ){
		/* Insertion fail, extract database error log and exit */
		Fatal(pDb,0);
	}
	/* A small formatted string */
	rc = unqlite_kv_store_fmt(pDb,"date",-1,"dummy date: %d:%d:%d",2013,06,07); /* Dummy date */
	if( rc != UNQLITE_OK ){
		/* Insertion fail, extract database error log and exit */
		Fatal(pDb,0);
	}
	
	/* Switch to the append interface */
	rc = unqlite_kv_append(pDb,"msg",-1,"Hello, ",7); //msg => 'Hello, '
	if( rc == UNQLITE_OK ){
		/* The second chunk */
		rc = unqlite_kv_append(pDb,"msg",-1,"dummy time is: ",17); /* msg => 'Hello, Current time is: '*/
		if( rc == UNQLITE_OK ){
			/* The last formatted chunk */
			rc = unqlite_kv_append_fmt(pDb,"msg",-1,"%d:%d:%d",10,16,53); /* msg => 'Hello, Current time is: 10:16:53' */
		}
	}
	/* Store 20 random records.*/
	for(i = 0 ; i < 20 ; ++i ){
		char zKey[12]; /* Random generated key */
		char zData[34]; /* Dummy data */
		
		/* Generate the random key */
		unqlite_util_random_string(pDb,zKey,sizeof(zKey));
		
		/* Perform the insertion */
		rc = unqlite_kv_store(pDb,zKey,sizeof(zKey),zData,sizeof(zData));
		if( rc != UNQLITE_OK ){
			break;
		}
	}
	if( rc != UNQLITE_OK ){
		/* Insertion fail, rollback the transaction  */
		rc = unqlite_rollback(pDb);
		if( rc != UNQLITE_OK ){
			/* Extract database error log and exit */
			Fatal(pDb,0);
		}
	}

	/* Delete a record */
	unqlite_kv_delete(pDb,"test",-1);

	puts("Done...Starting the iteration process");

	/* Allocate a new cursor instance */
	rc = unqlite_kv_cursor_init(pDb,&pCur);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Out of memory");
	}
	/* Point to the first record */
	unqlite_kv_cursor_first_entry(pCur);
	
		
	/* Iterate over the entries */
	while( unqlite_kv_cursor_valid_entry(pCur) ){
		int nKeyLen;
		/*unqlite_int64 nDataLen;*/
		
		/* Consume the key */
		unqlite_kv_cursor_key(pCur,0,&nKeyLen); /* Extract key length */
		printf("\nKey ==> %u\n\t",nKeyLen);
		unqlite_kv_cursor_key_callback(pCur,DataConsumerCallback,0);
			
		/* Consume the data */
		
		/*
		unqlite_kv_cursor_data(pCur,0,&nDataLen);
		printf("\nData ==> %lld\n\t",nDataLen);
		unqlite_kv_cursor_data_callback(pCur,DataConsumerCallback,0);
		*/

		/* Point to the next entry */
		unqlite_kv_cursor_next_entry(pCur);

	}
	/* Finally, Release our cursor */
	unqlite_kv_cursor_release(pDb,pCur);
	
	/* Auto-commit the transaction and close our database */
	unqlite_close(pDb);
	return 0;
}

#ifdef __WINNT__
#include <Windows.h>
#else
/* Assume UNIX */
#include <unistd.h>
#endif
/*
 * The following define is used by the UNIX build process and have
 * no particular meaning on windows.
 */
#ifndef STDOUT_FILENO
#define STDOUT_FILENO	1
#endif
/*
 * Data consumer callback [unqlite_kv_fetch_callback(), unqlite_kv_cursor_key_callback(), etc.).
 * 
 * Rather than allocating a static or dynamic buffer (Inefficient scenario for large data).
 * The caller simply need to supply a consumer callback which is responsible of consuming
 * the record data perhaps redirecting it (i.e. Record data) to its standard output (STDOUT),
 * disk file, connected peer and so forth.
 * Depending on how large the extracted data, the callback may be invoked more than once.
 */
static int DataConsumerCallback(const void *pData,unsigned int nDatalen,void *pUserData /* Unused */)
{
#ifdef __WINNT__
	BOOL rc;
	rc = WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),pData,(DWORD)nDatalen,0,0);
	if( !rc ){
		/* Abort processing */
		return UNQLITE_ABORT;
	}
#else
	ssize_t nWr;
	nWr = write(STDOUT_FILENO,pData,nDatalen);
	if( nWr < 0 ){
		/* Abort processing */
		return UNQLITE_ABORT;
	}
#endif /* __WINT__ */
	
	/* All done, data was redirected to STDOUT */
	return UNQLITE_OK;
}
