/*
 * Symisc unQLite: An Embeddable NoSQL (Post Modern) Database Engine.
 * Copyright (C) 2012-2026, Symisc Systems https://unqlite.symisc.net/
 * Version 1.2.1
 * For information on licensing, redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES
 * please contact Symisc Systems via:
 *       legal@symisc.net
 *       licensing@symisc.net
 *       contact@symisc.net
 * or visit:
 *      https://unqlite.symisc.net/licensing.html
 */
 /* $SymiscID: os.c v1.0 FreeBSD 2012-11-12 21:27 devel <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif
/* OS interfaces abstraction layers: Mostly SQLite3 source tree */
/*
** The following routines are convenience wrappers around methods
** of the unqlite_file object.  This is mostly just syntactic sugar. All
** of this would be completely automatic if UnQLite were coded using
** C++ instead of plain old C.
*/
UNQLITE_PRIVATE int unqliteOsRead(unqlite_file *id, void *pBuf, unqlite_int64 amt, unqlite_int64 offset)
{
  return id->pMethods->xRead(id, pBuf, amt, offset);
}
UNQLITE_PRIVATE int unqliteOsWrite(unqlite_file *id, const void *pBuf, unqlite_int64 amt, unqlite_int64 offset)
{
  return id->pMethods->xWrite(id, pBuf, amt, offset);
}
UNQLITE_PRIVATE int unqliteOsTruncate(unqlite_file *id, unqlite_int64 size)
{
  return id->pMethods->xTruncate(id, size);
}
UNQLITE_PRIVATE int unqliteOsSync(unqlite_file *id, int flags)
{
  return id->pMethods->xSync(id, flags);
}
UNQLITE_PRIVATE int unqliteOsFileSize(unqlite_file *id, unqlite_int64 *pSize)
{
  return id->pMethods->xFileSize(id, pSize);
}
UNQLITE_PRIVATE int unqliteOsLock(unqlite_file *id, int lockType)
{
  return id->pMethods->xLock(id, lockType);
}
UNQLITE_PRIVATE int unqliteOsUnlock(unqlite_file *id, int lockType)
{
  return id->pMethods->xUnlock(id, lockType);
}
UNQLITE_PRIVATE int unqliteOsCheckReservedLock(unqlite_file *id, int *pResOut)
{
  return id->pMethods->xCheckReservedLock(id, pResOut);
}
UNQLITE_PRIVATE int unqliteOsSectorSize(unqlite_file *id)
{
  if( id->pMethods->xSectorSize ){
	  return id->pMethods->xSectorSize(id);
  }
  return  UNQLITE_DEFAULT_SECTOR_SIZE;
}
/*
** The next group of routines are convenience wrappers around the
** VFS methods.
*/
UNQLITE_PRIVATE int unqliteOsOpen(
  unqlite_vfs *pVfs,
  SyMemBackend *pAlloc,
  const char *zPath, 
  unqlite_file **ppOut, 
  unsigned int flags 
)
{
	unqlite_file *pFile;
	int rc;
	*ppOut = 0;
	if( zPath == 0 ){
		/* May happen if dealing with an in-memory database */
		return SXERR_EMPTY;
	}
	/* Allocate a new instance */
	pFile = (unqlite_file *)SyMemBackendAlloc(pAlloc,sizeof(unqlite_file)+pVfs->szOsFile);
	if( pFile == 0 ){
		return UNQLITE_NOMEM;
	}
	/* Zero the structure */
	SyZero(pFile,sizeof(unqlite_file)+pVfs->szOsFile);
	/* Invoke the xOpen method of the underlying VFS */
	rc = pVfs->xOpen(pVfs, zPath, pFile, flags);
	if( rc != UNQLITE_OK ){
		SyMemBackendFree(pAlloc,pFile);
		pFile = 0;
	}
	*ppOut = pFile;
	return rc;
}
UNQLITE_PRIVATE int unqliteOsCloseFree(SyMemBackend *pAlloc,unqlite_file *pId)
{
	int rc = UNQLITE_OK;
	if( pId ){
		rc = pId->pMethods->xClose(pId);
		SyMemBackendFree(pAlloc,pId);
	}
	return rc;
}
UNQLITE_PRIVATE int unqliteOsDelete(unqlite_vfs *pVfs, const char *zPath, int dirSync){
  return pVfs->xDelete(pVfs, zPath, dirSync);
}
UNQLITE_PRIVATE int unqliteOsAccess(
  unqlite_vfs *pVfs, 
  const char *zPath, 
  int flags, 
  int *pResOut
){
  return pVfs->xAccess(pVfs, zPath, flags, pResOut);
}
