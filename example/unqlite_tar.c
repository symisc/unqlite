/*
 * Compile this file together with the UnQLite database engine source code
 * to generate the executable. For example:
 *  gcc -W -Wall -O6 unqlite_tar.c unqlite.c -o unqlite_tar
*/
/*
 * This simple program is a quick introduction on how to embed and start
 * experimenting with UnQLite without having to do a lot of tedious
 * reading and configuration.
 *
 * Turn a UnQLite database into a TAR-like archive with O(1) record lookup.
 *
 * Typical usage of this program:
 *
 * To store files in the database, simply invoke the program with the '-w' command as follows:
 *
 *  ./unqlite_tar test.db -w file1 file2 file3...
 *
 * To extract a stored file, use the '-r' as follows:
 *
 * ./unqlite_tar test.db -r file1 file2...
 *
 * To iterate (using the cursor interfaces) over the inserted records, use the '-i' command
 * as follows:
 *
 *  ./unqlite_tar test.db -i
 *
 * Only Key/Value store plus some utility interfaces are used in this example.
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
	"UnQLite TAR                                                 \n"
	"                                         http://unqlite.org/\n"
	"============================================================\n"
};
/*
 * Display the banner, a help message and exit.
 */
static void Help(void)
{
	puts(zBanner);
	puts("unqlite_tar db_name (-r|-w|-i) file1 [file2 ...]");
	puts("\t-w: Store one or more files in the database");
	puts("\t-r: Extract records from the database");
	puts("\t-i: Iterate over the stored files");
	/* Exit immediately */
	exit(0);
}
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


int main(int argc, char *argv[])
{
	int db_extract = 0;          /* TRUE to extract records from the dabase */
	int db_store = 0;            /* TRUE to store files in the database */
	int db_iterate = 0;          /* TRUE to iterate over the inserted elements */
	unqlite *pDb;                /* Database handle */
	int c, i, rc;

	if (argc < 3) {
		/* Missing database name */
		Help();
	}

	c = argv[2][0];
	if (c != '-') {
		/* Missing command */
		Help();
	}
	c = argv[2][1];
	if (c == 'i' || c == 'I') {
		/* Iterate over the inserted elements */
		db_iterate = 1;
	}
	else if (c == 'w' || c == 'W') {
		/* Store some files */
		db_store = 1;
	}
	else {
		/* Extract some records */
		db_extract = 1;
	}


	/* Open our database */
	rc = unqlite_open(&pDb, argv[1], db_store ? UNQLITE_OPEN_CREATE : (UNQLITE_OPEN_READONLY | UNQLITE_OPEN_MMAP) /* Read-only DB */);
	if (rc != UNQLITE_OK) {
		Fatal(0, "Out of memory");
	}

	if (db_store) {
		void *pMap;          /* Read-only memory view of the target file */
		unqlite_int64 nSize; /* file size */

		/* Start the insertion */
		for (i = 3; i < argc; ++i) {
			const char *zFile = argv[i];
			printf("Inserting %s\t ... ", zFile);

			/* Obtain a read-only memory view of the whole file */
			rc = unqlite_util_load_mmaped_file(zFile, &pMap, &nSize);
			if (rc == UNQLITE_OK) {
				/* Store the whole file */
				rc = unqlite_kv_store(pDb, zFile, -1, pMap, nSize);
				/* Discard this view */
				unqlite_util_release_mmaped_file(pMap, nSize);
			}
			puts(rc == UNQLITE_OK ? "OK" : "Fail");
		}
		/* Manually commit the transaction.
		 * In fact, a call to unqlite_commit() is not necessary since UnQLite
		 * will automatically commit the transaction during a call to unqlite_close().
		 */
		rc = unqlite_commit(pDb);
		if (rc != UNQLITE_OK) {
			/* Rollback the transaction */
			rc = unqlite_rollback(pDb);
		}
		if (rc != UNQLITE_OK) {
			/* Something goes wrong, extract the database error log and exit */
			Fatal(pDb, 0);
		}
	}
	else if (db_iterate) {
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
			unqlite_int64 nDataLen;

			/* Consume the key */
			unqlite_kv_cursor_key_callback(pCur, DataConsumerCallback, 0);

			/* Extract the data size */
			unqlite_kv_cursor_data(pCur, 0, &nDataLen);
			printf(":\t %ld Bytes\n", nDataLen);
			/* unqlite_kv_cursor_data_callback(pCur,DataConsumerCallback,0); */

			/* Point to the next entry */
			unqlite_kv_cursor_next_entry(pCur);
		}
		/* Finally, Release our cursor */
		unqlite_kv_cursor_release(pDb, pCur);
	}
	else {
		/* Extract one more records */
		for (i = 3; i < argc; ++i) {
			const char *zFile = argv[i];
			rc = unqlite_kv_fetch_callback(pDb, zFile, -1, DataConsumerCallback, 0);
			if (rc == UNQLITE_NOTFOUND) {
				printf("No such record: %s\n", zFile);
			}
		}
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
