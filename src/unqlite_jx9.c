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
 /* $SymiscID: unql_jx9.c v1.2 FreeBSD 2013-01-24 22:45 stable <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif
/* 
 * This file implements UnQLite functions (db_exists(), db_create(), db_put(), db_get(), etc.) for the
 * underlying Jx9 Virtual Machine. 
 */
/*
 * string db_version(void)
 *   Return the current version of the unQLite database engine.
 * Parameter
 *   None
 * Return
 *    unQLite version number (string).
 */
static int unqliteBuiltin_db_version(jx9_context *pCtx,int argc,jx9_value **argv)
{
	SXUNUSED(argc); /* cc warning */
	SXUNUSED(argv);
	jx9_result_string(pCtx,UNQLITE_VERSION,(int)sizeof(UNQLITE_VERSION)-1);
	return JX9_OK;
}
/*
 * string db_errlog(void)
 *   Return the database error log.
 * Parameter
 *   None
 * Return
 *    Database error log (string).
 */
static int unqliteBuiltin_db_errlog(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_vm *pVm;
	SyBlob *pErr;
	
	SXUNUSED(argc); /* cc warning */
	SXUNUSED(argv);

	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Point to the error log */
	pErr = &pVm->pDb->sErr;
	/* Return the log */
	jx9_result_string(pCtx,(const char *)SyBlobData(pErr),(int)SyBlobLength(pErr));
	return JX9_OK;
}
/*
 * string db_copyright(void)
 * string db_credits(void)
 *   Return the unQLite database engine copyright notice.
 * Parameter
 *   None
 * Return
 *    Copyright notice.
 */
static int unqliteBuiltin_db_credits(jx9_context *pCtx,int argc,jx9_value **argv)
{
	SXUNUSED(argc); /* cc warning */
	SXUNUSED(argv);
	jx9_result_string(pCtx,UNQLITE_COPYRIGHT,(int)sizeof(UNQLITE_COPYRIGHT)-1);
	return JX9_OK;
}
/*
 * string db_sig(void)
 *   Return the unQLite database engine unique signature.
 * Parameter
 *   None
 * Return
 *    unQLite signature.
 */
static int unqliteBuiltin_db_sig(jx9_context *pCtx,int argc,jx9_value **argv)
{
	SXUNUSED(argc); /* cc warning */
	SXUNUSED(argv);
	jx9_result_string(pCtx,UNQLITE_IDENT,sizeof(UNQLITE_IDENT)-1);
	return JX9_OK;
}
/*
 * bool collection_exists(string $name)
 * bool db_exits(string $name)
 *   Check if a given collection exists in the underlying database.
 * Parameter
 *   name: Lookup name
 * Return
 *    TRUE if the collection exits. FALSE otherwise.
 */
static int unqliteBuiltin_collection_exists(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Perform the lookup */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	/* Lookup result */
	jx9_result_bool(pCtx,pCol ? 1 : 0);
	return JX9_OK;
}
/*
 * bool collection_create(string $name)
 * bool db_create(string $name)
 *   Create a new collection.
 * Parameter
 *   name: Collection name
 * Return
 *    TRUE if the collection was successfuly created. FALSE otherwise.
 */
static int unqliteBuiltin_collection_create(jx9_context *pCtx,int argc,jx9_value **argv)
{
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	int rc;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Try to create the collection */
	rc = unqliteCreateCollection(pVm,&sName);
	/* Return the result to the caller */
	jx9_result_bool(pCtx,rc == UNQLITE_OK ? 1 : 0);
	return JX9_OK;
}
/*
 * value db_fetch(string $col_name)
 * value db_get(string $col_name)
 *   Fetch the current record from a given collection and advance
 *   the record cursor.
 * Parameter
 *   col_name: Collection name
 * Return
 *    Record content success. NULL on failure (No more records to retrieve).
 */
static int unqliteBuiltin_db_fetch_next(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	int rc;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name");
		/* Return null */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return null */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		/* Fetch the current record */
		jx9_value *pValue;
		pValue = jx9_context_new_scalar(pCtx);
		if( pValue == 0 ){
			jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Jx9 is running out of memory");
			jx9_result_null(pCtx);
			return JX9_OK;
		}else{
			rc = unqliteCollectionFetchNextRecord(pCol,pValue);
			if( rc == UNQLITE_OK ){
				jx9_result_value(pCtx,pValue);
				/* pValue will be automatically released as soon we return from this function */
			}else{
				/* Return null */
				jx9_result_null(pCtx);
			}
		}
	}else{
		/* No such collection, return null */
		jx9_result_null(pCtx);
	}
	return JX9_OK;
}
/*
 * value db_fetch_by_id(string $col_name,int64 $record_id)
 * value db_get_by_id(string $col_name,int64 $record_id)
 *   Fetch a record using its unique ID from a given collection.
 * Parameter
 *   col_name:  Collection name
 *   record_id: Record number (__id field of a JSON object)
 * Return
 *    Record content success. NULL on failure (No such record).
 */
static int unqliteBuiltin_db_fetch_by_id(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	jx9_int64 nId;
	int nByte;
	int rc;
	/* Extract collection name */
	if( argc < 2 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name and/or record ID");
		/* Return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	/* Extract the record ID */
	nId = jx9_value_to_int(argv[1]);
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		/* Fetch the desired record */
		jx9_value *pValue;
		pValue = jx9_context_new_scalar(pCtx);
		if( pValue == 0 ){
			jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Jx9 is running out of memory");
			jx9_result_null(pCtx);
			return JX9_OK;
		}else{
			rc = unqliteCollectionFetchRecordById(pCol,nId,pValue);
			if( rc == UNQLITE_OK ){
				jx9_result_value(pCtx,pValue);
				/* pValue will be automatically released as soon we return from this function */
			}else{
				/* No such record, return null */
				jx9_result_null(pCtx);
			}
		}
	}else{
		/* No such collection, return null */
		jx9_result_null(pCtx);
	}
	return JX9_OK;
}
/*
 * array db_fetch_all(string $col_name,[callback filter_callback])
 * array db_get_all(string $col_name,[callback filter_callback])
 *   Retrieve all records of a given collection and apply the given
 *   callback if available to filter records.
 * Parameter
 *   col_name: Collection name
 * Return
 *    Contents of the collection (JSON array) on success. NULL on failure.
 */
static int unqliteBuiltin_db_fetch_all(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	int rc;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name");
		/* Return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return NULL */
		jx9_result_null(pCtx);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		jx9_value *pValue,*pArray,*pCallback = 0;
		jx9_value sResult; /* Callback result */
		/* Allocate an empty scalar value and an empty JSON array */
		pArray = jx9_context_new_array(pCtx);
		pValue = jx9_context_new_scalar(pCtx);
		jx9MemObjInit(pCtx->pVm,&sResult);
		if( pValue == 0 || pArray == 0 ){
			jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Jx9 is running out of memory");
			jx9_result_null(pCtx);
			return JX9_OK;
		}
		if( argc > 1 && jx9_value_is_callable(argv[1]) ){
			pCallback = argv[1];
		}
		unqliteCollectionResetRecordCursor(pCol);
		/* Fetch collection records one after one */
		while( UNQLITE_OK == unqliteCollectionFetchNextRecord(pCol,pValue) ){
			if( pCallback ){
				jx9_value *apArg[2];
				/* Invoke the filter callback */
				apArg[0] = pValue;
				rc = jx9VmCallUserFunction(pCtx->pVm,pCallback,1,apArg,&sResult);
				if( rc == JX9_OK ){
					int iResult; /* Callback result */
					/* Extract callback result */
					iResult = jx9_value_to_bool(&sResult);
					if( !iResult ){
						/* Discard the result */
						unqliteCollectionCacheRemoveRecord(pCol,unqliteCollectionCurrentRecordId(pCol) - 1);
						continue;
					}
				}
			}
			/* Put the value in the JSON array */
			jx9_array_add_elem(pArray,0,pValue);
			/* Release the value */
			jx9_value_null(pValue);
		}
		jx9MemObjRelease(&sResult);
		/* Finally, return our array */
		jx9_result_value(pCtx,pArray);
		/* pValue will be automatically released as soon we return from
		 * this foreign function.
		 */
	}else{
		/* No such collection, return null */
		jx9_result_null(pCtx);
	}
	return JX9_OK;
}
/*
 * int64 db_last_record_id(string $col_name)
 *   Return the ID of the last inserted record.
 * Parameter
 *   col_name: Collection name
 * Return
 *    Record ID (64-bit integer) on success. FALSE on failure.
 */
static int unqliteBuiltin_db_last_record_id(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		jx9_result_int64(pCtx,unqliteCollectionLastRecordId(pCol));
	}else{
		/* No such collection, return FALSE */
		jx9_result_bool(pCtx,0);
	}
	return JX9_OK;
}
/*
 * inr64 db_current_record_id(string $col_name)
 *   Return the current record ID.
 * Parameter
 *   col_name: Collection name
 * Return
 *    Current record ID (64-bit integer) on success. FALSE on failure.
 */
static int unqliteBuiltin_db_current_record_id(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		jx9_result_int64(pCtx,unqliteCollectionCurrentRecordId(pCol));
	}else{
		/* No such collection, return FALSE */
		jx9_result_bool(pCtx,0);
	}
	return JX9_OK;
}
/*
 * bool db_reset_record_cursor(string $col_name)
 *   Reset the record ID cursor.
 * Parameter
 *   col_name: Collection name
 * Return
 *    TRUE on success. FALSE on failure.
 */
static int unqliteBuiltin_db_reset_record_cursor(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		unqliteCollectionResetRecordCursor(pCol);
		jx9_result_bool(pCtx,1);
	}else{
		/* No such collection */
		jx9_result_bool(pCtx,0);
	}
	return JX9_OK;
}
/*
 * int64 db_total_records(string $col_name)
 *   Return the total number of inserted records in the given collection.
 * Parameter
 *   col_name: Collection name
 * Return
 *    Total number of records on success. FALSE on failure.
 */
static int unqliteBuiltin_db_total_records(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		unqlite_int64 nRec;
		nRec = unqliteCollectionTotalRecords(pCol);
		jx9_result_int64(pCtx,nRec);
	}else{
		/* No such collection */
		jx9_result_bool(pCtx,0);
	}
	return JX9_OK;
}
/*
 * string db_creation_date(string $col_name)
 *   Return the creation date of the given collection.
 * Parameter
 *   col_name: Collection name
 * Return
 *    Creation date on success. FALSE on failure.
 */
static int unqliteBuiltin_db_creation_date(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		Sytm *pTm = &pCol->sCreation;
		jx9_result_string_format(pCtx,"%d-%d-%d %02d:%02d:%02d",
			pTm->tm_year,pTm->tm_mon,pTm->tm_mday,
			pTm->tm_hour,pTm->tm_min,pTm->tm_sec
			);
	}else{
		/* No such collection */
		jx9_result_bool(pCtx,0);
	}
	return JX9_OK;
}
/*
 * bool db_store(string $col_name,...)
 * bool db_put(string $col_name,...)
 *   Store one or more JSON values in a given collection.
 * Parameter
 *   col_name: Collection name
 * Return
 *    TRUE on success. FALSE on failure.
 */
static int unqliteBuiltin_db_store(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	int rc;
	int i;
	/* Extract collection name */
	if( argc < 2 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name and/or records");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol == 0 ){
		jx9_context_throw_error_format(pCtx,JX9_CTX_ERR,"No such collection '%z'",&sName);
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	/* Store the given values */
	for( i = 1 ; i < argc ; ++i ){
		rc = unqliteCollectionPut(pCol,argv[i],0);
		if( rc != UNQLITE_OK){
			jx9_context_throw_error_format(pCtx,JX9_CTX_ERR,
				"Error while storing record %d in collection '%z'",i,&sName
				);
			/* Return false */
			jx9_result_bool(pCtx,0);
			return JX9_OK;
		}
	}
	/* All done, return TRUE */
	jx9_result_bool(pCtx,1);
	return JX9_OK;
}
/*
 * bool db_drop_collection(string $col_name)
 * bool collection_delete(string $col_name)
 *   Remove a given collection from the database.
 * Parameter
 *   col_name: Collection name
 * Return
 *    TRUE on success. FALSE on failure.
 */
static int unqliteBuiltin_db_drop_col(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	int rc;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol == 0 ){
		jx9_context_throw_error_format(pCtx,JX9_CTX_ERR,"No such collection '%z'",&sName);
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	/* Drop the collection */
	rc = unqliteDropCollection(pCol);
	/* Processing result */
	jx9_result_bool(pCtx,rc == UNQLITE_OK);
	return JX9_OK;
}
/*
 * bool db_drop_record(string $col_name,int64 record_id)
 *   Remove a given record from a collection.
 * Parameter
 *   col_name: Collection name.
 *   record_id: ID of the record.
 * Return
 *    TRUE on success. FALSE on failure.
 */
static int unqliteBuiltin_db_drop_record(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	jx9_int64 nId;
	int nByte;
	int rc;
	/* Extract collection name */
	if( argc < 2 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name and/or records");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol == 0 ){
		jx9_context_throw_error_format(pCtx,JX9_CTX_ERR,"No such collection '%z'",&sName);
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	/* Extract the record ID */
	nId = jx9_value_to_int64(argv[1]);
	/* Drop the record */
	rc = unqliteCollectionDropRecord(pCol,nId,1,1);
	/* Processing result */
	jx9_result_bool(pCtx,rc == UNQLITE_OK);
	return JX9_OK;
}
/*
 * bool db_set_schema(string $col_name, object $json_object)
 *   Set a schema for a given collection.
 * Parameter
 *   col_name: Collection name.
 *   json_object: Collection schema (Must be a JSON object).
 * Return
 *    TRUE on success. FALSE on failure.
 */
static int unqliteBuiltin_db_set_schema(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	int rc;
	/* Extract collection name */
	if( argc < 2 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name and/or db scheme");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	if( !jx9_value_is_json_object(argv[1]) ){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection scheme");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	rc = UNQLITE_NOOP;
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		/* Set the collection scheme */
		rc = unqliteCollectionSetSchema(pCol,argv[1]);
	}else{
		jx9_context_throw_error_format(pCtx,JX9_CTX_WARNING,
			"No such collection '%z'",
			&sName
			);
	}
	/* Processing result */
	jx9_result_bool(pCtx,rc == UNQLITE_OK);
	return JX9_OK;
}
/*
 * object db_get_schema(string $col_name)
 *   Return the schema associated with a given collection.
 * Parameter
 *   col_name: Collection name
 * Return
 *    Collection schema on success. null otherwise.
 */
static int unqliteBuiltin_db_get_schema(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_col *pCol;
	const char *zName;
	unqlite_vm *pVm;
	SyString sName;
	int nByte;
	/* Extract collection name */
	if( argc < 1 ){
		/* Missing arguments */
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Missing collection name and/or db scheme");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	zName = jx9_value_to_string(argv[0],&nByte);
	if( nByte < 1){
		jx9_context_throw_error(pCtx,JX9_CTX_ERR,"Invalid collection name");
		/* Return false */
		jx9_result_bool(pCtx,0);
		return JX9_OK;
	}
	SyStringInitFromBuf(&sName,zName,nByte);
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Fetch the collection */
	pCol = unqliteCollectionFetch(pVm,&sName,UNQLITE_VM_AUTO_LOAD);
	if( pCol ){
		/* Return the collection schema */
		jx9_result_value(pCtx,&pCol->sSchema);
	}else{
		jx9_context_throw_error_format(pCtx,JX9_CTX_WARNING,
			"No such collection '%z'",
			&sName
			);
		jx9_result_null(pCtx);
	}
	return JX9_OK;
}
/*
 * bool db_begin(void)
 *   Manually begin a write transaction.
 * Parameter
 *   None
 * Return
 *    TRUE on success. FALSE otherwise.
 */
static int unqliteBuiltin_db_begin(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_vm *pVm;
	unqlite *pDb;
	int rc;
	SXUNUSED(argc); /* cc warning */
	SXUNUSED(argv);
	/* Point to the unqlite Vm */
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Point to the underlying database handle  */
	pDb = pVm->pDb;
	/* Begin the transaction */
	rc = unqlitePagerBegin(pDb->sDB.pPager);
	/* result */
	jx9_result_bool(pCtx,rc == UNQLITE_OK );
	return JX9_OK;
}
/*
 * bool db_commit(void)
 *   Manually commit a transaction.
 * Parameter
 *   None
 * Return
 *    TRUE if the transaction was successfuly commited. FALSE otherwise.
 */
static int unqliteBuiltin_db_commit(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_vm *pVm;
	unqlite *pDb;
	int rc;
	SXUNUSED(argc); /* cc warning */
	SXUNUSED(argv);
	/* Point to the unqlite Vm */
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Point to the underlying database handle  */
	pDb = pVm->pDb;
	/* Commit the transaction if any */
	rc = unqlitePagerCommit(pDb->sDB.pPager);
	/* Commit result */
	jx9_result_bool(pCtx,rc == UNQLITE_OK );
	return JX9_OK;
}
/*
 * bool db_rollback(void)
 *   Manually rollback a transaction.
 * Parameter
 *   None
 * Return
 *    TRUE if the transaction was successfuly rolled back. FALSE otherwise
 */
static int unqliteBuiltin_db_rollback(jx9_context *pCtx,int argc,jx9_value **argv)
{
	unqlite_vm *pVm;
	unqlite *pDb;
	int rc;
	SXUNUSED(argc); /* cc warning */
	SXUNUSED(argv);
	/* Point to the unqlite Vm */
	pVm = (unqlite_vm *)jx9_context_user_data(pCtx);
	/* Point to the underlying database handle  */
	pDb = pVm->pDb;
	/* Rollback the transaction if any */
	rc = unqlitePagerRollback(pDb->sDB.pPager,TRUE);
	/* Rollback result */
	jx9_result_bool(pCtx,rc == UNQLITE_OK );
	return JX9_OK;
}
/*
 * Register all the UnQLite foreign functions defined above.
 */
UNQLITE_PRIVATE int unqliteRegisterJx9Functions(unqlite_vm *pVm)
{
	static const jx9_builtin_func aBuiltin[] = {
		{ "db_version" , unqliteBuiltin_db_version },
		{ "db_copyright", unqliteBuiltin_db_credits },
		{ "db_credits" , unqliteBuiltin_db_credits },
		{ "db_sig" ,     unqliteBuiltin_db_sig     },
		{ "db_errlog",   unqliteBuiltin_db_errlog  },
		{ "collection_exists", unqliteBuiltin_collection_exists },
		{ "db_exists",         unqliteBuiltin_collection_exists }, 
		{ "collection_create", unqliteBuiltin_collection_create },
		{ "db_create",         unqliteBuiltin_collection_create },
		{ "db_fetch",          unqliteBuiltin_db_fetch_next     },
		{ "db_get",            unqliteBuiltin_db_fetch_next     },
		{ "db_fetch_by_id",    unqliteBuiltin_db_fetch_by_id    },
		{ "db_get_by_id",      unqliteBuiltin_db_fetch_by_id    },
		{ "db_fetch_all",      unqliteBuiltin_db_fetch_all      },
		{ "db_get_all",        unqliteBuiltin_db_fetch_all      },
		{ "db_last_record_id", unqliteBuiltin_db_last_record_id },
		{ "db_current_record_id", unqliteBuiltin_db_current_record_id },
		{ "db_reset_record_cursor", unqliteBuiltin_db_reset_record_cursor },
		{ "db_total_records",  unqliteBuiltin_db_total_records  },
		{ "db_creation_date",  unqliteBuiltin_db_creation_date  },
		{ "db_store",          unqliteBuiltin_db_store          },
		{ "db_put",            unqliteBuiltin_db_store          },
		{ "db_drop_collection", unqliteBuiltin_db_drop_col      },
		{ "collection_delete", unqliteBuiltin_db_drop_col       },
		{ "db_drop_record",    unqliteBuiltin_db_drop_record    },
		{ "db_set_schema",     unqliteBuiltin_db_set_schema     },
		{ "db_get_schema",     unqliteBuiltin_db_get_schema     },
		{ "db_begin",          unqliteBuiltin_db_begin          },
		{ "db_commit",         unqliteBuiltin_db_commit         },
		{ "db_rollback",       unqliteBuiltin_db_rollback       },
	};
	int rc = UNQLITE_OK;
	sxu32 n;
	/* Register the unQLite functions defined above in the Jx9 call table */
	for( n = 0 ; n < SX_ARRAYSIZE(aBuiltin) ; ++n ){
		rc = jx9_create_function(pVm->pJx9Vm,aBuiltin[n].zName,aBuiltin[n].xFunc,pVm);
	}
	return rc;
}
