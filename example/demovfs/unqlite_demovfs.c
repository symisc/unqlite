/*
** 2015 June 12
** Author: Luca Sturaro 
** Based and "heavily inspired" by test_demovfs.c for SQLite3 (http://www.sqlite.org/vfs.html)
** 
**
*************************************************************************
**
** This file implements an example of a simple VFS following "litterally" 
** the test_demovfs for SQLite3. This works because UnQLite is based 
** on a similar implementation too. It has been created following the
** Unix POSIX calls but it can be changed for further customizations so 
** this will not compile under Windows systems. 
** 
** Compile this file together with the UnQLite database engine source code
** to generate the executable. For example: 
**  gcc unqlite_demovfs.c unqlite.c -o unqlite_demovfs
**
** We can pass a CFLAG for customization: HAVE_FTRUNCATE
**
**
**
** OVERVIEW
**
**   The code in this file implements a minimal UNQLITE VFS that can be 
**   used on Linux and other posix-like operating systems. The following 
**   system calls are used:
**
**    File-system: access(), unlink(), getcwd()
**    File IO:     open(), read(), write(), fsync(), close(), fstat()
**    Other:       sleep(), usleep(), time()
**
**   The following VFS features are omitted:
**
**     1. File locking. The user must ensure that there is at most one
**        connection to each database when using this VFS. 
**
**     2. This does not port Jx9 engine vfs. This is for UnQLite DBMS only.
**
**
**
**   It is assumed that the system uses UNIX-like path-names. Specifically,
**   that '/' characters are used to separate path components and that
**   a path-name is a relative path unless it begins with a '/'. And that
**   no UTF-8 encoded paths are greater than 512 bytes in length.
**
**   Code in this file allocates a fixed size buffer of 
**   UNQLITE_DEMOVFS_BUFFERSZ using unqlite_malloc() whenever a
**   file is opened. It uses the buffer to aggregate sequential
**   writes into aligned UNQLITE_DEMOVFS_BUFFERSZ blocks. When UnQLite
**   invokes the xSync() method to sync the contents of the file to disk,
**   all accumulated data is written out, even if it does not constitute
**   a complete block.
**
**   
**   WARNING:
**
**   This file should be used as a simple template for a custom VFS
**   according to your OS. This is NOT a replacement of the VFS provided
**   in UNQLite itself for UNIXES systems (see os_unix.c)
*/

#if !defined(UNQLITE_TEST) 

#include "unqlite.h"

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/param.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>


#include <stdio.h>  /* puts(), - see Fatal function and snprintf() in vfs;  */
#include <stdlib.h> /* exit() */



/*
** Size of the write buffer used by journal files in bytes.
*/
#ifndef UNQLITE_DEMOVFS_BUFFERSZ
# define UNQLITE_DEMOVFS_BUFFERSZ 8192
#endif


/*
** Size of unqlite page file size.
** It is configured using unqlite_lib_config(UNQLITE_LIB_CONFIG_PAGE_SIZE, UNQLITE_PAGE_SIZE)
*/
#ifndef UNQLITE_PAGE_SIZE
# define UNQLITE_PAGE_SIZE 512
#endif




/*
** The maximum pathname length supported by this VFS.
*/
#define MAXPATHNAME 512

/*
** When using this VFS, the unqlite_file* handles that UnQLite uses are
** actually pointers to instances of type DemoFile.
*/
typedef struct DemoFile DemoFile;
struct DemoFile {
  unqlite_file base;              /* Base class. Must be first. */
  unqlite_vfs *uvfs;              /* The VFS that created this DemoFile */
  int fd;                         /* File descriptor */
  char *aBuffer;                  /* Pointer to malloc'd buffer */
  int nBuffer;                    /* Valid bytes of data in zBuffer */
  unqlite_int64 iBufferOfst;      /* Offset in file of zBuffer[0] */
};


/*
** Write directly to the file passed as the first argument. Even if the
** file has a write-buffer (DemoFile.aBuffer), ignore it.
*/
static int demoDirectWrite(
  DemoFile *p,                    /* File handle */
  const void *zBuf,               /* Buffer containing data to write */
  int iAmt,                       /* Size of data to write in bytes */
  unqlite_int64 iOfst             /* File offset to write to */
  ){
  off_t ofst;                     /* Return value from lseek() */
  size_t nWrite;                  /* Return value from write() */

  ofst = lseek(p->fd, iOfst, SEEK_SET);
  if( ofst!=iOfst ){
    return UNQLITE_IOERR;
  }

  nWrite = write(p->fd, zBuf, iAmt);
  if( nWrite!=iAmt ){
    return UNQLITE_IOERR;
  }

  return UNQLITE_OK;
}

/*
** Flush the contents of the DemoFile.aBuffer buffer to disk. This is a
** no-op if this particular file does not have a buffer (i.e. it is not
** a journal file) or if the buffer is currently empty.
*/
static int demoFlushBuffer(DemoFile *p){
  int rc = UNQLITE_OK;
  if( p->nBuffer ){
    rc = demoDirectWrite(p, p->aBuffer, p->nBuffer, p->iBufferOfst);
    p->nBuffer = 0;
  }
  return rc;
}

/*
** Close a file.
*/
static int demoClose(unqlite_file *pFile){
  int rc;
  DemoFile *p = (DemoFile*)pFile;
  rc = demoFlushBuffer(p);
  unqlite_free(p->aBuffer);
  close(p->fd);
  return rc;
}

/*
** Read data from a file.
*/
static int demoRead(
  unqlite_file *pFile, 
  void *zBuf, 
  unqlite_int64 iAmt, 
  unqlite_int64 iOfst
){
  DemoFile *p = (DemoFile*)pFile;
  off_t ofst;                     /* Return value from lseek() */
  int nRead;                      /* Return value from read() */
  int rc;                         /* Return code from demoFlushBuffer() */

  /* Flush any data in the write buffer to disk in case this operation
  ** is trying to read data the file-region currently cached in the buffer.
  */
  rc = demoFlushBuffer(p);
  if( rc!=UNQLITE_OK ){
    return rc;
  }

  ofst = lseek(p->fd, iOfst, SEEK_SET);
  if( ofst!=iOfst ){
    return UNQLITE_IOERR;
  }
  nRead = read(p->fd, zBuf, iAmt);

  if( nRead==(int)iAmt ){
    return UNQLITE_OK;
  }else if( nRead>=0 ){
    return UNQLITE_IOERR;
  }
  return UNQLITE_IOERR;
}

/*
** Write data to a crash-file.
*/
static int demoWrite(
  unqlite_file *pFile, 
  const void *zBuf, 
  unqlite_int64 iAmt, 
  unqlite_int64 iOfst
){
  DemoFile *p = (DemoFile*)pFile;
  
  if( p->aBuffer ){
    char *z = (char *)zBuf;       /* Pointer to remaining data to write */
    unqlite_int64 n = iAmt;                 /* Number of bytes at z */
    unqlite_int64 i = iOfst;      /* File offset to write to */

    while( n>0 ){
      int nCopy;                  /* Number of bytes to copy into buffer */

      /* If the buffer is full, or if this data is not being written directly
      ** following the data already buffered, flush the buffer. Flushing
      ** the buffer is a no-op if it is empty.  
      */
      if( p->nBuffer==UNQLITE_DEMOVFS_BUFFERSZ || p->iBufferOfst+p->nBuffer!=i ){
        int rc = demoFlushBuffer(p);
        if( rc!=UNQLITE_OK ){
          return rc;
        }
      }
      assert( p->nBuffer==0 || p->iBufferOfst+p->nBuffer==i );
      p->iBufferOfst = i - p->nBuffer;

      /* Copy as much data as possible into the buffer. */
      nCopy = UNQLITE_DEMOVFS_BUFFERSZ - p->nBuffer;
      if( nCopy>n ){
        nCopy = n;
      }
      memcpy(&p->aBuffer[p->nBuffer], z, nCopy);
      p->nBuffer += nCopy;

      n -= nCopy;
      i += nCopy;
      z += nCopy;
    }
  }else{
    return demoDirectWrite(p, zBuf, iAmt, iOfst);
  }

  return UNQLITE_OK;
}

/*
** Truncate a file. This is a no-op for this VFS (see header comments at
** the top of the file).
*/
static int demoTruncate(unqlite_file *pFile, unqlite_int64 size){
#if defined(HAVE_FTRUNCATE)
  int rc;
  rc = ftruncate(((DemoFile *)pFile)->fd, (off_t)size);
  if( rc ){
    return UNQLITE_IOERR;
  }
#endif
  return UNQLITE_OK;
}

/*
** Sync the contents of the file to the persistent media.
*/
static int demoSync(unqlite_file *pFile, int flags){
  DemoFile *p = (DemoFile*)pFile;
  int rc;

  rc = demoFlushBuffer(p);
  if( rc!=UNQLITE_OK ){
    return rc;
  }

  rc = fsync(p->fd);
  return (rc==0 ? UNQLITE_OK : UNQLITE_IOERR);
}

/*
** Write the size of the file in bytes to *pSize.
*/
static int demoFileSize(unqlite_file *pFile, unqlite_int64 *pSize){
  DemoFile *p = (DemoFile*)pFile;
  int rc;                         /* Return code from fstat() call */
  struct stat sStat;              /* Output of fstat() call */

  /* Flush the contents of the buffer to disk. As with the flush in the
  ** demoRead() method, it would be possible to avoid this and save a write
  ** here and there. But in practice this comes up so infrequently it is
  ** not worth the trouble.
  */
  rc = demoFlushBuffer(p);
  if( rc!=UNQLITE_OK ){
    return rc;
  }

  rc = fstat(p->fd, &sStat);
  if( rc!=0 ) return UNQLITE_IOERR;
  *pSize = sStat.st_size;
  return UNQLITE_OK;
}

/*
** Locking functions. The xLock() and xUnlock() methods are both no-ops.
** The xCheckReservedLock() always indicates that no other process holds
** a reserved lock on the database file. This ensures that if a hot-journal
** file is found in the file-system it is rolled back.
*/
static int demoLock(unqlite_file *pFile, int eLock){
  return UNQLITE_OK;
}
static int demoUnlock(unqlite_file *pFile, int eLock){
  return UNQLITE_OK;
}
static int demoCheckReservedLock(unqlite_file *pFile, int *pResOut){
  *pResOut = 0;
  return UNQLITE_OK;
}


/*
** The xSectorSize() function.
** This may return special values allowing UnQlite to optimize file-system 
** access to some extent. But we simply return 0.
*/
static int demoSectorSize(unqlite_file *pFile){
  return 0;
}



/*
** Open a file handle.
*/
static int demoOpen(
  unqlite_vfs *pVfs,              /* VFS */
  const char *zName,              /* File to open, or 0 for a temp file */
  unqlite_file *pFile,            /* Pointer to DemoFile struct to populate */
  unsigned int flags                      /* Input UNQLITE_OPEN_XXX flags, Input flags to control the opening */
){
  static const unqlite_io_methods demoio = {
    1,                            /* iVersion */
    demoClose,                    /* xClose */
    demoRead,                     /* xRead */
    demoWrite,                    /* xWrite */
    demoTruncate,                 /* xTruncate */
    demoSync,                     /* xSync */
    demoFileSize,                 /* xFileSize */
    demoLock,                     /* xLock */
    demoUnlock,                   /* xUnlock */
    demoCheckReservedLock,        /* xCheckReservedLock */
    demoSectorSize,               /* xSectorSize */
  };

  DemoFile *p = (DemoFile*)pFile; /* Populate this structure */
  char *aBuf = 0;

  int fd = -1;                   /* File descriptor returned by open() */
  int openFlags = 0;             /* Flags to pass to open() */
  int rc = UNQLITE_OK;            /* Function Return Code */
  
  int isExclusive  = (flags & UNQLITE_OPEN_EXCLUSIVE);
  int isDelete     = (flags & UNQLITE_OPEN_TEMP_DB);
  int isCreate     = (flags & UNQLITE_OPEN_CREATE);
  int isReadonly   = (flags & UNQLITE_OPEN_READONLY);
  int isReadWrite  = (flags & UNQLITE_OPEN_READWRITE);
  
  
  
  if( zName==0 ){
    return UNQLITE_IOERR;
  }

  aBuf = (char *)unqlite_malloc(UNQLITE_DEMOVFS_BUFFERSZ);
  if( !aBuf ){
    return UNQLITE_NOMEM;
  }
  
  /* Determine the value of the flags parameter passed to POSIX function
  ** open(). These must be calculated even if open() is not called, as
  ** they may be stored as part of the file handle and used by the 
  ** 'conch file' locking functions later on.  */
  if( isExclusive ) openFlags |= O_EXCL;
  if( isReadonly )  openFlags |= O_RDONLY;
  if( isReadWrite ) openFlags |= O_RDWR;
  if( isCreate )    openFlags |= O_CREAT;
  
  
  memset(p, 0, sizeof(DemoFile));
  p->fd = open(zName, openFlags, 0600);
  if( p->fd<0 ){
    unqlite_free(aBuf);
	return UNQLITE_CANTOPEN;
  }
  
  if( isDelete ){
    unlink(zName);
  }
  
  p->aBuffer = aBuf;
  p->uvfs = pVfs;
  p->base.pMethods = &demoio;
  return UNQLITE_OK;
}

/*
** Delete the file identified by argument zPath. If the dirSync parameter
** is non-zero, then ensure the file-system modification to delete the
** file has been synced to disk before returning.
*/
static int demoDelete(unqlite_vfs *pVfs, const char *zPath, int dirSync){
  int rc = UNQLITE_OK;                     /* Return code */

  rc = unlink(zPath);
  if( rc==(-1) && errno!=ENOENT ){
	  return UNQLITE_IOERR;
  }
  if( rc==0 && dirSync ){
    int dfd;                      /* File descriptor open on directory */
    int i;                        /* Iterator variable */
    char zDir[MAXPATHNAME+1];     /* Name of directory containing file zPath */

    /* Figure out the directory name from the path of the file deleted. */
    zDir[MAXPATHNAME] = '\0';
    for(i=strlen(zDir); i>1 && zDir[i]!='/'; i++);
    zDir[i] = '\0';

    /* Open a file-descriptor on the directory. Sync. Close. */
    dfd = open(zDir, O_RDONLY, 0);
    if( dfd<0 ){
      rc = -1;
    }else{
      rc = fsync(dfd);
      close(dfd);
    }
  }
  return (rc==0 ? UNQLITE_OK : UNQLITE_IOERR);
}

#ifndef F_OK
# define F_OK 0
#endif
#ifndef R_OK
# define R_OK 4
#endif
#ifndef W_OK
# define W_OK 2
#endif

/*
** Query the file-system to see if the named file exists, is readable or
** is both readable and writable.
*/
static int demoAccess(
  unqlite_vfs *pVfs, 
  const char *zPath, 
  int flags, 
  int *pResOut
){
  int rc;                         /* access() return code */
  int eAccess = F_OK;             /* Second argument to access() */

  assert( flags==UNQLITE_ACCESS_EXISTS       /* access(zPath, F_OK) */
       || flags==UNQLITE_ACCESS_READ         /* access(zPath, R_OK) */
       || flags==UNQLITE_ACCESS_READWRITE    /* access(zPath, R_OK|W_OK) */
  );

  if( flags==UNQLITE_ACCESS_READWRITE ) eAccess = R_OK|W_OK;
  if( flags==UNQLITE_ACCESS_READ )      eAccess = R_OK;

  /* we could do just  this */
  //  rc = access(zPath, eAccess);
  //  *pResOut = (rc==0);
  
  /* taken from unqlite unixAccess function */
  *pResOut = (access(zPath, eAccess)==0);
  if( flags==UNQLITE_ACCESS_EXISTS && *pResOut ){
    struct stat buf;
    if( 0==stat(zPath, &buf) && buf.st_size==0 ){
      *pResOut = 0;
    }
  }
  return UNQLITE_OK;

}

/*
** Argument zPath points to a null-terminated string containing a file path.
** If zPath is an absolute path, then it is copied as is into the output 
** buffer. Otherwise, if it is a relative path, then the equivalent full
** path is written to the output buffer.
**
** This function assumes that paths are UNIX style. Specifically, that:
**
**   1. Path components are separated by a '/'. and 
**   2. Full paths begin with a '/' character.
*/
static int demoFullPathname(
  unqlite_vfs *pVfs,              /* VFS */
  const char *zPath,              /* Input path (possibly a relative path) */
  int nPathOut,                   /* Size of output buffer in bytes */
  char *zPathOut                  /* Pointer to output buffer */
){
  char zDir[MAXPATHNAME+1];
  if( zPath[0]=='/' ){
    zDir[0] = '\0';
  }else{
    if( getcwd(zDir, sizeof(zDir))==0 ) return UNQLITE_IOERR;
  }
  zDir[MAXPATHNAME] = '\0';
  /* FIXME: Remove snprintf, try to avoid dependencies on <stdio.h> for pure vfs; use strcpy? */ 
  snprintf (zPathOut, nPathOut,"%s/%s",zDir, zPath);
  zPathOut[nPathOut-1] = '\0';

  return UNQLITE_OK;
}




/*
** Sleep for at least nMicro microseconds. Return the (approximate) number 
** of microseconds slept for.
*/
static int demoSleep(unqlite_vfs *pVfs, int nMicro){
  int seconds = (nMicro+999999)/1000000;
  /* if we have at least sleep function */
  sleep(seconds);
  return seconds*1000000;
}

/*
** Set *pTime to the current UTC time expressed as a Julian day. Return
** UNQLITE_OK if successful, or an error code otherwise.
**
**   http://en.wikipedia.org/wiki/Julian_day
**
** This implementation is not very good. The current time is rounded to
** an integer number of seconds. Also, assuming time_t is a signed 32-bit 
** value, it will stop working some time in the year 2038 AD (the so-called
** "year 2038" problem that afflicts systems that store time this way). 
*/
static int demoCurrentTime(unqlite_vfs *pVfs, Sytm *pOut){
	struct tm *pTm;
	time_t tt;
	time(&tt);
	pTm = gmtime(&tt);
	if( pTm ){ /* Yes, it can fail */
		STRUCT_TM_TO_SYTM(pTm,pOut);
	}
	return UNQLITE_OK;
}


/*
** This function returns a pointer to the VFS implemented in this file.
** To make the VFS available to UnQLite
**
**   unqlite_lib_config(UNQLITE_LIB_CONFIG_VFS, unqliteExportDemoVfs());
**	
** As for initialization, the sequence I use is as follows:
** -unqlite_lib_config(UNQLITE_LIB_CONFIG_VFS, pVfs)
** -unqlite_lib_config(UNQLITE_LIB_CONFIG_PAGE_SIZE, 512)
** -unqlite_open() (it automatically initialize the library without calling unqlite_lib_init() function)
*/
const unqlite_vfs *unqliteExportDemoVfs(void){
  static unqlite_vfs demovfs = {
    "unqlite_demo_vfs",           /* zName - Name of this virtual file system [i.e: Windows, UNIX, etc.] */
	1,                            /* iVersion - Structure version number (currently 1) */
	sizeof(DemoFile),             /* szOsFile - Size of subclassed unqlite_file */
	MAXPATHNAME,                  /* mxPathname -Maximum file pathname length */
    demoOpen,                     /* xOpen */
    demoDelete,                   /* xDelete */
    demoAccess,                   /* xAccess */
    demoFullPathname,             /* xFullPathname */
	0,                            /* xTmpDir */
    demoSleep,                    /* xSleep */
    demoCurrentTime,              /* xCurrentTime */
	0,                            /* xGetLastError */
  };
  return &demovfs;
}

#endif /*  !defined(UNQLITE_TEST) */



/****************************************************************************************************/
/****************************************************************************************************/
/*  END OF VFS - Below we provide a function which registers this VFS and set PAGE_SIZE. 
/*  Up to the user calling the unqlite_open() and do the job. */
/****************************************************************************************************/
/****************************************************************************************************/



/*
** This function is an unqlite add-on to launch unqlite registering VFS implemented in this file.
** To initialize unqlite with this VFS you just do:
** unqlite_start();
** unqlite_open();
**	
** To make the VFS available to unqlite:
**
**   unqlite_lib_config(UNQLITE_LIB_CONFIG_VFS, unqliteExportDemoVfs());
**	
** As for initialization, the sequence I use here is as follows:
** -unqlite_lib_config(UNQLITE_LIB_CONFIG_VFS, pVfs)
** -unqlite_lib_config(UNQLITE_LIB_CONFIG_PAGE_SIZE, 512)
**
** The user just need to call this function and then:
** -unqlite_open() (it automatically initialize the library without calling unqlite_lib_init() function)
*/
static void unqlite_start(void) {
	
	int rc;
	const char *failVfsMsg = "Error in VFS configuration";
	const char *failPageSzMsg = "Error in setting PageSize";
	/* configure this vfs as the current one (unqlite supports just one VFS at a time) */
	rc = unqlite_lib_config(UNQLITE_LIB_CONFIG_VFS, unqliteExportDemoVfs());
	if( rc!=UNQLITE_OK ){
		puts(failVfsMsg);
		exit(0);
	}
	/* set the page size */
	rc = unqlite_lib_config(UNQLITE_LIB_CONFIG_PAGE_SIZE, UNQLITE_PAGE_SIZE);
	if( rc!=UNQLITE_OK ){
		puts(failPageSzMsg);
		exit(0);
	}
	
}



/*
**	Here we put other functions just to start the db and show how to use this vfs
*/

/*
 * Banner.
 */
static const char zBanner[] = {
	"============================================================\n"
	"UnQLite VFS DemoFile Functions                              \n"
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




int main(int argc,char *argv[])
{
	unqlite *pDb;       /* Database handle */
	int rc;
	int i;
	
	/* Print Banner */
	puts(zBanner);
	puts("Starting vfs set\n");	
	/* Configure with our own vfs and page size */
	unqlite_start();
	puts("Successfully vfs set\n");	
	
	/* Open our database */
	/* Pass a real db file to test the this vfs*/
	rc = unqlite_open(&pDb,argc > 1 ? argv[1] /* On-disk DB */ : ":mem:" /* In-mem DB */,UNQLITE_OPEN_CREATE);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Out of memory");
	}
	/* Taken from 1.c, just store some values as example */
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
		puts("Key Successfully stored");
	}
	if( rc != UNQLITE_OK ){
		/* Insertion fail, rollback the transaction  */
		rc = unqlite_rollback(pDb);
		puts("Rollback OK, even if it wasn't expected");
		if( rc != UNQLITE_OK ){
			/* Extract database error log and exit */
			Fatal(pDb,0);
		}
	}
	
	/* Auto-commit the transaction and close our handle */
	unqlite_close(pDb);
	puts("AutoCommit and Close");
}