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
 /* $SymiscID: os_win.c v1.2 Win7 2012-11-10 12:10 devel <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif
/* Omit the whole layer from the build if compiling for platforms other than Windows */
#ifdef __WINNT__
/* This file contains code that is specific to windows. (Mostly SQLite3 source tree) */
#include <Windows.h>
/*
** Some microsoft compilers lack this definition.
*/
#ifndef INVALID_FILE_ATTRIBUTES
# define INVALID_FILE_ATTRIBUTES ((DWORD)-1) 
#endif
/*
** WinCE lacks native support for file locking so we have to fake it
** with some code of our own.
*/
#ifdef __WIN_CE__
typedef struct winceLock {
  int nReaders;       /* Number of reader locks obtained */
  BOOL bPending;      /* Indicates a pending lock has been obtained */
  BOOL bReserved;     /* Indicates a reserved lock has been obtained */
  BOOL bExclusive;    /* Indicates an exclusive lock has been obtained */
} winceLock;
#define AreFileApisANSI() 1
#define FormatMessageW(a,b,c,d,e,f,g) 0
#endif

/*
** The winFile structure is a subclass of unqlite_file* specific to the win32
** portability layer.
*/
typedef struct winFile winFile;
struct winFile {
  const unqlite_io_methods *pMethod; /*** Must be first ***/
  unqlite_vfs *pVfs;      /* The VFS used to open this file */
  HANDLE h;               /* Handle for accessing the file */
  sxu8 locktype;          /* Type of lock currently held on this file */
  short sharedLockByte;   /* Randomly chosen byte used as a shared lock */
  DWORD lastErrno;        /* The Windows errno from the last I/O error */
  DWORD sectorSize;       /* Sector size of the device file is on */
  int szChunk;            /* Chunk size */
#ifdef __WIN_CE__
  WCHAR *zDeleteOnClose;  /* Name of file to delete when closing */
  HANDLE hMutex;          /* Mutex used to control access to shared lock */  
  HANDLE hShared;         /* Shared memory segment used for locking */
  winceLock local;        /* Locks obtained by this instance of winFile */
  winceLock *shared;      /* Global shared lock memory for the file  */
#endif
};
/*
** Convert a UTF-8 string to microsoft unicode (UTF-16?). 
**
** Space to hold the returned string is obtained from HeapAlloc().
*/
static WCHAR *utf8ToUnicode(const char *zFilename){
  int nChar;
  WCHAR *zWideFilename;

  nChar = MultiByteToWideChar(CP_UTF8, 0, zFilename, -1, 0, 0);
  zWideFilename = (WCHAR *)HeapAlloc(GetProcessHeap(),0,nChar*sizeof(zWideFilename[0]) );
  if( zWideFilename==0 ){
    return 0;
  }
  nChar = MultiByteToWideChar(CP_UTF8, 0, zFilename, -1, zWideFilename, nChar);
  if( nChar==0 ){
    HeapFree(GetProcessHeap(),0,zWideFilename);
    zWideFilename = 0;
  }
  return zWideFilename;
}

/*
** Convert microsoft unicode to UTF-8.  Space to hold the returned string is
** obtained from malloc().
*/
static char *unicodeToUtf8(const WCHAR *zWideFilename){
  int nByte;
  char *zFilename;

  nByte = WideCharToMultiByte(CP_UTF8, 0, zWideFilename, -1, 0, 0, 0, 0);
  zFilename = (char *)HeapAlloc(GetProcessHeap(),0,nByte );
  if( zFilename==0 ){
    return 0;
  }
  nByte = WideCharToMultiByte(CP_UTF8, 0, zWideFilename, -1, zFilename, nByte,
                              0, 0);
  if( nByte == 0 ){
    HeapFree(GetProcessHeap(),0,zFilename);
    zFilename = 0;
  }
  return zFilename;
}

/*
** Convert an ansi string to microsoft unicode, based on the
** current codepage settings for file apis.
** 
** Space to hold the returned string is obtained
** from malloc.
*/
static WCHAR *mbcsToUnicode(const char *zFilename){
  int nByte;
  WCHAR *zMbcsFilename;
  int codepage = AreFileApisANSI() ? CP_ACP : CP_OEMCP;

  nByte = MultiByteToWideChar(codepage, 0, zFilename, -1, 0,0)*sizeof(WCHAR);
  zMbcsFilename = (WCHAR *)HeapAlloc(GetProcessHeap(),0,nByte*sizeof(zMbcsFilename[0]) );
  if( zMbcsFilename==0 ){
    return 0;
  }
  nByte = MultiByteToWideChar(codepage, 0, zFilename, -1, zMbcsFilename, nByte);
  if( nByte==0 ){
    HeapFree(GetProcessHeap(),0,zMbcsFilename);
    zMbcsFilename = 0;
  }
  return zMbcsFilename;
}
/*
** Convert multibyte character string to UTF-8.  Space to hold the
** returned string is obtained from malloc().
*/
char *unqlite_win32_mbcs_to_utf8(const char *zFilename){
  char *zFilenameUtf8;
  WCHAR *zTmpWide;

  zTmpWide = mbcsToUnicode(zFilename);
  if( zTmpWide==0 ){
    return 0;
  }
  zFilenameUtf8 = unicodeToUtf8(zTmpWide);
  HeapFree(GetProcessHeap(),0,zTmpWide);
  return zFilenameUtf8;
}
/*
** Some microsoft compilers lack this definition.
*/
#ifndef INVALID_SET_FILE_POINTER
# define INVALID_SET_FILE_POINTER ((DWORD)-1)
#endif

/*
** Move the current position of the file handle passed as the first 
** argument to offset iOffset within the file. If successful, return 0. 
** Otherwise, set pFile->lastErrno and return non-zero.
*/
static int seekWinFile(winFile *pFile, unqlite_int64 iOffset){
  LONG upperBits;                 /* Most sig. 32 bits of new offset */
  LONG lowerBits;                 /* Least sig. 32 bits of new offset */
  DWORD dwRet;                    /* Value returned by SetFilePointer() */

  upperBits = (LONG)((iOffset>>32) & 0x7fffffff);
  lowerBits = (LONG)(iOffset & 0xffffffff);

  /* API oddity: If successful, SetFilePointer() returns a dword 
  ** containing the lower 32-bits of the new file-offset. Or, if it fails,
  ** it returns INVALID_SET_FILE_POINTER. However according to MSDN, 
  ** INVALID_SET_FILE_POINTER may also be a valid new offset. So to determine 
  ** whether an error has actually occured, it is also necessary to call 
  ** GetLastError().
  */
  dwRet = SetFilePointer(pFile->h, lowerBits, &upperBits, FILE_BEGIN);
  if( (dwRet==INVALID_SET_FILE_POINTER && GetLastError()!=NO_ERROR) ){
    pFile->lastErrno = GetLastError();
    return 1;
  }
  return 0;
}
/*
** Close a file.
**
** It is reported that an attempt to close a handle might sometimes
** fail.  This is a very unreasonable result, but windows is notorious
** for being unreasonable so I do not doubt that it might happen.  If
** the close fails, we pause for 100 milliseconds and try again.  As
** many as MX_CLOSE_ATTEMPT attempts to close the handle are made before
** giving up and returning an error.
*/
#define MX_CLOSE_ATTEMPT 3
static int winClose(unqlite_file *id)
{
  int rc, cnt = 0;
  winFile *pFile = (winFile*)id;
  do{
    rc = CloseHandle(pFile->h);
  }while( rc==0 && ++cnt < MX_CLOSE_ATTEMPT && (Sleep(100), 1) );

  return rc ? UNQLITE_OK : UNQLITE_IOERR;
}
/*
** Read data from a file into a buffer.  Return UNQLITE_OK if all
** bytes were read successfully and UNQLITE_IOERR if anything goes
** wrong.
*/
static int winRead(
  unqlite_file *id,          /* File to read from */
  void *pBuf,                /* Write content into this buffer */
  unqlite_int64 amt,        /* Number of bytes to read */
  unqlite_int64 offset       /* Begin reading at this offset */
){
  winFile *pFile = (winFile*)id;  /* file handle */
  DWORD nRead;                    /* Number of bytes actually read from file */

  if( seekWinFile(pFile, offset) ){
    return UNQLITE_FULL;
  }
  if( !ReadFile(pFile->h, pBuf, (DWORD)amt, &nRead, 0) ){
    pFile->lastErrno = GetLastError();
    return UNQLITE_IOERR;
  }
  if( nRead<(DWORD)amt ){
    /* Unread parts of the buffer must be zero-filled */
    SyZero(&((char*)pBuf)[nRead],(sxu32)(amt-nRead));
    return UNQLITE_IOERR;
  }

  return UNQLITE_OK;
}

/*
** Write data from a buffer into a file.  Return UNQLITE_OK on success
** or some other error code on failure.
*/
static int winWrite(
  unqlite_file *id,               /* File to write into */
  const void *pBuf,               /* The bytes to be written */
  unqlite_int64 amt,                        /* Number of bytes to write */
  unqlite_int64 offset            /* Offset into the file to begin writing at */
){
  int rc;                         /* True if error has occured, else false */
  winFile *pFile = (winFile*)id;  /* File handle */

  rc = seekWinFile(pFile, offset);
  if( rc==0 ){
    sxu8 *aRem = (sxu8 *)pBuf;        /* Data yet to be written */
    unqlite_int64 nRem = amt;         /* Number of bytes yet to be written */
    DWORD nWrite;                 /* Bytes written by each WriteFile() call */

    while( nRem>0 && WriteFile(pFile->h, aRem, (DWORD)nRem, &nWrite, 0) && nWrite>0 ){
      aRem += nWrite;
      nRem -= nWrite;
    }
    if( nRem>0 ){
      pFile->lastErrno = GetLastError();
      rc = 1;
    }
  }
  if( rc ){
    if( pFile->lastErrno==ERROR_HANDLE_DISK_FULL ){
      return UNQLITE_FULL;
    }
    return UNQLITE_IOERR;
  }
  return UNQLITE_OK;
}

/*
** Truncate an open file to a specified size
*/
static int winTruncate(unqlite_file *id, unqlite_int64 nByte){
  winFile *pFile = (winFile*)id;  /* File handle object */
  int rc = UNQLITE_OK;             /* Return code for this function */


  /* If the user has configured a chunk-size for this file, truncate the
  ** file so that it consists of an integer number of chunks (i.e. the
  ** actual file size after the operation may be larger than the requested
  ** size).
  */
  if( pFile->szChunk ){
    nByte = ((nByte + pFile->szChunk - 1)/pFile->szChunk) * pFile->szChunk;
  }

  /* SetEndOfFile() returns non-zero when successful, or zero when it fails. */
  if( seekWinFile(pFile, nByte) ){
    rc = UNQLITE_IOERR;
  }else if( 0==SetEndOfFile(pFile->h) ){
    pFile->lastErrno = GetLastError();
    rc = UNQLITE_IOERR;
  }
  return rc;
}
/*
** Make sure all writes to a particular file are committed to disk.
*/
static int winSync(unqlite_file *id, int flags){
  winFile *pFile = (winFile*)id;
  SXUNUSED(flags); /* MSVC warning */
  if( FlushFileBuffers(pFile->h) ){
    return UNQLITE_OK;
  }else{
    pFile->lastErrno = GetLastError();
    return UNQLITE_IOERR;
  }
}
/*
** Determine the current size of a file in bytes
*/
static int winFileSize(unqlite_file *id, unqlite_int64 *pSize){
  DWORD upperBits;
  DWORD lowerBits;
  winFile *pFile = (winFile*)id;
  DWORD error;
  lowerBits = GetFileSize(pFile->h, &upperBits);
  if(   (lowerBits == INVALID_FILE_SIZE)
     && ((error = GetLastError()) != NO_ERROR) )
  {
    pFile->lastErrno = error;
    return UNQLITE_IOERR;
  }
  *pSize = (((unqlite_int64)upperBits)<<32) + lowerBits;
  return UNQLITE_OK;
}
/*
** LOCKFILE_FAIL_IMMEDIATELY is undefined on some Windows systems.
*/
#ifndef LOCKFILE_FAIL_IMMEDIATELY
# define LOCKFILE_FAIL_IMMEDIATELY 1
#endif

/*
** Acquire a reader lock.
*/
static int getReadLock(winFile *pFile){
  int res;
  OVERLAPPED ovlp;
  ovlp.Offset = SHARED_FIRST;
  ovlp.OffsetHigh = 0;
  ovlp.hEvent = 0;
  res = LockFileEx(pFile->h, LOCKFILE_FAIL_IMMEDIATELY,0, SHARED_SIZE, 0, &ovlp);
  if( res == 0 ){
    pFile->lastErrno = GetLastError();
  }
  return res;
}
/*
** Undo a readlock
*/
static int unlockReadLock(winFile *pFile){
  int res;
  res = UnlockFile(pFile->h, SHARED_FIRST, 0, SHARED_SIZE, 0);
  if( res == 0 ){
    pFile->lastErrno = GetLastError();
  }
  return res;
}
/*
** Lock the file with the lock specified by parameter locktype - one
** of the following:
**
**     (1) SHARED_LOCK
**     (2) RESERVED_LOCK
**     (3) PENDING_LOCK
**     (4) EXCLUSIVE_LOCK
**
** Sometimes when requesting one lock state, additional lock states
** are inserted in between.  The locking might fail on one of the later
** transitions leaving the lock state different from what it started but
** still short of its goal.  The following chart shows the allowed
** transitions and the inserted intermediate states:
**
**    UNLOCKED -> SHARED
**    SHARED -> RESERVED
**    SHARED -> (PENDING) -> EXCLUSIVE
**    RESERVED -> (PENDING) -> EXCLUSIVE
**    PENDING -> EXCLUSIVE
**
** This routine will only increase a lock.  The winUnlock() routine
** erases all locks at once and returns us immediately to locking level 0.
** It is not possible to lower the locking level one step at a time.  You
** must go straight to locking level 0.
*/
static int winLock(unqlite_file *id, int locktype){
  int rc = UNQLITE_OK;    /* Return code from subroutines */
  int res = 1;           /* Result of a windows lock call */
  int newLocktype;       /* Set pFile->locktype to this value before exiting */
  int gotPendingLock = 0;/* True if we acquired a PENDING lock this time */
  winFile *pFile = (winFile*)id;
  DWORD error = NO_ERROR;

  /* If there is already a lock of this type or more restrictive on the
  ** OsFile, do nothing.
  */
  if( pFile->locktype>=locktype ){
    return UNQLITE_OK;
  }

  /* Make sure the locking sequence is correct
  assert( pFile->locktype!=NO_LOCK || locktype==SHARED_LOCK );
  assert( locktype!=PENDING_LOCK );
  assert( locktype!=RESERVED_LOCK || pFile->locktype==SHARED_LOCK );
  */
  /* Lock the PENDING_LOCK byte if we need to acquire a PENDING lock or
  ** a SHARED lock.  If we are acquiring a SHARED lock, the acquisition of
  ** the PENDING_LOCK byte is temporary.
  */
  newLocktype = pFile->locktype;
  if(   (pFile->locktype==NO_LOCK)
     || (   (locktype==EXCLUSIVE_LOCK)
         && (pFile->locktype==RESERVED_LOCK))
  ){
    int cnt = 3;
    while( cnt-->0 && (res = LockFile(pFile->h, PENDING_BYTE, 0, 1, 0))==0 ){
      /* Try 3 times to get the pending lock.  The pending lock might be
      ** held by another reader process who will release it momentarily.
	  */
      Sleep(1);
    }
    gotPendingLock = res;
    if( !res ){
      error = GetLastError();
    }
  }

  /* Acquire a shared lock
  */
  if( locktype==SHARED_LOCK && res ){
   /* assert( pFile->locktype==NO_LOCK ); */
    res = getReadLock(pFile);
    if( res ){
      newLocktype = SHARED_LOCK;
    }else{
      error = GetLastError();
    }
  }

  /* Acquire a RESERVED lock
  */
  if( locktype==RESERVED_LOCK && res ){
    /* assert( pFile->locktype==SHARED_LOCK ); */
    res = LockFile(pFile->h, RESERVED_BYTE, 0, 1, 0);
    if( res ){
      newLocktype = RESERVED_LOCK;
    }else{
      error = GetLastError();
    }
  }

  /* Acquire a PENDING lock
  */
  if( locktype==EXCLUSIVE_LOCK && res ){
    newLocktype = PENDING_LOCK;
    gotPendingLock = 0;
  }

  /* Acquire an EXCLUSIVE lock
  */
  if( locktype==EXCLUSIVE_LOCK && res ){
    /* assert( pFile->locktype>=SHARED_LOCK ); */
    res = unlockReadLock(pFile);
    res = LockFile(pFile->h, SHARED_FIRST, 0, SHARED_SIZE, 0);
    if( res ){
      newLocktype = EXCLUSIVE_LOCK;
    }else{
      error = GetLastError();
      getReadLock(pFile);
    }
  }

  /* If we are holding a PENDING lock that ought to be released, then
  ** release it now.
  */
  if( gotPendingLock && locktype==SHARED_LOCK ){
    UnlockFile(pFile->h, PENDING_BYTE, 0, 1, 0);
  }

  /* Update the state of the lock has held in the file descriptor then
  ** return the appropriate result code.
  */
  if( res ){
    rc = UNQLITE_OK;
  }else{
    pFile->lastErrno = error;
    rc = UNQLITE_BUSY;
  }
  pFile->locktype = (sxu8)newLocktype;
  return rc;
}
/*
** This routine checks if there is a RESERVED lock held on the specified
** file by this or any other process. If such a lock is held, return
** non-zero, otherwise zero.
*/
static int winCheckReservedLock(unqlite_file *id, int *pResOut){
  int rc;
  winFile *pFile = (winFile*)id;
  if( pFile->locktype>=RESERVED_LOCK ){
    rc = 1;
  }else{
    rc = LockFile(pFile->h, RESERVED_BYTE, 0, 1, 0);
    if( rc ){
      UnlockFile(pFile->h, RESERVED_BYTE, 0, 1, 0);
    }
    rc = !rc;
  }
  *pResOut = rc;
  return UNQLITE_OK;
}
/*
** Lower the locking level on file descriptor id to locktype.  locktype
** must be either NO_LOCK or SHARED_LOCK.
**
** If the locking level of the file descriptor is already at or below
** the requested locking level, this routine is a no-op.
**
** It is not possible for this routine to fail if the second argument
** is NO_LOCK.  If the second argument is SHARED_LOCK then this routine
** might return UNQLITE_IOERR;
*/
static int winUnlock(unqlite_file *id, int locktype){
  int type;
  winFile *pFile = (winFile*)id;
  int rc = UNQLITE_OK;

  type = pFile->locktype;
  if( type>=EXCLUSIVE_LOCK ){
    UnlockFile(pFile->h, SHARED_FIRST, 0, SHARED_SIZE, 0);
    if( locktype==SHARED_LOCK && !getReadLock(pFile) ){
      /* This should never happen.  We should always be able to
      ** reacquire the read lock */
      rc = UNQLITE_IOERR;
    }
  }
  if( type>=RESERVED_LOCK ){
    UnlockFile(pFile->h, RESERVED_BYTE, 0, 1, 0);
  }
  if( locktype==NO_LOCK && type>=SHARED_LOCK ){
    unlockReadLock(pFile);
  }
  if( type>=PENDING_LOCK ){
    UnlockFile(pFile->h, PENDING_BYTE, 0, 1, 0);
  }
  pFile->locktype = (sxu8)locktype;
  return rc;
}
/*
** Return the sector size in bytes of the underlying block device for
** the specified file. This is almost always 512 bytes, but may be
** larger for some devices.
**
*/
static int winSectorSize(unqlite_file *id){
  return (int)(((winFile*)id)->sectorSize);
}
/*
** This vector defines all the methods that can operate on an
** unqlite_file for Windows systems.
*/
static const unqlite_io_methods winIoMethod = {
  1,                              /* iVersion */
  winClose,                       /* xClose */
  winRead,                        /* xRead */
  winWrite,                       /* xWrite */
  winTruncate,                    /* xTruncate */
  winSync,                        /* xSync */
  winFileSize,                    /* xFileSize */
  winLock,                        /* xLock */
  winUnlock,                      /* xUnlock */
  winCheckReservedLock,           /* xCheckReservedLock */
  winSectorSize,                  /* xSectorSize */
};
/*
 * Windows VFS Methods.
 */
/*
** Convert a UTF-8 filename into whatever form the underlying
** operating system wants filenames in.  Space to hold the result
** is obtained from malloc and must be freed by the calling
** function.
*/
static void *convertUtf8Filename(const char *zFilename)
{
  void *zConverted;
  zConverted = utf8ToUnicode(zFilename);
  /* caller will handle out of memory */
  return zConverted;
}
/*
** Delete the named file.
**
** Note that windows does not allow a file to be deleted if some other
** process has it open.  Sometimes a virus scanner or indexing program
** will open a journal file shortly after it is created in order to do
** whatever it does.  While this other process is holding the
** file open, we will be unable to delete it.  To work around this
** problem, we delay 100 milliseconds and try to delete again.  Up
** to MX_DELETION_ATTEMPTs deletion attempts are run before giving
** up and returning an error.
*/
#define MX_DELETION_ATTEMPTS 5
static int winDelete(
  unqlite_vfs *pVfs,          /* Not used on win32 */
  const char *zFilename,      /* Name of file to delete */
  int syncDir                 /* Not used on win32 */
){
  int cnt = 0;
  DWORD rc;
  DWORD error = 0;
  void *zConverted;
  zConverted = convertUtf8Filename(zFilename);
  if( zConverted==0 ){
	   SXUNUSED(pVfs);
	   SXUNUSED(syncDir);
    return UNQLITE_NOMEM;
  }
  do{
	  DeleteFileW((LPCWSTR)zConverted);
  }while(   (   ((rc = GetFileAttributesW((LPCWSTR)zConverted)) != INVALID_FILE_ATTRIBUTES)
	  || ((error = GetLastError()) == ERROR_ACCESS_DENIED))
	  && (++cnt < MX_DELETION_ATTEMPTS)
	  && (Sleep(100), 1)
	  );
	HeapFree(GetProcessHeap(),0,zConverted);
 
  return (   (rc == INVALID_FILE_ATTRIBUTES) 
          && (error == ERROR_FILE_NOT_FOUND)) ? UNQLITE_OK : UNQLITE_IOERR;
}
/*
** Check the existance and status of a file.
*/
static int winAccess(
  unqlite_vfs *pVfs,         /* Not used  */
  const char *zFilename,     /* Name of file to check */
  int flags,                 /* Type of test to make on this file */
  int *pResOut               /* OUT: Result */
){
  WIN32_FILE_ATTRIBUTE_DATA sAttrData;
  DWORD attr;
  int rc = 0;
  void *zConverted;
  SXUNUSED(pVfs);

  zConverted = convertUtf8Filename(zFilename);
  if( zConverted==0 ){
    return UNQLITE_NOMEM;
  }
  SyZero(&sAttrData,sizeof(sAttrData));
  if( GetFileAttributesExW((WCHAR*)zConverted,
	  GetFileExInfoStandard, 
	  &sAttrData) ){
      /* For an UNQLITE_ACCESS_EXISTS query, treat a zero-length file
      ** as if it does not exist.
      */
      if(    flags==UNQLITE_ACCESS_EXISTS
          && sAttrData.nFileSizeHigh==0 
          && sAttrData.nFileSizeLow==0 ){
        attr = INVALID_FILE_ATTRIBUTES;
      }else{
        attr = sAttrData.dwFileAttributes;
      }
    }else{
      if( GetLastError()!=ERROR_FILE_NOT_FOUND ){
        HeapFree(GetProcessHeap(),0,zConverted);
        return UNQLITE_IOERR;
      }else{
        attr = INVALID_FILE_ATTRIBUTES;
      }
    }
  HeapFree(GetProcessHeap(),0,zConverted);
  switch( flags ){
     case UNQLITE_ACCESS_READWRITE:
      rc = (attr & FILE_ATTRIBUTE_READONLY)==0;
      break;
    case UNQLITE_ACCESS_READ:
    case UNQLITE_ACCESS_EXISTS:
	default:
      rc = attr!=INVALID_FILE_ATTRIBUTES;
      break;
  }
  *pResOut = rc;
  return UNQLITE_OK;
}
/*
** Turn a relative pathname into a full pathname.  Write the full
** pathname into zOut[].  zOut[] will be at least pVfs->mxPathname
** bytes in size.
*/
static int winFullPathname(
  unqlite_vfs *pVfs,            /* Pointer to vfs object */
  const char *zRelative,        /* Possibly relative input path */
  int nFull,                    /* Size of output buffer in bytes */
  char *zFull                   /* Output buffer */
){
  int nByte;
  void *zConverted;
  WCHAR *zTemp;
  char *zOut;
  SXUNUSED(nFull);
  zConverted = convertUtf8Filename(zRelative);
  if( zConverted == 0 ){
	  return UNQLITE_NOMEM;
  }
  nByte = GetFullPathNameW((WCHAR*)zConverted, 0, 0, 0) + 3;
  zTemp = (WCHAR *)HeapAlloc(GetProcessHeap(),0,nByte*sizeof(zTemp[0]) );
  if( zTemp==0 ){
	  HeapFree(GetProcessHeap(),0,zConverted);
	  return UNQLITE_NOMEM;
  }
  GetFullPathNameW((WCHAR*)zConverted, nByte, zTemp, 0);
  HeapFree(GetProcessHeap(),0,zConverted);
  zOut = unicodeToUtf8(zTemp);
  HeapFree(GetProcessHeap(),0,zTemp);
  if( zOut == 0 ){
    return UNQLITE_NOMEM;
  }
  Systrcpy(zFull,(sxu32)pVfs->mxPathname,zOut,0);
  HeapFree(GetProcessHeap(),0,zOut);
  return UNQLITE_OK;
}
/*
** Get the sector size of the device used to store
** file.
*/
static int getSectorSize(
    unqlite_vfs *pVfs,
    const char *zRelative     /* UTF-8 file name */
){
  DWORD bytesPerSector = UNQLITE_DEFAULT_SECTOR_SIZE;
  char zFullpath[MAX_PATH+1];
  int rc;
  DWORD dwRet = 0;
  DWORD dwDummy;
  /*
  ** We need to get the full path name of the file
  ** to get the drive letter to look up the sector
  ** size.
  */
  rc = winFullPathname(pVfs, zRelative, MAX_PATH, zFullpath);
  if( rc == UNQLITE_OK )
  {
    void *zConverted = convertUtf8Filename(zFullpath);
    if( zConverted ){
        /* trim path to just drive reference */
        WCHAR *p = (WCHAR *)zConverted;
        for(;*p;p++){
          if( *p == '\\' ){
            *p = '\0';
            break;
          }
        }
        dwRet = GetDiskFreeSpaceW((WCHAR*)zConverted,
                                  &dwDummy,
                                  &bytesPerSector,
                                  &dwDummy,
                                  &dwDummy);
		 HeapFree(GetProcessHeap(),0,zConverted);
	}
    if( !dwRet ){
      bytesPerSector = UNQLITE_DEFAULT_SECTOR_SIZE;
    }
  }
  return (int) bytesPerSector; 
}
/*
** Sleep for a little while.  Return the amount of time slept.
*/
static int winSleep(unqlite_vfs *pVfs, int microsec){
  Sleep((microsec+999)/1000);
  SXUNUSED(pVfs);
  return ((microsec+999)/1000)*1000;
}
/*
 * Export the current system time.
 */
static int winCurrentTime(unqlite_vfs *pVfs,Sytm *pOut)
{
	SYSTEMTIME sSys;
	SXUNUSED(pVfs);
	GetSystemTime(&sSys);
	SYSTEMTIME_TO_SYTM(&sSys,pOut);
	return UNQLITE_OK;
}
/*
** The idea is that this function works like a combination of
** GetLastError() and FormatMessage() on windows (or errno and
** strerror_r() on unix). After an error is returned by an OS
** function, UnQLite calls this function with zBuf pointing to
** a buffer of nBuf bytes. The OS layer should populate the
** buffer with a nul-terminated UTF-8 encoded error message
** describing the last IO error to have occurred within the calling
** thread.
**
** If the error message is too large for the supplied buffer,
** it should be truncated. The return value of xGetLastError
** is zero if the error message fits in the buffer, or non-zero
** otherwise (if the message was truncated). If non-zero is returned,
** then it is not necessary to include the nul-terminator character
** in the output buffer.
*/
static int winGetLastError(unqlite_vfs *pVfs, int nBuf, char *zBuf)
{
  /* FormatMessage returns 0 on failure.  Otherwise it
  ** returns the number of TCHARs written to the output
  ** buffer, excluding the terminating null char.
  */
  DWORD error = GetLastError();
  WCHAR *zTempWide = 0;
  DWORD dwLen;
  char *zOut = 0;

  SXUNUSED(pVfs);
  dwLen = FormatMessageW(
	  FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	  0,
	  error,
	  0,
	  (LPWSTR) &zTempWide,
	  0,
	  0
	  );
    if( dwLen > 0 ){
      /* allocate a buffer and convert to UTF8 */
      zOut = unicodeToUtf8(zTempWide);
      /* free the system buffer allocated by FormatMessage */
      LocalFree(zTempWide);
    }
	if( 0 == dwLen ){
		Systrcpy(zBuf,(sxu32)nBuf,"OS Error",sizeof("OS Error")-1);
	}else{
		/* copy a maximum of nBuf chars to output buffer */
		Systrcpy(zBuf,(sxu32)nBuf,zOut,0 /* Compute input length automatically */);
		/* free the UTF8 buffer */
		HeapFree(GetProcessHeap(),0,zOut);
	}
  return 0;
}
/*
** Open a file.
*/
static int winOpen(
  unqlite_vfs *pVfs,        /* Not used */
  const char *zName,        /* Name of the file (UTF-8) */
  unqlite_file *id,         /* Write the UnQLite file handle here */
  unsigned int flags                /* Open mode flags */
){
  HANDLE h;
  DWORD dwDesiredAccess;
  DWORD dwShareMode;
  DWORD dwCreationDisposition;
  DWORD dwFlagsAndAttributes = 0;
  winFile *pFile = (winFile*)id;
  void *zConverted;              /* Filename in OS encoding */
  const char *zUtf8Name = zName; /* Filename in UTF-8 encoding */
  int isExclusive  = (flags & UNQLITE_OPEN_EXCLUSIVE);
  int isDelete     = (flags & UNQLITE_OPEN_TEMP_DB);
  int isCreate     = (flags & UNQLITE_OPEN_CREATE);
  int isReadWrite  = (flags & UNQLITE_OPEN_READWRITE);

  pFile->h = INVALID_HANDLE_VALUE;
  /* Convert the filename to the system encoding. */
  zConverted = convertUtf8Filename(zUtf8Name);
  if( zConverted==0 ){
    return UNQLITE_NOMEM;
  }
  if( isReadWrite ){
    dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
  }else{
    dwDesiredAccess = GENERIC_READ;
  }
  /* UNQLITE_OPEN_EXCLUSIVE is used to make sure that a new file is 
  ** created.
  */
  if( isExclusive ){
    /* Creates a new file, only if it does not already exist. */
    /* If the file exists, it fails. */
    dwCreationDisposition = CREATE_NEW;
  }else if( isCreate ){
    /* Open existing file, or create if it doesn't exist */
    dwCreationDisposition = OPEN_ALWAYS;
  }else{
    /* Opens a file, only if it exists. */
    dwCreationDisposition = OPEN_EXISTING;
  }

  dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;

  if( isDelete ){
    dwFlagsAndAttributes = FILE_ATTRIBUTE_TEMPORARY
                               | FILE_ATTRIBUTE_HIDDEN
                               | FILE_FLAG_DELETE_ON_CLOSE;
  }else{
    dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
  }
  h = CreateFileW((WCHAR*)zConverted,
       dwDesiredAccess,
       dwShareMode,
       NULL,
       dwCreationDisposition,
       dwFlagsAndAttributes,
       NULL
    );
  if( h==INVALID_HANDLE_VALUE ){
    pFile->lastErrno = GetLastError();
    HeapFree(GetProcessHeap(),0,zConverted);
	return UNQLITE_IOERR;
  }
  SyZero(pFile,sizeof(*pFile));
  pFile->pMethod = &winIoMethod;
  pFile->h = h;
  pFile->lastErrno = NO_ERROR;
  pFile->pVfs = pVfs;
  pFile->sectorSize = getSectorSize(pVfs, zUtf8Name);
  HeapFree(GetProcessHeap(),0,zConverted);
  return UNQLITE_OK;
}
/*
 * Export the Windows Vfs.
 */
UNQLITE_PRIVATE const unqlite_vfs * unqliteExportBuiltinVfs(void)
{
	static const unqlite_vfs sWinvfs = {
		"Windows",           /* Vfs name */
		1,                   /* Vfs structure version */
		sizeof(winFile),     /* szOsFile */
		MAX_PATH,            /* mxPathName */
		winOpen,             /* xOpen */
		winDelete,           /* xDelete */
		winAccess,           /* xAccess */
		winFullPathname,     /* xFullPathname */
		0,                   /* xTmp */
		winSleep,            /* xSleep */
		winCurrentTime,      /* xCurrentTime */
		winGetLastError,     /* xGetLastError */
	};
	return &sWinvfs;
}

void * unqlite_malloc(unsigned int nByte)
{
    return malloc(nByte);
}

void unqlite_free(void *p)
{
    free(p);
}

#endif /* __WINNT__ */
