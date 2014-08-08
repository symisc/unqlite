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
 /* $SymiscID: os_unix.c v1.3 FreeBSD 2013-04-05 01:10 devel <chm@symisc.net> $ */
#ifndef UNQLITE_AMALGAMATION
#include "unqliteInt.h"
#endif
/* 
 * Omit the whole layer from the build if compiling for platforms other than Unix (Linux, BSD, Solaris, OS X, etc.).
 * Note: Mostly SQLite3 source tree.
 */
#if defined(__UNIXES__)
/** This file contains the VFS implementation for unix-like operating systems
** include Linux, MacOSX, *BSD, QNX, VxWorks, AIX, HPUX, and others.
**
** There are actually several different VFS implementations in this file.
** The differences are in the way that file locking is done.  The default
** implementation uses Posix Advisory Locks.  Alternative implementations
** use flock(), dot-files, various proprietary locking schemas, or simply
** skip locking all together.
**
** This source file is organized into divisions where the logic for various
** subfunctions is contained within the appropriate division.  PLEASE
** KEEP THE STRUCTURE OF THIS FILE INTACT.  New code should be placed
** in the correct division and should be clearly labeled.
**
*/
/*
** standard include files.
*/
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#if defined(__APPLE__) 
# include <sys/mount.h>
#endif
/*
** Allowed values of unixFile.fsFlags
*/
#define UNQLITE_FSFLAGS_IS_MSDOS     0x1

/*
** Default permissions when creating a new file
*/
#ifndef UNQLITE_DEFAULT_FILE_PERMISSIONS
# define UNQLITE_DEFAULT_FILE_PERMISSIONS 0644
#endif
/*
 ** Default permissions when creating auto proxy dir
 */
#ifndef UNQLITE_DEFAULT_PROXYDIR_PERMISSIONS
# define UNQLITE_DEFAULT_PROXYDIR_PERMISSIONS 0755
#endif
/*
** Maximum supported path-length.
*/
#define MAX_PATHNAME 512
/*
** Only set the lastErrno if the error code is a real error and not 
** a normal expected return code of UNQLITE_BUSY or UNQLITE_OK
*/
#define IS_LOCK_ERROR(x)  ((x != UNQLITE_OK) && (x != UNQLITE_BUSY))
/* Forward references */
typedef struct unixInodeInfo unixInodeInfo;   /* An i-node */
typedef struct UnixUnusedFd UnixUnusedFd;     /* An unused file descriptor */
/*
** Sometimes, after a file handle is closed by SQLite, the file descriptor
** cannot be closed immediately. In these cases, instances of the following
** structure are used to store the file descriptor while waiting for an
** opportunity to either close or reuse it.
*/
struct UnixUnusedFd {
  int fd;                   /* File descriptor to close */
  int flags;                /* Flags this file descriptor was opened with */
  UnixUnusedFd *pNext;      /* Next unused file descriptor on same file */
};
/*
** The unixFile structure is subclass of unqlite3_file specific to the unix
** VFS implementations.
*/
typedef struct unixFile unixFile;
struct unixFile {
  const unqlite_io_methods *pMethod;  /* Always the first entry */
  unixInodeInfo *pInode;              /* Info about locks on this inode */
  int h;                              /* The file descriptor */
  int dirfd;                          /* File descriptor for the directory */
  unsigned char eFileLock;            /* The type of lock held on this fd */
  int lastErrno;                      /* The unix errno from last I/O error */
  void *lockingContext;               /* Locking style specific state */
  UnixUnusedFd *pUnused;              /* Pre-allocated UnixUnusedFd */
  int fileFlags;                      /* Miscellanous flags */
  const char *zPath;                  /* Name of the file */
  unsigned fsFlags;                   /* cached details from statfs() */
};
/*
** The following macros define bits in unixFile.fileFlags
*/
#define UNQLITE_WHOLE_FILE_LOCKING  0x0001   /* Use whole-file locking */
/*
** Define various macros that are missing from some systems.
*/
#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif
#ifndef O_NOFOLLOW
# define O_NOFOLLOW 0
#endif
#ifndef O_BINARY
# define O_BINARY 0
#endif
/*
** Helper functions to obtain and relinquish the global mutex. The
** global mutex is used to protect the unixInodeInfo and
** vxworksFileId objects used by this file, all of which may be 
** shared by multiple threads.
**
** Function unixMutexHeld() is used to assert() that the global mutex 
** is held when required. This function is only used as part of assert() 
** statements. e.g.
**
**   unixEnterMutex()
**     assert( unixMutexHeld() );
**   unixEnterLeave()
*/
static void unixEnterMutex(void){
#ifdef UNQLITE_ENABLE_THREADS
	const SyMutexMethods *pMutexMethods = SyMutexExportMethods();
	if( pMutexMethods ){
		SyMutex *pMutex = pMutexMethods->xNew(SXMUTEX_TYPE_STATIC_2); /* pre-allocated, never fail */
		SyMutexEnter(pMutexMethods,pMutex);
	}
#endif /* UNQLITE_ENABLE_THREADS */
}
static void unixLeaveMutex(void){
#ifdef UNQLITE_ENABLE_THREADS
  const SyMutexMethods *pMutexMethods = SyMutexExportMethods();
  if( pMutexMethods ){
	 SyMutex *pMutex = pMutexMethods->xNew(SXMUTEX_TYPE_STATIC_2); /* pre-allocated, never fail */
	 SyMutexLeave(pMutexMethods,pMutex);
  }
#endif /* UNQLITE_ENABLE_THREADS */
}
/*
** This routine translates a standard POSIX errno code into something
** useful to the clients of the unqlite3 functions.  Specifically, it is
** intended to translate a variety of "try again" errors into UNQLITE_BUSY
** and a variety of "please close the file descriptor NOW" errors into 
** UNQLITE_IOERR
** 
** Errors during initialization of locks, or file system support for locks,
** should handle ENOLCK, ENOTSUP, EOPNOTSUPP separately.
*/
static int unqliteErrorFromPosixError(int posixError, int unqliteIOErr) {
  switch (posixError) {
  case 0: 
    return UNQLITE_OK;
    
  case EAGAIN:
  case ETIMEDOUT:
  case EBUSY:
  case EINTR:
  case ENOLCK:  
    /* random NFS retry error, unless during file system support 
     * introspection, in which it actually means what it says */
    return UNQLITE_BUSY;
 
  case EACCES: 
    /* EACCES is like EAGAIN during locking operations, but not any other time*/
      return UNQLITE_BUSY;
    
  case EPERM: 
    return UNQLITE_PERM;
    
  case EDEADLK:
    return UNQLITE_IOERR;
    
#if EOPNOTSUPP!=ENOTSUP
  case EOPNOTSUPP: 
    /* something went terribly awry, unless during file system support 
     * introspection, in which it actually means what it says */
#endif
#ifdef ENOTSUP
  case ENOTSUP: 
    /* invalid fd, unless during file system support introspection, in which 
     * it actually means what it says */
#endif
  case EIO:
  case EBADF:
  case EINVAL:
  case ENOTCONN:
  case ENODEV:
  case ENXIO:
  case ENOENT:
  case ESTALE:
  case ENOSYS:
    /* these should force the client to close the file and reconnect */
    
  default: 
    return unqliteIOErr;
  }
}
/******************************************************************************
*************************** Posix Advisory Locking ****************************
**
** POSIX advisory locks are broken by design.  ANSI STD 1003.1 (1996)
** section 6.5.2.2 lines 483 through 490 specify that when a process
** sets or clears a lock, that operation overrides any prior locks set
** by the same process.  It does not explicitly say so, but this implies
** that it overrides locks set by the same process using a different
** file descriptor.  Consider this test case:
**
**       int fd1 = open("./file1", O_RDWR|O_CREAT, 0644);
**       int fd2 = open("./file2", O_RDWR|O_CREAT, 0644);
**
** Suppose ./file1 and ./file2 are really the same file (because
** one is a hard or symbolic link to the other) then if you set
** an exclusive lock on fd1, then try to get an exclusive lock
** on fd2, it works.  I would have expected the second lock to
** fail since there was already a lock on the file due to fd1.
** But not so.  Since both locks came from the same process, the
** second overrides the first, even though they were on different
** file descriptors opened on different file names.
**
** This means that we cannot use POSIX locks to synchronize file access
** among competing threads of the same process.  POSIX locks will work fine
** to synchronize access for threads in separate processes, but not
** threads within the same process.
**
** To work around the problem, SQLite has to manage file locks internally
** on its own.  Whenever a new database is opened, we have to find the
** specific inode of the database file (the inode is determined by the
** st_dev and st_ino fields of the stat structure that fstat() fills in)
** and check for locks already existing on that inode.  When locks are
** created or removed, we have to look at our own internal record of the
** locks to see if another thread has previously set a lock on that same
** inode.
**
** (Aside: The use of inode numbers as unique IDs does not work on VxWorks.
** For VxWorks, we have to use the alternative unique ID system based on
** canonical filename and implemented in the previous division.)
**
** There is one locking structure
** per inode, so if the same inode is opened twice, both unixFile structures
** point to the same locking structure.  The locking structure keeps
** a reference count (so we will know when to delete it) and a "cnt"
** field that tells us its internal lock status.  cnt==0 means the
** file is unlocked.  cnt==-1 means the file has an exclusive lock.
** cnt>0 means there are cnt shared locks on the file.
**
** Any attempt to lock or unlock a file first checks the locking
** structure.  The fcntl() system call is only invoked to set a 
** POSIX lock if the internal lock structure transitions between
** a locked and an unlocked state.
**
** But wait:  there are yet more problems with POSIX advisory locks.
**
** If you close a file descriptor that points to a file that has locks,
** all locks on that file that are owned by the current process are
** released.  To work around this problem, each unixInodeInfo object
** maintains a count of the number of pending locks on that inode.
** When an attempt is made to close an unixFile, if there are
** other unixFile open on the same inode that are holding locks, the call
** to close() the file descriptor is deferred until all of the locks clear.
** The unixInodeInfo structure keeps a list of file descriptors that need to
** be closed and that list is walked (and cleared) when the last lock
** clears.
**
** Yet another problem:  LinuxThreads do not play well with posix locks.
**
** Many older versions of linux use the LinuxThreads library which is
** not posix compliant.  Under LinuxThreads, a lock created by thread
** A cannot be modified or overridden by a different thread B.
** Only thread A can modify the lock.  Locking behavior is correct
** if the appliation uses the newer Native Posix Thread Library (NPTL)
** on linux - with NPTL a lock created by thread A can override locks
** in thread B.  But there is no way to know at compile-time which
** threading library is being used.  So there is no way to know at
** compile-time whether or not thread A can override locks on thread B.
** One has to do a run-time check to discover the behavior of the
** current process.
**
*/

/*
** An instance of the following structure serves as the key used
** to locate a particular unixInodeInfo object.
*/
struct unixFileId {
  dev_t dev;                  /* Device number */
  ino_t ino;                  /* Inode number */
};
/*
** An instance of the following structure is allocated for each open
** inode.  Or, on LinuxThreads, there is one of these structures for
** each inode opened by each thread.
**
** A single inode can have multiple file descriptors, so each unixFile
** structure contains a pointer to an instance of this object and this
** object keeps a count of the number of unixFile pointing to it.
*/
struct unixInodeInfo {
  struct unixFileId fileId;       /* The lookup key */
  int nShared;                    /* Number of SHARED locks held */
  int eFileLock;                  /* One of SHARED_LOCK, RESERVED_LOCK etc. */
  int nRef;                       /* Number of pointers to this structure */
  int nLock;                      /* Number of outstanding file locks */
  UnixUnusedFd *pUnused;          /* Unused file descriptors to close */
  unixInodeInfo *pNext;           /* List of all unixInodeInfo objects */
  unixInodeInfo *pPrev;           /*    .... doubly linked */
};

static unixInodeInfo *inodeList = 0;
/*
 * Local memory allocation stuff.
 */
static void * unqlite_malloc(sxu32 nByte)
{
	SyMemBackend *pAlloc;
	void *p;
	pAlloc = (SyMemBackend *)unqliteExportMemBackend();
	p = SyMemBackendAlloc(pAlloc,nByte);
	return p;
}
static void unqlite_free(void *p)
{
	SyMemBackend *pAlloc;
	pAlloc = (SyMemBackend *)unqliteExportMemBackend();
	SyMemBackendFree(pAlloc,p);
}
/*
** Close all file descriptors accumuated in the unixInodeInfo->pUnused list.
** If all such file descriptors are closed without error, the list is
** cleared and UNQLITE_OK returned.
**
** Otherwise, if an error occurs, then successfully closed file descriptor
** entries are removed from the list, and UNQLITE_IOERR_CLOSE returned. 
** not deleted and UNQLITE_IOERR_CLOSE returned.
*/ 
static int closePendingFds(unixFile *pFile){
  int rc = UNQLITE_OK;
  unixInodeInfo *pInode = pFile->pInode;
  UnixUnusedFd *pError = 0;
  UnixUnusedFd *p;
  UnixUnusedFd *pNext;
  for(p=pInode->pUnused; p; p=pNext){
    pNext = p->pNext;
    if( close(p->fd) ){
      pFile->lastErrno = errno;
	  rc = UNQLITE_IOERR;
      p->pNext = pError;
      pError = p;
    }else{
      unqlite_free(p);
    }
  }
  pInode->pUnused = pError;
  return rc;
}
/*
** Release a unixInodeInfo structure previously allocated by findInodeInfo().
**
** The mutex entered using the unixEnterMutex() function must be held
** when this function is called.
*/
static void releaseInodeInfo(unixFile *pFile){
  unixInodeInfo *pInode = pFile->pInode;
  if( pInode ){
    pInode->nRef--;
    if( pInode->nRef==0 ){
      closePendingFds(pFile);
      if( pInode->pPrev ){
        pInode->pPrev->pNext = pInode->pNext;
      }else{
        inodeList = pInode->pNext;
      }
      if( pInode->pNext ){
        pInode->pNext->pPrev = pInode->pPrev;
      }
      unqlite_free(pInode);
    }
  }
}
/*
** Given a file descriptor, locate the unixInodeInfo object that
** describes that file descriptor.  Create a new one if necessary.  The
** return value might be uninitialized if an error occurs.
**
** The mutex entered using the unixEnterMutex() function must be held
** when this function is called.
**
** Return an appropriate error code.
*/
static int findInodeInfo(
  unixFile *pFile,               /* Unix file with file desc used in the key */
  unixInodeInfo **ppInode        /* Return the unixInodeInfo object here */
){
  int rc;                        /* System call return code */
  int fd;                        /* The file descriptor for pFile */
  struct unixFileId fileId;      /* Lookup key for the unixInodeInfo */
  struct stat statbuf;           /* Low-level file information */
  unixInodeInfo *pInode = 0;     /* Candidate unixInodeInfo object */

  /* Get low-level information about the file that we can used to
  ** create a unique name for the file.
  */
  fd = pFile->h;
  rc = fstat(fd, &statbuf);
  if( rc!=0 ){
    pFile->lastErrno = errno;
#ifdef EOVERFLOW
	if( pFile->lastErrno==EOVERFLOW ) return UNQLITE_NOTIMPLEMENTED;
#endif
    return UNQLITE_IOERR;
  }

#ifdef __APPLE__
  /* On OS X on an msdos filesystem, the inode number is reported
  ** incorrectly for zero-size files.  See ticket #3260.  To work
  ** around this problem (we consider it a bug in OS X, not SQLite)
  ** we always increase the file size to 1 by writing a single byte
  ** prior to accessing the inode number.  The one byte written is
  ** an ASCII 'S' character which also happens to be the first byte
  ** in the header of every SQLite database.  In this way, if there
  ** is a race condition such that another thread has already populated
  ** the first page of the database, no damage is done.
  */
  if( statbuf.st_size==0 && (pFile->fsFlags & UNQLITE_FSFLAGS_IS_MSDOS)!=0 ){
    rc = write(fd, "S", 1);
    if( rc!=1 ){
      pFile->lastErrno = errno;
      return UNQLITE_IOERR;
    }
    rc = fstat(fd, &statbuf);
    if( rc!=0 ){
      pFile->lastErrno = errno;
      return UNQLITE_IOERR;
    }
  }
#endif
  SyZero(&fileId,sizeof(fileId));
  fileId.dev = statbuf.st_dev;
  fileId.ino = statbuf.st_ino;
  pInode = inodeList;
  while( pInode && SyMemcmp((const void *)&fileId,(const void *)&pInode->fileId, sizeof(fileId)) ){
    pInode = pInode->pNext;
  }
  if( pInode==0 ){
    pInode = (unixInodeInfo *)unqlite_malloc( sizeof(*pInode) );
    if( pInode==0 ){
      return UNQLITE_NOMEM;
    }
    SyZero(pInode,sizeof(*pInode));
	SyMemcpy((const void *)&fileId,(void *)&pInode->fileId,sizeof(fileId));
    pInode->nRef = 1;
    pInode->pNext = inodeList;
    pInode->pPrev = 0;
    if( inodeList ) inodeList->pPrev = pInode;
    inodeList = pInode;
  }else{
    pInode->nRef++;
  }
  *ppInode = pInode;
  return UNQLITE_OK;
}
/*
** This routine checks if there is a RESERVED lock held on the specified
** file by this or any other process. If such a lock is held, set *pResOut
** to a non-zero value otherwise *pResOut is set to zero.  The return value
** is set to UNQLITE_OK unless an I/O error occurs during lock checking.
*/
static int unixCheckReservedLock(unqlite_file *id, int *pResOut){
  int rc = UNQLITE_OK;
  int reserved = 0;
  unixFile *pFile = (unixFile*)id;

 
  unixEnterMutex(); /* Because pFile->pInode is shared across threads */

  /* Check if a thread in this process holds such a lock */
  if( pFile->pInode->eFileLock>SHARED_LOCK ){
    reserved = 1;
  }

  /* Otherwise see if some other process holds it.
  */
  if( !reserved ){
    struct flock lock;
    lock.l_whence = SEEK_SET;
    lock.l_start = RESERVED_BYTE;
    lock.l_len = 1;
    lock.l_type = F_WRLCK;
    if (-1 == fcntl(pFile->h, F_GETLK, &lock)) {
      int tErrno = errno;
	  rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR);
      pFile->lastErrno = tErrno;
    } else if( lock.l_type!=F_UNLCK ){
      reserved = 1;
    }
  }
  
  unixLeaveMutex();
 
  *pResOut = reserved;
  return rc;
}
/*
** Lock the file with the lock specified by parameter eFileLock - one
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
** This routine will only increase a lock.  Use the unqliteOsUnlock()
** routine to lower a locking level.
*/
static int unixLock(unqlite_file *id, int eFileLock){
  /* The following describes the implementation of the various locks and
  ** lock transitions in terms of the POSIX advisory shared and exclusive
  ** lock primitives (called read-locks and write-locks below, to avoid
  ** confusion with SQLite lock names). The algorithms are complicated
  ** slightly in order to be compatible with unixdows systems simultaneously
  ** accessing the same database file, in case that is ever required.
  **
  ** Symbols defined in os.h indentify the 'pending byte' and the 'reserved
  ** byte', each single bytes at well known offsets, and the 'shared byte
  ** range', a range of 510 bytes at a well known offset.
  **
  ** To obtain a SHARED lock, a read-lock is obtained on the 'pending
  ** byte'.  If this is successful, a random byte from the 'shared byte
  ** range' is read-locked and the lock on the 'pending byte' released.
  **
  ** A process may only obtain a RESERVED lock after it has a SHARED lock.
  ** A RESERVED lock is implemented by grabbing a write-lock on the
  ** 'reserved byte'. 
  **
  ** A process may only obtain a PENDING lock after it has obtained a
  ** SHARED lock. A PENDING lock is implemented by obtaining a write-lock
  ** on the 'pending byte'. This ensures that no new SHARED locks can be
  ** obtained, but existing SHARED locks are allowed to persist. A process
  ** does not have to obtain a RESERVED lock on the way to a PENDING lock.
  ** This property is used by the algorithm for rolling back a journal file
  ** after a crash.
  **
  ** An EXCLUSIVE lock, obtained after a PENDING lock is held, is
  ** implemented by obtaining a write-lock on the entire 'shared byte
  ** range'. Since all other locks require a read-lock on one of the bytes
  ** within this range, this ensures that no other locks are held on the
  ** database. 
  **
  ** The reason a single byte cannot be used instead of the 'shared byte
  ** range' is that some versions of unixdows do not support read-locks. By
  ** locking a random byte from a range, concurrent SHARED locks may exist
  ** even if the locking primitive used is always a write-lock.
  */
  int rc = UNQLITE_OK;
  unixFile *pFile = (unixFile*)id;
  unixInodeInfo *pInode = pFile->pInode;
  struct flock lock;
  int s = 0;
  int tErrno = 0;

  /* If there is already a lock of this type or more restrictive on the
  ** unixFile, do nothing. Don't use the end_lock: exit path, as
  ** unixEnterMutex() hasn't been called yet.
  */
  if( pFile->eFileLock>=eFileLock ){
    return UNQLITE_OK;
  }
  /* This mutex is needed because pFile->pInode is shared across threads
  */
  unixEnterMutex();
  pInode = pFile->pInode;

  /* If some thread using this PID has a lock via a different unixFile*
  ** handle that precludes the requested lock, return BUSY.
  */
  if( (pFile->eFileLock!=pInode->eFileLock && 
          (pInode->eFileLock>=PENDING_LOCK || eFileLock>SHARED_LOCK))
  ){
    rc = UNQLITE_BUSY;
    goto end_lock;
  }

  /* If a SHARED lock is requested, and some thread using this PID already
  ** has a SHARED or RESERVED lock, then increment reference counts and
  ** return UNQLITE_OK.
  */
  if( eFileLock==SHARED_LOCK && 
      (pInode->eFileLock==SHARED_LOCK || pInode->eFileLock==RESERVED_LOCK) ){
    pFile->eFileLock = SHARED_LOCK;
    pInode->nShared++;
    pInode->nLock++;
    goto end_lock;
  }
  /* A PENDING lock is needed before acquiring a SHARED lock and before
  ** acquiring an EXCLUSIVE lock.  For the SHARED lock, the PENDING will
  ** be released.
  */
  lock.l_len = 1L;
  lock.l_whence = SEEK_SET;
  if( eFileLock==SHARED_LOCK 
      || (eFileLock==EXCLUSIVE_LOCK && pFile->eFileLock<PENDING_LOCK)
  ){
    lock.l_type = (eFileLock==SHARED_LOCK?F_RDLCK:F_WRLCK);
    lock.l_start = PENDING_BYTE;
    s = fcntl(pFile->h, F_SETLK, &lock);
    if( s==(-1) ){
      tErrno = errno;
      rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR);
      if( IS_LOCK_ERROR(rc) ){
        pFile->lastErrno = tErrno;
      }
      goto end_lock;
    }
  }
  /* If control gets to this point, then actually go ahead and make
  ** operating system calls for the specified lock.
  */
  if( eFileLock==SHARED_LOCK ){
    /* Now get the read-lock */
    lock.l_start = SHARED_FIRST;
    lock.l_len = SHARED_SIZE;
    if( (s = fcntl(pFile->h, F_SETLK, &lock))==(-1) ){
      tErrno = errno;
    }
    /* Drop the temporary PENDING lock */
    lock.l_start = PENDING_BYTE;
    lock.l_len = 1L;
    lock.l_type = F_UNLCK;
    if( fcntl(pFile->h, F_SETLK, &lock)!=0 ){
      if( s != -1 ){
        /* This could happen with a network mount */
        tErrno = errno; 
        rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR); 
        if( IS_LOCK_ERROR(rc) ){
          pFile->lastErrno = tErrno;
        }
        goto end_lock;
      }
    }
    if( s==(-1) ){
		rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR);
      if( IS_LOCK_ERROR(rc) ){
        pFile->lastErrno = tErrno;
      }
    }else{
      pFile->eFileLock = SHARED_LOCK;
      pInode->nLock++;
      pInode->nShared = 1;
    }
  }else if( eFileLock==EXCLUSIVE_LOCK && pInode->nShared>1 ){
    /* We are trying for an exclusive lock but another thread in this
    ** same process is still holding a shared lock. */
    rc = UNQLITE_BUSY;
  }else{
    /* The request was for a RESERVED or EXCLUSIVE lock.  It is
    ** assumed that there is a SHARED or greater lock on the file
    ** already.
    */
    lock.l_type = F_WRLCK;
    switch( eFileLock ){
      case RESERVED_LOCK:
        lock.l_start = RESERVED_BYTE;
        break;
      case EXCLUSIVE_LOCK:
        lock.l_start = SHARED_FIRST;
        lock.l_len = SHARED_SIZE;
        break;
      default:
		  /* Can't happen */
        break;
    }
    s = fcntl(pFile->h, F_SETLK, &lock);
    if( s==(-1) ){
      tErrno = errno;
      rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR);
      if( IS_LOCK_ERROR(rc) ){
        pFile->lastErrno = tErrno;
      }
    }
  }
  if( rc==UNQLITE_OK ){
    pFile->eFileLock = eFileLock;
    pInode->eFileLock = eFileLock;
  }else if( eFileLock==EXCLUSIVE_LOCK ){
    pFile->eFileLock = PENDING_LOCK;
    pInode->eFileLock = PENDING_LOCK;
  }
end_lock:
  unixLeaveMutex();
  return rc;
}
/*
** Add the file descriptor used by file handle pFile to the corresponding
** pUnused list.
*/
static void setPendingFd(unixFile *pFile){
  unixInodeInfo *pInode = pFile->pInode;
  UnixUnusedFd *p = pFile->pUnused;
  p->pNext = pInode->pUnused;
  pInode->pUnused = p;
  pFile->h = -1;
  pFile->pUnused = 0;
}
/*
** Lower the locking level on file descriptor pFile to eFileLock.  eFileLock
** must be either NO_LOCK or SHARED_LOCK.
**
** If the locking level of the file descriptor is already at or below
** the requested locking level, this routine is a no-op.
** 
** If handleNFSUnlock is true, then on downgrading an EXCLUSIVE_LOCK to SHARED
** the byte range is divided into 2 parts and the first part is unlocked then
** set to a read lock, then the other part is simply unlocked.  This works 
** around a bug in BSD NFS lockd (also seen on MacOSX 10.3+) that fails to 
** remove the write lock on a region when a read lock is set.
*/
static int _posixUnlock(unqlite_file *id, int eFileLock, int handleNFSUnlock){
  unixFile *pFile = (unixFile*)id;
  unixInodeInfo *pInode;
  struct flock lock;
  int rc = UNQLITE_OK;
  int h;
  int tErrno;                      /* Error code from system call errors */

   if( pFile->eFileLock<=eFileLock ){
    return UNQLITE_OK;
  }
  unixEnterMutex();
  
  h = pFile->h;
  pInode = pFile->pInode;
  
  if( pFile->eFileLock>SHARED_LOCK ){
    /* downgrading to a shared lock on NFS involves clearing the write lock
    ** before establishing the readlock - to avoid a race condition we downgrade
    ** the lock in 2 blocks, so that part of the range will be covered by a 
    ** write lock until the rest is covered by a read lock:
    **  1:   [WWWWW]
    **  2:   [....W]
    **  3:   [RRRRW]
    **  4:   [RRRR.]
    */
    if( eFileLock==SHARED_LOCK ){
      if( handleNFSUnlock ){
        off_t divSize = SHARED_SIZE - 1;
        
        lock.l_type = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = SHARED_FIRST;
        lock.l_len = divSize;
        if( fcntl(h, F_SETLK, &lock)==(-1) ){
          tErrno = errno;
		  rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR);
          if( IS_LOCK_ERROR(rc) ){
            pFile->lastErrno = tErrno;
          }
          goto end_unlock;
        }
        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = SHARED_FIRST;
        lock.l_len = divSize;
        if( fcntl(h, F_SETLK, &lock)==(-1) ){
          tErrno = errno;
		  rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR);
          if( IS_LOCK_ERROR(rc) ){
            pFile->lastErrno = tErrno;
          }
          goto end_unlock;
        }
        lock.l_type = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = SHARED_FIRST+divSize;
        lock.l_len = SHARED_SIZE-divSize;
        if( fcntl(h, F_SETLK, &lock)==(-1) ){
          tErrno = errno;
		  rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR);
          if( IS_LOCK_ERROR(rc) ){
            pFile->lastErrno = tErrno;
          }
          goto end_unlock;
        }
      }else{
        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = SHARED_FIRST;
        lock.l_len = SHARED_SIZE;
        if( fcntl(h, F_SETLK, &lock)==(-1) ){
          tErrno = errno;
		  rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR);
          if( IS_LOCK_ERROR(rc) ){
            pFile->lastErrno = tErrno;
          }
          goto end_unlock;
        }
      }
    }
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = PENDING_BYTE;
    lock.l_len = 2L;
    if( fcntl(h, F_SETLK, &lock)!=(-1) ){
      pInode->eFileLock = SHARED_LOCK;
    }else{
      tErrno = errno;
	  rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR);
      if( IS_LOCK_ERROR(rc) ){
        pFile->lastErrno = tErrno;
      }
      goto end_unlock;
    }
  }
  if( eFileLock==NO_LOCK ){
    /* Decrement the shared lock counter.  Release the lock using an
    ** OS call only when all threads in this same process have released
    ** the lock.
    */
    pInode->nShared--;
    if( pInode->nShared==0 ){
      lock.l_type = F_UNLCK;
      lock.l_whence = SEEK_SET;
      lock.l_start = lock.l_len = 0L;
      
      if( fcntl(h, F_SETLK, &lock)!=(-1) ){
        pInode->eFileLock = NO_LOCK;
      }else{
        tErrno = errno;
		rc = unqliteErrorFromPosixError(tErrno, UNQLITE_LOCKERR);
        if( IS_LOCK_ERROR(rc) ){
          pFile->lastErrno = tErrno;
        }
        pInode->eFileLock = NO_LOCK;
        pFile->eFileLock = NO_LOCK;
      }
    }

    /* Decrement the count of locks against this same file.  When the
    ** count reaches zero, close any other file descriptors whose close
    ** was deferred because of outstanding locks.
    */
    pInode->nLock--;
 
    if( pInode->nLock==0 ){
      int rc2 = closePendingFds(pFile);
      if( rc==UNQLITE_OK ){
        rc = rc2;
      }
    }
  }
	
end_unlock:

  unixLeaveMutex();
  
  if( rc==UNQLITE_OK ) pFile->eFileLock = eFileLock;
  return rc;
}
/*
** Lower the locking level on file descriptor pFile to eFileLock.  eFileLock
** must be either NO_LOCK or SHARED_LOCK.
**
** If the locking level of the file descriptor is already at or below
** the requested locking level, this routine is a no-op.
*/
static int unixUnlock(unqlite_file *id, int eFileLock){
  return _posixUnlock(id, eFileLock, 0);
}
/*
** This function performs the parts of the "close file" operation 
** common to all locking schemes. It closes the directory and file
** handles, if they are valid, and sets all fields of the unixFile
** structure to 0.
**
*/
static int closeUnixFile(unqlite_file *id){
  unixFile *pFile = (unixFile*)id;
  if( pFile ){
    if( pFile->dirfd>=0 ){
      int err = close(pFile->dirfd);
      if( err ){
        pFile->lastErrno = errno;
        return UNQLITE_IOERR;
      }else{
        pFile->dirfd=-1;
      }
    }
    if( pFile->h>=0 ){
      int err = close(pFile->h);
      if( err ){
        pFile->lastErrno = errno;
        return UNQLITE_IOERR;
      }
    }
    unqlite_free(pFile->pUnused);
    SyZero(pFile,sizeof(unixFile));
  }
  return UNQLITE_OK;
}
/*
** Close a file.
*/
static int unixClose(unqlite_file *id){
  int rc = UNQLITE_OK;
  if( id ){
    unixFile *pFile = (unixFile *)id;
    unixUnlock(id, NO_LOCK);
    unixEnterMutex();
    if( pFile->pInode && pFile->pInode->nLock ){
      /* If there are outstanding locks, do not actually close the file just
      ** yet because that would clear those locks.  Instead, add the file
      ** descriptor to pInode->pUnused list.  It will be automatically closed 
      ** when the last lock is cleared.
      */
      setPendingFd(pFile);
    }
    releaseInodeInfo(pFile);
    rc = closeUnixFile(id);
    unixLeaveMutex();
  }
  return rc;
}
/************** End of the posix advisory lock implementation *****************
******************************************************************************/
/*
**
** The next division contains implementations for all methods of the 
** unqlite_file object other than the locking methods.  The locking
** methods were defined in divisions above (one locking method per
** division).  Those methods that are common to all locking modes
** are gather together into this division.
*/
/*
** Seek to the offset passed as the second argument, then read cnt 
** bytes into pBuf. Return the number of bytes actually read.
**
** NB:  If you define USE_PREAD or USE_PREAD64, then it might also
** be necessary to define _XOPEN_SOURCE to be 500.  This varies from
** one system to another.  Since SQLite does not define USE_PREAD
** any form by default, we will not attempt to define _XOPEN_SOURCE.
** See tickets #2741 and #2681.
**
** To avoid stomping the errno value on a failed read the lastErrno value
** is set before returning.
*/
static int seekAndRead(unixFile *id, unqlite_int64 offset, void *pBuf, int cnt){
  int got;
#if (!defined(USE_PREAD) && !defined(USE_PREAD64))
  unqlite_int64 newOffset;
#endif
 
#if defined(USE_PREAD)
  got = pread(id->h, pBuf, cnt, offset);
#elif defined(USE_PREAD64)
  got = pread64(id->h, pBuf, cnt, offset);
#else
  newOffset = lseek(id->h, offset, SEEK_SET);
  
  if( newOffset!=offset ){
    if( newOffset == -1 ){
      ((unixFile*)id)->lastErrno = errno;
    }else{
      ((unixFile*)id)->lastErrno = 0;			
    }
    return -1;
  }
  got = read(id->h, pBuf, cnt);
#endif
  if( got<0 ){
    ((unixFile*)id)->lastErrno = errno;
  }
  return got;
}
/*
** Read data from a file into a buffer.  Return UNQLITE_OK if all
** bytes were read successfully and UNQLITE_IOERR if anything goes
** wrong.
*/
static int unixRead(
  unqlite_file *id, 
  void *pBuf, 
  unqlite_int64 amt,
  unqlite_int64 offset
){
  unixFile *pFile = (unixFile *)id;
  int got;
  
  got = seekAndRead(pFile, offset, pBuf, (int)amt);
  if( got==(int)amt ){
    return UNQLITE_OK;
  }else if( got<0 ){
    /* lastErrno set by seekAndRead */
    return UNQLITE_IOERR;
  }else{
    pFile->lastErrno = 0; /* not a system error */
    /* Unread parts of the buffer must be zero-filled */
    SyZero(&((char*)pBuf)[got],(sxu32)amt-got);
    return UNQLITE_IOERR;
  }
}
/*
** Seek to the offset in id->offset then read cnt bytes into pBuf.
** Return the number of bytes actually read.  Update the offset.
**
** To avoid stomping the errno value on a failed write the lastErrno value
** is set before returning.
*/
static int seekAndWrite(unixFile *id, unqlite_int64 offset, const void *pBuf, unqlite_int64 cnt){
  int got;
#if (!defined(USE_PREAD) && !defined(USE_PREAD64))
  unqlite_int64 newOffset;
#endif
  
#if defined(USE_PREAD)
  got = pwrite(id->h, pBuf, cnt, offset);
#elif defined(USE_PREAD64)
  got = pwrite64(id->h, pBuf, cnt, offset);
#else
  newOffset = lseek(id->h, offset, SEEK_SET);
  if( newOffset!=offset ){
    if( newOffset == -1 ){
      ((unixFile*)id)->lastErrno = errno;
    }else{
      ((unixFile*)id)->lastErrno = 0;			
    }
    return -1;
  }
  got = write(id->h, pBuf, cnt);
#endif
  if( got<0 ){
    ((unixFile*)id)->lastErrno = errno;
  }
  return got;
}
/*
** Write data from a buffer into a file.  Return UNQLITE_OK on success
** or some other error code on failure.
*/
static int unixWrite(
  unqlite_file *id, 
  const void *pBuf, 
  unqlite_int64 amt,
  unqlite_int64 offset 
){
  unixFile *pFile = (unixFile*)id;
  int wrote = 0;

  while( amt>0 && (wrote = seekAndWrite(pFile, offset, pBuf, amt))>0 ){
    amt -= wrote;
    offset += wrote;
    pBuf = &((char*)pBuf)[wrote];
  }
  
  if( amt>0 ){
    if( wrote<0 ){
      /* lastErrno set by seekAndWrite */
      return UNQLITE_IOERR;
    }else{
      pFile->lastErrno = 0; /* not a system error */
      return UNQLITE_FULL;
    }
  }
  return UNQLITE_OK;
}
/*
** We do not trust systems to provide a working fdatasync().  Some do.
** Others do no.  To be safe, we will stick with the (slower) fsync().
** If you know that your system does support fdatasync() correctly,
** then simply compile with -Dfdatasync=fdatasync
*/
#if !defined(fdatasync) && !defined(__linux__)
# define fdatasync fsync
#endif

/*
** Define HAVE_FULLFSYNC to 0 or 1 depending on whether or not
** the F_FULLFSYNC macro is defined.  F_FULLFSYNC is currently
** only available on Mac OS X.  But that could change.
*/
#ifdef F_FULLFSYNC
# define HAVE_FULLFSYNC 1
#else
# define HAVE_FULLFSYNC 0
#endif
/*
** The fsync() system call does not work as advertised on many
** unix systems.  The following procedure is an attempt to make
** it work better.
**
**
** SQLite sets the dataOnly flag if the size of the file is unchanged.
** The idea behind dataOnly is that it should only write the file content
** to disk, not the inode.  We only set dataOnly if the file size is 
** unchanged since the file size is part of the inode.  However, 
** Ted Ts'o tells us that fdatasync() will also write the inode if the
** file size has changed.  The only real difference between fdatasync()
** and fsync(), Ted tells us, is that fdatasync() will not flush the
** inode if the mtime or owner or other inode attributes have changed.
** We only care about the file size, not the other file attributes, so
** as far as SQLite is concerned, an fdatasync() is always adequate.
** So, we always use fdatasync() if it is available, regardless of
** the value of the dataOnly flag.
*/
static int full_fsync(int fd, int fullSync, int dataOnly){
  int rc;
#if HAVE_FULLFSYNC
  SXUNUSED(dataOnly);
#else
  SXUNUSED(fullSync);
  SXUNUSED(dataOnly);
#endif

  /* If we compiled with the UNQLITE_NO_SYNC flag, then syncing is a
  ** no-op
  */
#if HAVE_FULLFSYNC
  if( fullSync ){
    rc = fcntl(fd, F_FULLFSYNC, 0);
  }else{
    rc = 1;
  }
  /* If the FULLFSYNC failed, fall back to attempting an fsync().
  ** It shouldn't be possible for fullfsync to fail on the local 
  ** file system (on OSX), so failure indicates that FULLFSYNC
  ** isn't supported for this file system. So, attempt an fsync 
  ** and (for now) ignore the overhead of a superfluous fcntl call.  
  ** It'd be better to detect fullfsync support once and avoid 
  ** the fcntl call every time sync is called.
  */
  if( rc ) rc = fsync(fd);

#elif defined(__APPLE__)
  /* fdatasync() on HFS+ doesn't yet flush the file size if it changed correctly
  ** so currently we default to the macro that redefines fdatasync to fsync
  */
  rc = fsync(fd);
#else 
  rc = fdatasync(fd);
#endif /* ifdef UNQLITE_NO_SYNC elif HAVE_FULLFSYNC */
  if( rc!= -1 ){
    rc = 0;
  }
  return rc;
}
/*
** Make sure all writes to a particular file are committed to disk.
**
** If dataOnly==0 then both the file itself and its metadata (file
** size, access time, etc) are synced.  If dataOnly!=0 then only the
** file data is synced.
**
** Under Unix, also make sure that the directory entry for the file
** has been created by fsync-ing the directory that contains the file.
** If we do not do this and we encounter a power failure, the directory
** entry for the journal might not exist after we reboot.  The next
** SQLite to access the file will not know that the journal exists (because
** the directory entry for the journal was never created) and the transaction
** will not roll back - possibly leading to database corruption.
*/
static int unixSync(unqlite_file *id, int flags){
  int rc;
  unixFile *pFile = (unixFile*)id;

  int isDataOnly = (flags&UNQLITE_SYNC_DATAONLY);
  int isFullsync = (flags&0x0F)==UNQLITE_SYNC_FULL;

  rc = full_fsync(pFile->h, isFullsync, isDataOnly);

  if( rc ){
    pFile->lastErrno = errno;
    return UNQLITE_IOERR;
  }
  if( pFile->dirfd>=0 ){
    int err;
#ifndef UNQLITE_DISABLE_DIRSYNC
    /* The directory sync is only attempted if full_fsync is
    ** turned off or unavailable.  If a full_fsync occurred above,
    ** then the directory sync is superfluous.
    */
    if( (!HAVE_FULLFSYNC || !isFullsync) && full_fsync(pFile->dirfd,0,0) ){
       /*
       ** We have received multiple reports of fsync() returning
       ** errors when applied to directories on certain file systems.
       ** A failed directory sync is not a big deal.  So it seems
       ** better to ignore the error.  Ticket #1657
       */
       /* pFile->lastErrno = errno; */
       /* return UNQLITE_IOERR; */
    }
#endif
    err = close(pFile->dirfd); /* Only need to sync once, so close the */
    if( err==0 ){              /* directory when we are done */
      pFile->dirfd = -1;
    }else{
      pFile->lastErrno = errno;
      rc = UNQLITE_IOERR;
    }
  }
  return rc;
}
/*
** Truncate an open file to a specified size
*/
static int unixTruncate(unqlite_file *id, sxi64 nByte){
  unixFile *pFile = (unixFile *)id;
  int rc;

  rc = ftruncate(pFile->h, (off_t)nByte);
  if( rc ){
    pFile->lastErrno = errno;
    return UNQLITE_IOERR;
  }else{
    return UNQLITE_OK;
  }
}
/*
** Determine the current size of a file in bytes
*/
static int unixFileSize(unqlite_file *id,sxi64 *pSize){
  int rc;
  struct stat buf;
  
  rc = fstat(((unixFile*)id)->h, &buf);
  
  if( rc!=0 ){
    ((unixFile*)id)->lastErrno = errno;
    return UNQLITE_IOERR;
  }
  *pSize = buf.st_size;

  /* When opening a zero-size database, the findInodeInfo() procedure
  ** writes a single byte into that file in order to work around a bug
  ** in the OS-X msdos filesystem.  In order to avoid problems with upper
  ** layers, we need to report this file size as zero even though it is
  ** really 1.   Ticket #3260.
  */
  if( *pSize==1 ) *pSize = 0;

  return UNQLITE_OK;
}
/*
** Return the sector size in bytes of the underlying block device for
** the specified file. This is almost always 512 bytes, but may be
** larger for some devices.
**
** SQLite code assumes this function cannot fail. It also assumes that
** if two files are created in the same file-system directory (i.e.
** a database and its journal file) that the sector size will be the
** same for both.
*/
static int unixSectorSize(unqlite_file *NotUsed){
  SXUNUSED(NotUsed);
  return UNQLITE_DEFAULT_SECTOR_SIZE;
}
/*
** This vector defines all the methods that can operate on an
** unqlite_file for Windows systems.
*/
static const unqlite_io_methods unixIoMethod = {
  1,                              /* iVersion */
  unixClose,                       /* xClose */
  unixRead,                        /* xRead */
  unixWrite,                       /* xWrite */
  unixTruncate,                    /* xTruncate */
  unixSync,                        /* xSync */
  unixFileSize,                    /* xFileSize */
  unixLock,                        /* xLock */
  unixUnlock,                      /* xUnlock */
  unixCheckReservedLock,           /* xCheckReservedLock */
  unixSectorSize,                  /* xSectorSize */
};
/****************************************************************************
**************************** unqlite_vfs methods ****************************
**
** This division contains the implementation of methods on the
** unqlite_vfs object.
*/
/*
** Initialize the contents of the unixFile structure pointed to by pId.
*/
static int fillInUnixFile(
  unqlite_vfs *pVfs,      /* Pointer to vfs object */
  int h,                  /* Open file descriptor of file being opened */
  int dirfd,              /* Directory file descriptor */
  unqlite_file *pId,      /* Write to the unixFile structure here */
  const char *zFilename,  /* Name of the file being opened */
  int noLock,             /* Omit locking if true */
  int isDelete            /* Delete on close if true */
){
  const unqlite_io_methods *pLockingStyle = &unixIoMethod;
  unixFile *pNew = (unixFile *)pId;
  int rc = UNQLITE_OK;

  /* Parameter isDelete is only used on vxworks. Express this explicitly 
  ** here to prevent compiler warnings about unused parameters.
  */
  SXUNUSED(isDelete);
  SXUNUSED(noLock);
  SXUNUSED(pVfs);

  pNew->h = h;
  pNew->dirfd = dirfd;
  pNew->fileFlags = 0;
  pNew->zPath = zFilename;
  
  unixEnterMutex();
  rc = findInodeInfo(pNew, &pNew->pInode);
  if( rc!=UNQLITE_OK ){
      /* If an error occured in findInodeInfo(), close the file descriptor
      ** immediately, before releasing the mutex. findInodeInfo() may fail
      ** in two scenarios:
      **
      **   (a) A call to fstat() failed.
      **   (b) A malloc failed.
      **
      ** Scenario (b) may only occur if the process is holding no other
      ** file descriptors open on the same file. If there were other file
      ** descriptors on this file, then no malloc would be required by
      ** findInodeInfo(). If this is the case, it is quite safe to close
      ** handle h - as it is guaranteed that no posix locks will be released
      ** by doing so.
      **
      ** If scenario (a) caused the error then things are not so safe. The
      ** implicit assumption here is that if fstat() fails, things are in
      ** such bad shape that dropping a lock or two doesn't matter much.
      */
      close(h);
      h = -1;
  }
  unixLeaveMutex();
  
  pNew->lastErrno = 0;
  if( rc!=UNQLITE_OK ){
    if( dirfd>=0 ) close(dirfd); /* silent leak if fail, already in error */
    if( h>=0 ) close(h);
  }else{
    pNew->pMethod = pLockingStyle;
  }
  return rc;
}
/*
** Open a file descriptor to the directory containing file zFilename.
** If successful, *pFd is set to the opened file descriptor and
** UNQLITE_OK is returned. If an error occurs, either UNQLITE_NOMEM
** or UNQLITE_CANTOPEN is returned and *pFd is set to an undefined
** value.
**
** If UNQLITE_OK is returned, the caller is responsible for closing
** the file descriptor *pFd using close().
*/
static int openDirectory(const char *zFilename, int *pFd){
  sxu32 ii;
  int fd = -1;
  char zDirname[MAX_PATHNAME+1];
  sxu32 n;
  n = Systrcpy(zDirname,sizeof(zDirname),zFilename,0);
  for(ii=n; ii>1 && zDirname[ii]!='/'; ii--);
  if( ii>0 ){
    zDirname[ii] = '\0';
    fd = open(zDirname, O_RDONLY|O_BINARY, 0);
    if( fd>=0 ){
#ifdef FD_CLOEXEC
      fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
#endif
    }
  }
  *pFd = fd;
  return (fd>=0?UNQLITE_OK: UNQLITE_IOERR );
}
/*
** Search for an unused file descriptor that was opened on the database 
** file (not a journal or master-journal file) identified by pathname
** zPath with UNQLITE_OPEN_XXX flags matching those passed as the second
** argument to this function.
**
** Such a file descriptor may exist if a database connection was closed
** but the associated file descriptor could not be closed because some
** other file descriptor open on the same file is holding a file-lock.
** Refer to comments in the unixClose() function and the lengthy comment
** describing "Posix Advisory Locking" at the start of this file for 
** further details. Also, ticket #4018.
**
** If a suitable file descriptor is found, then it is returned. If no
** such file descriptor is located, -1 is returned.
*/
static UnixUnusedFd *findReusableFd(const char *zPath, int flags){
  UnixUnusedFd *pUnused = 0;
  struct stat sStat;                   /* Results of stat() call */
  /* A stat() call may fail for various reasons. If this happens, it is
  ** almost certain that an open() call on the same path will also fail.
  ** For this reason, if an error occurs in the stat() call here, it is
  ** ignored and -1 is returned. The caller will try to open a new file
  ** descriptor on the same path, fail, and return an error to SQLite.
  **
  ** Even if a subsequent open() call does succeed, the consequences of
  ** not searching for a resusable file descriptor are not dire.  */
  if( 0==stat(zPath, &sStat) ){
    unixInodeInfo *pInode;

    unixEnterMutex();
    pInode = inodeList;
    while( pInode && (pInode->fileId.dev!=sStat.st_dev
                     || pInode->fileId.ino!=sStat.st_ino) ){
       pInode = pInode->pNext;
    }
    if( pInode ){
      UnixUnusedFd **pp;
      for(pp=&pInode->pUnused; *pp && (*pp)->flags!=flags; pp=&((*pp)->pNext));
      pUnused = *pp;
      if( pUnused ){
        *pp = pUnused->pNext;
      }
    }
    unixLeaveMutex();
  }
  return pUnused;
}
/*
** This function is called by unixOpen() to determine the unix permissions
** to create new files with. If no error occurs, then UNQLITE_OK is returned
** and a value suitable for passing as the third argument to open(2) is
** written to *pMode. If an IO error occurs, an SQLite error code is 
** returned and the value of *pMode is not modified.
**
** If the file being opened is a temporary file, it is always created with
** the octal permissions 0600 (read/writable by owner only). If the file
** is a database or master journal file, it is created with the permissions 
** mask UNQLITE_DEFAULT_FILE_PERMISSIONS.
**
** Finally, if the file being opened is a WAL or regular journal file, then 
** this function queries the file-system for the permissions on the 
** corresponding database file and sets *pMode to this value. Whenever 
** possible, WAL and journal files are created using the same permissions 
** as the associated database file.
*/
static int findCreateFileMode(
  const char *zPath,              /* Path of file (possibly) being created */
  int flags,                      /* Flags passed as 4th argument to xOpen() */
  mode_t *pMode                   /* OUT: Permissions to open file with */
){
  int rc = UNQLITE_OK;             /* Return Code */
  if( flags & UNQLITE_OPEN_TEMP_DB ){
    *pMode = 0600;
     SXUNUSED(zPath);
  }else{
    *pMode = UNQLITE_DEFAULT_FILE_PERMISSIONS;
  }
  return rc;
}
/*
** Open the file zPath.
** 
** Previously, the SQLite OS layer used three functions in place of this
** one:
**
**     unqliteOsOpenReadWrite();
**     unqliteOsOpenReadOnly();
**     unqliteOsOpenExclusive();
**
** These calls correspond to the following combinations of flags:
**
**     ReadWrite() ->     (READWRITE | CREATE)
**     ReadOnly()  ->     (READONLY) 
**     OpenExclusive() -> (READWRITE | CREATE | EXCLUSIVE)
**
** The old OpenExclusive() accepted a boolean argument - "delFlag". If
** true, the file was configured to be automatically deleted when the
** file handle closed. To achieve the same effect using this new 
** interface, add the DELETEONCLOSE flag to those specified above for 
** OpenExclusive().
*/
static int unixOpen(
  unqlite_vfs *pVfs,           /* The VFS for which this is the xOpen method */
  const char *zPath,           /* Pathname of file to be opened */
  unqlite_file *pFile,         /* The file descriptor to be filled in */
  unsigned int flags           /* Input flags to control the opening */
){
  unixFile *p = (unixFile *)pFile;
  int fd = -1;                   /* File descriptor returned by open() */
  int dirfd = -1;                /* Directory file descriptor */
  int openFlags = 0;             /* Flags to pass to open() */
  int noLock;                    /* True to omit locking primitives */
  int rc = UNQLITE_OK;            /* Function Return Code */
  UnixUnusedFd *pUnused;
  int isExclusive  = (flags & UNQLITE_OPEN_EXCLUSIVE);
  int isDelete     = (flags & UNQLITE_OPEN_TEMP_DB);
  int isCreate     = (flags & UNQLITE_OPEN_CREATE);
  int isReadonly   = (flags & UNQLITE_OPEN_READONLY);
  int isReadWrite  = (flags & UNQLITE_OPEN_READWRITE);
  /* If creating a master or main-file journal, this function will open
  ** a file-descriptor on the directory too. The first time unixSync()
  ** is called the directory file descriptor will be fsync()ed and close()d.
  */
  int isOpenDirectory = isCreate ;
  const char *zName = zPath;

  SyZero(p,sizeof(unixFile));
  
  pUnused = findReusableFd(zName, flags);
  if( pUnused ){
	  fd = pUnused->fd;
  }else{
	  pUnused = unqlite_malloc(sizeof(*pUnused));
      if( !pUnused ){
        return UNQLITE_NOMEM;
      }
  }
  p->pUnused = pUnused;
  
  /* Determine the value of the flags parameter passed to POSIX function
  ** open(). These must be calculated even if open() is not called, as
  ** they may be stored as part of the file handle and used by the 
  ** 'conch file' locking functions later on.  */
  if( isReadonly )  openFlags |= O_RDONLY;
  if( isReadWrite ) openFlags |= O_RDWR;
  if( isCreate )    openFlags |= O_CREAT;
  if( isExclusive ) openFlags |= (O_EXCL|O_NOFOLLOW);
  openFlags |= (O_LARGEFILE|O_BINARY);

  if( fd<0 ){
    mode_t openMode;              /* Permissions to create file with */
    rc = findCreateFileMode(zName, flags, &openMode);
    if( rc!=UNQLITE_OK ){
      return rc;
    }
    fd = open(zName, openFlags, openMode);
    if( fd<0 ){
	  rc = UNQLITE_IOERR;
      goto open_finished;
    }
  }
  
  if( p->pUnused ){
    p->pUnused->fd = fd;
    p->pUnused->flags = flags;
  }

  if( isDelete ){
    unlink(zName);
  }

  if( isOpenDirectory ){
    rc = openDirectory(zPath, &dirfd);
    if( rc!=UNQLITE_OK ){
      /* It is safe to close fd at this point, because it is guaranteed not
      ** to be open on a database file. If it were open on a database file,
      ** it would not be safe to close as this would release any locks held
      ** on the file by this process.  */
      close(fd);             /* silently leak if fail, already in error */
      goto open_finished;
    }
  }

#ifdef FD_CLOEXEC
  fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
#endif

  noLock = 0;

#if defined(__APPLE__) 
  struct statfs fsInfo;
  if( fstatfs(fd, &fsInfo) == -1 ){
    ((unixFile*)pFile)->lastErrno = errno;
    if( dirfd>=0 ) close(dirfd); /* silently leak if fail, in error */
    close(fd); /* silently leak if fail, in error */
    return UNQLITE_IOERR;
  }
  if (0 == SyStrncmp("msdos", fsInfo.f_fstypename, 5)) {
    ((unixFile*)pFile)->fsFlags |= UNQLITE_FSFLAGS_IS_MSDOS;
  }
#endif
  
  rc = fillInUnixFile(pVfs, fd, dirfd, pFile, zPath, noLock, isDelete);
open_finished:
  if( rc!=UNQLITE_OK ){
    unqlite_free(p->pUnused);
  }
  return rc;
}
/*
** Delete the file at zPath. If the dirSync argument is true, fsync()
** the directory after deleting the file.
*/
static int unixDelete(
  unqlite_vfs *NotUsed,     /* VFS containing this as the xDelete method */
  const char *zPath,        /* Name of file to be deleted */
  int dirSync               /* If true, fsync() directory after deleting file */
){
  int rc = UNQLITE_OK;
  SXUNUSED(NotUsed);
  
  if( unlink(zPath)==(-1) && errno!=ENOENT ){
	  return UNQLITE_IOERR;
  }
#ifndef UNQLITE_DISABLE_DIRSYNC
  if( dirSync ){
    int fd;
    rc = openDirectory(zPath, &fd);
    if( rc==UNQLITE_OK ){
      if( fsync(fd) )
      {
        rc = UNQLITE_IOERR;
      }
      if( close(fd) && !rc ){
        rc = UNQLITE_IOERR;
      }
    }
  }
#endif
  return rc;
}
/*
** Sleep for a little while.  Return the amount of time slept.
** The argument is the number of microseconds we want to sleep.
** The return value is the number of microseconds of sleep actually
** requested from the underlying operating system, a number which
** might be greater than or equal to the argument, but not less
** than the argument.
*/
static int unixSleep(unqlite_vfs *NotUsed, int microseconds)
{
#if defined(HAVE_USLEEP) && HAVE_USLEEP
  usleep(microseconds);
  SXUNUSED(NotUsed);
  return microseconds;
#else
  int seconds = (microseconds+999999)/1000000;
  SXUNUSED(NotUsed);
  sleep(seconds);
  return seconds*1000000;
#endif
}
/*
 * Export the current system time.
 */
static int unixCurrentTime(unqlite_vfs *pVfs,Sytm *pOut)
{
	struct tm *pTm;
	time_t tt;
	SXUNUSED(pVfs);
	time(&tt);
	pTm = gmtime(&tt);
	if( pTm ){ /* Yes, it can fail */
		STRUCT_TM_TO_SYTM(pTm,pOut);
	}
	return UNQLITE_OK;
}
/*
** Test the existance of or access permissions of file zPath. The
** test performed depends on the value of flags:
**
**     UNQLITE_ACCESS_EXISTS: Return 1 if the file exists
**     UNQLITE_ACCESS_READWRITE: Return 1 if the file is read and writable.
**     UNQLITE_ACCESS_READONLY: Return 1 if the file is readable.
**
** Otherwise return 0.
*/
static int unixAccess(
  unqlite_vfs *NotUsed,   /* The VFS containing this xAccess method */
  const char *zPath,      /* Path of the file to examine */
  int flags,              /* What do we want to learn about the zPath file? */
  int *pResOut            /* Write result boolean here */
){
  int amode = 0;
  SXUNUSED(NotUsed);
  switch( flags ){
    case UNQLITE_ACCESS_EXISTS:
      amode = F_OK;
      break;
    case UNQLITE_ACCESS_READWRITE:
      amode = W_OK|R_OK;
      break;
    case UNQLITE_ACCESS_READ:
      amode = R_OK;
      break;
    default:
		/* Can't happen */
      break;
  }
  *pResOut = (access(zPath, amode)==0);
  if( flags==UNQLITE_ACCESS_EXISTS && *pResOut ){
    struct stat buf;
    if( 0==stat(zPath, &buf) && buf.st_size==0 ){
      *pResOut = 0;
    }
  }
  return UNQLITE_OK;
}
/*
** Turn a relative pathname into a full pathname. The relative path
** is stored as a nul-terminated string in the buffer pointed to by
** zPath. 
**
** zOut points to a buffer of at least unqlite_vfs.mxPathname bytes 
** (in this case, MAX_PATHNAME bytes). The full-path is written to
** this buffer before returning.
*/
static int unixFullPathname(
  unqlite_vfs *pVfs,            /* Pointer to vfs object */
  const char *zPath,            /* Possibly relative input path */
  int nOut,                     /* Size of output buffer in bytes */
  char *zOut                    /* Output buffer */
){
  if( zPath[0]=='/' ){
	  Systrcpy(zOut,(sxu32)nOut,zPath,0);
	  SXUNUSED(pVfs);
  }else{
    sxu32 nCwd;
	zOut[nOut-1] = '\0';
    if( getcwd(zOut, nOut-1)==0 ){
		return UNQLITE_IOERR;
    }
    nCwd = SyStrlen(zOut);
    SyBufferFormat(&zOut[nCwd],(sxu32)nOut-nCwd,"/%s",zPath);
  }
  return UNQLITE_OK;
}
/*
 * Export the Unix Vfs.
 */
UNQLITE_PRIVATE const unqlite_vfs * unqliteExportBuiltinVfs(void)
{
	static const unqlite_vfs sUnixvfs = {
		"Unix",              /* Vfs name */
		1,                   /* Vfs structure version */
		sizeof(unixFile),    /* szOsFile */
		MAX_PATHNAME,        /* mxPathName */
		unixOpen,            /* xOpen */
		unixDelete,          /* xDelete */
		unixAccess,          /* xAccess */
		unixFullPathname,    /* xFullPathname */
		0,                   /* xTmp */
		unixSleep,           /* xSleep */
		unixCurrentTime,     /* xCurrentTime */
		0,                   /* xGetLastError */
	};
	return &sUnixvfs;
}

#endif /* __UNIXES__ */
