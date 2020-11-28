/*
 * Compile this file together with the UnQLite database engine source code
 * to generate the executable. For example: 
 *  gcc -W -Wall -O6 unqlite_csr_intro.c unqlite.c -o unqlite_csr
*/
/*
 * This simple program is a quick introduction on how to embed and start
 * experimenting with UnQLite without having to do a lot of tedious
 * reading and configuration.
 *
 * Using Database Cursors:
 *
 * Cursors provide a mechanism by which you can iterate over the records
 * in a database. Using cursors, you can seek, fetch, move, and delete
 * database records.
 *
 * Before playing with cursors, you must first allocate a new cursor
 * handle using unqlite_kv_cursor_init().
 * This is often the first UnQLite cursor API call that an application
 * makes and is a prerequisite in order to use cursors. When done, you must
 * call unqlite_kv_cursor_release() to release any allocated resource by
 * the cursor and thus to avoid memory leaks.
 *
 * To iterate over database records, from the first record to the last, simply
 * call unqlite_kv_cursor_first_entry() with successive call to unqlite_kv_cursor_next_entry()
 * until it return a value other than UNQLITE_OK (See example below).
 * Note that you can call unqlite_kv_cursor_valid_entry() to check if the cursor
 * is pointing to a valid record (This will return 1 when valid. 0 otherwise).
 *
 * You can also use cursors to search for records and start the iteration process
 * from there. To do that, simply call unqlite_kv_cursor_seek() with the target
 * record key and the seek direction (Last argument)... 
 *
 * For an introduction to the UnQLite cursor interface, please refer to:
 *        http://unqlite.org/c_api/unqlite_kv_cursor.html
 * For an introduction to the UnQLite C/C++ interface, please refer to:
 *        http://unqlite.org/api_intro.html
 * For the full C/C++ API reference guide, please refer to:
 *        http://unqlite.org/c_api.html
 * UnQLite in 5 Minutes or Less:
 *        http://unqlite.org/intro.html
 * The Architecture of the UnQLite Database Engine:
 *        http://unqlite.org/arch.html
 * For an introduction to Jx9 which is the scripting language which power
 * the Document-Store interface to UnQLite, please refer to:
 *        http://unqlite.org/jx9.html
 */
/* $SymiscID: unqlite_csr_intro.c v1.0 FreeBSD 2013-05-17 00:02 stable <chm@symisc.net> $ */
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
	"UnQLite Cursors Intro                                       \n"
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
/*
 * Maximum random records to be inserted in our database.
 */
#define MAX_RECORDS 20

int main(int argc,char *argv[])
{
	unqlite *pDb;               /* Database handle */
	unqlite_kv_cursor *pCur;    /* Cursor handle */
	char zKey[14];              /* Random generated key */
	char zData[32];             /* Dummy data */
	int i,rc;

	puts(zBanner);

	/* Open our database */
	rc = unqlite_open(&pDb,argc > 1 ? argv[1] /* On-disk DB */ : ":mem:" /* In-mem DB */,UNQLITE_OPEN_CREATE);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Out of memory");
	}
	
	printf("Starting insertions of %d random records...\n",MAX_RECORDS);
	
	/* Start the random insertions */
	for( i = 0 ; i < MAX_RECORDS; ++i ){
		
		/* Genearte the random key first */
		unqlite_util_random_string(pDb,zKey,sizeof(zKey));

		/* Perform the insertion */
		rc = unqlite_kv_store(pDb,zKey,sizeof(zKey),zData,sizeof(zData));
		if( rc != UNQLITE_OK ){
			/* Something goes wrong */
			break;
		}
	}
	if( rc != UNQLITE_OK ){
		/* Something goes wrong, extract the database error log and exit */
		Fatal(pDb,0);
	}
	puts("Done...Starting the iteration process");

	/* Allocate a new cursor instance */
	rc = unqlite_kv_cursor_init(pDb,&pCur);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Out of memory");
	}
	/* Point to the first record */
	unqlite_kv_cursor_first_entry(pCur);
	/* To point to the last record instead of the first, simply call [unqlite_kv_cursor_last_entry()] as follows */
	
	/* unqlite_kv_cursor_last_entry(pCur); */
		
	/* Iterate over the entries */
	while( unqlite_kv_cursor_valid_entry(pCur) ){
		int nKeyLen;
		/* unqlite_int64 nDataLen; */
		
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

		/*unqlite_kv_cursor_prev_entry(pCur); //If [unqlite_kv_cursor_last_entry(pCur)] instead of [unqlite_kv_cursor_first_entry(pCur)] */
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
