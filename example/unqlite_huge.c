/*
 * Compile this file together with the UnQLite database engine source code
 * to generate the executable. For example:
 *  gcc -W -Wall -O6 unqlite_huge_insert.c unqlite.c -o unqlite_huge
*/
/*
 * This simple program is a quick introduction on how to embed and start
 * experimenting with UnQLite without having to do a lot of tedious
 * reading and configuration.
 *
 * This program stores over 100000 random records (Dummy data of length 32 + random key of length 14)
 * in the given database. The random keys are obtained using
 * the powerful [unqlite_util_random_string()] interface.
 * Feel free to raise this number to 1 million or whatever value
 * you want and do your own benchmark.
 * Note that if you generate 1 million records, you'll end up
 * with a 560 MB database file with garbage data.
 * Only Key/Value store interfaces (unqlite_kv_store()) are used
 * in this example.
 *
 * Typical usage of this program:
 *
 *  ./unqlite_huge test.db
 *
 * To iterate (using the cursor interfaces) over the inserted records, use the '-i' command
 * as follows:
 *
 *  ./unqlite_huge test.db -i
 *
 * To start an in-memory database, invoke the program without arguments as follows:
 *
 *  ./unqlite_huge
 *
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
 /* $SymiscID: unqlite_huge_insert.c v1.0 Solaris 2013-05-15 20:17 stable <chm@symisc.net> $ */
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
	"UnQLite Huge Random Insertions                              \n"
	"                                         http://unqlite.org/\n"
	"============================================================\n"
};
/*
 * Extract the database error log and exit.
 */
static void Fatal(unqlite *pDb, const char *zMsg)
{
	if (pDb) {
		const char *zErr;
		int iLen = 0; /* Stupid cc warning */

		/* Extract the database error log */
		unqlite_config(pDb, UNQLITE_CONFIG_ERR_LOG, &zErr, &iLen);
		if (iLen > 0) {
			/* Output the DB error log */
			puts(zErr); /* Always null terminated */
		}
	}
	else {
		if (zMsg) {
			puts(zMsg);
		}
	}
	/* Manually shutdown the library */
	unqlite_lib_shutdown();
	/* Exit immediately */
	exit(0);
}
/* Forward declaration: Data consumer callback */
static int DataConsumerCallback(const void *pData, unsigned int nDatalen, void *pUserData /* Unused */);
/*
 * Maximum records to be inserted in our database.
 */
#define MAX_RECORDS 100000

int main(int argc, char *argv[])
{
	const char *zPath = ":mem:"; /* Assume an in-memory database */
	int db_iterate = 0;          /* TRUE to iterate over the inserted elements */
	unqlite *pDb;                /* Database handle */
	char zKey[14];               /* Random generated key */
	char zData[32];              /* Dummy data */
	int i, rc;

	/* Process arguments */
	for (i = 1; i < argc; ++i) {
		int c;
		if (argv[i][0] != '-') {
			/* Database file */
			zPath = argv[i];
			continue;
		}
		c = argv[i][1];
		if (c == 'i' || c == 'I') {
			/* Iterate over the inserted elements */
			db_iterate = 1;
		}
	}
	puts(zBanner);

	/* Open our database */
	rc = unqlite_open(&pDb, zPath, UNQLITE_OPEN_CREATE);
	if (rc != UNQLITE_OK) {
		Fatal(0, "Out of memory");
	}

	printf("Starting insertions of %d random records...\n", MAX_RECORDS);

	/* Start the random insertions */
	for (i = 0; i < MAX_RECORDS; ++i) {

		/* Generate the random key first */
		unqlite_util_random_string(pDb, zKey, sizeof(zKey));

		/* Perform the insertion */
		rc = unqlite_kv_store(pDb, zKey, sizeof(zKey), zData, sizeof(zData));
		if (rc != UNQLITE_OK) {
			/* Something goes wrong */
			break;
		}

		if (i == 79125) {
			/* Insert a sentinel record */

			/* time(&tt); pTm = localtime(&tt); ... */
			unqlite_kv_store_fmt(pDb, "sentinel", -1, "I'm a sentinel record inserted on %d:%d:%d\n", 14, 15, 18); /* Dummy time */
		}
	}

	/* If we are OK, then manually commit the transaction */
	if (rc == UNQLITE_OK) {
		/*
		 * In fact, a call to unqlite_commit() is not necessary since UnQLite
		 * will automatically commit the transaction during a call to unqlite_close().
		 */
		rc = unqlite_commit(pDb);
		if (rc != UNQLITE_OK) {
			/* Rollback the transaction */
			rc = unqlite_rollback(pDb);
		}
	}

	if (rc != UNQLITE_OK) {
		/* Something goes wrong, extract the database error log and exit */
		Fatal(pDb, 0);
	}
	puts("Done...Fetching the 'sentinel' record: ");

	/* Fetch the sentinel */
	rc = unqlite_kv_fetch_callback(pDb, "sentinel", -1, DataConsumerCallback, 0);
	if (rc != UNQLITE_OK) {
		/* Can't happen */
		Fatal(0, "Sentinel record not found");
	}

	if (db_iterate) {
		/* Iterate over the inserted records */
		unqlite_kv_cursor *pCur;

		/* Allocate a new cursor instance */
		rc = unqlite_kv_cursor_init(pDb, &pCur);
		if (rc != UNQLITE_OK) {
			Fatal(0, "Out of memory");
		}

		/* Point to the first record */
		unqlite_kv_cursor_first_entry(pCur);

		/* Iterate over the entries */
		while (unqlite_kv_cursor_valid_entry(pCur)) {
			int nKeyLen;
			/* unqlite_int64 nDataLen; */

			/* Consume the key */
			unqlite_kv_cursor_key(pCur, 0, &nKeyLen); /* Extract key length */
			printf("\nKey ==> %u\n\t", nKeyLen);
			unqlite_kv_cursor_key_callback(pCur, DataConsumerCallback, 0);

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
		unqlite_kv_cursor_release(pDb, pCur);
	}

	/* All done, close our database */
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
static int DataConsumerCallback(const void *pData, unsigned int nDatalen, void *pUserData /* Unused */)
{
#ifdef __WINNT__
	BOOL rc;
	rc = WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), pData, (DWORD)nDatalen, 0, 0);
	if (!rc) {
		/* Abort processing */
		return UNQLITE_ABORT;
	}
#else
	ssize_t nWr;
	nWr = write(STDOUT_FILENO, pData, nDatalen);
	if (nWr < 0) {
		/* Abort processing */
		return UNQLITE_ABORT;
	}
#endif /* __WINT__ */
	/* All done, data was redirected to STDOUT */
	return UNQLITE_OK;
}
