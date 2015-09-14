/*
 * Symisc JX9: A Highly Efficient Embeddable Scripting Engine Based on JSON.
 * Copyright (C) 2012-2013, Symisc Systems http://jx9.symisc.net/
 * Version 1.7.2
 * For information on licensing, redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES
 * please contact Symisc Systems via:
 *       legal@symisc.net
 *       licensing@symisc.net
 *       contact@symisc.net
 * or visit:
 *      http://jx9.symisc.net/
 */
 /* $SymiscID: vfs.c v2.1 Ubuntu 2012-12-13 00:013 stable <chm@symisc.net> $ */
#ifndef JX9_AMALGAMATION
#include "jx9Int.h"
#endif
/*
 * This file implement a virtual file systems (VFS) for the JX9 engine.
 */
/*
 * Given a string containing the path of a file or directory, this function 
 * return the parent directory's path.
 */
JX9_PRIVATE const char * jx9ExtractDirName(const char *zPath, int nByte, int *pLen)
{
	const char *zEnd = &zPath[nByte - 1];
	int c, d;
	c = d = '/';
#ifdef __WINNT__
	d = '\\';
#endif
	while( zEnd > zPath && ( (int)zEnd[0] != c && (int)zEnd[0] != d ) ){
		zEnd--;
	}
	*pLen = (int)(zEnd-zPath);
#ifdef __WINNT__
	if( (*pLen) == (int)sizeof(char) && zPath[0] == '/' ){
		/* Normalize path on windows */
		return "\\";
	}
#endif
	if( zEnd == zPath && ( (int)zEnd[0] != c && (int)zEnd[0] != d) ){
		/* No separator, return "." as the current directory */
		*pLen = sizeof(char);
		return ".";
	}
	if( (*pLen) == 0 ){
		*pLen = sizeof(char);
#ifdef __WINNT__
		return "\\";
#else
		return "/";
#endif
	}
	return zPath;
}
/*
 * Omit the vfs layer implementation from the built if the JX9_DISABLE_BUILTIN_FUNC directive is defined.
 */
#ifndef JX9_DISABLE_BUILTIN_FUNC
/*
 * bool chdir(string $directory)
 *  Change the current directory.
 * Parameters
 *  $directory
 *   The new current directory
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_chdir(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xChdir == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xChdir(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool chroot(string $directory)
 *  Change the root directory.
 * Parameters
 *  $directory
 *   The path to change the root directory to
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_chroot(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xChroot == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xChroot(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * string getcwd(void)
 *  Gets the current working directory.
 * Parameters
 *  None
 * Return
 *  Returns the current working directory on success, or FALSE on failure.
 */
static int jx9Vfs_getcwd(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_vfs *pVfs;
	int rc;
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xGetcwd == 0 ){
		SXUNUSED(nArg); /* cc warning */
		SXUNUSED(apArg);
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	jx9_result_string(pCtx, "", 0);
	/* Perform the requested operation */
	rc = pVfs->xGetcwd(pCtx);
	if( rc != JX9_OK ){
		/* Error, return FALSE */
		jx9_result_bool(pCtx, 0);
	}
	return JX9_OK;
}
/*
 * bool rmdir(string $directory)
 *  Removes directory.
 * Parameters
 *  $directory
 *   The path to the directory
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_rmdir(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xRmdir == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xRmdir(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool is_dir(string $filename)
 *  Tells whether the given filename is a directory.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_is_dir(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xIsdir == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xIsdir(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool mkdir(string $pathname[, int $mode = 0777])
 *  Make a directory.
 * Parameters
 *  $pathname
 *   The directory path.
 * $mode
 *  The mode is 0777 by default, which means the widest possible access.
 *  Note:
 *   mode is ignored on Windows.
 *   Note that you probably want to specify the mode as an octal number, which means
 *   it should have a leading zero. The mode is also modified by the current umask
 *   which you can change using umask().
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_mkdir(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	int iRecursive = 0;
	const char *zPath;
	jx9_vfs *pVfs;
	int iMode, rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xMkdir == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
#ifdef __WINNT__
	iMode = 0;
#else
	/* Assume UNIX */
	iMode = 0777;
#endif
	if( nArg > 1 ){
		iMode = jx9_value_to_int(apArg[1]);
		if( nArg > 2 ){
			iRecursive = jx9_value_to_bool(apArg[2]);
		}
	}
	/* Perform the requested operation */
	rc = pVfs->xMkdir(zPath, iMode, iRecursive);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool rename(string $oldname, string $newname)
 *  Attempts to rename oldname to newname.
 * Parameters
 *  $oldname
 *   Old name.
 *  $newname
 *   New name.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_rename(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zOld, *zNew;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 2 || !jx9_value_is_string(apArg[0]) || !jx9_value_is_string(apArg[1]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xRename == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	zOld = jx9_value_to_string(apArg[0], 0);
	zNew = jx9_value_to_string(apArg[1], 0);
	rc = pVfs->xRename(zOld, zNew);
	/* IO result */
	jx9_result_bool(pCtx, rc == JX9_OK );
	return JX9_OK;
}
/*
 * string realpath(string $path)
 *  Returns canonicalized absolute pathname.
 * Parameters
 *  $path
 *   Target path.
 * Return
 *  Canonicalized absolute pathname on success. or FALSE on failure.
 */
static int jx9Vfs_realpath(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
        int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xRealpath == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Set an empty string untnil the underlying OS interface change that */
	jx9_result_string(pCtx, "", 0);
	/* Perform the requested operation */
	zPath = jx9_value_to_string(apArg[0], 0);
	rc = pVfs->xRealpath(zPath, pCtx);
	if( rc != JX9_OK ){
	 jx9_result_bool(pCtx, 0);
	}
	return JX9_OK;
}
/*
 * int sleep(int $seconds)
 *  Delays the program execution for the given number of seconds.
 * Parameters
 *  $seconds
 *   Halt time in seconds.
 * Return
 *  Zero on success or FALSE on failure.
 */
static int jx9Vfs_sleep(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_vfs *pVfs;
	int rc, nSleep;
	if( nArg < 1 || !jx9_value_is_int(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xSleep == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Amount to sleep */
	nSleep = jx9_value_to_int(apArg[0]);
	if( nSleep < 0 ){
		/* Invalid value, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation (Microseconds) */
	rc = pVfs->xSleep((unsigned int)(nSleep * SX_USEC_PER_SEC));
	if( rc != JX9_OK ){
		/* Return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		/* Return zero */
		jx9_result_int(pCtx, 0);
	}
	return JX9_OK;
}
/*
 * void usleep(int $micro_seconds)
 *  Delays program execution for the given number of micro seconds.
 * Parameters
 *  $micro_seconds
 *   Halt time in micro seconds. A micro second is one millionth of a second.
 * Return
 *  None.
 */
static int jx9Vfs_usleep(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_vfs *pVfs;
	int nSleep;
	if( nArg < 1 || !jx9_value_is_int(apArg[0]) ){
		/* Missing/Invalid argument, return immediately */
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xSleep == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS", 
			jx9_function_name(pCtx)
			);
		return JX9_OK;
	}
	/* Amount to sleep */
	nSleep = jx9_value_to_int(apArg[0]);
	if( nSleep < 0 ){
		/* Invalid value, return immediately */
		return JX9_OK;
	}
	/* Perform the requested operation (Microseconds) */
	pVfs->xSleep((unsigned int)nSleep);
	return JX9_OK;
}
/*
 * bool unlink (string $filename)
 *  Delete a file.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_unlink(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xUnlink == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xUnlink(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool chmod(string $filename, int $mode)
 *  Attempts to change the mode of the specified file to that given in mode.
 * Parameters
 *  $filename
 *   Path to the file.
 * $mode
 *   Mode (Must be an integer)
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_chmod(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int iMode;
	int rc;
	if( nArg < 2 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xChmod == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Extract the mode */
	iMode = jx9_value_to_int(apArg[1]);
	/* Perform the requested operation */
	rc = pVfs->xChmod(zPath, iMode);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool chown(string $filename, string $user)
 *  Attempts to change the owner of the file filename to user user.
 * Parameters
 *  $filename
 *   Path to the file.
 * $user
 *   Username.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_chown(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath, *zUser;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 2 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xChown == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Extract the user */
	zUser = jx9_value_to_string(apArg[1], 0);
	/* Perform the requested operation */
	rc = pVfs->xChown(zPath, zUser);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool chgrp(string $filename, string $group)
 *  Attempts to change the group of the file filename to group.
 * Parameters
 *  $filename
 *   Path to the file.
 * $group
 *   groupname.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_chgrp(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath, *zGroup;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 2 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xChgrp == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Extract the user */
	zGroup = jx9_value_to_string(apArg[1], 0);
	/* Perform the requested operation */
	rc = pVfs->xChgrp(zPath, zGroup);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * int64 disk_free_space(string $directory)
 *  Returns available space on filesystem or disk partition.
 * Parameters
 *  $directory
 *   A directory of the filesystem or disk partition.
 * Return
 *  Returns the number of available bytes as a 64-bit integer or FALSE on failure.
 */
static int jx9Vfs_disk_free_space(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_int64 iSize;
	jx9_vfs *pVfs;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xFreeSpace == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	iSize = pVfs->xFreeSpace(zPath);
	/* IO return value */
	jx9_result_int64(pCtx, iSize);
	return JX9_OK;
}
/*
 * int64 disk_total_space(string $directory)
 *  Returns the total size of a filesystem or disk partition.
 * Parameters
 *  $directory
 *   A directory of the filesystem or disk partition.
 * Return
 *  Returns the number of available bytes as a 64-bit integer or FALSE on failure.
 */
static int jx9Vfs_disk_total_space(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_int64 iSize;
	jx9_vfs *pVfs;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xTotalSpace == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	iSize = pVfs->xTotalSpace(zPath);
	/* IO return value */
	jx9_result_int64(pCtx, iSize);
	return JX9_OK;
}
/*
 * bool file_exists(string $filename)
 *  Checks whether a file or directory exists.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_file_exists(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xFileExists == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xFileExists(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * int64 file_size(string $filename)
 *  Gets the size for the given file.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  File size on success or FALSE on failure.
 */
static int jx9Vfs_file_size(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_int64 iSize;
	jx9_vfs *pVfs;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xFileSize == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	iSize = pVfs->xFileSize(zPath);
	/* IO return value */
	jx9_result_int64(pCtx, iSize);
	return JX9_OK;
}
/*
 * int64 fileatime(string $filename)
 *  Gets the last access time of the given file.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  File atime on success or FALSE on failure.
 */
static int jx9Vfs_file_atime(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_int64 iTime;
	jx9_vfs *pVfs;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xFileAtime == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	iTime = pVfs->xFileAtime(zPath);
	/* IO return value */
	jx9_result_int64(pCtx, iTime);
	return JX9_OK;
}
/*
 * int64 filemtime(string $filename)
 *  Gets file modification time.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  File mtime on success or FALSE on failure.
 */
static int jx9Vfs_file_mtime(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_int64 iTime;
	jx9_vfs *pVfs;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xFileMtime == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	iTime = pVfs->xFileMtime(zPath);
	/* IO return value */
	jx9_result_int64(pCtx, iTime);
	return JX9_OK;
}
/*
 * int64 filectime(string $filename)
 *  Gets inode change time of file.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  File ctime on success or FALSE on failure.
 */
static int jx9Vfs_file_ctime(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_int64 iTime;
	jx9_vfs *pVfs;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xFileCtime == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	iTime = pVfs->xFileCtime(zPath);
	/* IO return value */
	jx9_result_int64(pCtx, iTime);
	return JX9_OK;
}
/*
 * bool is_file(string $filename)
 *  Tells whether the filename is a regular file.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_is_file(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xIsfile == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xIsfile(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool is_link(string $filename)
 *  Tells whether the filename is a symbolic link.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_is_link(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xIslink == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xIslink(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool is_readable(string $filename)
 *  Tells whether a file exists and is readable.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_is_readable(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xReadable == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xReadable(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool is_writable(string $filename)
 *  Tells whether the filename is writable.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_is_writable(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xWritable == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xWritable(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * bool is_executable(string $filename)
 *  Tells whether the filename is executable.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_is_executable(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xExecutable == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xExecutable(zPath);
	/* IO return value */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * string filetype(string $filename)
 *  Gets file type.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  The type of the file. Possible values are fifo, char, dir, block, link
 *  file, socket and unknown.
 */
static int jx9Vfs_filetype(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	jx9_vfs *pVfs;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return 'unknown' */
		jx9_result_string(pCtx, "unknown", sizeof("unknown")-1);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xFiletype == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the desired directory */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Set the empty string as the default return value */
	jx9_result_string(pCtx, "", 0);
	/* Perform the requested operation */
	pVfs->xFiletype(zPath, pCtx);
	return JX9_OK;
}
/*
 * array stat(string $filename)
 *  Gives information about a file.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  An associative array on success holding the following entries on success
 *  0   dev     device number
 * 1    ino     inode number (zero on windows)
 * 2    mode    inode protection mode
 * 3    nlink   number of links
 * 4    uid     userid of owner (zero on windows)
 * 5    gid     groupid of owner (zero on windows)
 * 6    rdev    device type, if inode device
 * 7    size    size in bytes
 * 8    atime   time of last access (Unix timestamp)
 * 9    mtime   time of last modification (Unix timestamp)
 * 10   ctime   time of last inode change (Unix timestamp)
 * 11   blksize blocksize of filesystem IO (zero on windows)
 * 12   blocks  number of 512-byte blocks allocated.
 * Note:
 *  FALSE is returned on failure.
 */
static int jx9Vfs_stat(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_value *pArray, *pValue;
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xStat == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Create the array and the working value */
	pArray = jx9_context_new_array(pCtx);
	pValue = jx9_context_new_scalar(pCtx);
	if( pArray == 0 || pValue == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the file path */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xStat(zPath, pArray, pValue);
	if( rc != JX9_OK ){
		/* IO error, return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		/* Return the associative array */
		jx9_result_value(pCtx, pArray);
	}
	/* Don't worry about freeing memory here, everything will be released
	 * automatically as soon we return from this function. */
	return JX9_OK;
}
/*
 * array lstat(string $filename)
 *  Gives information about a file or symbolic link.
 * Parameters
 *  $filename
 *   Path to the file.
 * Return
 *  An associative array on success holding the following entries on success
 *  0   dev     device number
 * 1    ino     inode number (zero on windows)
 * 2    mode    inode protection mode
 * 3    nlink   number of links
 * 4    uid     userid of owner (zero on windows)
 * 5    gid     groupid of owner (zero on windows)
 * 6    rdev    device type, if inode device
 * 7    size    size in bytes
 * 8    atime   time of last access (Unix timestamp)
 * 9    mtime   time of last modification (Unix timestamp)
 * 10   ctime   time of last inode change (Unix timestamp)
 * 11   blksize blocksize of filesystem IO (zero on windows)
 * 12   blocks  number of 512-byte blocks allocated.
 * Note:
 *  FALSE is returned on failure.
 */
static int jx9Vfs_lstat(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_value *pArray, *pValue;
	const char *zPath;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xlStat == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Create the array and the working value */
	pArray = jx9_context_new_array(pCtx);
	pValue = jx9_context_new_scalar(pCtx);
	if( pArray == 0 || pValue == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the file path */
	zPath = jx9_value_to_string(apArg[0], 0);
	/* Perform the requested operation */
	rc = pVfs->xlStat(zPath, pArray, pValue);
	if( rc != JX9_OK ){
		/* IO error, return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		/* Return the associative array */
		jx9_result_value(pCtx, pArray);
	}
	/* Don't worry about freeing memory here, everything will be released
	 * automatically as soon we return from this function. */
	return JX9_OK;
}
/*
 * string getenv(string $varname)
 *  Gets the value of an environment variable.
 * Parameters
 *  $varname
 *   The variable name.
 * Return
 *  Returns the value of the environment variable varname, or FALSE if the environment
 * variable varname does not exist. 
 */
static int jx9Vfs_getenv(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zEnv;
	jx9_vfs *pVfs;
	int iLen;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xGetenv == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the environment variable */
	zEnv = jx9_value_to_string(apArg[0], &iLen);
	/* Set a boolean FALSE as the default return value */
	jx9_result_bool(pCtx, 0);
	if( iLen < 1 ){
		/* Empty string */
		return JX9_OK;
	}
	/* Perform the requested operation */
	pVfs->xGetenv(zEnv, pCtx);
	return JX9_OK;
}
/*
 * bool putenv(string $settings)
 *  Set the value of an environment variable.
 * Parameters
 *  $setting
 *   The setting, like "FOO=BAR"
 * Return
 *  TRUE on success or FALSE on failure.  
 */
static int jx9Vfs_putenv(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zName, *zValue;
	char *zSettings, *zEnd;
	jx9_vfs *pVfs;
	int iLen, rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the setting variable */
	zSettings = (char *)jx9_value_to_string(apArg[0], &iLen);
	if( iLen < 1 ){
		/* Empty string, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Parse the setting */
	zEnd = &zSettings[iLen];
	zValue = 0;
	zName = zSettings;
	while( zSettings < zEnd ){
		if( zSettings[0] == '=' ){
			/* Null terminate the name */
			zSettings[0] = 0;
			zValue = &zSettings[1];
			break;
		}
		zSettings++;
	}
	/* Install the environment variable in the $_Env array */
	if( zValue == 0 || zName[0] == 0 || zValue >= zEnd || zName >= zValue ){
		/* Invalid settings, retun FALSE */
		jx9_result_bool(pCtx, 0);
		if( zSettings  < zEnd ){
			zSettings[0] = '=';
		}
		return JX9_OK;
	}
	jx9_vm_config(pCtx->pVm, JX9_VM_CONFIG_ENV_ATTR, zName, zValue, (int)(zEnd-zValue));
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xSetenv == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		zSettings[0] = '=';
		return JX9_OK;
	}
	/* Perform the requested operation */
	rc = pVfs->xSetenv(zName, zValue);
	jx9_result_bool(pCtx, rc == JX9_OK );
	zSettings[0] = '=';
	return JX9_OK;
}
/*
 * bool touch(string $filename[, int64 $time = time()[, int64 $atime]])
 *  Sets access and modification time of file.
 * Note: On windows
 *   If the file does not exists, it will not be created.
 * Parameters
 *  $filename
 *   The name of the file being touched.
 *  $time
 *   The touch time. If time is not supplied, the current system time is used.
 * $atime
 *   If present, the access time of the given filename is set to the value of atime.
 *   Otherwise, it is set to the value passed to the time parameter. If neither are 
 *   present, the current system time is used.
 * Return
 *  TRUE on success or FALSE on failure.
*/
static int jx9Vfs_touch(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_int64 nTime, nAccess;
	const char *zFile;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xTouch == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	nTime = nAccess = -1;
	zFile = jx9_value_to_string(apArg[0], 0);
	if( nArg > 1 ){
		nTime = jx9_value_to_int64(apArg[1]);
		if( nArg > 2 ){
			nAccess = jx9_value_to_int64(apArg[1]);
		}else{
			nAccess = nTime;
		}
	}
	rc = pVfs->xTouch(zFile, nTime, nAccess);
	/* IO result */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * Path processing functions that do not need access to the VFS layer
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */
/*
 * string dirname(string $path)
 *  Returns parent directory's path.
 * Parameters
 * $path
 *  Target path.
 *  On Windows, both slash (/) and backslash (\) are used as directory separator character.
 *  In other environments, it is the forward slash (/).
 * Return
 *  The path of the parent directory. If there are no slashes in path, a dot ('.') 
 *  is returned, indicating the current directory.
 */
static int jx9Builtin_dirname(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath, *zDir;
	int iLen, iDirlen;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return the empty string */
		jx9_result_string(pCtx, "", 0);
		return JX9_OK;
	}
	/* Point to the target path */
	zPath = jx9_value_to_string(apArg[0], &iLen);
	if( iLen < 1 ){
		/* Reuturn "." */
		jx9_result_string(pCtx, ".", sizeof(char));
		return JX9_OK;
	}
	/* Perform the requested operation */
	zDir = jx9ExtractDirName(zPath, iLen, &iDirlen);
	/* Return directory name */
	jx9_result_string(pCtx, zDir, iDirlen);
	return JX9_OK;
}
/*
 * string basename(string $path[, string $suffix ])
 *  Returns trailing name component of path.
 * Parameters
 * $path
 *  Target path.
 *  On Windows, both slash (/) and backslash (\) are used as directory separator character.
 *  In other environments, it is the forward slash (/).
 * $suffix
 *  If the name component ends in suffix this will also be cut off.
 * Return
 *  The base name of the given path. 
 */
static int jx9Builtin_basename(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath, *zBase, *zEnd;
	int c, d, iLen;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return the empty string */
		jx9_result_string(pCtx, "", 0);
		return JX9_OK;
	}
	c = d = '/';
#ifdef __WINNT__
	d = '\\';
#endif
	/* Point to the target path */
	zPath = jx9_value_to_string(apArg[0], &iLen);
	if( iLen < 1 ){
		/* Empty string */
		jx9_result_string(pCtx, "", 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	zEnd = &zPath[iLen - 1];
	/* Ignore trailing '/' */
	while( zEnd > zPath && ( (int)zEnd[0] == c || (int)zEnd[0] == d ) ){
		zEnd--;
	}
	iLen = (int)(&zEnd[1]-zPath);
	while( zEnd > zPath && ( (int)zEnd[0] != c && (int)zEnd[0] != d ) ){
		zEnd--;
	}
	zBase = (zEnd > zPath) ? &zEnd[1] : zPath;
	zEnd = &zPath[iLen];
	if( nArg > 1 && jx9_value_is_string(apArg[1]) ){
		const char *zSuffix;
		int nSuffix;
		/* Strip suffix */
		zSuffix = jx9_value_to_string(apArg[1], &nSuffix);
		if( nSuffix > 0 && nSuffix < iLen && SyMemcmp(&zEnd[-nSuffix], zSuffix, nSuffix) == 0 ){
			zEnd -= nSuffix;
		}
	}
	/* Store the basename */
	jx9_result_string(pCtx, zBase, (int)(zEnd-zBase));
	return JX9_OK;
}
/*
 * value pathinfo(string $path [, int $options = PATHINFO_DIRNAME | PATHINFO_BASENAME | PATHINFO_EXTENSION | PATHINFO_FILENAME ])
 *  Returns information about a file path.
 * Parameter
 *  $path
 *   The path to be parsed.
 *  $options
 *    If present, specifies a specific element to be returned; one of
 *      PATHINFO_DIRNAME, PATHINFO_BASENAME, PATHINFO_EXTENSION or PATHINFO_FILENAME.
 * Return
 *  If the options parameter is not passed, an associative array containing the following
 *  elements is returned: dirname, basename, extension (if any), and filename. 
 *  If options is present, returns a string containing the requested element. 
 */
typedef struct path_info path_info;
struct path_info
{
	SyString sDir; /* Directory [i.e: /var/www] */
	SyString sBasename; /* Basename [i.e httpd.conf] */
	SyString sExtension; /* File extension [i.e xml, pdf..] */
	SyString sFilename;  /* Filename */
};
/*
 * Extract path fields.
 */
static sxi32 ExtractPathInfo(const char *zPath, int nByte, path_info *pOut)
{
	const char *zPtr, *zEnd = &zPath[nByte - 1];
	SyString *pCur;
	int c, d;
	c = d = '/';
#ifdef __WINNT__
	d = '\\';
#endif
	/* Zero the structure */
	SyZero(pOut, sizeof(path_info));
	/* Handle special case */
	if( nByte == sizeof(char) && ( (int)zPath[0] == c || (int)zPath[0] == d ) ){
#ifdef __WINNT__
		SyStringInitFromBuf(&pOut->sDir, "\\", sizeof(char));
#else
		SyStringInitFromBuf(&pOut->sDir, "/", sizeof(char));
#endif
		return SXRET_OK;
	}
	/* Extract the basename */
	while( zEnd > zPath && ( (int)zEnd[0] != c && (int)zEnd[0] != d ) ){
		zEnd--;
	}
	zPtr = (zEnd > zPath) ? &zEnd[1] : zPath;
	zEnd = &zPath[nByte];
	/* dirname */
	pCur = &pOut->sDir;
	SyStringInitFromBuf(pCur, zPath, zPtr-zPath);
	if( pCur->nByte > 1 ){
		SyStringTrimTrailingChar(pCur, '/');
#ifdef __WINNT__
		SyStringTrimTrailingChar(pCur, '\\');
#endif
	}else if( (int)zPath[0] == c || (int)zPath[0] == d ){
#ifdef __WINNT__
		SyStringInitFromBuf(&pOut->sDir, "\\", sizeof(char));
#else
		SyStringInitFromBuf(&pOut->sDir, "/", sizeof(char));
#endif
	}
	/* basename/filename */
	pCur = &pOut->sBasename;
	SyStringInitFromBuf(pCur, zPtr, zEnd-zPtr);
	SyStringTrimLeadingChar(pCur, '/');
#ifdef __WINNT__
	SyStringTrimLeadingChar(pCur, '\\');
#endif
	SyStringDupPtr(&pOut->sFilename, pCur);
	if( pCur->nByte > 0 ){
		/* extension */
		zEnd--;
		while( zEnd > pCur->zString /*basename*/ && zEnd[0] != '.' ){
			zEnd--;
		}
		if( zEnd > pCur->zString ){
			zEnd++; /* Jump leading dot */
			SyStringInitFromBuf(&pOut->sExtension, zEnd, &zPath[nByte]-zEnd);
			/* Fix filename */
			pCur = &pOut->sFilename;
			if( pCur->nByte > SyStringLength(&pOut->sExtension) ){
				pCur->nByte -= 1 + SyStringLength(&pOut->sExtension);
			}
		}
	}
	return SXRET_OK;
}
/*
 * value pathinfo(string $path [, int $options = PATHINFO_DIRNAME | PATHINFO_BASENAME | PATHINFO_EXTENSION | PATHINFO_FILENAME ])
 *  See block comment above.
 */
static int jx9Builtin_pathinfo(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zPath;
	path_info sInfo;
	SyString *pComp;
	int iLen;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid argument, return the empty string */
		jx9_result_string(pCtx, "", 0);
		return JX9_OK;
	}
	/* Point to the target path */
	zPath = jx9_value_to_string(apArg[0], &iLen);
	if( iLen < 1 ){
		/* Empty string */
		jx9_result_string(pCtx, "", 0);
		return JX9_OK;
	}
	/* Extract path info */
	ExtractPathInfo(zPath, iLen, &sInfo);
	if( nArg > 1 && jx9_value_is_int(apArg[1]) ){
		/* Return path component */
		int nComp = jx9_value_to_int(apArg[1]);
		switch(nComp){
		case 1: /* PATHINFO_DIRNAME */
			pComp = &sInfo.sDir;
			if( pComp->nByte > 0 ){
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}else{
				/* Expand the empty string */
				jx9_result_string(pCtx, "", 0);
			}
			break;
		case 2: /*PATHINFO_BASENAME*/
			pComp = &sInfo.sBasename;
			if( pComp->nByte > 0 ){
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}else{
				/* Expand the empty string */
				jx9_result_string(pCtx, "", 0);
			}
			break;
		case 3: /*PATHINFO_EXTENSION*/
			pComp = &sInfo.sExtension;
			if( pComp->nByte > 0 ){
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}else{
				/* Expand the empty string */
				jx9_result_string(pCtx, "", 0);
			}
			break;
		case 4: /*PATHINFO_FILENAME*/
			pComp = &sInfo.sFilename;
			if( pComp->nByte > 0 ){
				jx9_result_string(pCtx, pComp->zString, (int)pComp->nByte);
			}else{
				/* Expand the empty string */
				jx9_result_string(pCtx, "", 0);
			}
			break;
		default:
			/* Expand the empty string */
			jx9_result_string(pCtx, "", 0);
			break;
		}
	}else{
		/* Return an associative array */
		jx9_value *pArray, *pValue;
		pArray = jx9_context_new_array(pCtx);
		pValue = jx9_context_new_scalar(pCtx);
		if( pArray == 0 || pValue == 0 ){
			/* Out of mem, return NULL */
			jx9_result_bool(pCtx, 0);
			return JX9_OK;
		}
		/* dirname */
		pComp = &sInfo.sDir;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			/* Perform the insertion */
			jx9_array_add_strkey_elem(pArray, "dirname", pValue); /* Will make it's own copy */
		}
		/* Reset the string cursor */
		jx9_value_reset_string_cursor(pValue);
		/* basername */
		pComp = &sInfo.sBasename;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			/* Perform the insertion */
			jx9_array_add_strkey_elem(pArray, "basename", pValue); /* Will make it's own copy */
		}
		/* Reset the string cursor */
		jx9_value_reset_string_cursor(pValue);
		/* extension */
		pComp = &sInfo.sExtension;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			/* Perform the insertion */
			jx9_array_add_strkey_elem(pArray, "extension", pValue); /* Will make it's own copy */
		}
		/* Reset the string cursor */
		jx9_value_reset_string_cursor(pValue);
		/* filename */
		pComp = &sInfo.sFilename;
		if( pComp->nByte > 0 ){
			jx9_value_string(pValue, pComp->zString, (int)pComp->nByte);
			/* Perform the insertion */
			jx9_array_add_strkey_elem(pArray, "filename", pValue); /* Will make it's own copy */
		}
		/* Return the created array */
		jx9_result_value(pCtx, pArray);
		/* Don't worry about freeing memory, everything will be released
		 * automatically as soon we return from this foreign function.
		 */
	}
	return JX9_OK;
}
/*
 * Globbing implementation extracted from the sqlite3 source tree.
 * Original author: D. Richard Hipp (http://www.sqlite.org)
 * Status: Public Domain
 */
typedef unsigned char u8;
/* An array to map all upper-case characters into their corresponding
** lower-case character. 
**
** SQLite only considers US-ASCII (or EBCDIC) characters.  We do not
** handle case conversions for the UTF character set since the tables
** involved are nearly as big or bigger than SQLite itself.
*/
static const unsigned char sqlite3UpperToLower[] = {
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 
     18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 
     36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 
     54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 97, 98, 99, 100, 101, 102, 103, 
    104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 
    122, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 
    108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 
    126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 
    144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 
    162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 
    198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 
    216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 
    234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 
    252, 253, 254, 255
};
#define GlogUpperToLower(A)     if( A<0x80 ){ A = sqlite3UpperToLower[A]; }
/*
** Assuming zIn points to the first byte of a UTF-8 character, 
** advance zIn to point to the first byte of the next UTF-8 character.
*/
#define SQLITE_SKIP_UTF8(zIn) {                        \
  if( (*(zIn++))>=0xc0 ){                              \
    while( (*zIn & 0xc0)==0x80 ){ zIn++; }             \
  }                                                    \
}
/*
** Compare two UTF-8 strings for equality where the first string can
** potentially be a "glob" expression.  Return true (1) if they
** are the same and false (0) if they are different.
**
** Globbing rules:
**
**      '*'       Matches any sequence of zero or more characters.
**
**      '?'       Matches exactly one character.
**
**     [...]      Matches one character from the enclosed list of
**                characters.
**
**     [^...]     Matches one character not in the enclosed list.
**
** With the [...] and [^...] matching, a ']' character can be included
** in the list by making it the first character after '[' or '^'.  A
** range of characters can be specified using '-'.  Example:
** "[a-z]" matches any single lower-case letter.  To match a '-', make
** it the last character in the list.
**
** This routine is usually quick, but can be N**2 in the worst case.
**
** Hints: to match '*' or '?', put them in "[]".  Like this:
**
**         abc[*]xyz        Matches "abc*xyz" only
*/
static int patternCompare(
  const u8 *zPattern,              /* The glob pattern */
  const u8 *zString,               /* The string to compare against the glob */
  const int esc,                    /* The escape character */
  int noCase
){
  int c, c2;
  int invert;
  int seen;
  u8 matchOne = '?';
  u8 matchAll = '*';
  u8 matchSet = '[';
  int prevEscape = 0;     /* True if the previous character was 'escape' */

  if( !zPattern || !zString ) return 0;
  while( (c = jx9Utf8Read(zPattern, 0, &zPattern))!=0 ){
    if( !prevEscape && c==matchAll ){
      while( (c= jx9Utf8Read(zPattern, 0, &zPattern)) == matchAll
               || c == matchOne ){
        if( c==matchOne && jx9Utf8Read(zString, 0, &zString)==0 ){
          return 0;
        }
      }
      if( c==0 ){
        return 1;
      }else if( c==esc ){
        c = jx9Utf8Read(zPattern, 0, &zPattern);
        if( c==0 ){
          return 0;
        }
      }else if( c==matchSet ){
	  if( (esc==0) || (matchSet<0x80) ) return 0;
	  while( *zString && patternCompare(&zPattern[-1], zString, esc, noCase)==0 ){
          SQLITE_SKIP_UTF8(zString);
        }
        return *zString!=0;
      }
      while( (c2 = jx9Utf8Read(zString, 0, &zString))!=0 ){
        if( noCase ){
          GlogUpperToLower(c2);
          GlogUpperToLower(c);
          while( c2 != 0 && c2 != c ){
            c2 = jx9Utf8Read(zString, 0, &zString);
            GlogUpperToLower(c2);
          }
        }else{
          while( c2 != 0 && c2 != c ){
            c2 = jx9Utf8Read(zString, 0, &zString);
          }
        }
        if( c2==0 ) return 0;
		if( patternCompare(zPattern, zString, esc, noCase) ) return 1;
      }
      return 0;
    }else if( !prevEscape && c==matchOne ){
      if( jx9Utf8Read(zString, 0, &zString)==0 ){
        return 0;
      }
    }else if( c==matchSet ){
      int prior_c = 0;
      if( esc == 0 ) return 0;
      seen = 0;
      invert = 0;
      c = jx9Utf8Read(zString, 0, &zString);
      if( c==0 ) return 0;
      c2 = jx9Utf8Read(zPattern, 0, &zPattern);
      if( c2=='^' ){
        invert = 1;
        c2 = jx9Utf8Read(zPattern, 0, &zPattern);
      }
      if( c2==']' ){
        if( c==']' ) seen = 1;
        c2 = jx9Utf8Read(zPattern, 0, &zPattern);
      }
      while( c2 && c2!=']' ){
        if( c2=='-' && zPattern[0]!=']' && zPattern[0]!=0 && prior_c>0 ){
          c2 = jx9Utf8Read(zPattern, 0, &zPattern);
          if( c>=prior_c && c<=c2 ) seen = 1;
          prior_c = 0;
        }else{
          if( c==c2 ){
            seen = 1;
          }
          prior_c = c2;
        }
        c2 = jx9Utf8Read(zPattern, 0, &zPattern);
      }
      if( c2==0 || (seen ^ invert)==0 ){
        return 0;
      }
    }else if( esc==c && !prevEscape ){
      prevEscape = 1;
    }else{
      c2 = jx9Utf8Read(zString, 0, &zString);
      if( noCase ){
        GlogUpperToLower(c);
        GlogUpperToLower(c2);
      }
      if( c!=c2 ){
        return 0;
      }
      prevEscape = 0;
    }
  }
  return *zString==0;
}
/*
 * Wrapper around patternCompare() defined above.
 * See block comment above for more information.
 */
static int Glob(const unsigned char *zPattern, const unsigned char *zString, int iEsc, int CaseCompare)
{
	int rc;
	if( iEsc < 0 ){
		iEsc = '\\';
	}
	rc = patternCompare(zPattern, zString, iEsc, CaseCompare);
	return rc;
}
/*
 * bool fnmatch(string $pattern, string $string[, int $flags = 0 ])
 *  Match filename against a pattern.
 * Parameters
 *  $pattern
 *   The shell wildcard pattern.
 * $string
 *  The tested string.
 * $flags
 *   A list of possible flags:
 *    FNM_NOESCAPE 	Disable backslash escaping.
 *    FNM_PATHNAME 	Slash in string only matches slash in the given pattern.
 *    FNM_PERIOD 	Leading period in string must be exactly matched by period in the given pattern.
 *    FNM_CASEFOLD 	Caseless match.
 * Return
 *  TRUE if there is a match, FALSE otherwise. 
 */
static int jx9Builtin_fnmatch(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zString, *zPattern;
	int iEsc = '\\';
	int noCase = 0;
	int rc;
	if( nArg < 2 || !jx9_value_is_string(apArg[0]) || !jx9_value_is_string(apArg[1]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the pattern and the string */
	zPattern  = jx9_value_to_string(apArg[0], 0);
	zString = jx9_value_to_string(apArg[1], 0);
	/* Extract the flags if avaialble */
	if( nArg > 2 && jx9_value_is_int(apArg[2]) ){
		rc = jx9_value_to_int(apArg[2]);
		if( rc & 0x01 /*FNM_NOESCAPE*/){
			iEsc = 0;
		}
		if( rc & 0x08 /*FNM_CASEFOLD*/){
			noCase = 1;
		}
	}
	/* Go globbing */
	rc = Glob((const unsigned char *)zPattern, (const unsigned char *)zString, iEsc, noCase);
	/* Globbing result */
	jx9_result_bool(pCtx, rc);
	return JX9_OK;
}
/*
 * bool strglob(string $pattern, string $string)
 *  Match string against a pattern.
 * Parameters
 *  $pattern
 *   The shell wildcard pattern.
 * $string
 *  The tested string.
 * Return
 *  TRUE if there is a match, FALSE otherwise. 
 */
static int jx9Builtin_strglob(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zString, *zPattern;
	int iEsc = '\\';
	int rc;
	if( nArg < 2 || !jx9_value_is_string(apArg[0]) || !jx9_value_is_string(apArg[1]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the pattern and the string */
	zPattern  = jx9_value_to_string(apArg[0], 0);
	zString = jx9_value_to_string(apArg[1], 0);
	/* Go globbing */
	rc = Glob((const unsigned char *)zPattern, (const unsigned char *)zString, iEsc, 0);
	/* Globbing result */
	jx9_result_bool(pCtx, rc);
	return JX9_OK;
}
/*
 * bool link(string $target, string $link)
 *  Create a hard link.
 * Parameters
 *  $target
 *   Target of the link.
 *  $link
 *   The link name.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_link(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zTarget, *zLink;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 2 || !jx9_value_is_string(apArg[0]) || !jx9_value_is_string(apArg[1]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xLink == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the given arguments */
	zTarget  = jx9_value_to_string(apArg[0], 0);
	zLink = jx9_value_to_string(apArg[1], 0);
	/* Perform the requested operation */
	rc = pVfs->xLink(zTarget, zLink, 0/*Not a symbolic link */);
	/* IO result */
	jx9_result_bool(pCtx, rc == JX9_OK );
	return JX9_OK;
}
/*
 * bool symlink(string $target, string $link)
 *  Creates a symbolic link.
 * Parameters
 *  $target
 *   Target of the link.
 *  $link
 *   The link name.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Vfs_symlink(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zTarget, *zLink;
	jx9_vfs *pVfs;
	int rc;
	if( nArg < 2 || !jx9_value_is_string(apArg[0]) || !jx9_value_is_string(apArg[1]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xLink == 0 ){
		/* IO routine not implemented, return NULL */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS, JX9 is returning FALSE", 
			jx9_function_name(pCtx)
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the given arguments */
	zTarget  = jx9_value_to_string(apArg[0], 0);
	zLink = jx9_value_to_string(apArg[1], 0);
	/* Perform the requested operation */
	rc = pVfs->xLink(zTarget, zLink, 1/*A symbolic link */);
	/* IO result */
	jx9_result_bool(pCtx, rc == JX9_OK );
	return JX9_OK;
}
/*
 * int umask([ int $mask ])
 *  Changes the current umask.
 * Parameters
 *  $mask
 *   The new umask.
 * Return
 *  umask() without arguments simply returns the current umask.
 *  Otherwise the old umask is returned.
 */
static int jx9Vfs_umask(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	int iOld, iNew;
	jx9_vfs *pVfs;
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xUmask == 0 ){
		/* IO routine not implemented, return -1 */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS", 
			jx9_function_name(pCtx)
			);
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	iNew = 0;
	if( nArg > 0 ){
		iNew = jx9_value_to_int(apArg[0]);
	}
	/* Perform the requested operation */
	iOld = pVfs->xUmask(iNew);
	/* Old mask */
	jx9_result_int(pCtx, iOld);
	return JX9_OK;
}
/*
 * string sys_get_temp_dir()
 *  Returns directory path used for temporary files.
 * Parameters
 *  None
 * Return
 *  Returns the path of the temporary directory.
 */
static int jx9Vfs_sys_get_temp_dir(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_vfs *pVfs;
	/* Set the empty string as the default return value */
	jx9_result_string(pCtx, "", 0);
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xTempDir == 0 ){
		SXUNUSED(nArg); /* cc warning */
		SXUNUSED(apArg);
		/* IO routine not implemented, return "" */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS", 
			jx9_function_name(pCtx)
			);		
		return JX9_OK;
	}
	/* Perform the requested operation */
	pVfs->xTempDir(pCtx);
	return JX9_OK;
}
/*
 * string get_current_user()
 *  Returns the name of the current working user.
 * Parameters
 *  None
 * Return
 *  Returns the name of the current working user.
 */
static int jx9Vfs_get_current_user(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_vfs *pVfs;
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xUsername == 0 ){
		SXUNUSED(nArg); /* cc warning */
		SXUNUSED(apArg);
		/* IO routine not implemented */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS", 
			jx9_function_name(pCtx)
			);		
		/* Set a dummy username */
		jx9_result_string(pCtx, "unknown", sizeof("unknown")-1);
		return JX9_OK;
	}
	/* Perform the requested operation */
	pVfs->xUsername(pCtx);
	return JX9_OK;
}
/*
 * int64 getmypid()
 *  Gets process ID.
 * Parameters
 *  None
 * Return
 *  Returns the process ID.
 */
static int jx9Vfs_getmypid(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_int64 nProcessId;
	jx9_vfs *pVfs;
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xProcessId == 0 ){
		SXUNUSED(nArg); /* cc warning */
		SXUNUSED(apArg);
		/* IO routine not implemented, return -1 */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS", 
			jx9_function_name(pCtx)
			);
		jx9_result_int(pCtx, -1);
		return JX9_OK;
	}
	/* Perform the requested operation */
	nProcessId = (jx9_int64)pVfs->xProcessId();
	/* Set the result */
	jx9_result_int64(pCtx, nProcessId);
	return JX9_OK;
}
/*
 * int getmyuid()
 *  Get user ID.
 * Parameters
 *  None
 * Return
 *  Returns the user ID.
 */
static int jx9Vfs_getmyuid(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_vfs *pVfs;
	int nUid;
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xUid == 0 ){
		SXUNUSED(nArg); /* cc warning */
		SXUNUSED(apArg);
		/* IO routine not implemented, return -1 */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS", 
			jx9_function_name(pCtx)
			);
		jx9_result_int(pCtx, -1);
		return JX9_OK;
	}
	/* Perform the requested operation */
	nUid = pVfs->xUid();
	/* Set the result */
	jx9_result_int(pCtx, nUid);
	return JX9_OK;
}
/*
 * int getmygid()
 *  Get group ID.
 * Parameters
 *  None
 * Return
 *  Returns the group ID.
 */
static int jx9Vfs_getmygid(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_vfs *pVfs;
	int nGid;
	/* Point to the underlying vfs */
	pVfs = (jx9_vfs *)jx9_context_user_data(pCtx);
	if( pVfs == 0 || pVfs->xGid == 0 ){
		SXUNUSED(nArg); /* cc warning */
		SXUNUSED(apArg);
		/* IO routine not implemented, return -1 */
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying VFS", 
			jx9_function_name(pCtx)
			);
		jx9_result_int(pCtx, -1);
		return JX9_OK;
	}
	/* Perform the requested operation */
	nGid = pVfs->xGid();
	/* Set the result */
	jx9_result_int(pCtx, nGid);
	return JX9_OK;
}
#ifdef __WINNT__
#include <Windows.h>
#elif defined(__UNIXES__)
#include <sys/utsname.h>
#endif
/*
 * string uname([ string $mode = "a" ])
 *  Returns information about the host operating system.
 * Parameters
 *  $mode
 *   mode is a single character that defines what information is returned:
 *    'a': This is the default. Contains all modes in the sequence "s n r v m".
 *    's': Operating system name. eg. FreeBSD.
 *    'n': Host name. eg. localhost.example.com.
 *    'r': Release name. eg. 5.1.2-RELEASE.
 *    'v': Version information. Varies a lot between operating systems.
 *    'm': Machine type. eg. i386.
 * Return
 *  OS description as a string.
 */
static int jx9Vfs_uname(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
#if defined(__WINNT__)
	const char *zName = "Microsoft Windows";
	OSVERSIONINFOW sVer;
#elif defined(__UNIXES__)
	struct utsname sName;
#endif
	const char *zMode = "a";
	if( nArg > 0 && jx9_value_is_string(apArg[0]) ){
		/* Extract the desired mode */
		zMode = jx9_value_to_string(apArg[0], 0);
	}
#if defined(__WINNT__)
	sVer.dwOSVersionInfoSize = sizeof(sVer);
	if( TRUE != GetVersionExW(&sVer)){
		jx9_result_string(pCtx, zName, -1);
		return JX9_OK;
	}
	if( sVer.dwPlatformId == VER_PLATFORM_WIN32_NT ){
		if( sVer.dwMajorVersion <= 4 ){
			zName = "Microsoft Windows NT";
		}else if( sVer.dwMajorVersion == 5 ){
			switch(sVer.dwMinorVersion){
				case 0:	zName = "Microsoft Windows 2000"; break;
				case 1: zName = "Microsoft Windows XP";   break;
				case 2: zName = "Microsoft Windows Server 2003"; break;
			}
		}else if( sVer.dwMajorVersion == 6){
				switch(sVer.dwMinorVersion){
					case 0: zName = "Microsoft Windows Vista"; break;
					case 1: zName = "Microsoft Windows 7"; break;
					case 2: zName = "Microsoft Windows 8"; break;
					default: break;
				}
		}
	}
	switch(zMode[0]){
	case 's':
		/* Operating system name */
		jx9_result_string(pCtx, zName, -1/* Compute length automatically*/);
		break;
	case 'n':
		/* Host name */
		jx9_result_string(pCtx, "localhost", (int)sizeof("localhost")-1);
		break;
	case 'r':
	case 'v':
		/* Version information. */
		jx9_result_string_format(pCtx, "%u.%u build %u", 
			sVer.dwMajorVersion, sVer.dwMinorVersion, sVer.dwBuildNumber
			);
		break;
	case 'm':
		/* Machine name */
		jx9_result_string(pCtx, "x86", (int)sizeof("x86")-1);
		break;
	default:
		jx9_result_string_format(pCtx, "%s localhost %u.%u build %u x86", 
			zName, 
			sVer.dwMajorVersion, sVer.dwMinorVersion, sVer.dwBuildNumber
			);
		break;
	}
#elif defined(__UNIXES__)
	if( uname(&sName) != 0 ){
		jx9_result_string(pCtx, "Unix", (int)sizeof("Unix")-1);
		return JX9_OK;
	}
	switch(zMode[0]){
	case 's':
		/* Operating system name */
		jx9_result_string(pCtx, sName.sysname, -1/* Compute length automatically*/);
		break;
	case 'n':
		/* Host name */
		jx9_result_string(pCtx, sName.nodename, -1/* Compute length automatically*/);
		break;
	case 'r':
		/* Release information */
		jx9_result_string(pCtx, sName.release, -1/* Compute length automatically*/);
		break;
	case 'v':
		/* Version information. */
		jx9_result_string(pCtx, sName.version, -1/* Compute length automatically*/);
		break;
	case 'm':
		/* Machine name */
		jx9_result_string(pCtx, sName.machine, -1/* Compute length automatically*/);
		break;
	default:
		jx9_result_string_format(pCtx, 
			"%s %s %s %s %s", 
			sName.sysname, 
			sName.release, 
			sName.version, 
			sName.nodename, 
			sName.machine
			);
		break;
	}
#else
	jx9_result_string(pCtx, "Host Operating System/localhost", (int)sizeof("Host Operating System/localhost")-1);
#endif
	return JX9_OK;
}
/*
 * Section:
 *    IO stream implementation.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */
typedef struct io_private io_private;
struct io_private
{
	const jx9_io_stream *pStream; /* Underlying IO device */
	void *pHandle; /* IO handle */
	/* Unbuffered IO */
	SyBlob sBuffer; /* Working buffer */
	sxu32 nOfft;    /* Current read offset */
	sxu32 iMagic;   /* Sanity check to avoid misuse */
};
#define IO_PRIVATE_MAGIC 0xFEAC14
/* Make sure we are dealing with a valid io_private instance */
#define IO_PRIVATE_INVALID(IO) ( IO == 0 || IO->iMagic != IO_PRIVATE_MAGIC ) 
/* Forward declaration */
static void ResetIOPrivate(io_private *pDev);
/*
 * bool ftruncate(resource $handle, int64 $size)
 *  Truncates a file to a given length.
 * Parameters
 *  $handle
 *   The file pointer.
 *   Note:
 *    The handle must be open for writing.
 * $size
 *   The size to truncate to.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Builtin_ftruncate(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	int rc;
	if( nArg < 2 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xTrunc == 0){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	rc = pStream->xTrunc(pDev->pHandle, jx9_value_to_int64(apArg[1]));
	if( rc == JX9_OK ){
		/* Discard buffered data */
		ResetIOPrivate(pDev);
	}
	/* IO result */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;	
}
/*
 * int fseek(resource $handle, int $offset[, int $whence = SEEK_SET ])
 *  Seeks on a file pointer.
 * Parameters
 *  $handle
 *   A file system pointer resource that is typically created using fopen().
 * $offset
 *   The offset.
 *   To move to a position before the end-of-file, you need to pass a negative
 *   value in offset and set whence to SEEK_END.
 *   whence
 *   whence values are:
 *    SEEK_SET - Set position equal to offset bytes.
 *    SEEK_CUR - Set position to current location plus offset.
 *    SEEK_END - Set position to end-of-file plus offset.
 * Return
 *  0 on success, -1 on failure
 */
static int jx9Builtin_fseek(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	jx9_int64 iOfft;
	int whence;  
	int rc;
	if( nArg < 2 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_int(pCtx, -1);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_int(pCtx, -1);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xSeek == 0){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_int(pCtx, -1);
		return JX9_OK;
	}
	/* Extract the offset */
	iOfft = jx9_value_to_int64(apArg[1]);
	whence = 0;/* SEEK_SET */
	if( nArg > 2 && jx9_value_is_int(apArg[2]) ){
		whence = jx9_value_to_int(apArg[2]);
	}
	/* Perform the requested operation */
	rc = pStream->xSeek(pDev->pHandle, iOfft, whence);
	if( rc == JX9_OK ){
		/* Ignore buffered data */
		ResetIOPrivate(pDev);
	}
	/* IO result */
	jx9_result_int(pCtx, rc == JX9_OK ? 0 : - 1);
	return JX9_OK;	
}
/*
 * int64 ftell(resource $handle)
 *  Returns the current position of the file read/write pointer.
 * Parameters
 *  $handle
 *   The file pointer.
 * Return
 *  Returns the position of the file pointer referenced by handle
 *  as an integer; i.e., its offset into the file stream.
 *  FALSE is returned on failure.
 */
static int jx9Builtin_ftell(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	jx9_int64 iOfft;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xTell == 0){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	iOfft = pStream->xTell(pDev->pHandle);
	/* IO result */
	jx9_result_int64(pCtx, iOfft);
	return JX9_OK;	
}
/*
 * bool rewind(resource $handle)
 *  Rewind the position of a file pointer.
 * Parameters
 *  $handle
 *   The file pointer.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Builtin_rewind(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	int rc;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xSeek == 0){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	rc = pStream->xSeek(pDev->pHandle, 0, 0/*SEEK_SET*/);
	if( rc == JX9_OK ){
		/* Ignore buffered data */
		ResetIOPrivate(pDev);
	}
	/* IO result */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;	
}
/*
 * bool fflush(resource $handle)
 *  Flushes the output to a file.
 * Parameters
 *  $handle
 *   The file pointer.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Builtin_fflush(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	int rc;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0 || pStream->xSync == 0){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	rc = pStream->xSync(pDev->pHandle);
	/* IO result */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;	
}
/*
 * bool feof(resource $handle)
 *  Tests for end-of-file on a file pointer.
 * Parameters
 *  $handle
 *   The file pointer.
 * Return
 *  Returns TRUE if the file pointer is at EOF.FALSE otherwise
 */
static int jx9Builtin_feof(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	int rc;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 1);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 1);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 1);
		return JX9_OK;
	}
	rc = SXERR_EOF;
	/* Perform the requested operation */
	if( SyBlobLength(&pDev->sBuffer) - pDev->nOfft > 0 ){
		/* Data is available */
		rc = JX9_OK;
	}else{
		char zBuf[4096];
		jx9_int64 n;
		/* Perform a buffered read */
		n = pStream->xRead(pDev->pHandle, zBuf, sizeof(zBuf));
		if( n > 0 ){
			/* Copy buffered data */
			SyBlobAppend(&pDev->sBuffer, zBuf, (sxu32)n);
			rc = JX9_OK;
		}
	}
	/* EOF or not */
	jx9_result_bool(pCtx, rc == SXERR_EOF);
	return JX9_OK;	
}
/*
 * Read n bytes from the underlying IO stream device.
 * Return total numbers of bytes readen on success. A number < 1 on failure 
 * [i.e: IO error ] or EOF.
 */
static jx9_int64 StreamRead(io_private *pDev, void *pBuf, jx9_int64 nLen)
{
	const jx9_io_stream *pStream = pDev->pStream;
	char *zBuf = (char *)pBuf;
	jx9_int64 n, nRead;
	n = SyBlobLength(&pDev->sBuffer) - pDev->nOfft;
	if( n > 0 ){
		if( n > nLen ){
			n = nLen;
		}
		/* Copy the buffered data */
		SyMemcpy(SyBlobDataAt(&pDev->sBuffer, pDev->nOfft), pBuf, (sxu32)n);
		/* Update the read offset */
		pDev->nOfft += (sxu32)n;
		if( pDev->nOfft >= SyBlobLength(&pDev->sBuffer) ){
			/* Reset the working buffer so that we avoid excessive memory allocation */
			SyBlobReset(&pDev->sBuffer);
			pDev->nOfft = 0;
		}
		nLen -= n;
		if( nLen < 1 ){
			/* All done */
			return n;
		}
		/* Advance the cursor */
		zBuf += n;
	}
	/* Read without buffering */
	nRead = pStream->xRead(pDev->pHandle, zBuf, nLen);
	if( nRead > 0 ){
		n += nRead;
	}else if( n < 1 ){
		/* EOF or IO error */
		return nRead;
	}
	return n;
}
/*
 * Extract a single line from the buffered input.
 */
static sxi32 GetLine(io_private *pDev, jx9_int64 *pLen, const char **pzLine)
{
	const char *zIn, *zEnd, *zPtr;
	zIn = (const char *)SyBlobDataAt(&pDev->sBuffer, pDev->nOfft);
	zEnd = &zIn[SyBlobLength(&pDev->sBuffer)-pDev->nOfft];
	zPtr = zIn;
	while( zIn < zEnd ){
		if( zIn[0] == '\n' ){
			/* Line found */
			zIn++; /* Include the line ending as requested by the JX9 specification */
			*pLen = (jx9_int64)(zIn-zPtr);
			*pzLine = zPtr;
			return SXRET_OK;
		}
		zIn++;
	}
	/* No line were found */
	return SXERR_NOTFOUND;
}
/*
 * Read a single line from the underlying IO stream device.
 */
static jx9_int64 StreamReadLine(io_private *pDev, const char **pzData, jx9_int64 nMaxLen)
{
	const jx9_io_stream *pStream = pDev->pStream;
	char zBuf[8192];
	jx9_int64 n;
	sxi32 rc;
	n = 0;
	if( pDev->nOfft >= SyBlobLength(&pDev->sBuffer) ){
		/* Reset the working buffer so that we avoid excessive memory allocation */
		SyBlobReset(&pDev->sBuffer);
		pDev->nOfft = 0;
	}
	if( SyBlobLength(&pDev->sBuffer) - pDev->nOfft > 0 ){
		/* Check if there is a line */
		rc = GetLine(pDev, &n, pzData);
		if( rc == SXRET_OK ){
			/* Got line, update the cursor  */
			pDev->nOfft += (sxu32)n;
			return n;
		}
	}
	/* Perform the read operation until a new line is extracted or length
	 * limit is reached.
	 */
	for(;;){
		n = pStream->xRead(pDev->pHandle, zBuf, (nMaxLen > 0 && nMaxLen < sizeof(zBuf)) ? nMaxLen : sizeof(zBuf));
		if( n < 1 ){
			/* EOF or IO error */
			break;
		}
		/* Append the data just read */
		SyBlobAppend(&pDev->sBuffer, zBuf, (sxu32)n);
		/* Try to extract a line */
		rc = GetLine(pDev, &n, pzData);
		if( rc == SXRET_OK ){
			/* Got one, return immediately */
			pDev->nOfft += (sxu32)n;
			return n;
		}
		if( nMaxLen > 0 && (SyBlobLength(&pDev->sBuffer) - pDev->nOfft >= nMaxLen) ){
			/* Read limit reached, return the available data */
			*pzData = (const char *)SyBlobDataAt(&pDev->sBuffer, pDev->nOfft);
			n = SyBlobLength(&pDev->sBuffer) - pDev->nOfft;
			/* Reset the working buffer */
			SyBlobReset(&pDev->sBuffer);
			pDev->nOfft = 0;
			return n;
		}
	}
	if( SyBlobLength(&pDev->sBuffer) - pDev->nOfft > 0 ){
		/* Read limit reached, return the available data */
		*pzData = (const char *)SyBlobDataAt(&pDev->sBuffer, pDev->nOfft);
		n = SyBlobLength(&pDev->sBuffer) - pDev->nOfft;
		/* Reset the working buffer */
		SyBlobReset(&pDev->sBuffer);
		pDev->nOfft = 0;
	}
	return n;
}
/*
 * Open an IO stream handle.
 * Notes on stream:
 * According to the JX9 reference manual.
 * In its simplest definition, a stream is a resource object which exhibits streamable behavior.
 * That is, it can be read from or written to in a linear fashion, and may be able to fseek() 
 * to an arbitrary locations within the stream.
 * A wrapper is additional code which tells the stream how to handle specific protocols/encodings.
 * For example, the http wrapper knows how to translate a URL into an HTTP/1.0 request for a file
 * on a remote server.
 * A stream is referenced as: scheme://target
 *   scheme(string) - The name of the wrapper to be used. Examples include: file, http...
 *   If no wrapper is specified, the function default is used (typically file://).
 *   target - Depends on the wrapper used. For filesystem related streams this is typically a path
 *  and filename of the desired file. For network related streams this is typically a hostname, often
 *  with a path appended. 
 *
 * Note that JX9 IO streams looks like JX9 streams but their implementation differ greately.
 * Please refer to the official documentation for a full discussion.
 * This function return a handle on success. Otherwise null.
 */
JX9_PRIVATE void * jx9StreamOpenHandle(jx9_vm *pVm, const jx9_io_stream *pStream, const char *zFile, 
	int iFlags, int use_include, jx9_value *pResource, int bPushInclude, int *pNew)
{
	void *pHandle = 0; /* cc warning */
	SyString sFile;
	int rc;
	if( pStream == 0 ){
		/* No such stream device */
		return 0;
	}
	SyStringInitFromBuf(&sFile, zFile, SyStrlen(zFile));
	if( use_include ){
		if(	sFile.zString[0] == '/' ||
#ifdef __WINNT__
			(sFile.nByte > 2 && sFile.zString[1] == ':' && (sFile.zString[2] == '\\' || sFile.zString[2] == '/') ) ||
#endif
			(sFile.nByte > 1 && sFile.zString[0] == '.' && sFile.zString[1] == '/') ||
			(sFile.nByte > 2 && sFile.zString[0] == '.' && sFile.zString[1] == '.' && sFile.zString[2] == '/') ){
				/*  Open the file directly */
				rc = pStream->xOpen(zFile, iFlags, pResource, &pHandle);
		}else{
			SyString *pPath;
			SyBlob sWorker;
#ifdef __WINNT__
			static const int c = '\\';
#else
			static const int c = '/';
#endif
			/* Init the path builder working buffer */
			SyBlobInit(&sWorker, &pVm->sAllocator);
			/* Build a path from the set of include path */
			SySetResetCursor(&pVm->aPaths);
			rc = SXERR_IO;
			while( SXRET_OK == SySetGetNextEntry(&pVm->aPaths, (void **)&pPath) ){
				/* Build full path */
				SyBlobFormat(&sWorker, "%z%c%z", pPath, c, &sFile);
				/* Append null terminator */
				if( SXRET_OK != SyBlobNullAppend(&sWorker) ){
					continue;
				}
				/* Try to open the file */
				rc = pStream->xOpen((const char *)SyBlobData(&sWorker), iFlags, pResource, &pHandle);
				if( rc == JX9_OK ){
					if( bPushInclude ){
						/* Mark as included */
						jx9VmPushFilePath(pVm, (const char *)SyBlobData(&sWorker), SyBlobLength(&sWorker), FALSE, pNew);
					}
					break;
				}
				/* Reset the working buffer */
				SyBlobReset(&sWorker);
				/* Check the next path */
			}
			SyBlobRelease(&sWorker);
		}
		if( rc == JX9_OK ){
			if( bPushInclude ){
				/* Mark as included */
				jx9VmPushFilePath(pVm, sFile.zString, sFile.nByte, FALSE, pNew);
			}
		}
	}else{
		/* Open the URI direcly */
		rc = pStream->xOpen(zFile, iFlags, pResource, &pHandle);
	}
	if( rc != JX9_OK ){
		/* IO error */
		return 0;
	}
	/* Return the file handle */
	return pHandle;
}
/*
 * Read the whole contents of an open IO stream handle [i.e local file/URL..]
 * Store the read data in the given BLOB (last argument).
 * The read operation is stopped when he hit the EOF or an IO error occurs.
 */
JX9_PRIVATE sxi32 jx9StreamReadWholeFile(void *pHandle, const jx9_io_stream *pStream, SyBlob *pOut)
{
	jx9_int64 nRead;
	char zBuf[8192]; /* 8K */
	int rc;
	/* Perform the requested operation */
	for(;;){
		nRead = pStream->xRead(pHandle, zBuf, sizeof(zBuf));
		if( nRead < 1 ){
			/* EOF or IO error */
			break;
		}
		/* Append contents */
		rc = SyBlobAppend(pOut, zBuf, (sxu32)nRead);
		if( rc != SXRET_OK ){
			break;
		}
	}
	return SyBlobLength(pOut) > 0 ? SXRET_OK : -1;
}
/*
 * Close an open IO stream handle [i.e local file/URI..].
 */
JX9_PRIVATE void jx9StreamCloseHandle(const jx9_io_stream *pStream, void *pHandle)
{
	if( pStream->xClose ){
		pStream->xClose(pHandle);
	}
}
/*
 * string fgetc(resource $handle)
 *  Gets a character from the given file pointer.
 * Parameters
 *  $handle
 *   The file pointer.
 * Return
 *  Returns a string containing a single character read from the file
 *  pointed to by handle. Returns FALSE on EOF. 
 * WARNING
 *  This operation is extremely slow.Avoid using it.
 */
static int jx9Builtin_fgetc(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	int c, n;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	n = (int)StreamRead(pDev, (void *)&c, sizeof(char));
	/* IO result */
	if( n < 1 ){
		/* EOF or error, return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		/* Return the string holding the character */
		jx9_result_string(pCtx, (const char *)&c, sizeof(char));
	}
	return JX9_OK;	
}
/*
 * string fgets(resource $handle[, int64 $length ])
 *  Gets line from file pointer.
 * Parameters
 *  $handle
 *   The file pointer.
 * $length
 *  Reading ends when length - 1 bytes have been read, on a newline
 *  (which is included in the return value), or on EOF (whichever comes first).
 *  If no length is specified, it will keep reading from the stream until it reaches
 *  the end of the line. 
 * Return
 *  Returns a string of up to length - 1 bytes read from the file pointed to by handle.
 *  If there is no more data to read in the file pointer, then FALSE is returned.
 *  If an error occurs, FALSE is returned.
 */
static int jx9Builtin_fgets(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	const char *zLine;
	io_private *pDev;
	jx9_int64 n, nLen;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	nLen = -1;
	if( nArg > 1 ){
		/* Maximum data to read */
		nLen = jx9_value_to_int64(apArg[1]);
	}
	/* Perform the requested operation */
	n = StreamReadLine(pDev, &zLine, nLen);
	if( n < 1 ){
		/* EOF or IO error, return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		/* Return the freshly extracted line */
		jx9_result_string(pCtx, zLine, (int)n);
	}
	return JX9_OK;	
}
/*
 * string fread(resource $handle, int64 $length)
 *  Binary-safe file read.
 * Parameters
 *  $handle
 *   The file pointer.
 * $length
 *  Up to length number of bytes read.
 * Return
 *  The data readen on success or FALSE on failure. 
 */
static int jx9Builtin_fread(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	jx9_int64 nRead;
	void *pBuf;
	int nLen;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
        nLen = 4096;
	if( nArg > 1 ){
 	  nLen = jx9_value_to_int(apArg[1]);
	  if( nLen < 1 ){
		/* Invalid length, set a default length */
		nLen = 4096;
	  }
        }
	/* Allocate enough buffer */
	pBuf = jx9_context_alloc_chunk(pCtx, (unsigned int)nLen, FALSE, FALSE);
	if( pBuf == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "JX9 is running out of memory");			
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	nRead = StreamRead(pDev, pBuf, (jx9_int64)nLen);
	if( nRead < 1 ){
		/* Nothing read, return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		/* Make a copy of the data just read */
		jx9_result_string(pCtx, (const char *)pBuf, (int)nRead);
	}
	/* Release the buffer */
	jx9_context_free_chunk(pCtx, pBuf);
	return JX9_OK;	
}
/*
 * array fgetcsv(resource $handle [, int $length = 0 
 *         [, string $delimiter = ', '[, string $enclosure = '"'[, string $escape='\\']]]])
 * Gets line from file pointer and parse for CSV fields.
 * Parameters
 * $handle
 *   The file pointer.
 * $length
 *  Reading ends when length - 1 bytes have been read, on a newline
 *  (which is included in the return value), or on EOF (whichever comes first).
 *  If no length is specified, it will keep reading from the stream until it reaches
 *  the end of the line. 
 * $delimiter
 *   Set the field delimiter (one character only).
 * $enclosure
 *   Set the field enclosure character (one character only).
 * $escape
 *   Set the escape character (one character only). Defaults as a backslash (\)
 * Return
 *  Returns a string of up to length - 1 bytes read from the file pointed to by handle.
 *  If there is no more data to read in the file pointer, then FALSE is returned.
 *  If an error occurs, FALSE is returned.
 */
static int jx9Builtin_fgetcsv(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	const char *zLine;
	io_private *pDev;
	jx9_int64 n, nLen;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	nLen = -1;
	if( nArg > 1 ){
		/* Maximum data to read */
		nLen = jx9_value_to_int64(apArg[1]);
	}
	/* Perform the requested operation */
	n = StreamReadLine(pDev, &zLine, nLen);
	if( n < 1 ){
		/* EOF or IO error, return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		jx9_value *pArray;
		int delim  = ',';   /* Delimiter */
		int encl   = '"' ;  /* Enclosure */
		int escape = '\\';  /* Escape character */
		if( nArg > 2 ){
			const char *zPtr;
			int i;
			if( jx9_value_is_string(apArg[2]) ){
				/* Extract the delimiter */
				zPtr = jx9_value_to_string(apArg[2], &i);
				if( i > 0 ){
					delim = zPtr[0];
				}
			}
			if( nArg > 3 ){
				if( jx9_value_is_string(apArg[3]) ){
					/* Extract the enclosure */
					zPtr = jx9_value_to_string(apArg[3], &i);
					if( i > 0 ){
						encl = zPtr[0];
					}
				}
				if( nArg > 4 ){
					if( jx9_value_is_string(apArg[4]) ){
						/* Extract the escape character */
						zPtr = jx9_value_to_string(apArg[4], &i);
						if( i > 0 ){
							escape = zPtr[0];
						}
					}
				}
			}
		}
		/* Create our array */
		pArray = jx9_context_new_array(pCtx);
		if( pArray == 0 ){
			jx9_context_throw_error(pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
			jx9_result_null(pCtx);
			return JX9_OK;
		}
		/* Parse the raw input */
		jx9ProcessCsv(zLine, (int)n, delim, encl, escape, jx9CsvConsumer, pArray);
		/* Return the freshly created array  */
		jx9_result_value(pCtx, pArray);
	}
	return JX9_OK;	
}
/*
 * string fgetss(resource $handle [, int $length [, string $allowable_tags ]])
 *  Gets line from file pointer and strip HTML tags.
 * Parameters
 * $handle
 *   The file pointer.
 * $length
 *  Reading ends when length - 1 bytes have been read, on a newline
 *  (which is included in the return value), or on EOF (whichever comes first).
 *  If no length is specified, it will keep reading from the stream until it reaches
 *  the end of the line. 
 * $allowable_tags
 *  You can use the optional second parameter to specify tags which should not be stripped. 
 * Return
 *  Returns a string of up to length - 1 bytes read from the file pointed to by 
 *  handle, with all HTML and JX9 code stripped. If an error occurs, returns FALSE. 
 */
static int jx9Builtin_fgetss(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	const char *zLine;
	io_private *pDev;
	jx9_int64 n, nLen;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	nLen = -1;
	if( nArg > 1 ){
		/* Maximum data to read */
		nLen = jx9_value_to_int64(apArg[1]);
	}
	/* Perform the requested operation */
	n = StreamReadLine(pDev, &zLine, nLen);
	if( n < 1 ){
		/* EOF or IO error, return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		const char *zTaglist = 0;
		int nTaglen = 0;
		if( nArg > 2 && jx9_value_is_string(apArg[2]) ){
			/* Allowed tag */
			zTaglist = jx9_value_to_string(apArg[2], &nTaglen);
		}
		/* Process data just read */
		jx9StripTagsFromString(pCtx, zLine, (int)n, zTaglist, nTaglen);
	}
	return JX9_OK;	
}
/*
 * string readdir(resource $dir_handle)
 *   Read entry from directory handle.
 * Parameter
 *  $dir_handle
 *   The directory handle resource previously opened with opendir().
 * Return
 *  Returns the filename on success or FALSE on failure.
 */
static int jx9Builtin_readdir(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	int rc;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xReadDir == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	jx9_result_bool(pCtx, 0);
	/* Perform the requested operation */
	rc = pStream->xReadDir(pDev->pHandle, pCtx);
	if( rc != JX9_OK ){
		/* Return FALSE */
		jx9_result_bool(pCtx, 0);
	}
	return JX9_OK;
}
/*
 * void rewinddir(resource $dir_handle)
 *   Rewind directory handle.
 * Parameter
 *  $dir_handle
 *   The directory handle resource previously opened with opendir().
 * Return
 *  FALSE on failure.
 */
static int jx9Builtin_rewinddir(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xRewindDir == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	pStream->xRewindDir(pDev->pHandle);
	return JX9_OK;
 }
/* Forward declaration */
static void InitIOPrivate(jx9_vm *pVm, const jx9_io_stream *pStream, io_private *pOut);
static void ReleaseIOPrivate(jx9_context *pCtx, io_private *pDev);
/*
 * void closedir(resource $dir_handle)
 *   Close directory handle.
 * Parameter
 *  $dir_handle
 *   The directory handle resource previously opened with opendir().
 * Return
 *  FALSE on failure.
 */
static int jx9Builtin_closedir(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xCloseDir == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	pStream->xCloseDir(pDev->pHandle);
	/* Release the private stucture */
	ReleaseIOPrivate(pCtx, pDev);
	jx9MemObjRelease(apArg[0]);
	return JX9_OK;
 }
/*
 * resource opendir(string $path[, resource $context])
 *  Open directory handle.
 * Parameters
 * $path
 *   The directory path that is to be opened.
 * $context
 *   A context stream resource.
 * Return
 *  A directory handle resource on success, or FALSE on failure.
 */
static int jx9Builtin_opendir(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	const char *zPath;
	io_private *pDev;
	int iLen, rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a directory path");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the target path */
	zPath  = jx9_value_to_string(apArg[0], &iLen);
	/* Try to extract a stream */
	pStream = jx9VmGetStreamDevice(pCtx->pVm, &zPath, iLen);
	if( pStream == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"No stream device is associated with the given path(%s)", zPath);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	if( pStream->xOpenDir == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device", 
			jx9_function_name(pCtx), pStream->zName
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Allocate a new IO private instance */
	pDev = (io_private *)jx9_context_alloc_chunk(pCtx, sizeof(io_private), TRUE, FALSE);
	if( pDev == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Initialize the structure */
	InitIOPrivate(pCtx->pVm, pStream, pDev);
	/* Open the target directory */
	rc = pStream->xOpenDir(zPath, nArg > 1 ? apArg[1] : 0, &pDev->pHandle);
	if( rc != JX9_OK ){
		/* IO error, return FALSE */
		ReleaseIOPrivate(pCtx, pDev);
		jx9_result_bool(pCtx, 0);
	}else{
		/* Return the handle as a resource */
		jx9_result_resource(pCtx, pDev);
	}
	return JX9_OK;
}
/*
 * int readfile(string $filename[, bool $use_include_path = false [, resource $context ]])
 *  Reads a file and writes it to the output buffer.
 * Parameters
 *  $filename
 *   The filename being read.
 *  $use_include_path
 *   You can use the optional second parameter and set it to
 *   TRUE, if you want to search for the file in the include_path, too.
 *  $context
 *   A context stream resource.
 * Return
 *  The number of bytes read from the file on success or FALSE on failure.
 */
static int jx9Builtin_readfile(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	int use_include  = FALSE;
	const jx9_io_stream *pStream;
	jx9_int64 n, nRead;
	const char *zFile;
	char zBuf[8192];
	void *pHandle;
	int rc, nLen;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a file path");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the file path */
	zFile = jx9_value_to_string(apArg[0], &nLen);
	/* Point to the target IO stream device */
	pStream = jx9VmGetStreamDevice(pCtx->pVm, &zFile, nLen);
	if( pStream == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "No such stream device, JX9 is returning FALSE");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	if( nArg > 1 ){
		use_include = jx9_value_to_bool(apArg[1]);
	}
	/* Try to open the file in read-only mode */
	pHandle = jx9StreamOpenHandle(pCtx->pVm, pStream, zFile, JX9_IO_OPEN_RDONLY, 
		use_include, nArg > 2 ? apArg[2] : 0, FALSE, 0);
	if( pHandle == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening '%s'", zFile);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	nRead = 0;
	for(;;){
		n = pStream->xRead(pHandle, zBuf, sizeof(zBuf));
		if( n < 1 ){
			/* EOF or IO error, break immediately */
			break;
		}
		/* Output data */
		rc = jx9_context_output(pCtx, zBuf, (int)n);
		if( rc == JX9_ABORT ){
			break;
		}
		/* Increment counter */
		nRead += n;
	}
	/* Close the stream */
	jx9StreamCloseHandle(pStream, pHandle);
	/* Total number of bytes readen */
	jx9_result_int64(pCtx, nRead);
	return JX9_OK;
}
/*
 * string file_get_contents(string $filename[, bool $use_include_path = false 
 *         [, resource $context [, int $offset = -1 [, int $maxlen ]]]])
 *  Reads entire file into a string.
 * Parameters
 *  $filename
 *   The filename being read.
 *  $use_include_path
 *   You can use the optional second parameter and set it to
 *   TRUE, if you want to search for the file in the include_path, too.
 *  $context
 *   A context stream resource.
 *  $offset
 *   The offset where the reading starts on the original stream.
 *  $maxlen
 *    Maximum length of data read. The default is to read until end of file 
 *    is reached. Note that this parameter is applied to the stream processed by the filters.
 * Return
 *   The function returns the read data or FALSE on failure.
 */
static int jx9Builtin_file_get_contents(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	jx9_int64 n, nRead, nMaxlen;
	int use_include  = FALSE;
	const char *zFile;
	char zBuf[8192];
	void *pHandle;
	int nLen;
	
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a file path");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the file path */
	zFile = jx9_value_to_string(apArg[0], &nLen);
	/* Point to the target IO stream device */
	pStream = jx9VmGetStreamDevice(pCtx->pVm, &zFile, nLen);
	if( pStream == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "No such stream device, JX9 is returning FALSE");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	nMaxlen = -1;
	if( nArg > 1 ){
		use_include = jx9_value_to_bool(apArg[1]);
	}
	/* Try to open the file in read-only mode */
	pHandle = jx9StreamOpenHandle(pCtx->pVm, pStream, zFile, JX9_IO_OPEN_RDONLY, use_include, nArg > 2 ? apArg[2] : 0, FALSE, 0);
	if( pHandle == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening '%s'", zFile);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	if( nArg > 3 ){
		/* Extract the offset */
		n = jx9_value_to_int64(apArg[3]);
		if( n > 0 ){
			if( pStream->xSeek ){
				/* Seek to the desired offset */
				pStream->xSeek(pHandle, n, 0/*SEEK_SET*/);
			}
		}
		if( nArg > 4 ){
			/* Maximum data to read */
			nMaxlen = jx9_value_to_int64(apArg[4]);
		}
	}
	/* Perform the requested operation */
	nRead = 0;
	for(;;){
		n = pStream->xRead(pHandle, zBuf, 
			(nMaxlen > 0 && (nMaxlen < sizeof(zBuf))) ? nMaxlen : sizeof(zBuf));
		if( n < 1 ){
			/* EOF or IO error, break immediately */
			break;
		}
		/* Append data */
		jx9_result_string(pCtx, zBuf, (int)n);
		/* Increment read counter */
		nRead += n;
		if( nMaxlen > 0 && nRead >= nMaxlen ){
			/* Read limit reached */
			break;
		}
	}
	/* Close the stream */
	jx9StreamCloseHandle(pStream, pHandle);
	/* Check if we have read something */
	if( jx9_context_result_buf_length(pCtx) < 1 ){
		/* Nothing read, return FALSE */
		jx9_result_bool(pCtx, 0);
	}
	return JX9_OK;
}
/*
 * int file_put_contents(string $filename, mixed $data[, int $flags = 0[, resource $context]])
 *  Write a string to a file.
 * Parameters
 *  $filename
 *  Path to the file where to write the data.
 * $data
 *  The data to write(Must be a string).
 * $flags
 *  The value of flags can be any combination of the following
 * flags, joined with the binary OR (|) operator.
 *   FILE_USE_INCLUDE_PATH 	Search for filename in the include directory. See include_path for more information.
 *   FILE_APPEND 	        If file filename already exists, append the data to the file instead of overwriting it.
 *   LOCK_EX 	            Acquire an exclusive lock on the file while proceeding to the writing.
 * context
 *  A context stream resource.
 * Return
 *  The function returns the number of bytes that were written to the file, or FALSE on failure. 
 */
static int jx9Builtin_file_put_contents(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	int use_include  = FALSE;
	const jx9_io_stream *pStream;
	const char *zFile;
	const char *zData;
	int iOpenFlags;
	void *pHandle;
	int iFlags;
	int nLen;
	
	if( nArg < 2 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a file path");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the file path */
	zFile = jx9_value_to_string(apArg[0], &nLen);
	/* Point to the target IO stream device */
	pStream = jx9VmGetStreamDevice(pCtx->pVm, &zFile, nLen);
	if( pStream == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "No such stream device, JX9 is returning FALSE");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Data to write */
	zData = jx9_value_to_string(apArg[1], &nLen);
	if( nLen < 1 ){
		/* Nothing to write, return immediately */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Try to open the file in read-write mode */
	iOpenFlags = JX9_IO_OPEN_CREATE|JX9_IO_OPEN_RDWR|JX9_IO_OPEN_TRUNC;
	/* Extract the flags */
	iFlags = 0;
	if( nArg > 2 ){
		iFlags = jx9_value_to_int(apArg[2]);
		if( iFlags & 0x01 /*FILE_USE_INCLUDE_PATH*/){
			use_include = TRUE;
		}
		if( iFlags & 0x08 /* FILE_APPEND */){
			/* If the file already exists, append the data to the file
			 * instead of overwriting it.
			 */
			iOpenFlags &= ~JX9_IO_OPEN_TRUNC;
			/* Append mode */
			iOpenFlags |= JX9_IO_OPEN_APPEND;
		}
	}
	pHandle = jx9StreamOpenHandle(pCtx->pVm, pStream, zFile, iOpenFlags, use_include, 
		nArg > 3 ? apArg[3] : 0, FALSE, FALSE);
	if( pHandle == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening '%s'", zFile);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	if( pStream->xWrite ){
		jx9_int64 n;
		if( (iFlags & 0x01/* LOCK_EX */) && pStream->xLock ){
			/* Try to acquire an exclusive lock */
			pStream->xLock(pHandle, 1/* LOCK_EX */);
		}
		/* Perform the write operation */
		n = pStream->xWrite(pHandle, (const void *)zData, nLen);
		if( n < 1 ){
			/* IO error, return FALSE */
			jx9_result_bool(pCtx, 0);
		}else{
			/* Total number of bytes written */
			jx9_result_int64(pCtx, n);
		}
	}else{
		/* Read-only stream */
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, 
			"Read-only stream(%s): Cannot perform write operation", 
			pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
	}
	/* Close the handle */
	jx9StreamCloseHandle(pStream, pHandle);
	return JX9_OK;
}
/*
 * array file(string $filename[, int $flags = 0[, resource $context]])
 *  Reads entire file into an array.
 * Parameters
 *  $filename
 *   The filename being read.
 *  $flags
 *   The optional parameter flags can be one, or more, of the following constants:
 *   FILE_USE_INCLUDE_PATH
 *       Search for the file in the include_path. 
 *   FILE_IGNORE_NEW_LINES
 *       Do not add newline at the end of each array element 
 *   FILE_SKIP_EMPTY_LINES
 *       Skip empty lines 
 *  $context
 *   A context stream resource.
 * Return
 *   The function returns the read data or FALSE on failure.
 */
static int jx9Builtin_file(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const char *zFile, *zPtr, *zEnd, *zBuf;
	jx9_value *pArray, *pLine;
	const jx9_io_stream *pStream;
	int use_include = 0;
	io_private *pDev;
	jx9_int64 n;
	int iFlags;
	int nLen;
	
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a file path");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the file path */
	zFile = jx9_value_to_string(apArg[0], &nLen);
	/* Point to the target IO stream device */
	pStream = jx9VmGetStreamDevice(pCtx->pVm, &zFile, nLen);
	if( pStream == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "No such stream device, JX9 is returning FALSE");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Allocate a new IO private instance */
	pDev = (io_private *)jx9_context_alloc_chunk(pCtx, sizeof(io_private), TRUE, FALSE);
	if( pDev == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Initialize the structure */
	InitIOPrivate(pCtx->pVm, pStream, pDev);
	iFlags = 0;
	if( nArg > 1 ){
		iFlags = jx9_value_to_int(apArg[1]);
	}
	if( iFlags & 0x01 /*FILE_USE_INCLUDE_PATH*/ ){
		use_include = TRUE;
	}
	/* Create the array and the working value */
	pArray = jx9_context_new_array(pCtx);
	pLine = jx9_context_new_scalar(pCtx);
	if( pArray == 0 || pLine == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Try to open the file in read-only mode */
	pDev->pHandle = jx9StreamOpenHandle(pCtx->pVm, pStream, zFile, JX9_IO_OPEN_RDONLY, use_include, nArg > 2 ? apArg[2] : 0, FALSE, 0);
	if( pDev->pHandle == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening '%s'", zFile);
		jx9_result_bool(pCtx, 0);
		/* Don't worry about freeing memory, everything will be released automatically
		 * as soon we return from this function.
		 */
		return JX9_OK;
	}
	/* Perform the requested operation */
	for(;;){
		/* Try to extract a line */
		n = StreamReadLine(pDev, &zBuf, -1);
		if( n < 1 ){
			/* EOF or IO error */
			break;
		}
		/* Reset the cursor */
		jx9_value_reset_string_cursor(pLine);
		/* Remove line ending if requested by the caller */
		zPtr = zBuf;
		zEnd = &zBuf[n];
		if( iFlags & 0x02 /* FILE_IGNORE_NEW_LINES */ ){
			/* Ignore trailig lines */
			while( zPtr < zEnd && (zEnd[-1] == '\n' 
#ifdef __WINNT__
				|| zEnd[-1] == '\r'
#endif
				)){
					n--;
					zEnd--;
			}
		}
		if( iFlags & 0x04 /* FILE_SKIP_EMPTY_LINES */ ){
			/* Ignore empty lines */
			while( zPtr < zEnd && (unsigned char)zPtr[0] < 0xc0 && SyisSpace(zPtr[0]) ){
				zPtr++;
			}
			if( zPtr >= zEnd ){
				/* Empty line */
				continue;
			}
		}
		jx9_value_string(pLine, zBuf, (int)(zEnd-zBuf));
		/* Insert line */
		jx9_array_add_elem(pArray, 0/* Automatic index assign*/, pLine);
	}
	/* Close the stream */
	jx9StreamCloseHandle(pStream, pDev->pHandle);
	/* Release the io_private instance */
	ReleaseIOPrivate(pCtx, pDev);
	/* Return the created array */
	jx9_result_value(pCtx, pArray);
	return JX9_OK;
}
/*
 * bool copy(string $source, string $dest[, resource $context ] )
 *  Makes a copy of the file source to dest.
 * Parameters
 *  $source
 *   Path to the source file.
 *  $dest
 *   The destination path. If dest is a URL, the copy operation 
 *   may fail if the wrapper does not support overwriting of existing files. 
 *  $context
 *   A context stream resource.
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Builtin_copy(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pSin, *pSout;	
	const char *zFile;
	char zBuf[8192];
	void *pIn, *pOut;
	jx9_int64 n;
	int nLen;
	if( nArg < 2 || !jx9_value_is_string(apArg[0]) || !jx9_value_is_string(apArg[1])){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a source and a destination path");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the source name */
	zFile = jx9_value_to_string(apArg[0], &nLen);
	/* Point to the target IO stream device */
	pSin = jx9VmGetStreamDevice(pCtx->pVm, &zFile, nLen);
	if( pSin == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "No such stream device, JX9 is returning FALSE");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Try to open the source file in a read-only mode */
	pIn = jx9StreamOpenHandle(pCtx->pVm, pSin, zFile, JX9_IO_OPEN_RDONLY, FALSE, nArg > 2 ? apArg[2] : 0, FALSE, 0);
	if( pIn == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening source: '%s'", zFile);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the destination name */
	zFile = jx9_value_to_string(apArg[1], &nLen);
	/* Point to the target IO stream device */
	pSout = jx9VmGetStreamDevice(pCtx->pVm, &zFile, nLen);
	if( pSout == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "No such stream device, JX9 is returning FALSE");
		jx9_result_bool(pCtx, 0);
		jx9StreamCloseHandle(pSin, pIn);
		return JX9_OK;
	}
	if( pSout->xWrite == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pSin->zName
			);
		jx9_result_bool(pCtx, 0);
		jx9StreamCloseHandle(pSin, pIn);
		return JX9_OK;
	}
	/* Try to open the destination file in a read-write mode */
	pOut = jx9StreamOpenHandle(pCtx->pVm, pSout, zFile, 
		JX9_IO_OPEN_CREATE|JX9_IO_OPEN_TRUNC|JX9_IO_OPEN_RDWR, FALSE, nArg > 2 ? apArg[2] : 0, FALSE, 0);
	if( pOut == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening destination: '%s'", zFile);
		jx9_result_bool(pCtx, 0);
		jx9StreamCloseHandle(pSin, pIn);
		return JX9_OK;
	}
	/* Perform the requested operation */
	for(;;){
		/* Read from source */
		n = pSin->xRead(pIn, zBuf, sizeof(zBuf));
		if( n < 1 ){
			/* EOF or IO error, break immediately */
			break;
		}
		/* Write to dest */
		n = pSout->xWrite(pOut, zBuf, n);
		if( n < 1 ){
			/* IO error, break immediately */
			break;
		}
	}
	/* Close the streams */
	jx9StreamCloseHandle(pSin, pIn);
	jx9StreamCloseHandle(pSout, pOut);
	/* Return TRUE */
	jx9_result_bool(pCtx, 1);
	return JX9_OK;
}
/*
 * array fstat(resource $handle)
 *  Gets information about a file using an open file pointer.
 * Parameters
 *  $handle
 *   The file pointer.
 * Return
 *  Returns an array with the statistics of the file or FALSE on failure.
 */
static int jx9Builtin_fstat(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	jx9_value *pArray, *pValue;
	const jx9_io_stream *pStream;
	io_private *pDev;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/* Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xStat == 0){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Create the array and the working value */
	pArray = jx9_context_new_array(pCtx);
	pValue = jx9_context_new_scalar(pCtx);
	if( pArray == 0 || pValue == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	pStream->xStat(pDev->pHandle, pArray, pValue);
	/* Return the freshly created array */
	jx9_result_value(pCtx, pArray);
	/* Don't worry about freeing memory here, everything will be
	 * released automatically as soon we return from this function.
	 */
	return JX9_OK;
}
/*
 * int fwrite(resource $handle, string $string[, int $length])
 *  Writes the contents of string to the file stream pointed to by handle.
 * Parameters
 *  $handle
 *   The file pointer.
 *  $string
 *   The string that is to be written.
 *  $length
 *   If the length argument is given, writing will stop after length bytes have been written
 *   or the end of string is reached, whichever comes first. 
 * Return
 *  Returns the number of bytes written, or FALSE on error.
 */
static int jx9Builtin_fwrite(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	const char *zString;
	io_private *pDev;
	int nLen, n;
	if( nArg < 2 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/* Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xWrite == 0){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the data to write */
	zString = jx9_value_to_string(apArg[1], &nLen);
	if( nArg > 2 ){
		/* Maximum data length to write */
		n = jx9_value_to_int(apArg[2]);
		if( n >= 0 && n < nLen ){
			nLen = n;
		}
	}
	if( nLen < 1 ){
		/* Nothing to write */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	n = (int)pStream->xWrite(pDev->pHandle, (const void *)zString, nLen);
	if( n <  0 ){
		/* IO error, return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		/* #Bytes written */
		jx9_result_int(pCtx, n);
	}
	return JX9_OK;
}
/*
 * bool flock(resource $handle, int $operation)
 *  Portable advisory file locking.
 * Parameters
 *  $handle
 *   The file pointer.
 *  $operation
 *   operation is one of the following:
 *      LOCK_SH to acquire a shared lock (reader).
 *      LOCK_EX to acquire an exclusive lock (writer).
 *      LOCK_UN to release a lock (shared or exclusive).
 * Return
 *  Returns TRUE on success or FALSE on failure.
 */
static int jx9Builtin_flock(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	int nLock;
	int rc;
	if( nArg < 2 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xLock == 0){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Requested lock operation */
	nLock = jx9_value_to_int(apArg[1]);
	/* Lock operation */
	rc = pStream->xLock(pDev->pHandle, nLock);
	/* IO result */
	jx9_result_bool(pCtx, rc == JX9_OK);
	return JX9_OK;
}
/*
 * int fpassthru(resource $handle)
 *  Output all remaining data on a file pointer.
 * Parameters
 *  $handle
 *   The file pointer. 
 * Return
 *  Total number of characters read from handle and passed through
 *  to the output on success or FALSE on failure.
 */
static int jx9Builtin_fpassthru(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	jx9_int64 n, nRead;
	char zBuf[8192];
	int rc;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Perform the requested operation */
	nRead = 0;
	for(;;){
		n = StreamRead(pDev, zBuf, sizeof(zBuf));
		if( n < 1 ){
			/* Error or EOF */
			break;
		}
		/* Increment the read counter */
		nRead += n;
		/* Output data */
		rc = jx9_context_output(pCtx, zBuf, (int)nRead /* FIXME: 64-bit issues */);
		if( rc == JX9_ABORT ){
			/* Consumer callback request an operation abort */
			break;
		}
	}
	/* Total number of bytes readen */
	jx9_result_int64(pCtx, nRead);
	return JX9_OK;
}
/* CSV reader/writer private data */
struct csv_data
{
	int delimiter;    /* Delimiter. Default ', ' */
	int enclosure;    /* Enclosure. Default '"'*/
	io_private *pDev; /* Open stream handle */
	int iCount;       /* Counter */
};
/* 
 * The following callback is used by the fputcsv() function inorder to iterate
 * throw array entries and output CSV data based on the current key and it's
 * associated data.
 */
static int csv_write_callback(jx9_value *pKey, jx9_value *pValue, void *pUserData)
{
	struct csv_data *pData = (struct csv_data *)pUserData;
	const char *zData;
	int nLen, c2;
	sxu32 n;
	/* Point to the raw data */
	zData = jx9_value_to_string(pValue, &nLen);
	if( nLen < 1 ){
		/* Nothing to write */
		return JX9_OK;
	}
	if( pData->iCount > 0 ){
		/* Write the delimiter */
		pData->pDev->pStream->xWrite(pData->pDev->pHandle, (const void *)&pData->delimiter, sizeof(char));
	}
	n = 1;
	c2 = 0;
	if( SyByteFind(zData, (sxu32)nLen, pData->delimiter, 0) == SXRET_OK || 
		SyByteFind(zData, (sxu32)nLen, pData->enclosure, &n) == SXRET_OK ){
			c2 = 1;
			if( n == 0 ){
				c2 = 2;
			}
			/* Write the enclosure */
			pData->pDev->pStream->xWrite(pData->pDev->pHandle, (const void *)&pData->enclosure, sizeof(char));
			if( c2 > 1 ){
				pData->pDev->pStream->xWrite(pData->pDev->pHandle, (const void *)&pData->enclosure, sizeof(char));
			}
	}
	/* Write the data */
	if( pData->pDev->pStream->xWrite(pData->pDev->pHandle, (const void *)zData, (jx9_int64)nLen) < 1 ){
		SXUNUSED(pKey); /* cc warning */
		return JX9_ABORT;
	}
	if( c2 > 0 ){
		/* Write the enclosure */
		pData->pDev->pStream->xWrite(pData->pDev->pHandle, (const void *)&pData->enclosure, sizeof(char));
		if( c2 > 1 ){
			pData->pDev->pStream->xWrite(pData->pDev->pHandle, (const void *)&pData->enclosure, sizeof(char));
		}
	}
	pData->iCount++;
	return JX9_OK;
}
/*
 * int fputcsv(resource $handle, array $fields[, string $delimiter = ', '[, string $enclosure = '"' ]])
 *  Format line as CSV and write to file pointer.
 * Parameters
 *  $handle
 *   Open file handle.
 * $fields
 *   An array of values.
 * $delimiter
 *   The optional delimiter parameter sets the field delimiter (one character only).
 * $enclosure
 *  The optional enclosure parameter sets the field enclosure (one character only).
 */
static int jx9Builtin_fputcsv(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	struct csv_data sCsv;
	io_private *pDev;
	const char *zEol;
	int eolen;
	if( nArg < 2 || !jx9_value_is_resource(apArg[0]) || !jx9_value_is_json_array(apArg[1]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Missing/Invalid arguments");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0  || pStream->xWrite == 0){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Set default csv separator */
	sCsv.delimiter = ',';
	sCsv.enclosure = '"';
	sCsv.pDev = pDev;
	sCsv.iCount = 0;
	if( nArg > 2 ){
		/* User delimiter */
		const char *z;
		int n;
		z = jx9_value_to_string(apArg[2], &n);
		if( n > 0 ){
			sCsv.delimiter = z[0];
		}
		if( nArg > 3 ){
			z = jx9_value_to_string(apArg[3], &n);
			if( n > 0 ){
				sCsv.enclosure = z[0];
			}
		}
	}
	/* Iterate throw array entries and write csv data */
	jx9_array_walk(apArg[1], csv_write_callback, &sCsv);
	/* Write a line ending */
#ifdef __WINNT__
	zEol = "\r\n";
	eolen = (int)sizeof("\r\n")-1;
#else
	/* Assume UNIX LF */
	zEol = "\n";
	eolen = (int)sizeof(char);
#endif
	pDev->pStream->xWrite(pDev->pHandle, (const void *)zEol, eolen);
	return JX9_OK;
}
/*
 * fprintf, vfprintf private data.
 * An instance of the following structure is passed to the formatted
 * input consumer callback defined below.
 */
typedef struct fprintf_data fprintf_data;
struct fprintf_data
{
	io_private *pIO;        /* IO stream */
	jx9_int64 nCount;       /* Total number of bytes written */
};
/*
 * Callback [i.e: Formatted input consumer] for the fprintf function.
 */
static int fprintfConsumer(jx9_context *pCtx, const char *zInput, int nLen, void *pUserData)
{
	fprintf_data *pFdata = (fprintf_data *)pUserData;
	jx9_int64 n;
	/* Write the formatted data */
	n = pFdata->pIO->pStream->xWrite(pFdata->pIO->pHandle, (const void *)zInput, nLen);
	if( n < 1 ){
		SXUNUSED(pCtx); /* cc warning */
		/* IO error, abort immediately */
		return SXERR_ABORT;
	}
	/* Increment counter */
	pFdata->nCount += n;
	return JX9_OK;
}
/*
 * int fprintf(resource $handle, string $format[, mixed $args [, mixed $... ]])
 *  Write a formatted string to a stream.
 * Parameters
 *  $handle
 *   The file pointer.
 *  $format
 *   String format (see sprintf()).
 * Return
 *  The length of the written string.
 */
static int jx9Builtin_fprintf(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	fprintf_data sFdata;
	const char *zFormat;
	io_private *pDev;
	int nLen;
	if( nArg < 2 || !jx9_value_is_resource(apArg[0]) || !jx9_value_is_string(apArg[1]) ){
		/* Missing/Invalid arguments, return zero */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Invalid arguments");
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	if( pDev->pStream == 0  || pDev->pStream->xWrite == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device", 
			jx9_function_name(pCtx), pDev->pStream ? pDev->pStream->zName : "null_stream"
			);
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the string format */
	zFormat = jx9_value_to_string(apArg[1], &nLen);
	if( nLen < 1 ){
		/* Empty string, return zero */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Prepare our private data */
	sFdata.nCount = 0;
	sFdata.pIO = pDev;
	/* Format the string */
	jx9InputFormat(fprintfConsumer, pCtx, zFormat, nLen, nArg - 1, &apArg[1], (void *)&sFdata, FALSE);
	/* Return total number of bytes written */
	jx9_result_int64(pCtx, sFdata.nCount);
	return JX9_OK;
}
/*
 * int vfprintf(resource $handle, string $format, array $args)
 *  Write a formatted string to a stream.
 * Parameters
 *  $handle
 *   The file pointer.
 *  $format
 *   String format (see sprintf()).
 * $args
 *   User arguments.
 * Return
 *  The length of the written string.
 */
static int jx9Builtin_vfprintf(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	fprintf_data sFdata;
	const char *zFormat;
	jx9_hashmap *pMap;
	io_private *pDev;
	SySet sArg;
	int n, nLen;
	if( nArg < 3 || !jx9_value_is_resource(apArg[0]) || !jx9_value_is_string(apArg[1])  || !jx9_value_is_json_array(apArg[2]) ){
		/* Missing/Invalid arguments, return zero */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Invalid arguments");
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	if( pDev->pStream == 0  || pDev->pStream->xWrite == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device", 
			jx9_function_name(pCtx), pDev->pStream ? pDev->pStream->zName : "null_stream"
			);
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the string format */
	zFormat = jx9_value_to_string(apArg[1], &nLen);
	if( nLen < 1 ){
		/* Empty string, return zero */
		jx9_result_int(pCtx, 0);
		return JX9_OK;
	}
	/* Point to hashmap */
	pMap = (jx9_hashmap *)apArg[2]->x.pOther;
	/* Extract arguments from the hashmap */
	n = jx9HashmapValuesToSet(pMap, &sArg);
	/* Prepare our private data */
	sFdata.nCount = 0;
	sFdata.pIO = pDev;
	/* Format the string */
	jx9InputFormat(fprintfConsumer, pCtx, zFormat, nLen, n, (jx9_value **)SySetBasePtr(&sArg), (void *)&sFdata, TRUE);
	/* Return total number of bytes written*/
	jx9_result_int64(pCtx, sFdata.nCount);
	SySetRelease(&sArg);
	return JX9_OK;
}
/*
 * Convert open modes (string passed to the fopen() function) [i.e: 'r', 'w+', 'a', ...] into JX9 flags.
 * According to the JX9 reference manual:
 *  The mode parameter specifies the type of access you require to the stream. It may be any of the following
 *   'r' 	Open for reading only; place the file pointer at the beginning of the file.
 *   'r+' 	Open for reading and writing; place the file pointer at the beginning of the file.
 *   'w' 	Open for writing only; place the file pointer at the beginning of the file and truncate the file
 *          to zero length. If the file does not exist, attempt to create it.
 *   'w+' 	Open for reading and writing; place the file pointer at the beginning of the file and truncate
 *              the file to zero length. If the file does not exist, attempt to create it.
 *   'a' 	Open for writing only; place the file pointer at the end of the file. If the file does not 
 *         exist, attempt to create it.
 *   'a+' 	Open for reading and writing; place the file pointer at the end of the file. If the file does 
 *          not exist, attempt to create it.
 *   'x' 	Create and open for writing only; place the file pointer at the beginning of the file. If the file
 *         already exists, 
 *         the fopen() call will fail by returning FALSE and generating an error of level E_WARNING. If the file
 *         does not exist attempt to create it. This is equivalent to specifying O_EXCL|O_CREAT flags for
 *         the underlying open(2) system call.
 *   'x+' 	Create and open for reading and writing; otherwise it has the same behavior as 'x'.
 *   'c' 	Open the file for writing only. If the file does not exist, it is created. If it exists, it is neither truncated
 *          (as opposed to 'w'), nor the call to this function fails (as is the case with 'x'). The file pointer
 *          is positioned on the beginning of the file.
 *          This may be useful if it's desired to get an advisory lock (see flock()) before attempting to modify the file
 *          as using 'w' could truncate the file before the lock was obtained (if truncation is desired, ftruncate() can
 *          be used after the lock is requested).
 *   'c+' 	Open the file for reading and writing; otherwise it has the same behavior as 'c'. 
 */
static int StrModeToFlags(jx9_context *pCtx, const char *zMode, int nLen)
{
	const char *zEnd = &zMode[nLen];
	int iFlag = 0;
	int c;
	if( nLen < 1 ){
		/* Open in a read-only mode */
		return JX9_IO_OPEN_RDONLY;
	}
	c = zMode[0];
	if( c == 'r' || c == 'R' ){
		/* Read-only access */
		iFlag = JX9_IO_OPEN_RDONLY;
		zMode++; /* Advance */
		if( zMode < zEnd ){
			c = zMode[0];
			if( c == '+' || c == 'w' || c == 'W' ){
				/* Read+Write access */
				iFlag = JX9_IO_OPEN_RDWR;
			}
		}
	}else if( c == 'w' || c == 'W' ){
		/* Overwrite mode.
		 * If the file does not exists, try to create it
		 */
		iFlag = JX9_IO_OPEN_WRONLY|JX9_IO_OPEN_TRUNC|JX9_IO_OPEN_CREATE;
		zMode++; /* Advance */
		if( zMode < zEnd ){
			c = zMode[0];
			if( c == '+' || c == 'r' || c == 'R' ){
				/* Read+Write access */
				iFlag &= ~JX9_IO_OPEN_WRONLY;
				iFlag |= JX9_IO_OPEN_RDWR;
			}
		}
	}else if( c == 'a' || c == 'A' ){
		/* Append mode (place the file pointer at the end of the file).
		 * Create the file if it does not exists.
		 */
		iFlag = JX9_IO_OPEN_WRONLY|JX9_IO_OPEN_APPEND|JX9_IO_OPEN_CREATE;
		zMode++; /* Advance */
		if( zMode < zEnd ){
			c = zMode[0];
			if( c == '+' ){
				/* Read-Write access */
				iFlag &= ~JX9_IO_OPEN_WRONLY;
				iFlag |= JX9_IO_OPEN_RDWR;
			}
		}
	}else if( c == 'x' || c == 'X' ){
		/* Exclusive access.
		 * If the file already exists, return immediately with a failure code.
		 * Otherwise create a new file.
		 */
		iFlag = JX9_IO_OPEN_WRONLY|JX9_IO_OPEN_EXCL;
		zMode++; /* Advance */
		if( zMode < zEnd ){
			c = zMode[0];
			if( c == '+' || c == 'r' || c == 'R' ){
				/* Read-Write access */
				iFlag &= ~JX9_IO_OPEN_WRONLY;
				iFlag |= JX9_IO_OPEN_RDWR;
			}
		}
	}else if( c == 'c' || c == 'C' ){
		/* Overwrite mode.Create the file if it does not exists.*/
		iFlag = JX9_IO_OPEN_WRONLY|JX9_IO_OPEN_CREATE;
		zMode++; /* Advance */
		if( zMode < zEnd ){
			c = zMode[0];
			if( c == '+' ){
				/* Read-Write access */
				iFlag &= ~JX9_IO_OPEN_WRONLY;
				iFlag |= JX9_IO_OPEN_RDWR;
			}
		}
	}else{
		/* Invalid mode. Assume a read only open */
		jx9_context_throw_error(pCtx, JX9_CTX_NOTICE, "Invalid open mode, JX9 is assuming a Read-Only open");
		iFlag = JX9_IO_OPEN_RDONLY;
	}
	while( zMode < zEnd ){
		c = zMode[0];
		if( c == 'b' || c == 'B' ){
			iFlag &= ~JX9_IO_OPEN_TEXT;
			iFlag |= JX9_IO_OPEN_BINARY;
		}else if( c == 't' || c == 'T' ){
			iFlag &= ~JX9_IO_OPEN_BINARY;
			iFlag |= JX9_IO_OPEN_TEXT;
		}
		zMode++;
	}
	return iFlag;
}
/*
 * Initialize the IO private structure.
 */
static void InitIOPrivate(jx9_vm *pVm, const jx9_io_stream *pStream, io_private *pOut)
{
	pOut->pStream = pStream;
	SyBlobInit(&pOut->sBuffer, &pVm->sAllocator);
	pOut->nOfft = 0;
	/* Set the magic number */
	pOut->iMagic = IO_PRIVATE_MAGIC;
}
/*
 * Release the IO private structure.
 */
static void ReleaseIOPrivate(jx9_context *pCtx, io_private *pDev)
{
	SyBlobRelease(&pDev->sBuffer);
	pDev->iMagic = 0x2126; /* Invalid magic number so we can detetct misuse */
	/* Release the whole structure */
	jx9_context_free_chunk(pCtx, pDev);
}
/*
 * Reset the IO private structure.
 */
static void ResetIOPrivate(io_private *pDev)
{
	SyBlobReset(&pDev->sBuffer);
	pDev->nOfft = 0;
}
/* Forward declaration */
static int is_jx9_stream(const jx9_io_stream *pStream);
/*
 * resource fopen(string $filename, string $mode [, bool $use_include_path = false[, resource $context ]])
 *  Open a file, a URL or any other IO stream.
 * Parameters
 *  $filename
 *   If filename is of the form "scheme://...", it is assumed to be a URL and JX9 will search
 *   for a protocol handler (also known as a wrapper) for that scheme. If no scheme is given
 *   then a regular file is assumed.
 *  $mode
 *   The mode parameter specifies the type of access you require to the stream
 *   See the block comment associated with the StrModeToFlags() for the supported 
 *   modes.
 *  $use_include_path
 *   You can use the optional second parameter and set it to
 *   TRUE, if you want to search for the file in the include_path, too.
 *  $context
 *   A context stream resource.
 * Return
 *  File handle on success or FALSE on failure.
 */
static int jx9Builtin_fopen(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	const char *zUri, *zMode;
	jx9_value *pResource;
	io_private *pDev;
	int iLen, imLen;
	int iOpenFlags;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a file path or URL");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the URI and the desired access mode */
	zUri  = jx9_value_to_string(apArg[0], &iLen);
	if( nArg > 1 ){
		zMode = jx9_value_to_string(apArg[1], &imLen);
	}else{
		/* Set a default read-only mode */
		zMode = "r";
		imLen = (int)sizeof(char);
	}
	/* Try to extract a stream */
	pStream = jx9VmGetStreamDevice(pCtx->pVm, &zUri, iLen);
	if( pStream == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"No stream device is associated with the given URI(%s)", zUri);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Allocate a new IO private instance */
	pDev = (io_private *)jx9_context_alloc_chunk(pCtx, sizeof(io_private), TRUE, FALSE);
	if( pDev == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "JX9 is running out of memory");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	pResource = 0;
	if( nArg > 3 ){
		pResource = apArg[3];
	}else if( is_jx9_stream(pStream) ){
		/* TICKET 1433-80: The jx9:// stream need a jx9_value to access the underlying
		 * virtual machine.
		 */
		pResource = apArg[0];
	}
	/* Initialize the structure */
	InitIOPrivate(pCtx->pVm, pStream, pDev);
	/* Convert open mode to JX9 flags */
	iOpenFlags = StrModeToFlags(pCtx, zMode, imLen);
	/* Try to get a handle */
	pDev->pHandle = jx9StreamOpenHandle(pCtx->pVm, pStream, zUri, iOpenFlags, 
		nArg > 2 ? jx9_value_to_bool(apArg[2]) : FALSE, pResource, FALSE, 0);
	if( pDev->pHandle == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening '%s'", zUri);
		jx9_result_bool(pCtx, 0);
		jx9_context_free_chunk(pCtx, pDev);
		return JX9_OK;
	}
	/* All done, return the io_private instance as a resource */
	jx9_result_resource(pCtx, pDev);
	return JX9_OK;
}
/*
 * bool fclose(resource $handle)
 *  Closes an open file pointer
 * Parameters
 *  $handle
 *   The file pointer. 
 * Return
 *  TRUE on success or FALSE on failure.
 */
static int jx9Builtin_fclose(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	io_private *pDev;
	jx9_vm *pVm;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract our private data */
	pDev = (io_private *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid io_private instance */
	if( IO_PRIVATE_INVALID(pDev) ){
		/*Expecting an IO handle */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting an IO handle");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the target IO stream device */
	pStream = pDev->pStream;
	if( pStream == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_WARNING, 
			"IO routine(%s) not implemented in the underlying stream(%s) device, JX9 is returning FALSE", 
			jx9_function_name(pCtx), pStream ? pStream->zName : "null_stream"
			);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the VM that own this context */
	pVm = pCtx->pVm;
	/* TICKET 1433-62: Keep the STDIN/STDOUT/STDERR handles open */
	if( pDev != pVm->pStdin && pDev != pVm->pStdout && pDev != pVm->pStderr ){
		/* Perform the requested operation */
		jx9StreamCloseHandle(pStream, pDev->pHandle);
		/* Release the IO private structure */
		ReleaseIOPrivate(pCtx, pDev);
		/* Invalidate the resource handle */
		jx9_value_release(apArg[0]);
	}
	/* Return TRUE */
	jx9_result_bool(pCtx, 1);
	return JX9_OK;
}
#if !defined(JX9_DISABLE_HASH_FUNC)
/*
 * MD5/SHA1 digest consumer.
 */
static int vfsHashConsumer(const void *pData, unsigned int nLen, void *pUserData)
{
	/* Append hex chunk verbatim */
	jx9_result_string((jx9_context *)pUserData, (const char *)pData, (int)nLen);
	return SXRET_OK;
}
/*
 * string md5_file(string $uri[, bool $raw_output = false ])
 *  Calculates the md5 hash of a given file.
 * Parameters
 *  $uri
 *   Target URI (file(/path/to/something) or URL(http://www.symisc.net/))
 *  $raw_output
 *   When TRUE, returns the digest in raw binary format with a length of 16.
 * Return
 *  Return the MD5 digest on success or FALSE on failure.
 */
static int jx9Builtin_md5_file(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	unsigned char zDigest[16];
	int raw_output  = FALSE;
	const char *zFile;
	MD5Context sCtx;
	char zBuf[8192];
	void *pHandle;
	jx9_int64 n;
	int nLen;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a file path");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the file path */
	zFile = jx9_value_to_string(apArg[0], &nLen);
	/* Point to the target IO stream device */
	pStream = jx9VmGetStreamDevice(pCtx->pVm, &zFile, nLen);
	if( pStream == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "No such stream device, JX9 is returning FALSE");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	if( nArg > 1 ){
		raw_output = jx9_value_to_bool(apArg[1]);
	}
	/* Try to open the file in read-only mode */
	pHandle = jx9StreamOpenHandle(pCtx->pVm, pStream, zFile, JX9_IO_OPEN_RDONLY, FALSE, 0, FALSE, 0);
	if( pHandle == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening '%s'", zFile);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Init the MD5 context */
	MD5Init(&sCtx);
	/* Perform the requested operation */ 
	for(;;){
		n = pStream->xRead(pHandle, zBuf, sizeof(zBuf));
		if( n < 1 ){
			/* EOF or IO error, break immediately */
			break;
		}
		MD5Update(&sCtx, (const unsigned char *)zBuf, (unsigned int)n);
	}
	/* Close the stream */
	jx9StreamCloseHandle(pStream, pHandle);
	/* Extract the digest */
	MD5Final(zDigest, &sCtx);
	if( raw_output ){
		/* Output raw digest */
		jx9_result_string(pCtx, (const char *)zDigest, sizeof(zDigest));
	}else{
		/* Perform a binary to hex conversion */
		SyBinToHexConsumer((const void *)zDigest, sizeof(zDigest), vfsHashConsumer, pCtx);
	}
	return JX9_OK;
}
/*
 * string sha1_file(string $uri[, bool $raw_output = false ])
 *  Calculates the SHA1 hash of a given file.
 * Parameters
 *  $uri
 *   Target URI (file(/path/to/something) or URL(http://www.symisc.net/))
 *  $raw_output
 *   When TRUE, returns the digest in raw binary format with a length of 20.
 * Return
 *  Return the SHA1 digest on success or FALSE on failure.
 */
static int jx9Builtin_sha1_file(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	unsigned char zDigest[20];
	int raw_output  = FALSE;
	const char *zFile;
	SHA1Context sCtx;
	char zBuf[8192];
	void *pHandle;
	jx9_int64 n;
	int nLen;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a file path");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the file path */
	zFile = jx9_value_to_string(apArg[0], &nLen);
	/* Point to the target IO stream device */
	pStream = jx9VmGetStreamDevice(pCtx->pVm, &zFile, nLen);
	if( pStream == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "No such stream device, JX9 is returning FALSE");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	if( nArg > 1 ){
		raw_output = jx9_value_to_bool(apArg[1]);
	}
	/* Try to open the file in read-only mode */
	pHandle = jx9StreamOpenHandle(pCtx->pVm, pStream, zFile, JX9_IO_OPEN_RDONLY, FALSE, 0, FALSE, 0);
	if( pHandle == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening '%s'", zFile);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Init the SHA1 context */
	SHA1Init(&sCtx);
	/* Perform the requested operation */ 
	for(;;){
		n = pStream->xRead(pHandle, zBuf, sizeof(zBuf));
		if( n < 1 ){
			/* EOF or IO error, break immediately */
			break;
		}
		SHA1Update(&sCtx, (const unsigned char *)zBuf, (unsigned int)n);
	}
	/* Close the stream */
	jx9StreamCloseHandle(pStream, pHandle);
	/* Extract the digest */
	SHA1Final(&sCtx, zDigest);
	if( raw_output ){
		/* Output raw digest */
		jx9_result_string(pCtx, (const char *)zDigest, sizeof(zDigest));
	}else{
		/* Perform a binary to hex conversion */
		SyBinToHexConsumer((const void *)zDigest, sizeof(zDigest), vfsHashConsumer, pCtx);
	}
	return JX9_OK;
}
#endif /* JX9_DISABLE_HASH_FUNC */
/*
 * array parse_ini_file(string $filename[, bool $process_sections = false [, int $scanner_mode = INI_SCANNER_NORMAL ]] )
 *  Parse a configuration file.
 * Parameters
 * $filename
 *  The filename of the ini file being parsed.
 * $process_sections
 *  By setting the process_sections parameter to TRUE, you get a multidimensional array
 *  with the section names and settings included.
 *  The default for process_sections is FALSE.
 * $scanner_mode
 *  Can either be INI_SCANNER_NORMAL (default) or INI_SCANNER_RAW.
 *  If INI_SCANNER_RAW is supplied, then option values will not be parsed.
 * Return
 *  The settings are returned as an associative array on success.
 *  Otherwise is returned.
 */
static int jx9Builtin_parse_ini_file(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	const char *zFile;
	SyBlob sContents;
	void *pHandle;
	int nLen;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a file path");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the file path */
	zFile = jx9_value_to_string(apArg[0], &nLen);
	/* Point to the target IO stream device */
	pStream = jx9VmGetStreamDevice(pCtx->pVm, &zFile, nLen);
	if( pStream == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "No such stream device, JX9 is returning FALSE");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Try to open the file in read-only mode */
	pHandle = jx9StreamOpenHandle(pCtx->pVm, pStream, zFile, JX9_IO_OPEN_RDONLY, FALSE, 0, FALSE, 0);
	if( pHandle == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening '%s'", zFile);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	SyBlobInit(&sContents, &pCtx->pVm->sAllocator);
	/* Read the whole file */
	jx9StreamReadWholeFile(pHandle, pStream, &sContents);
	if( SyBlobLength(&sContents) < 1 ){
		/* Empty buffer, return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		/* Process the raw INI buffer */
		jx9ParseIniString(pCtx, (const char *)SyBlobData(&sContents), SyBlobLength(&sContents), 
			nArg > 1 ? jx9_value_to_bool(apArg[1]) : 0);
	}
	/* Close the stream */
	jx9StreamCloseHandle(pStream, pHandle);
	/* Release the working buffer */
	SyBlobRelease(&sContents);
	return JX9_OK;
}
/*
 * Section:
 *    ZIP archive processing.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */
typedef struct zip_raw_data zip_raw_data;
struct zip_raw_data
{
	int iType;         /* Where the raw data is stored */
	union raw_data{
		struct mmap_data{
			void *pMap;          /* Memory mapped data */
			jx9_int64 nSize;     /* Map size */
			const jx9_vfs *pVfs; /* Underlying vfs */
		}mmap;
		SyBlob sBlob;  /* Memory buffer */
	}raw;
};
#define ZIP_RAW_DATA_MMAPED 1 /* Memory mapped ZIP raw data */
#define ZIP_RAW_DATA_MEMBUF 2 /* ZIP raw data stored in a dynamically
                               * allocated memory chunk.
							   */
 /*
  * mixed zip_open(string $filename)
  *  Opens a new zip archive for reading.
  * Parameters
  *  $filename
  *   The file name of the ZIP archive to open.
  * Return
  *  A resource handle for later use with zip_read() and zip_close() or FALSE on failure.
  */
static int jx9Builtin_zip_open(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	const jx9_io_stream *pStream;
	SyArchive *pArchive;
	zip_raw_data *pRaw;
	const char *zFile;
	SyBlob *pContents;
	void *pHandle;
	int nLen;
	sxi32 rc;
	if( nArg < 1 || !jx9_value_is_string(apArg[0]) ){
		/* Missing/Invalid arguments, return FALSE */
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "Expecting a file path");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the file path */
	zFile = jx9_value_to_string(apArg[0], &nLen);
	/* Point to the target IO stream device */
	pStream = jx9VmGetStreamDevice(pCtx->pVm, &zFile, nLen);
	if( pStream == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "No such stream device, JX9 is returning FALSE");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Create an in-memory archive */
	pArchive = (SyArchive *)jx9_context_alloc_chunk(pCtx, sizeof(SyArchive)+sizeof(zip_raw_data), TRUE, FALSE);
	if( pArchive == 0 ){
		jx9_context_throw_error(pCtx, JX9_CTX_WARNING, "JX9 is running out of memory");
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	pRaw = (zip_raw_data *)&pArchive[1];
	/* Initialize the archive */
	SyArchiveInit(pArchive, &pCtx->pVm->sAllocator, 0, 0);
	/* Extract the default stream */
	if( pStream == pCtx->pVm->pDefStream /* file:// stream*/){
		const jx9_vfs *pVfs;
		/* Try to get a memory view of the whole file since ZIP files
		 * tends to be very big this days, this is a huge performance win.
		 */
		pVfs = jx9ExportBuiltinVfs();
		if( pVfs && pVfs->xMmap ){
			rc = pVfs->xMmap(zFile, &pRaw->raw.mmap.pMap, &pRaw->raw.mmap.nSize);
			if( rc == JX9_OK ){
				/* Nice, Extract the whole archive */
				rc = SyZipExtractFromBuf(pArchive, (const char *)pRaw->raw.mmap.pMap, (sxu32)pRaw->raw.mmap.nSize);
				if( rc != SXRET_OK ){
					if( pVfs->xUnmap ){
						pVfs->xUnmap(pRaw->raw.mmap.pMap, pRaw->raw.mmap.nSize);
					}
					/* Release the allocated chunk */
					jx9_context_free_chunk(pCtx, pArchive);
					/* Something goes wrong with this ZIP archive, return FALSE */
					jx9_result_bool(pCtx, 0);
					return JX9_OK;
				}
				/* Archive successfully opened */
				pRaw->iType = ZIP_RAW_DATA_MMAPED;
				pRaw->raw.mmap.pVfs = pVfs;
				goto success;
			}
		}
		/* FALL THROUGH */
	}
	/* Try to open the file in read-only mode */
	pHandle = jx9StreamOpenHandle(pCtx->pVm, pStream, zFile, JX9_IO_OPEN_RDONLY, FALSE, 0, FALSE, 0);
	if( pHandle == 0 ){
		jx9_context_throw_error_format(pCtx, JX9_CTX_ERR, "IO error while opening '%s'", zFile);
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	pContents = &pRaw->raw.sBlob;
	SyBlobInit(pContents, &pCtx->pVm->sAllocator);
	/* Read the whole file */
	jx9StreamReadWholeFile(pHandle, pStream, pContents);
	/* Assume an invalid ZIP file */
	rc = SXERR_INVALID;
	if( SyBlobLength(pContents) > 0 ){
		/* Extract archive entries */
		rc = SyZipExtractFromBuf(pArchive, (const char *)SyBlobData(pContents), SyBlobLength(pContents));
	}
	pRaw->iType = ZIP_RAW_DATA_MEMBUF;
	/* Close the stream */
	jx9StreamCloseHandle(pStream, pHandle);
	if( rc != SXRET_OK ){
		/* Release the working buffer */
		SyBlobRelease(pContents);
		/* Release the allocated chunk */
		jx9_context_free_chunk(pCtx, pArchive);
		/* Something goes wrong with this ZIP archive, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
success:
	/* Reset the loop cursor */
	SyArchiveResetLoopCursor(pArchive);
	/* Return the in-memory archive as a resource handle */
	jx9_result_resource(pCtx, pArchive);
	return JX9_OK;
}
/*
  * void zip_close(resource $zip)
  *  Close an in-memory ZIP archive.
  * Parameters
  *  $zip
  *   A ZIP file previously opened with zip_open().
  * Return
  *  null.
  */
static int jx9Builtin_zip_close(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyArchive *pArchive;
	zip_raw_data *pRaw;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive");
		return JX9_OK;
	}
	/* Point to the in-memory archive */
	pArchive = (SyArchive *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid ZIP archive */
	if( SXARCH_INVALID(pArchive) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive");
		return JX9_OK;
	}
	/* Release the archive */
	SyArchiveRelease(pArchive);
	pRaw = (zip_raw_data *)&pArchive[1];
	if( pRaw->iType == ZIP_RAW_DATA_MEMBUF ){
		SyBlobRelease(&pRaw->raw.sBlob);
	}else{
		const jx9_vfs *pVfs = pRaw->raw.mmap.pVfs;
		if( pVfs->xUnmap ){
			/* Unmap the memory view */
			pVfs->xUnmap(pRaw->raw.mmap.pMap, pRaw->raw.mmap.nSize);
		}
	}
	/* Release the memory chunk */
	jx9_context_free_chunk(pCtx, pArchive);
	return JX9_OK;
}
/*
  * mixed zip_read(resource $zip)
  *  Reads the next entry from an in-memory ZIP archive.
  * Parameters
  *  $zip
  *   A ZIP file previously opened with zip_open().
  * Return
  *  A directory entry resource for later use with the zip_entry_... functions
  *  or FALSE if there are no more entries to read, or an error code if an error occurred.
  */
static int jx9Builtin_zip_read(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyArchiveEntry *pNext = 0; /* cc warning */
	SyArchive *pArchive;
	sxi32 rc;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the in-memory archive */
	pArchive = (SyArchive *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid ZIP archive */
	if( SXARCH_INVALID(pArchive) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Extract the next entry */
	rc = SyArchiveGetNextEntry(pArchive, &pNext);
	if( rc != SXRET_OK ){
		/* No more entries in the central directory, return FALSE */
		jx9_result_bool(pCtx, 0);
	}else{
		/* Return as a resource handle */
		jx9_result_resource(pCtx, pNext);
		/* Point to the ZIP raw data */
		pNext->pUserData = (void *)&pArchive[1];
	}
	return JX9_OK;
}
/*
  * bool zip_entry_open(resource $zip, resource $zip_entry[, string $mode ])
  *  Open a directory entry for reading
  * Parameters
  *  $zip
  *   A ZIP file previously opened with zip_open().
  *  $zip_entry
  *   A directory entry returned by zip_read().
  * $mode
  *   Not used
  * Return
  *  A directory entry resource for later use with the zip_entry_... functions
  *  or FALSE if there are no more entries to read, or an error code if an error occurred.
  */
static int jx9Builtin_zip_entry_open(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyArchiveEntry *pEntry;
	SyArchive *pArchive;
	if( nArg < 2 || !jx9_value_is_resource(apArg[0]) || !jx9_value_is_resource(apArg[1]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Point to the in-memory archive */
	pArchive = (SyArchive *)jx9_value_to_resource(apArg[0]);
	/* Make sure we are dealing with a valid ZIP archive */
	if( SXARCH_INVALID(pArchive) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid ZIP archive entry */
	pEntry = (SyArchiveEntry *)jx9_value_to_resource(apArg[1]);
	if( SXARCH_ENTRY_INVALID(pEntry) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* All done. Actually this function is a no-op, return TRUE */
	jx9_result_bool(pCtx, 1);
	return JX9_OK;
}
/*
  * bool zip_entry_close(resource $zip_entry)
  *  Close a directory entry.
  * Parameters
  *  $zip_entry
  *   A directory entry returned by zip_read().
  * Return
  *  Returns TRUE on success or FALSE on failure.
  */
static int jx9Builtin_zip_entry_close(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyArchiveEntry *pEntry;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid ZIP archive entry */
	pEntry = (SyArchiveEntry *)jx9_value_to_resource(apArg[0]);
	if( SXARCH_ENTRY_INVALID(pEntry) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Reset the read cursor */
	pEntry->nReadCount = 0;
	/*All done. Actually this function is a no-op, return TRUE */
	jx9_result_bool(pCtx, 1);
	return JX9_OK;
}
/*
  * string zip_entry_name(resource $zip_entry)
  *  Retrieve the name of a directory entry.
  * Parameters
  *  $zip_entry
  *   A directory entry returned by zip_read().
  * Return
  *  The name of the directory entry.
  */
static int jx9Builtin_zip_entry_name(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyArchiveEntry *pEntry;
	SyString *pName;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid ZIP archive entry */
	pEntry = (SyArchiveEntry *)jx9_value_to_resource(apArg[0]);
	if( SXARCH_ENTRY_INVALID(pEntry) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Return entry name */
	pName = &pEntry->sFileName;
	jx9_result_string(pCtx, pName->zString, (int)pName->nByte);
	return JX9_OK;
}
/*
  * int64 zip_entry_filesize(resource $zip_entry)
  *  Retrieve the actual file size of a directory entry.
  * Parameters
  *  $zip_entry
  *   A directory entry returned by zip_read().
  * Return
  *  The size of the directory entry.
  */
static int jx9Builtin_zip_entry_filesize(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyArchiveEntry *pEntry;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid ZIP archive entry */
	pEntry = (SyArchiveEntry *)jx9_value_to_resource(apArg[0]);
	if( SXARCH_ENTRY_INVALID(pEntry) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Return entry size */
	jx9_result_int64(pCtx, (jx9_int64)pEntry->nByte);
	return JX9_OK;
}
/*
  * int64 zip_entry_compressedsize(resource $zip_entry)
  *  Retrieve the compressed size of a directory entry.
  * Parameters
  *  $zip_entry
  *   A directory entry returned by zip_read().
  * Return
  *  The compressed size.
  */
static int jx9Builtin_zip_entry_compressedsize(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyArchiveEntry *pEntry;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid ZIP archive entry */
	pEntry = (SyArchiveEntry *)jx9_value_to_resource(apArg[0]);
	if( SXARCH_ENTRY_INVALID(pEntry) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Return entry compressed size */
	jx9_result_int64(pCtx, (jx9_int64)pEntry->nByteCompr);
	return JX9_OK;
}
/*
  * string zip_entry_read(resource $zip_entry[, int $length])
  *  Reads from an open directory entry.
  * Parameters
  *  $zip_entry
  *   A directory entry returned by zip_read().
  *  $length
  *   The number of bytes to return. If not specified, this function
  *   will attempt to read 1024 bytes.
  * Return
  *  Returns the data read, or FALSE if the end of the file is reached.
  */
static int jx9Builtin_zip_entry_read(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyArchiveEntry *pEntry;
	zip_raw_data *pRaw;
	const char *zData;
	int iLength;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid ZIP archive entry */
	pEntry = (SyArchiveEntry *)jx9_value_to_resource(apArg[0]);
	if( SXARCH_ENTRY_INVALID(pEntry) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	zData = 0;
	if( pEntry->nReadCount >= pEntry->nByteCompr ){
		/* No more data to read, return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Set a default read length */
	iLength = 1024;
	if( nArg > 1 ){
		iLength = jx9_value_to_int(apArg[1]);
		if( iLength < 1 ){
			iLength = 1024;
		}
	}
	if( (sxu32)iLength > pEntry->nByteCompr - pEntry->nReadCount ){
		iLength = (int)(pEntry->nByteCompr - pEntry->nReadCount);
	}
	/* Return the entry contents */
	pRaw = (zip_raw_data *)pEntry->pUserData;
	if( pRaw->iType == ZIP_RAW_DATA_MEMBUF ){
		zData = (const char *)SyBlobDataAt(&pRaw->raw.sBlob, (pEntry->nOfft+pEntry->nReadCount));
	}else{
		const char *zMap = (const char *)pRaw->raw.mmap.pMap;
		/* Memory mmaped chunk */
		zData = &zMap[pEntry->nOfft+pEntry->nReadCount];
	}
	/* Increment the read counter */
	pEntry->nReadCount += iLength;
	/* Return the raw data */
	jx9_result_string(pCtx, zData, iLength);
	return JX9_OK;
}
/*
  * bool zip_entry_reset_cursor(resource $zip_entry)
  *  Reset the read cursor of an open directory entry.
  * Parameters
  *  $zip_entry
  *   A directory entry returned by zip_read().
  * Return
  *  TRUE on success, FALSE on failure.
  */
static int jx9Builtin_zip_entry_reset_cursor(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyArchiveEntry *pEntry;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid ZIP archive entry */
	pEntry = (SyArchiveEntry *)jx9_value_to_resource(apArg[0]);
	if( SXARCH_ENTRY_INVALID(pEntry) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Reset the cursor */
	pEntry->nReadCount = 0;
	/* Return TRUE */
	jx9_result_bool(pCtx, 1);
	return JX9_OK;
}
/*
  * string zip_entry_compressionmethod(resource $zip_entry)
  *  Retrieve the compression method of a directory entry.
  * Parameters
  *  $zip_entry
  *   A directory entry returned by zip_read().
  * Return
  *  The compression method on success or FALSE on failure.
  */
static int jx9Builtin_zip_entry_compressionmethod(jx9_context *pCtx, int nArg, jx9_value **apArg)
{
	SyArchiveEntry *pEntry;
	if( nArg < 1 || !jx9_value_is_resource(apArg[0]) ){
		/* Missing/Invalid arguments */
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	/* Make sure we are dealing with a valid ZIP archive entry */
	pEntry = (SyArchiveEntry *)jx9_value_to_resource(apArg[0]);
	if( SXARCH_ENTRY_INVALID(pEntry) ){
		jx9_context_throw_error(pCtx, JX9_CTX_ERR, "Expecting a ZIP archive entry");
		/* return FALSE */
		jx9_result_bool(pCtx, 0);
		return JX9_OK;
	}
	switch(pEntry->nComprMeth){
	case 0:
		/* No compression;entry is stored */
		jx9_result_string(pCtx, "stored", (int)sizeof("stored")-1);
		break;
	case 8:
		/* Entry is deflated (Default compression algorithm)  */
		jx9_result_string(pCtx, "deflate", (int)sizeof("deflate")-1);
		break;
		/* Exotic compression algorithms */ 
	case 1:
		jx9_result_string(pCtx, "shrunk", (int)sizeof("shrunk")-1);
		break;
	case 2:
	case 3:
	case 4:
	case 5:
		/* Entry is reduced */
		jx9_result_string(pCtx, "reduced", (int)sizeof("reduced")-1);
		break;
	case 6:
		/* Entry is imploded */
		jx9_result_string(pCtx, "implode", (int)sizeof("implode")-1);
		break;
	default:
		jx9_result_string(pCtx, "unknown", (int)sizeof("unknown")-1);
		break;
	}
	return JX9_OK;
}
#endif /* #ifndef JX9_DISABLE_BUILTIN_FUNC*/
/* NULL VFS [i.e: a no-op VFS]*/
static const jx9_vfs null_vfs = {
	"null_vfs", 
	JX9_VFS_VERSION, 
	0, /* int (*xChdir)(const char *) */
	0, /* int (*xChroot)(const char *); */
	0, /* int (*xGetcwd)(jx9_context *) */
	0, /* int (*xMkdir)(const char *, int, int) */
	0, /* int (*xRmdir)(const char *) */ 
	0, /* int (*xIsdir)(const char *) */
	0, /* int (*xRename)(const char *, const char *) */
	0, /*int (*xRealpath)(const char *, jx9_context *)*/
	0, /* int (*xSleep)(unsigned int) */
	0, /* int (*xUnlink)(const char *) */
	0, /* int (*xFileExists)(const char *) */
	0, /*int (*xChmod)(const char *, int)*/
	0, /*int (*xChown)(const char *, const char *)*/
	0, /*int (*xChgrp)(const char *, const char *)*/
	0, /* jx9_int64 (*xFreeSpace)(const char *) */
	0, /* jx9_int64 (*xTotalSpace)(const char *) */
	0, /* jx9_int64 (*xFileSize)(const char *) */
	0, /* jx9_int64 (*xFileAtime)(const char *) */
	0, /* jx9_int64 (*xFileMtime)(const char *) */
	0, /* jx9_int64 (*xFileCtime)(const char *) */
	0, /* int (*xStat)(const char *, jx9_value *, jx9_value *) */
	0, /* int (*xlStat)(const char *, jx9_value *, jx9_value *) */
	0, /* int (*xIsfile)(const char *) */
	0, /* int (*xIslink)(const char *) */
	0, /* int (*xReadable)(const char *) */
	0, /* int (*xWritable)(const char *) */
	0, /* int (*xExecutable)(const char *) */
	0, /* int (*xFiletype)(const char *, jx9_context *) */
	0, /* int (*xGetenv)(const char *, jx9_context *) */
	0, /* int (*xSetenv)(const char *, const char *) */ 
	0, /* int (*xTouch)(const char *, jx9_int64, jx9_int64) */
	0, /* int (*xMmap)(const char *, void **, jx9_int64 *) */
	0, /* void (*xUnmap)(void *, jx9_int64);  */
	0, /* int (*xLink)(const char *, const char *, int) */
	0, /* int (*xUmask)(int) */
	0, /* void (*xTempDir)(jx9_context *) */
	0, /* unsigned int (*xProcessId)(void) */
	0, /* int (*xUid)(void) */
	0, /* int (*xGid)(void) */
	0, /* void (*xUsername)(jx9_context *) */
	0  /* int (*xExec)(const char *, jx9_context *) */
};
#ifndef JX9_DISABLE_BUILTIN_FUNC
#ifndef JX9_DISABLE_DISK_IO
#ifdef __WINNT__
/*
 * Windows VFS implementation for the JX9 engine.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */
/* What follows here is code that is specific to windows systems. */
#include <Windows.h>
/*
** Convert a UTF-8 string to microsoft unicode (UTF-16?).
**
** Space to hold the returned string is obtained from HeapAlloc().
** Taken from the sqlite3 source tree
** status: Public Domain
*/
static WCHAR *jx9utf8ToUnicode(const char *zFilename){
  int nChar;
  WCHAR *zWideFilename;

  nChar = MultiByteToWideChar(CP_UTF8, 0, zFilename, -1, 0, 0);
  zWideFilename = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, nChar*sizeof(zWideFilename[0]));
  if( zWideFilename == 0 ){
 	return 0;
  } 
  nChar = MultiByteToWideChar(CP_UTF8, 0, zFilename, -1, zWideFilename, nChar);
  if( nChar==0 ){
    HeapFree(GetProcessHeap(), 0, zWideFilename);
    return 0;
  }
  return zWideFilename;
}
/*
** Convert a UTF-8 filename into whatever form the underlying
** operating system wants filenames in.Space to hold the result
** is obtained from HeapAlloc() and must be freed by the calling
** function.
** Taken from the sqlite3 source tree
** status: Public Domain
*/
static void *jx9convertUtf8Filename(const char *zFilename){
  void *zConverted;
  zConverted = jx9utf8ToUnicode(zFilename);
  return zConverted;
}
/*
** Convert microsoft unicode to UTF-8.  Space to hold the returned string is
** obtained from HeapAlloc().
** Taken from the sqlite3 source tree
** status: Public Domain
*/
static char *jx9unicodeToUtf8(const WCHAR *zWideFilename){
  char *zFilename;
  int nByte;

  nByte = WideCharToMultiByte(CP_UTF8, 0, zWideFilename, -1, 0, 0, 0, 0);
  zFilename = (char *)HeapAlloc(GetProcessHeap(), 0, nByte);
  if( zFilename == 0 ){
  	return 0;
  }
  nByte = WideCharToMultiByte(CP_UTF8, 0, zWideFilename, -1, zFilename, nByte, 0, 0);
  if( nByte == 0 ){
    HeapFree(GetProcessHeap(), 0, zFilename);
    return 0;
  }
  return zFilename;
}
/* int (*xchdir)(const char *) */
static int WinVfs_chdir(const char *zPath)
{
	void * pConverted;
	BOOL rc;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	rc = SetCurrentDirectoryW((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	return rc ? JX9_OK : -1;
}
/* int (*xGetcwd)(jx9_context *) */
static int WinVfs_getcwd(jx9_context *pCtx)
{
	WCHAR zDir[2048];
	char *zConverted;
	DWORD rc;
	/* Get the current directory */
	rc = GetCurrentDirectoryW(sizeof(zDir), zDir);
	if( rc < 1 ){
		return -1;
	}
	zConverted = jx9unicodeToUtf8(zDir);
	if( zConverted == 0 ){
		return -1;
	}
	jx9_result_string(pCtx, zConverted, -1/*Compute length automatically*/); /* Will make it's own copy */
	HeapFree(GetProcessHeap(), 0, zConverted);
	return JX9_OK;
}
/* int (*xMkdir)(const char *, int, int) */
static int WinVfs_mkdir(const char *zPath, int mode, int recursive)
{
	void * pConverted;
	BOOL rc;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	mode= 0; /* MSVC warning */
	recursive = 0;
	rc = CreateDirectoryW((LPCWSTR)pConverted, 0);
	HeapFree(GetProcessHeap(), 0, pConverted);
	return rc ? JX9_OK : -1;
}
/* int (*xRmdir)(const char *) */
static int WinVfs_rmdir(const char *zPath)
{
	void * pConverted;
	BOOL rc;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	rc = RemoveDirectoryW((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	return rc ? JX9_OK : -1;
}
/* int (*xIsdir)(const char *) */
static int WinVfs_isdir(const char *zPath)
{
	void * pConverted;
	DWORD dwAttr;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	dwAttr = GetFileAttributesW((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( dwAttr == INVALID_FILE_ATTRIBUTES ){
		return -1;
	}
	return (dwAttr & FILE_ATTRIBUTE_DIRECTORY) ? JX9_OK : -1;
}
/* int (*xRename)(const char *, const char *) */
static int WinVfs_Rename(const char *zOld, const char *zNew)
{
	void *pOld, *pNew;
	BOOL rc = 0;
	pOld = jx9convertUtf8Filename(zOld);
	if( pOld == 0 ){
		return -1;
	}
	pNew = jx9convertUtf8Filename(zNew);
	if( pNew  ){
		rc = MoveFileW((LPCWSTR)pOld, (LPCWSTR)pNew);
	}
	HeapFree(GetProcessHeap(), 0, pOld);
	if( pNew ){
		HeapFree(GetProcessHeap(), 0, pNew);
	}
	return rc ? JX9_OK : - 1;
}
/* int (*xRealpath)(const char *, jx9_context *) */
static int WinVfs_Realpath(const char *zPath, jx9_context *pCtx)
{
	WCHAR zTemp[2048];
	void *pPath;
	char *zReal;
	DWORD n;
	pPath = jx9convertUtf8Filename(zPath);
	if( pPath == 0 ){
		return -1;
	}
	n = GetFullPathNameW((LPCWSTR)pPath, 0, 0, 0);
	if( n > 0 ){
		if( n >= sizeof(zTemp) ){
			n = sizeof(zTemp) - 1;
		}
		GetFullPathNameW((LPCWSTR)pPath, n, zTemp, 0);
	}
	HeapFree(GetProcessHeap(), 0, pPath);
	if( !n ){
		return -1;
	}
	zReal = jx9unicodeToUtf8(zTemp);
	if( zReal == 0 ){
		return -1;
	}
	jx9_result_string(pCtx, zReal, -1); /* Will make it's own copy */
	HeapFree(GetProcessHeap(), 0, zReal);
	return JX9_OK;
}
/* int (*xSleep)(unsigned int) */
static int WinVfs_Sleep(unsigned int uSec)
{
	Sleep(uSec/1000/*uSec per Millisec */);
	return JX9_OK;
}
/* int (*xUnlink)(const char *) */
static int WinVfs_unlink(const char *zPath)
{
	void * pConverted;
	BOOL rc;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	rc = DeleteFileW((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	return rc ? JX9_OK : - 1;
}
/* jx9_int64 (*xFreeSpace)(const char *) */
static jx9_int64 WinVfs_DiskFreeSpace(const char *zPath)
{
#ifdef _WIN32_WCE
	/* GetDiskFreeSpace is not supported under WINCE */
	SXUNUSED(zPath);
	return 0;
#else
	DWORD dwSectPerClust, dwBytesPerSect, dwFreeClusters, dwTotalClusters;
	void * pConverted;
	WCHAR *p;
	BOOL rc;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return 0;
	}
	p = (WCHAR *)pConverted;
	for(;*p;p++){
		if( *p == '\\' || *p == '/'){
			*p = '\0';
			break;
		}
	}
	rc = GetDiskFreeSpaceW((LPCWSTR)pConverted, &dwSectPerClust, &dwBytesPerSect, &dwFreeClusters, &dwTotalClusters);
	if( !rc ){
		return 0;
	}
	return (jx9_int64)dwFreeClusters * dwSectPerClust * dwBytesPerSect;
#endif
}
/* jx9_int64 (*xTotalSpace)(const char *) */
static jx9_int64 WinVfs_DiskTotalSpace(const char *zPath)
{
#ifdef _WIN32_WCE
	/* GetDiskFreeSpace is not supported under WINCE */
	SXUNUSED(zPath);
	return 0;
#else
	DWORD dwSectPerClust, dwBytesPerSect, dwFreeClusters, dwTotalClusters;
	void * pConverted;
	WCHAR *p;
	BOOL rc;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return 0;
	}
	p = (WCHAR *)pConverted;
	for(;*p;p++){
		if( *p == '\\' || *p == '/'){
			*p = '\0';
			break;
		}
	}
	rc = GetDiskFreeSpaceW((LPCWSTR)pConverted, &dwSectPerClust, &dwBytesPerSect, &dwFreeClusters, &dwTotalClusters);
	if( !rc ){
		return 0;
	}
	return (jx9_int64)dwTotalClusters * dwSectPerClust * dwBytesPerSect;
#endif
}
/* int (*xFileExists)(const char *) */
static int WinVfs_FileExists(const char *zPath)
{
	void * pConverted;
	DWORD dwAttr;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	dwAttr = GetFileAttributesW((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( dwAttr == INVALID_FILE_ATTRIBUTES ){
		return -1;
	}
	return JX9_OK;
}
/* Open a file in a read-only mode */
static HANDLE OpenReadOnly(LPCWSTR pPath)
{
	DWORD dwType = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;
	DWORD dwShare = FILE_SHARE_READ | FILE_SHARE_WRITE;
	DWORD dwAccess = GENERIC_READ;
	DWORD dwCreate = OPEN_EXISTING;	
	HANDLE pHandle;
	pHandle = CreateFileW(pPath, dwAccess, dwShare, 0, dwCreate, dwType, 0);
	if( pHandle == INVALID_HANDLE_VALUE){
		return 0;
	}
	return pHandle;
}
/* jx9_int64 (*xFileSize)(const char *) */
static jx9_int64 WinVfs_FileSize(const char *zPath)
{
	DWORD dwLow, dwHigh;
	void * pConverted;
	jx9_int64 nSize;
	HANDLE pHandle;
	
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	/* Open the file in read-only mode */
	pHandle = OpenReadOnly((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( pHandle ){
		dwLow = GetFileSize(pHandle, &dwHigh);
		nSize = dwHigh;
		nSize <<= 32;
		nSize += dwLow;
		CloseHandle(pHandle);
	}else{
		nSize = -1;
	}
	return nSize;
}
#define TICKS_PER_SECOND 10000000
#define EPOCH_DIFFERENCE 11644473600LL
/* Convert Windows timestamp to UNIX timestamp */
static jx9_int64 convertWindowsTimeToUnixTime(LPFILETIME pTime)
{
    jx9_int64 input, temp;
	input = pTime->dwHighDateTime;
	input <<= 32;
	input += pTime->dwLowDateTime;
    temp = input / TICKS_PER_SECOND; /*convert from 100ns intervals to seconds*/
    temp = temp - EPOCH_DIFFERENCE;  /*subtract number of seconds between epochs*/
    return temp;
}
/* Convert UNIX timestamp to Windows timestamp */
static void convertUnixTimeToWindowsTime(jx9_int64 nUnixtime, LPFILETIME pOut)
{
  jx9_int64 result = EPOCH_DIFFERENCE;
  result += nUnixtime;
  result *= 10000000LL;
  pOut->dwHighDateTime = (DWORD)(nUnixtime>>32);
  pOut->dwLowDateTime = (DWORD)nUnixtime;
}
/* int (*xTouch)(const char *, jx9_int64, jx9_int64) */
static int WinVfs_Touch(const char *zPath, jx9_int64 touch_time, jx9_int64 access_time)
{
	FILETIME sTouch, sAccess;
	void *pConverted;
	void *pHandle;
	BOOL rc = 0;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	pHandle = OpenReadOnly((LPCWSTR)pConverted);
	if( pHandle ){
		if( touch_time < 0 ){
			GetSystemTimeAsFileTime(&sTouch);
		}else{
			convertUnixTimeToWindowsTime(touch_time, &sTouch);
		}
		if( access_time < 0 ){
			/* Use the touch time */
			sAccess = sTouch; /* Structure assignment */
		}else{
			convertUnixTimeToWindowsTime(access_time, &sAccess);
		}
		rc = SetFileTime(pHandle, &sTouch, &sAccess, 0);
		/* Close the handle */
		CloseHandle(pHandle);
	}
	HeapFree(GetProcessHeap(), 0, pConverted);
	return rc ? JX9_OK : -1;
}
/* jx9_int64 (*xFileAtime)(const char *) */
static jx9_int64 WinVfs_FileAtime(const char *zPath)
{
	BY_HANDLE_FILE_INFORMATION sInfo;
	void * pConverted;
	jx9_int64 atime;
	HANDLE pHandle;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	/* Open the file in read-only mode */
	pHandle = OpenReadOnly((LPCWSTR)pConverted);
	if( pHandle ){
		BOOL rc;
		rc = GetFileInformationByHandle(pHandle, &sInfo);
		if( rc ){
			atime = convertWindowsTimeToUnixTime(&sInfo.ftLastAccessTime);
		}else{
			atime = -1;
		}
		CloseHandle(pHandle);
	}else{
		atime = -1;
	}
	HeapFree(GetProcessHeap(), 0, pConverted);
	return atime;
}
/* jx9_int64 (*xFileMtime)(const char *) */
static jx9_int64 WinVfs_FileMtime(const char *zPath)
{
	BY_HANDLE_FILE_INFORMATION sInfo;
	void * pConverted;
	jx9_int64 mtime;
	HANDLE pHandle;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	/* Open the file in read-only mode */
	pHandle = OpenReadOnly((LPCWSTR)pConverted);
	if( pHandle ){
		BOOL rc;
		rc = GetFileInformationByHandle(pHandle, &sInfo);
		if( rc ){
			mtime = convertWindowsTimeToUnixTime(&sInfo.ftLastWriteTime);
		}else{
			mtime = -1;
		}
		CloseHandle(pHandle);
	}else{
		mtime = -1;
	}
	HeapFree(GetProcessHeap(), 0, pConverted);
	return mtime;
}
/* jx9_int64 (*xFileCtime)(const char *) */
static jx9_int64 WinVfs_FileCtime(const char *zPath)
{
	BY_HANDLE_FILE_INFORMATION sInfo;
	void * pConverted;
	jx9_int64 ctime;
	HANDLE pHandle;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	/* Open the file in read-only mode */
	pHandle = OpenReadOnly((LPCWSTR)pConverted);
	if( pHandle ){
		BOOL rc;
		rc = GetFileInformationByHandle(pHandle, &sInfo);
		if( rc ){
			ctime = convertWindowsTimeToUnixTime(&sInfo.ftCreationTime);
		}else{
			ctime = -1;
		}
		CloseHandle(pHandle);
	}else{
		ctime = -1;
	}
	HeapFree(GetProcessHeap(), 0, pConverted);
	return ctime;
}
/* int (*xStat)(const char *, jx9_value *, jx9_value *) */
/* int (*xlStat)(const char *, jx9_value *, jx9_value *) */
static int WinVfs_Stat(const char *zPath, jx9_value *pArray, jx9_value *pWorker)
{
	BY_HANDLE_FILE_INFORMATION sInfo;
	void *pConverted;
	HANDLE pHandle;
	BOOL rc;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	/* Open the file in read-only mode */
	pHandle = OpenReadOnly((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( pHandle == 0 ){
		return -1;
	}
	rc = GetFileInformationByHandle(pHandle, &sInfo);
	CloseHandle(pHandle);
	if( !rc ){
		return -1;
	}
	/* dev */
	jx9_value_int64(pWorker, (jx9_int64)sInfo.dwVolumeSerialNumber);
	jx9_array_add_strkey_elem(pArray, "dev", pWorker); /* Will make it's own copy */
	/* ino */
	jx9_value_int64(pWorker, (jx9_int64)(((jx9_int64)sInfo.nFileIndexHigh << 32) | sInfo.nFileIndexLow));
	jx9_array_add_strkey_elem(pArray, "ino", pWorker); /* Will make it's own copy */
	/* mode */
	jx9_value_int(pWorker, 0);
	jx9_array_add_strkey_elem(pArray, "mode", pWorker);
	/* nlink */
	jx9_value_int(pWorker, (int)sInfo.nNumberOfLinks);
	jx9_array_add_strkey_elem(pArray, "nlink", pWorker); /* Will make it's own copy */
	/* uid, gid, rdev */
	jx9_value_int(pWorker, 0);
	jx9_array_add_strkey_elem(pArray, "uid", pWorker);
	jx9_array_add_strkey_elem(pArray, "gid", pWorker);
	jx9_array_add_strkey_elem(pArray, "rdev", pWorker);
	/* size */
	jx9_value_int64(pWorker, (jx9_int64)(((jx9_int64)sInfo.nFileSizeHigh << 32) | sInfo.nFileSizeLow));
	jx9_array_add_strkey_elem(pArray, "size", pWorker); /* Will make it's own copy */
	/* atime */
	jx9_value_int64(pWorker, convertWindowsTimeToUnixTime(&sInfo.ftLastAccessTime));
	jx9_array_add_strkey_elem(pArray, "atime", pWorker); /* Will make it's own copy */
	/* mtime */
	jx9_value_int64(pWorker, convertWindowsTimeToUnixTime(&sInfo.ftLastWriteTime));
	jx9_array_add_strkey_elem(pArray, "mtime", pWorker); /* Will make it's own copy */
	/* ctime */
	jx9_value_int64(pWorker, convertWindowsTimeToUnixTime(&sInfo.ftCreationTime));
	jx9_array_add_strkey_elem(pArray, "ctime", pWorker); /* Will make it's own copy */
	/* blksize, blocks */
	jx9_value_int(pWorker, 0);		
	jx9_array_add_strkey_elem(pArray, "blksize", pWorker);
	jx9_array_add_strkey_elem(pArray, "blocks", pWorker);
	return JX9_OK;
}
/* int (*xIsfile)(const char *) */
static int WinVfs_isfile(const char *zPath)
{
	void * pConverted;
	DWORD dwAttr;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	dwAttr = GetFileAttributesW((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( dwAttr == INVALID_FILE_ATTRIBUTES ){
		return -1;
	}
	return (dwAttr & (FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_ARCHIVE)) ? JX9_OK : -1;
}
/* int (*xIslink)(const char *) */
static int WinVfs_islink(const char *zPath)
{
	void * pConverted;
	DWORD dwAttr;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	dwAttr = GetFileAttributesW((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( dwAttr == INVALID_FILE_ATTRIBUTES ){
		return -1;
	}
	return (dwAttr & FILE_ATTRIBUTE_REPARSE_POINT) ? JX9_OK : -1;
}
/* int (*xWritable)(const char *) */
static int WinVfs_iswritable(const char *zPath)
{
	void * pConverted;
	DWORD dwAttr;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	dwAttr = GetFileAttributesW((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( dwAttr == INVALID_FILE_ATTRIBUTES ){
		return -1;
	}
	if( (dwAttr & (FILE_ATTRIBUTE_ARCHIVE|FILE_ATTRIBUTE_NORMAL)) == 0 ){
		/* Not a regular file */
		return -1;
	}
	if( dwAttr & FILE_ATTRIBUTE_READONLY ){
		/* Read-only file */
		return -1;
	}
	/* File is writable */
	return JX9_OK;
}
/* int (*xExecutable)(const char *) */
static int WinVfs_isexecutable(const char *zPath)
{
	void * pConverted;
	DWORD dwAttr;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	dwAttr = GetFileAttributesW((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( dwAttr == INVALID_FILE_ATTRIBUTES ){
		return -1;
	}
	if( (dwAttr & FILE_ATTRIBUTE_NORMAL) == 0 ){
		/* Not a regular file */
		return -1;
	}
	/* File is executable */
	return JX9_OK;
}
/* int (*xFiletype)(const char *, jx9_context *) */
static int WinVfs_Filetype(const char *zPath, jx9_context *pCtx)
{
	void * pConverted;
	DWORD dwAttr;
	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		/* Expand 'unknown' */
		jx9_result_string(pCtx, "unknown", sizeof("unknown")-1);
		return -1;
	}
	dwAttr = GetFileAttributesW((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( dwAttr == INVALID_FILE_ATTRIBUTES ){
		/* Expand 'unknown' */
		jx9_result_string(pCtx, "unknown", sizeof("unknown")-1);
		return -1;
	}
	if(dwAttr & (FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_ARCHIVE) ){
		jx9_result_string(pCtx, "file", sizeof("file")-1);
	}else if(dwAttr & FILE_ATTRIBUTE_DIRECTORY){
		jx9_result_string(pCtx, "dir", sizeof("dir")-1);
	}else if(dwAttr & FILE_ATTRIBUTE_REPARSE_POINT){
		jx9_result_string(pCtx, "link", sizeof("link")-1);
	}else if(dwAttr & (FILE_ATTRIBUTE_DEVICE)){
		jx9_result_string(pCtx, "block", sizeof("block")-1);
	}else{
		jx9_result_string(pCtx, "unknown", sizeof("unknown")-1);
	}
	return JX9_OK;
}
/* int (*xGetenv)(const char *, jx9_context *) */
static int WinVfs_Getenv(const char *zVar, jx9_context *pCtx)
{
	char zValue[1024];
	DWORD n;
	/*
	 * According to MSDN
	 * If lpBuffer is not large enough to hold the data, the return 
	 * value is the buffer size, in characters, required to hold the 
	 * string and its terminating null character and the contents 
	 * of lpBuffer are undefined.
	 */
	n = sizeof(zValue);
	SyMemcpy("Undefined", zValue, sizeof("Undefined")-1);
	/* Extract the environment value */
	n = GetEnvironmentVariableA(zVar, zValue, sizeof(zValue));
	if( !n ){
		/* No such variable*/
		return -1;
	}
	jx9_result_string(pCtx, zValue, (int)n);
	return JX9_OK;
}
/* int (*xSetenv)(const char *, const char *) */
static int WinVfs_Setenv(const char *zName, const char *zValue)
{
	BOOL rc;
	rc = SetEnvironmentVariableA(zName, zValue);
	return rc ? JX9_OK : -1;
}
/* int (*xMmap)(const char *, void **, jx9_int64 *) */
static int WinVfs_Mmap(const char *zPath, void **ppMap, jx9_int64 *pSize)
{
	DWORD dwSizeLow, dwSizeHigh;
	HANDLE pHandle, pMapHandle;
	void *pConverted, *pView;

	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	pHandle = OpenReadOnly((LPCWSTR)pConverted);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( pHandle == 0 ){
		return -1;
	}
	/* Get the file size */
	dwSizeLow = GetFileSize(pHandle, &dwSizeHigh);
	/* Create the mapping */
	pMapHandle = CreateFileMappingW(pHandle, 0, PAGE_READONLY, dwSizeHigh, dwSizeLow, 0);
	if( pMapHandle == 0 ){
		CloseHandle(pHandle);
		return -1;
	}
	*pSize = ((jx9_int64)dwSizeHigh << 32) | dwSizeLow;
	/* Obtain the view */
	pView = MapViewOfFile(pMapHandle, FILE_MAP_READ, 0, 0, (SIZE_T)(*pSize));
	if( pView ){
		/* Let the upper layer point to the view */
		*ppMap = pView;
	}
	/* Close the handle
	 * According to MSDN it's OK the close the HANDLES.
	 */
	CloseHandle(pMapHandle);
	CloseHandle(pHandle);
	return pView ? JX9_OK : -1;
}
/* void (*xUnmap)(void *, jx9_int64)  */
static void WinVfs_Unmap(void *pView, jx9_int64 nSize)
{
	nSize = 0; /* Compiler warning */
	UnmapViewOfFile(pView);
}
/* void (*xTempDir)(jx9_context *) */
static void WinVfs_TempDir(jx9_context *pCtx)
{
	CHAR zTemp[1024];
	DWORD n;
	n = GetTempPathA(sizeof(zTemp), zTemp);
	if( n < 1 ){
		/* Assume the default windows temp directory */
		jx9_result_string(pCtx, "C:\\Windows\\Temp", -1/*Compute length automatically*/);
	}else{
		jx9_result_string(pCtx, zTemp, (int)n);
	}
}
/* unsigned int (*xProcessId)(void) */
static unsigned int WinVfs_ProcessId(void)
{
	DWORD nID = 0;
#ifndef __MINGW32__
	nID = GetProcessId(GetCurrentProcess());
#endif /* __MINGW32__ */
	return (unsigned int)nID;
}

/* Export the windows vfs */
static const jx9_vfs sWinVfs = {
	"Windows_vfs", 
	JX9_VFS_VERSION, 
	WinVfs_chdir,    /* int (*xChdir)(const char *) */
	0,               /* int (*xChroot)(const char *); */
	WinVfs_getcwd,   /* int (*xGetcwd)(jx9_context *) */
	WinVfs_mkdir,    /* int (*xMkdir)(const char *, int, int) */
	WinVfs_rmdir,    /* int (*xRmdir)(const char *) */ 
	WinVfs_isdir,    /* int (*xIsdir)(const char *) */
	WinVfs_Rename,   /* int (*xRename)(const char *, const char *) */
	WinVfs_Realpath, /*int (*xRealpath)(const char *, jx9_context *)*/
	WinVfs_Sleep,               /* int (*xSleep)(unsigned int) */
	WinVfs_unlink,   /* int (*xUnlink)(const char *) */
	WinVfs_FileExists, /* int (*xFileExists)(const char *) */
	0, /*int (*xChmod)(const char *, int)*/
	0, /*int (*xChown)(const char *, const char *)*/
	0, /*int (*xChgrp)(const char *, const char *)*/
	WinVfs_DiskFreeSpace, /* jx9_int64 (*xFreeSpace)(const char *) */
	WinVfs_DiskTotalSpace, /* jx9_int64 (*xTotalSpace)(const char *) */
	WinVfs_FileSize, /* jx9_int64 (*xFileSize)(const char *) */
	WinVfs_FileAtime, /* jx9_int64 (*xFileAtime)(const char *) */
	WinVfs_FileMtime, /* jx9_int64 (*xFileMtime)(const char *) */
	WinVfs_FileCtime, /* jx9_int64 (*xFileCtime)(const char *) */
	WinVfs_Stat, /* int (*xStat)(const char *, jx9_value *, jx9_value *) */
	WinVfs_Stat, /* int (*xlStat)(const char *, jx9_value *, jx9_value *) */
	WinVfs_isfile,     /* int (*xIsfile)(const char *) */
	WinVfs_islink,     /* int (*xIslink)(const char *) */
	WinVfs_isfile,     /* int (*xReadable)(const char *) */
	WinVfs_iswritable, /* int (*xWritable)(const char *) */
	WinVfs_isexecutable, /* int (*xExecutable)(const char *) */
	WinVfs_Filetype,   /* int (*xFiletype)(const char *, jx9_context *) */
	WinVfs_Getenv,     /* int (*xGetenv)(const char *, jx9_context *) */
	WinVfs_Setenv,     /* int (*xSetenv)(const char *, const char *) */ 
	WinVfs_Touch,      /* int (*xTouch)(const char *, jx9_int64, jx9_int64) */
	WinVfs_Mmap,       /* int (*xMmap)(const char *, void **, jx9_int64 *) */
	WinVfs_Unmap,      /* void (*xUnmap)(void *, jx9_int64);  */
	0,                 /* int (*xLink)(const char *, const char *, int) */
	0,                 /* int (*xUmask)(int) */
	WinVfs_TempDir,    /* void (*xTempDir)(jx9_context *) */
	WinVfs_ProcessId,  /* unsigned int (*xProcessId)(void) */
	0, /* int (*xUid)(void) */
	0, /* int (*xGid)(void) */
	0, /* void (*xUsername)(jx9_context *) */
	0  /* int (*xExec)(const char *, jx9_context *) */
};
/* Windows file IO */
#ifndef INVALID_SET_FILE_POINTER
# define INVALID_SET_FILE_POINTER ((DWORD)-1)
#endif
/* int (*xOpen)(const char *, int, jx9_value *, void **) */
static int WinFile_Open(const char *zPath, int iOpenMode, jx9_value *pResource, void **ppHandle)
{
	DWORD dwType = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;
	DWORD dwAccess = GENERIC_READ;
	DWORD dwShare, dwCreate;	
	void *pConverted;
	HANDLE pHandle;

	pConverted = jx9convertUtf8Filename(zPath);
	if( pConverted == 0 ){
		return -1;
	}
	/* Set the desired flags according to the open mode */
	if( iOpenMode & JX9_IO_OPEN_CREATE ){
		/* Open existing file, or create if it doesn't exist */
		dwCreate = OPEN_ALWAYS;
		if( iOpenMode & JX9_IO_OPEN_TRUNC ){
			/* If the specified file exists and is writable, the function overwrites the file */
			dwCreate = CREATE_ALWAYS;
		}
	}else if( iOpenMode & JX9_IO_OPEN_EXCL ){
		/* Creates a new file, only if it does not already exist.
		* If the file exists, it fails.
		*/
		dwCreate = CREATE_NEW;
	}else if( iOpenMode & JX9_IO_OPEN_TRUNC ){
		/* Opens a file and truncates it so that its size is zero bytes
		 * The file must exist.
		 */
		dwCreate = TRUNCATE_EXISTING;
	}else{
		/* Opens a file, only if it exists. */
		dwCreate = OPEN_EXISTING;
	}
	if( iOpenMode & JX9_IO_OPEN_RDWR ){
		/* Read+Write access */
		dwAccess |= GENERIC_WRITE;
	}else if( iOpenMode & JX9_IO_OPEN_WRONLY ){
		/* Write only access */
		dwAccess = GENERIC_WRITE;
	}
	if( iOpenMode & JX9_IO_OPEN_APPEND ){
		/* Append mode */
		dwAccess = FILE_APPEND_DATA;
	}
	if( iOpenMode & JX9_IO_OPEN_TEMP ){
		/* File is temporary */
		dwType = FILE_ATTRIBUTE_TEMPORARY;
	}
	dwShare = FILE_SHARE_READ | FILE_SHARE_WRITE;
	pHandle = CreateFileW((LPCWSTR)pConverted, dwAccess, dwShare, 0, dwCreate, dwType, 0);
	HeapFree(GetProcessHeap(), 0, pConverted);
	if( pHandle == INVALID_HANDLE_VALUE){
		SXUNUSED(pResource); /* MSVC warning */
		return -1;
	}
	/* Make the handle accessible to the upper layer */
	*ppHandle = (void *)pHandle;
	return JX9_OK;
}
/* An instance of the following structure is used to record state information 
 * while iterating throw directory entries.
 */
typedef struct WinDir_Info WinDir_Info;
struct WinDir_Info
{
	HANDLE pDirHandle;
	void *pPath;
	WIN32_FIND_DATAW sInfo;
	int rc;
};
/* int (*xOpenDir)(const char *, jx9_value *, void **) */
static int WinDir_Open(const char *zPath, jx9_value *pResource, void **ppHandle)
{
	WinDir_Info *pDirInfo;
	void *pConverted;
	char *zPrep;
	sxu32 n;
	/* Prepare the path */
	n = SyStrlen(zPath);
	zPrep = (char *)HeapAlloc(GetProcessHeap(), 0, n+sizeof("\\*")+4);
	if( zPrep == 0 ){
		return -1;
	}
	SyMemcpy((const void *)zPath, zPrep, n);
	zPrep[n]   = '\\';
	zPrep[n+1] =  '*';
	zPrep[n+2] = 0;
	pConverted = jx9convertUtf8Filename(zPrep);
	HeapFree(GetProcessHeap(), 0, zPrep);
	if( pConverted == 0 ){
		return -1;
	}
	/* Allocate a new instance */
	pDirInfo = (WinDir_Info *)HeapAlloc(GetProcessHeap(), 0, sizeof(WinDir_Info));
	if( pDirInfo == 0 ){
		pResource = 0; /* Compiler warning */
		return -1;
	}
	pDirInfo->rc = SXRET_OK;
	pDirInfo->pDirHandle = FindFirstFileW((LPCWSTR)pConverted, &pDirInfo->sInfo);
	if( pDirInfo->pDirHandle == INVALID_HANDLE_VALUE ){
		/* Cannot open directory */
		HeapFree(GetProcessHeap(), 0, pConverted);
		HeapFree(GetProcessHeap(), 0, pDirInfo);
		return -1;
	}
	/* Save the path */
	pDirInfo->pPath = pConverted;
	/* Save our structure */
	*ppHandle = pDirInfo;
	return JX9_OK;
}
/* void (*xCloseDir)(void *) */
static void WinDir_Close(void *pUserData)
{
	WinDir_Info *pDirInfo = (WinDir_Info *)pUserData;
	if( pDirInfo->pDirHandle != INVALID_HANDLE_VALUE ){
		FindClose(pDirInfo->pDirHandle);
	}
	HeapFree(GetProcessHeap(), 0, pDirInfo->pPath);
	HeapFree(GetProcessHeap(), 0, pDirInfo);
}
/* void (*xClose)(void *); */
static void WinFile_Close(void *pUserData)
{
	HANDLE pHandle = (HANDLE)pUserData;
	CloseHandle(pHandle);
}
/* int (*xReadDir)(void *, jx9_context *) */
static int WinDir_Read(void *pUserData, jx9_context *pCtx)
{
	WinDir_Info *pDirInfo = (WinDir_Info *)pUserData;
	LPWIN32_FIND_DATAW pData;
	char *zName;
	BOOL rc;
	sxu32 n;
	if( pDirInfo->rc != SXRET_OK ){
		/* No more entry to process */
		return -1;
	}
	pData = &pDirInfo->sInfo;
	for(;;){
		zName = jx9unicodeToUtf8(pData->cFileName);
		if( zName == 0 ){
			/* Out of memory */
			return -1;
		}
		n = SyStrlen(zName);
		/* Ignore '.' && '..' */
		if( n > sizeof("..")-1 || zName[0] != '.' || ( n == sizeof("..")-1 && zName[1] != '.') ){
			break;
		}
		HeapFree(GetProcessHeap(), 0, zName);
		rc = FindNextFileW(pDirInfo->pDirHandle, &pDirInfo->sInfo);
		if( !rc ){
			return -1;
		}
	}
	/* Return the current file name */
	jx9_result_string(pCtx, zName, -1);
	HeapFree(GetProcessHeap(), 0, zName);
	/* Point to the next entry */
	rc = FindNextFileW(pDirInfo->pDirHandle, &pDirInfo->sInfo);
	if( !rc ){
		pDirInfo->rc = SXERR_EOF;
	}
	return JX9_OK;
}
/* void (*xRewindDir)(void *) */
static void WinDir_RewindDir(void *pUserData)
{
	WinDir_Info *pDirInfo = (WinDir_Info *)pUserData;
	FindClose(pDirInfo->pDirHandle);
	pDirInfo->pDirHandle = FindFirstFileW((LPCWSTR)pDirInfo->pPath, &pDirInfo->sInfo);
	if( pDirInfo->pDirHandle == INVALID_HANDLE_VALUE ){
		pDirInfo->rc = SXERR_EOF;
	}else{
		pDirInfo->rc = SXRET_OK;
	}
}
/* jx9_int64 (*xRead)(void *, void *, jx9_int64); */
static jx9_int64 WinFile_Read(void *pOS, void *pBuffer, jx9_int64 nDatatoRead)
{
	HANDLE pHandle = (HANDLE)pOS;
	DWORD nRd;
	BOOL rc;
	rc = ReadFile(pHandle, pBuffer, (DWORD)nDatatoRead, &nRd, 0);
	if( !rc ){
		/* EOF or IO error */
		return -1;
	}
	return (jx9_int64)nRd;
}
/* jx9_int64 (*xWrite)(void *, const void *, jx9_int64); */
static jx9_int64 WinFile_Write(void *pOS, const void *pBuffer, jx9_int64 nWrite)
{
	const char *zData = (const char *)pBuffer;
	HANDLE pHandle = (HANDLE)pOS;
	jx9_int64 nCount;
	DWORD nWr;
	BOOL rc;
	nWr = 0;
	nCount = 0;
	for(;;){
		if( nWrite < 1 ){
			break;
		}
		rc = WriteFile(pHandle, zData, (DWORD)nWrite, &nWr, 0);
		if( !rc ){
			/* IO error */
			break;
		}
		nWrite -= nWr;
		nCount += nWr;
		zData += nWr;
	}
	if( nWrite > 0 ){
		return -1;
	}
	return nCount;
}
/* int (*xSeek)(void *, jx9_int64, int) */
static int WinFile_Seek(void *pUserData, jx9_int64 iOfft, int whence)
{
	HANDLE pHandle = (HANDLE)pUserData;
	DWORD dwMove, dwNew;
	LONG nHighOfft;
	switch(whence){
	case 1:/*SEEK_CUR*/
		dwMove = FILE_CURRENT;
		break;
	case 2: /* SEEK_END */
		dwMove = FILE_END;
		break;
	case 0: /* SEEK_SET */
	default:
		dwMove = FILE_BEGIN;
		break;
	}
	nHighOfft = (LONG)(iOfft >> 32);
	dwNew = SetFilePointer(pHandle, (LONG)iOfft, &nHighOfft, dwMove);
	if( dwNew == INVALID_SET_FILE_POINTER ){
		return -1;
	}
	return JX9_OK;
}
/* int (*xLock)(void *, int) */
static int WinFile_Lock(void *pUserData, int lock_type)
{
	HANDLE pHandle = (HANDLE)pUserData;
	static DWORD dwLo = 0, dwHi = 0; /* xx: MT-SAFE */
	OVERLAPPED sDummy;
	BOOL rc;
	SyZero(&sDummy, sizeof(sDummy));
	/* Get the file size */
	if( lock_type < 1 ){
		/* Unlock the file */
		rc = UnlockFileEx(pHandle, 0, dwLo, dwHi, &sDummy);
	}else{
		DWORD dwFlags = LOCKFILE_FAIL_IMMEDIATELY; /* Shared non-blocking lock by default*/
		/* Lock the file */
		if( lock_type == 1 /* LOCK_EXCL */ ){
			dwFlags |= LOCKFILE_EXCLUSIVE_LOCK;
		}
		dwLo = GetFileSize(pHandle, &dwHi);
		rc = LockFileEx(pHandle, dwFlags, 0, dwLo, dwHi, &sDummy);
	}
	return rc ? JX9_OK : -1 /* Lock error */;
}
/* jx9_int64 (*xTell)(void *) */
static jx9_int64 WinFile_Tell(void *pUserData)
{
	HANDLE pHandle = (HANDLE)pUserData;
	DWORD dwNew;
	dwNew = SetFilePointer(pHandle, 0, 0, FILE_CURRENT/* SEEK_CUR */);
	if( dwNew == INVALID_SET_FILE_POINTER ){
		return -1;
	}
	return (jx9_int64)dwNew;
}
/* int (*xTrunc)(void *, jx9_int64) */
static int WinFile_Trunc(void *pUserData, jx9_int64 nOfft)
{
	HANDLE pHandle = (HANDLE)pUserData;
	LONG HighOfft;
	DWORD dwNew;
	BOOL rc;
	HighOfft = (LONG)(nOfft >> 32);
	dwNew = SetFilePointer(pHandle, (LONG)nOfft, &HighOfft, FILE_BEGIN);
	if( dwNew == INVALID_SET_FILE_POINTER ){
		return -1;
	}
	rc = SetEndOfFile(pHandle);
	return rc ? JX9_OK : -1;
}
/* int (*xSync)(void *); */
static int WinFile_Sync(void *pUserData)
{
	HANDLE pHandle = (HANDLE)pUserData;
	BOOL rc;
	rc = FlushFileBuffers(pHandle);
	return rc ? JX9_OK : - 1;
}
/* int (*xStat)(void *, jx9_value *, jx9_value *) */
static int WinFile_Stat(void *pUserData, jx9_value *pArray, jx9_value *pWorker)
{
	BY_HANDLE_FILE_INFORMATION sInfo;
	HANDLE pHandle = (HANDLE)pUserData;
	BOOL rc;
	rc = GetFileInformationByHandle(pHandle, &sInfo);
	if( !rc ){
		return -1;
	}
	/* dev */
	jx9_value_int64(pWorker, (jx9_int64)sInfo.dwVolumeSerialNumber);
	jx9_array_add_strkey_elem(pArray, "dev", pWorker); /* Will make it's own copy */
	/* ino */
	jx9_value_int64(pWorker, (jx9_int64)(((jx9_int64)sInfo.nFileIndexHigh << 32) | sInfo.nFileIndexLow));
	jx9_array_add_strkey_elem(pArray, "ino", pWorker); /* Will make it's own copy */
	/* mode */
	jx9_value_int(pWorker, 0);
	jx9_array_add_strkey_elem(pArray, "mode", pWorker);
	/* nlink */
	jx9_value_int(pWorker, (int)sInfo.nNumberOfLinks);
	jx9_array_add_strkey_elem(pArray, "nlink", pWorker); /* Will make it's own copy */
	/* uid, gid, rdev */
	jx9_value_int(pWorker, 0);
	jx9_array_add_strkey_elem(pArray, "uid", pWorker);
	jx9_array_add_strkey_elem(pArray, "gid", pWorker);
	jx9_array_add_strkey_elem(pArray, "rdev", pWorker);
	/* size */
	jx9_value_int64(pWorker, (jx9_int64)(((jx9_int64)sInfo.nFileSizeHigh << 32) | sInfo.nFileSizeLow));
	jx9_array_add_strkey_elem(pArray, "size", pWorker); /* Will make it's own copy */
	/* atime */
	jx9_value_int64(pWorker, convertWindowsTimeToUnixTime(&sInfo.ftLastAccessTime));
	jx9_array_add_strkey_elem(pArray, "atime", pWorker); /* Will make it's own copy */
	/* mtime */
	jx9_value_int64(pWorker, convertWindowsTimeToUnixTime(&sInfo.ftLastWriteTime));
	jx9_array_add_strkey_elem(pArray, "mtime", pWorker); /* Will make it's own copy */
	/* ctime */
	jx9_value_int64(pWorker, convertWindowsTimeToUnixTime(&sInfo.ftCreationTime));
	jx9_array_add_strkey_elem(pArray, "ctime", pWorker); /* Will make it's own copy */
	/* blksize, blocks */
	jx9_value_int(pWorker, 0);		
	jx9_array_add_strkey_elem(pArray, "blksize", pWorker);
	jx9_array_add_strkey_elem(pArray, "blocks", pWorker);
	return JX9_OK;
}
/* Export the file:// stream */
static const jx9_io_stream sWinFileStream = {
	"file", /* Stream name */
	JX9_IO_STREAM_VERSION, 
	WinFile_Open,  /* xOpen */
	WinDir_Open,   /* xOpenDir */
	WinFile_Close, /* xClose */
	WinDir_Close,  /* xCloseDir */
	WinFile_Read,  /* xRead */
	WinDir_Read,   /* xReadDir */
	WinFile_Write, /* xWrite */
	WinFile_Seek,  /* xSeek */
	WinFile_Lock,  /* xLock */
	WinDir_RewindDir, /* xRewindDir */
	WinFile_Tell,  /* xTell */
	WinFile_Trunc, /* xTrunc */
	WinFile_Sync,  /* xSeek */
	WinFile_Stat   /* xStat */
};
#elif defined(__UNIXES__)
/*
 * UNIX VFS implementation for the JX9 engine.
 * Authors:
 *    Symisc Systems, devel@symisc.net.
 *    Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *    Stable.
 */
#include <sys/types.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <utime.h>
#include <stdio.h>
#include <stdlib.h>
/* int (*xchdir)(const char *) */
static int UnixVfs_chdir(const char *zPath)
{
  int rc;
  rc = chdir(zPath);
  return rc == 0 ? JX9_OK : -1;
}
/* int (*xGetcwd)(jx9_context *) */
static int UnixVfs_getcwd(jx9_context *pCtx)
{
	char zBuf[4096];
	char *zDir;
	/* Get the current directory */
	zDir = getcwd(zBuf, sizeof(zBuf));
	if( zDir == 0 ){
	  return -1;
    }
	jx9_result_string(pCtx, zDir, -1/*Compute length automatically*/);
	return JX9_OK;
}
/* int (*xMkdir)(const char *, int, int) */
static int UnixVfs_mkdir(const char *zPath, int mode, int recursive)
{
	int rc;
        rc = mkdir(zPath, mode);
	recursive = 0; /* cc warning */
	return rc == 0 ? JX9_OK : -1;
}
/* int (*xRmdir)(const char *) */
static int UnixVfs_rmdir(const char *zPath)
{
	int rc;
	rc = rmdir(zPath);
	return rc == 0 ? JX9_OK : -1;
}
/* int (*xIsdir)(const char *) */
static int UnixVfs_isdir(const char *zPath)
{
	struct stat st;
	int rc;
	rc = stat(zPath, &st);
	if( rc != 0 ){
	 return -1;
	}
	rc = S_ISDIR(st.st_mode);
	return rc ? JX9_OK : -1 ;
}
/* int (*xRename)(const char *, const char *) */
static int UnixVfs_Rename(const char *zOld, const char *zNew)
{
	int rc;
	rc = rename(zOld, zNew);
	return rc == 0 ? JX9_OK : -1;
}
/* int (*xRealpath)(const char *, jx9_context *) */
static int UnixVfs_Realpath(const char *zPath, jx9_context *pCtx)
{
#ifndef JX9_UNIX_OLD_LIBC
	char *zReal;
	zReal = realpath(zPath, 0);
	if( zReal == 0 ){
	  return -1;
	}
	jx9_result_string(pCtx, zReal, -1/*Compute length automatically*/);
        /* Release the allocated buffer */
	free(zReal);
	return JX9_OK;
#else
    zPath = 0; /* cc warning */
    pCtx = 0;
    return -1;
#endif
}
/* int (*xSleep)(unsigned int) */
static int UnixVfs_Sleep(unsigned int uSec)
{
	usleep(uSec);
	return JX9_OK;
}
/* int (*xUnlink)(const char *) */
static int UnixVfs_unlink(const char *zPath)
{
	int rc;
	rc = unlink(zPath);
	return rc == 0 ? JX9_OK : -1 ;
}
/* int (*xFileExists)(const char *) */
static int UnixVfs_FileExists(const char *zPath)
{
	int rc;
	rc = access(zPath, F_OK);
	return rc == 0 ? JX9_OK : -1;
}
/* jx9_int64 (*xFileSize)(const char *) */
static jx9_int64 UnixVfs_FileSize(const char *zPath)
{
	struct stat st;
	int rc;
	rc = stat(zPath, &st);
	if( rc != 0 ){ 
	 return -1;
	}
	return (jx9_int64)st.st_size;
}
/* int (*xTouch)(const char *, jx9_int64, jx9_int64) */
static int UnixVfs_Touch(const char *zPath, jx9_int64 touch_time, jx9_int64 access_time)
{
	struct utimbuf ut;
	int rc;
	ut.actime  = (time_t)access_time;
	ut.modtime = (time_t)touch_time;
	rc = utime(zPath, &ut);
	if( rc != 0 ){
	 return -1;
	}
	return JX9_OK;
}
/* jx9_int64 (*xFileAtime)(const char *) */
static jx9_int64 UnixVfs_FileAtime(const char *zPath)
{
	struct stat st;
	int rc;
	rc = stat(zPath, &st);
	if( rc != 0 ){ 
	 return -1;
	}
	return (jx9_int64)st.st_atime;
}
/* jx9_int64 (*xFileMtime)(const char *) */
static jx9_int64 UnixVfs_FileMtime(const char *zPath)
{
	struct stat st;
	int rc;
	rc = stat(zPath, &st);
	if( rc != 0 ){ 
	 return -1;
	}
	return (jx9_int64)st.st_mtime;
}
/* jx9_int64 (*xFileCtime)(const char *) */
static jx9_int64 UnixVfs_FileCtime(const char *zPath)
{
	struct stat st;
	int rc;
	rc = stat(zPath, &st);
	if( rc != 0 ){ 
	 return -1;
	}
	return (jx9_int64)st.st_ctime;
}
/* int (*xStat)(const char *, jx9_value *, jx9_value *) */
static int UnixVfs_Stat(const char *zPath, jx9_value *pArray, jx9_value *pWorker)
{
	struct stat st;
	int rc;
	rc = stat(zPath, &st);
	if( rc != 0 ){ 
	 return -1;
	}
	/* dev */
	jx9_value_int64(pWorker, (jx9_int64)st.st_dev);
	jx9_array_add_strkey_elem(pArray, "dev", pWorker); /* Will make it's own copy */
	/* ino */
	jx9_value_int64(pWorker, (jx9_int64)st.st_ino);
	jx9_array_add_strkey_elem(pArray, "ino", pWorker); /* Will make it's own copy */
	/* mode */
	jx9_value_int(pWorker, (int)st.st_mode);
	jx9_array_add_strkey_elem(pArray, "mode", pWorker);
	/* nlink */
	jx9_value_int(pWorker, (int)st.st_nlink);
	jx9_array_add_strkey_elem(pArray, "nlink", pWorker); /* Will make it's own copy */
	/* uid, gid, rdev */
	jx9_value_int(pWorker, (int)st.st_uid);
	jx9_array_add_strkey_elem(pArray, "uid", pWorker);
	jx9_value_int(pWorker, (int)st.st_gid);
	jx9_array_add_strkey_elem(pArray, "gid", pWorker);
	jx9_value_int(pWorker, (int)st.st_rdev);
	jx9_array_add_strkey_elem(pArray, "rdev", pWorker);
	/* size */
	jx9_value_int64(pWorker, (jx9_int64)st.st_size);
	jx9_array_add_strkey_elem(pArray, "size", pWorker); /* Will make it's own copy */
	/* atime */
	jx9_value_int64(pWorker, (jx9_int64)st.st_atime);
	jx9_array_add_strkey_elem(pArray, "atime", pWorker); /* Will make it's own copy */
	/* mtime */
	jx9_value_int64(pWorker, (jx9_int64)st.st_mtime);
	jx9_array_add_strkey_elem(pArray, "mtime", pWorker); /* Will make it's own copy */
	/* ctime */
	jx9_value_int64(pWorker, (jx9_int64)st.st_ctime);
	jx9_array_add_strkey_elem(pArray, "ctime", pWorker); /* Will make it's own copy */
	/* blksize, blocks */
	jx9_value_int(pWorker, (int)st.st_blksize);		
	jx9_array_add_strkey_elem(pArray, "blksize", pWorker);
	jx9_value_int(pWorker, (int)st.st_blocks);
	jx9_array_add_strkey_elem(pArray, "blocks", pWorker);
	return JX9_OK;
}
/* int (*xlStat)(const char *, jx9_value *, jx9_value *) */
static int UnixVfs_lStat(const char *zPath, jx9_value *pArray, jx9_value *pWorker)
{
	struct stat st;
	int rc;
	rc = lstat(zPath, &st);
	if( rc != 0 ){ 
	 return -1;
	}
	/* dev */
	jx9_value_int64(pWorker, (jx9_int64)st.st_dev);
	jx9_array_add_strkey_elem(pArray, "dev", pWorker); /* Will make it's own copy */
	/* ino */
	jx9_value_int64(pWorker, (jx9_int64)st.st_ino);
	jx9_array_add_strkey_elem(pArray, "ino", pWorker); /* Will make it's own copy */
	/* mode */
	jx9_value_int(pWorker, (int)st.st_mode);
	jx9_array_add_strkey_elem(pArray, "mode", pWorker);
	/* nlink */
	jx9_value_int(pWorker, (int)st.st_nlink);
	jx9_array_add_strkey_elem(pArray, "nlink", pWorker); /* Will make it's own copy */
	/* uid, gid, rdev */
	jx9_value_int(pWorker, (int)st.st_uid);
	jx9_array_add_strkey_elem(pArray, "uid", pWorker);
	jx9_value_int(pWorker, (int)st.st_gid);
	jx9_array_add_strkey_elem(pArray, "gid", pWorker);
	jx9_value_int(pWorker, (int)st.st_rdev);
	jx9_array_add_strkey_elem(pArray, "rdev", pWorker);
	/* size */
	jx9_value_int64(pWorker, (jx9_int64)st.st_size);
	jx9_array_add_strkey_elem(pArray, "size", pWorker); /* Will make it's own copy */
	/* atime */
	jx9_value_int64(pWorker, (jx9_int64)st.st_atime);
	jx9_array_add_strkey_elem(pArray, "atime", pWorker); /* Will make it's own copy */
	/* mtime */
	jx9_value_int64(pWorker, (jx9_int64)st.st_mtime);
	jx9_array_add_strkey_elem(pArray, "mtime", pWorker); /* Will make it's own copy */
	/* ctime */
	jx9_value_int64(pWorker, (jx9_int64)st.st_ctime);
	jx9_array_add_strkey_elem(pArray, "ctime", pWorker); /* Will make it's own copy */
	/* blksize, blocks */
	jx9_value_int(pWorker, (int)st.st_blksize);		
	jx9_array_add_strkey_elem(pArray, "blksize", pWorker);
	jx9_value_int(pWorker, (int)st.st_blocks);
	jx9_array_add_strkey_elem(pArray, "blocks", pWorker);
	return JX9_OK;
}
/* int (*xChmod)(const char *, int) */
static int UnixVfs_Chmod(const char *zPath, int mode)
{
    int rc;
    rc = chmod(zPath, (mode_t)mode);
    return rc == 0 ? JX9_OK : - 1;
}
/* int (*xChown)(const char *, const char *) */
static int UnixVfs_Chown(const char *zPath, const char *zUser)
{
#ifndef JX9_UNIX_STATIC_BUILD
  struct passwd *pwd;
  uid_t uid;
  int rc;
  pwd = getpwnam(zUser);   /* Try getting UID for username */
  if (pwd == 0) {
    return -1;
  }
  uid = pwd->pw_uid;
  rc = chown(zPath, uid, -1);
  return rc == 0 ? JX9_OK : -1;
#else
	SXUNUSED(zPath);
	SXUNUSED(zUser);
	return -1;
#endif /* JX9_UNIX_STATIC_BUILD */
}
/* int (*xChgrp)(const char *, const char *) */
static int UnixVfs_Chgrp(const char *zPath, const char *zGroup)
{
#ifndef JX9_UNIX_STATIC_BUILD
  struct group *group;
  gid_t gid;
  int rc;
  group = getgrnam(zGroup);
  if (group == 0) {
    return -1;
  }
  gid = group->gr_gid;
  rc = chown(zPath, -1, gid);
  return rc == 0 ? JX9_OK : -1;
#else
	SXUNUSED(zPath);
	SXUNUSED(zGroup);
	return -1;
#endif /* JX9_UNIX_STATIC_BUILD */
}
/* int (*xIsfile)(const char *) */
static int UnixVfs_isfile(const char *zPath)
{
	struct stat st;
	int rc;
	rc = stat(zPath, &st);
	if( rc != 0 ){
	 return -1;
	}
	rc = S_ISREG(st.st_mode);
	return rc ? JX9_OK : -1 ;
}
/* int (*xIslink)(const char *) */
static int UnixVfs_islink(const char *zPath)
{
	struct stat st;
	int rc;
	rc = stat(zPath, &st);
	if( rc != 0 ){
	 return -1;
	}
	rc = S_ISLNK(st.st_mode);
	return rc ? JX9_OK : -1 ;
}
/* int (*xReadable)(const char *) */
static int UnixVfs_isreadable(const char *zPath)
{
	int rc;
	rc = access(zPath, R_OK);
	return rc == 0 ? JX9_OK : -1;
}
/* int (*xWritable)(const char *) */
static int UnixVfs_iswritable(const char *zPath)
{
	int rc;
	rc = access(zPath, W_OK);
	return rc == 0 ? JX9_OK : -1;
}
/* int (*xExecutable)(const char *) */
static int UnixVfs_isexecutable(const char *zPath)
{
	int rc;
	rc = access(zPath, X_OK);
	return rc == 0 ? JX9_OK : -1;
}
/* int (*xFiletype)(const char *, jx9_context *) */
static int UnixVfs_Filetype(const char *zPath, jx9_context *pCtx)
{
	struct stat st;
	int rc;
    rc = stat(zPath, &st);
	if( rc != 0 ){
	  /* Expand 'unknown' */
	  jx9_result_string(pCtx, "unknown", sizeof("unknown")-1);
	  return -1;
	}
	if(S_ISREG(st.st_mode) ){
		jx9_result_string(pCtx, "file", sizeof("file")-1);
	}else if(S_ISDIR(st.st_mode)){
		jx9_result_string(pCtx, "dir", sizeof("dir")-1);
	}else if(S_ISLNK(st.st_mode)){
		jx9_result_string(pCtx, "link", sizeof("link")-1);
	}else if(S_ISBLK(st.st_mode)){
		jx9_result_string(pCtx, "block", sizeof("block")-1);
    }else if(S_ISSOCK(st.st_mode)){
		jx9_result_string(pCtx, "socket", sizeof("socket")-1);
	}else if(S_ISFIFO(st.st_mode)){
       jx9_result_string(pCtx, "fifo", sizeof("fifo")-1);
	}else{
		jx9_result_string(pCtx, "unknown", sizeof("unknown")-1);
	}
	return JX9_OK;
}
/* int (*xGetenv)(const char *, jx9_context *) */
static int UnixVfs_Getenv(const char *zVar, jx9_context *pCtx)
{
	char *zEnv;
	zEnv = getenv(zVar);
	if( zEnv == 0 ){
	  return -1;
	}
	jx9_result_string(pCtx, zEnv, -1/*Compute length automatically*/);
	return JX9_OK;
}
/* int (*xSetenv)(const char *, const char *) */
static int UnixVfs_Setenv(const char *zName, const char *zValue)
{
   int rc;
   rc = setenv(zName, zValue, 1);
   return rc == 0 ? JX9_OK : -1;
}
/* int (*xMmap)(const char *, void **, jx9_int64 *) */
static int UnixVfs_Mmap(const char *zPath, void **ppMap, jx9_int64 *pSize)
{
	struct stat st;
	void *pMap;
	int fd;
	int rc;
	/* Open the file in a read-only mode */
	fd = open(zPath, O_RDONLY);
	if( fd < 0 ){
		return -1;
	}
	/* stat the handle */
	fstat(fd, &st);
	/* Obtain a memory view of the whole file */
	pMap = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE|MAP_FILE, fd, 0);
	rc = JX9_OK;
	if( pMap == MAP_FAILED ){
		rc = -1;
	}else{
		/* Point to the memory view */
		*ppMap = pMap;
		*pSize = (jx9_int64)st.st_size;
	}
	close(fd);
	return rc;
}
/* void (*xUnmap)(void *, jx9_int64)  */
static void UnixVfs_Unmap(void *pView, jx9_int64 nSize)
{
	munmap(pView, (size_t)nSize);
}
/* void (*xTempDir)(jx9_context *) */
static void UnixVfs_TempDir(jx9_context *pCtx)
{
	static const char *azDirs[] = {
     "/var/tmp", 
     "/usr/tmp", 
	 "/usr/local/tmp"
  };
  unsigned int i;
  struct stat buf;
  const char *zDir;
  zDir = getenv("TMPDIR");
  if( zDir && zDir[0] != 0 && !access(zDir, 07) ){
	  jx9_result_string(pCtx, zDir, -1);
	  return;
  }
  for(i=0; i<sizeof(azDirs)/sizeof(azDirs[0]); i++){
	zDir=azDirs[i];
    if( zDir==0 ) continue;
    if( stat(zDir, &buf) ) continue;
    if( !S_ISDIR(buf.st_mode) ) continue;
    if( access(zDir, 07) ) continue;
    /* Got one */
	jx9_result_string(pCtx, zDir, -1);
	return;
  }
  /* Default temp dir */
  jx9_result_string(pCtx, "/tmp", (int)sizeof("/tmp")-1);
}
/* unsigned int (*xProcessId)(void) */
static unsigned int UnixVfs_ProcessId(void)
{
	return (unsigned int)getpid();
}
/* int (*xUid)(void) */
static int UnixVfs_uid(void)
{
	return (int)getuid();
}
/* int (*xGid)(void) */
static int UnixVfs_gid(void)
{
	return (int)getgid();
}
/* int (*xUmask)(int) */
static int UnixVfs_Umask(int new_mask)
{
	int old_mask;
	old_mask = umask(new_mask);
	return old_mask;
}
/* void (*xUsername)(jx9_context *) */
static void UnixVfs_Username(jx9_context *pCtx)
{
#ifndef JX9_UNIX_STATIC_BUILD
  struct passwd *pwd;
  uid_t uid;
  uid = getuid();
  pwd = getpwuid(uid);   /* Try getting UID for username */
  if (pwd == 0) {
    return;
  }
  /* Return the username */
  jx9_result_string(pCtx, pwd->pw_name, -1);
#else
  jx9_result_string(pCtx, "Unknown", -1);
#endif /* JX9_UNIX_STATIC_BUILD */
  return;
}
/* int (*xLink)(const char *, const char *, int) */
static int UnixVfs_link(const char *zSrc, const char *zTarget, int is_sym)
{
	int rc;
	if( is_sym ){
		/* Symbolic link */
		rc = symlink(zSrc, zTarget);
	}else{
		/* Hard link */
		rc = link(zSrc, zTarget);
	}
	return rc == 0 ? JX9_OK : -1;
}
/* int (*xChroot)(const char *) */
static int UnixVfs_chroot(const char *zRootDir)
{
	int rc;
	rc = chroot(zRootDir);
	return rc == 0 ? JX9_OK : -1;
}
/* Export the UNIX vfs */
static const jx9_vfs sUnixVfs = {
	"Unix_vfs", 
	JX9_VFS_VERSION, 
	UnixVfs_chdir,    /* int (*xChdir)(const char *) */
	UnixVfs_chroot,   /* int (*xChroot)(const char *); */
	UnixVfs_getcwd,   /* int (*xGetcwd)(jx9_context *) */
	UnixVfs_mkdir,    /* int (*xMkdir)(const char *, int, int) */
	UnixVfs_rmdir,    /* int (*xRmdir)(const char *) */ 
	UnixVfs_isdir,    /* int (*xIsdir)(const char *) */
	UnixVfs_Rename,   /* int (*xRename)(const char *, const char *) */
	UnixVfs_Realpath, /*int (*xRealpath)(const char *, jx9_context *)*/
	UnixVfs_Sleep,    /* int (*xSleep)(unsigned int) */
	UnixVfs_unlink,   /* int (*xUnlink)(const char *) */
	UnixVfs_FileExists, /* int (*xFileExists)(const char *) */
	UnixVfs_Chmod, /*int (*xChmod)(const char *, int)*/
	UnixVfs_Chown, /*int (*xChown)(const char *, const char *)*/
	UnixVfs_Chgrp, /*int (*xChgrp)(const char *, const char *)*/
	0,             /* jx9_int64 (*xFreeSpace)(const char *) */
	0,             /* jx9_int64 (*xTotalSpace)(const char *) */
	UnixVfs_FileSize, /* jx9_int64 (*xFileSize)(const char *) */
	UnixVfs_FileAtime, /* jx9_int64 (*xFileAtime)(const char *) */
	UnixVfs_FileMtime, /* jx9_int64 (*xFileMtime)(const char *) */
	UnixVfs_FileCtime, /* jx9_int64 (*xFileCtime)(const char *) */
	UnixVfs_Stat,  /* int (*xStat)(const char *, jx9_value *, jx9_value *) */
	UnixVfs_lStat, /* int (*xlStat)(const char *, jx9_value *, jx9_value *) */
	UnixVfs_isfile,     /* int (*xIsfile)(const char *) */
	UnixVfs_islink,     /* int (*xIslink)(const char *) */
	UnixVfs_isreadable, /* int (*xReadable)(const char *) */
	UnixVfs_iswritable, /* int (*xWritable)(const char *) */
	UnixVfs_isexecutable, /* int (*xExecutable)(const char *) */
	UnixVfs_Filetype,   /* int (*xFiletype)(const char *, jx9_context *) */
	UnixVfs_Getenv,     /* int (*xGetenv)(const char *, jx9_context *) */
	UnixVfs_Setenv,     /* int (*xSetenv)(const char *, const char *) */ 
	UnixVfs_Touch,      /* int (*xTouch)(const char *, jx9_int64, jx9_int64) */
	UnixVfs_Mmap,       /* int (*xMmap)(const char *, void **, jx9_int64 *) */
	UnixVfs_Unmap,      /* void (*xUnmap)(void *, jx9_int64);  */
	UnixVfs_link,       /* int (*xLink)(const char *, const char *, int) */
	UnixVfs_Umask,      /* int (*xUmask)(int) */
	UnixVfs_TempDir,    /* void (*xTempDir)(jx9_context *) */
	UnixVfs_ProcessId,  /* unsigned int (*xProcessId)(void) */
	UnixVfs_uid, /* int (*xUid)(void) */
	UnixVfs_gid, /* int (*xGid)(void) */
	UnixVfs_Username,    /* void (*xUsername)(jx9_context *) */
	0 /* int (*xExec)(const char *, jx9_context *) */
};
/* UNIX File IO */
#define JX9_UNIX_OPEN_MODE	0640 /* Default open mode */
/* int (*xOpen)(const char *, int, jx9_value *, void **) */
static int UnixFile_Open(const char *zPath, int iOpenMode, jx9_value *pResource, void **ppHandle)
{
	int iOpen = O_RDONLY;
	int fd;
	/* Set the desired flags according to the open mode */
	if( iOpenMode & JX9_IO_OPEN_CREATE ){
		/* Open existing file, or create if it doesn't exist */
		iOpen = O_CREAT;
		if( iOpenMode & JX9_IO_OPEN_TRUNC ){
			/* If the specified file exists and is writable, the function overwrites the file */
			iOpen |= O_TRUNC;
			SXUNUSED(pResource); /* cc warning */
		}
	}else if( iOpenMode & JX9_IO_OPEN_EXCL ){
		/* Creates a new file, only if it does not already exist.
		* If the file exists, it fails.
		*/
		iOpen = O_CREAT|O_EXCL;
	}else if( iOpenMode & JX9_IO_OPEN_TRUNC ){
		/* Opens a file and truncates it so that its size is zero bytes
		 * The file must exist.
		 */
		iOpen = O_RDWR|O_TRUNC;
	}
	if( iOpenMode & JX9_IO_OPEN_RDWR ){
		/* Read+Write access */
		iOpen &= ~O_RDONLY;
		iOpen |= O_RDWR;
	}else if( iOpenMode & JX9_IO_OPEN_WRONLY ){
		/* Write only access */
		iOpen &= ~O_RDONLY;
		iOpen |= O_WRONLY;
	}
	if( iOpenMode & JX9_IO_OPEN_APPEND ){
		/* Append mode */
		iOpen |= O_APPEND;
	}
#ifdef O_TEMP
	if( iOpenMode & JX9_IO_OPEN_TEMP ){
		/* File is temporary */
		iOpen |= O_TEMP;
	}
#endif
	/* Open the file now */
	fd = open(zPath, iOpen, JX9_UNIX_OPEN_MODE);
	if( fd < 0 ){
		/* IO error */
		return -1;
	}
	/* Save the handle */
	*ppHandle = SX_INT_TO_PTR(fd);
	return JX9_OK;
}
/* int (*xOpenDir)(const char *, jx9_value *, void **) */
static int UnixDir_Open(const char *zPath, jx9_value *pResource, void **ppHandle)
{
	DIR *pDir;
	/* Open the target directory */
	pDir = opendir(zPath);
	if( pDir == 0 ){
		pResource = 0; /* Compiler warning */
		return -1;
	}
	/* Save our structure */
	*ppHandle = pDir;
	return JX9_OK;
}
/* void (*xCloseDir)(void *) */
static void UnixDir_Close(void *pUserData)
{
	closedir((DIR *)pUserData);
}
/* void (*xClose)(void *); */
static void UnixFile_Close(void *pUserData)
{
	close(SX_PTR_TO_INT(pUserData));
}
/* int (*xReadDir)(void *, jx9_context *) */
static int UnixDir_Read(void *pUserData, jx9_context *pCtx)
{
	DIR *pDir = (DIR *)pUserData;
	struct dirent *pEntry;
	char *zName = 0; /* cc warning */
	sxu32 n = 0;
	for(;;){
		pEntry = readdir(pDir);
		if( pEntry == 0 ){
			/* No more entries to process */
			return -1;
		}
		zName = pEntry->d_name; 
		n = SyStrlen(zName);
		/* Ignore '.' && '..' */
		if( n > sizeof("..")-1 || zName[0] != '.' || ( n == sizeof("..")-1 && zName[1] != '.') ){
			break;
		}
		/* Next entry */
	}
	/* Return the current file name */
	jx9_result_string(pCtx, zName, (int)n);
	return JX9_OK;
}
/* void (*xRewindDir)(void *) */
static void UnixDir_Rewind(void *pUserData)
{
	rewinddir((DIR *)pUserData);
}
/* jx9_int64 (*xRead)(void *, void *, jx9_int64); */
static jx9_int64 UnixFile_Read(void *pUserData, void *pBuffer, jx9_int64 nDatatoRead)
{
	ssize_t nRd;
	nRd = read(SX_PTR_TO_INT(pUserData), pBuffer, (size_t)nDatatoRead);
	if( nRd < 1 ){
		/* EOF or IO error */
		return -1;
	}
	return (jx9_int64)nRd;
}
/* jx9_int64 (*xWrite)(void *, const void *, jx9_int64); */
static jx9_int64 UnixFile_Write(void *pUserData, const void *pBuffer, jx9_int64 nWrite)
{
	const char *zData = (const char *)pBuffer;
	int fd = SX_PTR_TO_INT(pUserData);
	jx9_int64 nCount;
	ssize_t nWr;
	nCount = 0;
	for(;;){
		if( nWrite < 1 ){
			break;
		}
		nWr = write(fd, zData, (size_t)nWrite);
		if( nWr < 1 ){
			/* IO error */
			break;
		}
		nWrite -= nWr;
		nCount += nWr;
		zData += nWr;
	}
	if( nWrite > 0 ){
		return -1;
	}
	return nCount;
}
/* int (*xSeek)(void *, jx9_int64, int) */
static int UnixFile_Seek(void *pUserData, jx9_int64 iOfft, int whence)
{
	off_t iNew;
	switch(whence){
	case 1:/*SEEK_CUR*/
		whence = SEEK_CUR;
		break;
	case 2: /* SEEK_END */
		whence = SEEK_END;
		break;
	case 0: /* SEEK_SET */
	default:
		whence = SEEK_SET;
		break;
	}
	iNew = lseek(SX_PTR_TO_INT(pUserData), (off_t)iOfft, whence);
	if( iNew < 0 ){
		return -1;
	}
	return JX9_OK;
}
/* int (*xLock)(void *, int) */
static int UnixFile_Lock(void *pUserData, int lock_type)
{
	int fd = SX_PTR_TO_INT(pUserData);
	int rc = JX9_OK; /* cc warning */
	if( lock_type < 0 ){
		/* Unlock the file */
		rc = flock(fd, LOCK_UN);
	}else{
		if( lock_type == 1 ){
			/* Exculsive lock */
			rc = flock(fd, LOCK_EX);
		}else{
			/* Shared lock */
			rc = flock(fd, LOCK_SH);
		}
	}
	return !rc ? JX9_OK : -1;
}
/* jx9_int64 (*xTell)(void *) */
static jx9_int64 UnixFile_Tell(void *pUserData)
{
	off_t iNew;
	iNew = lseek(SX_PTR_TO_INT(pUserData), 0, SEEK_CUR);
	return (jx9_int64)iNew;
}
/* int (*xTrunc)(void *, jx9_int64) */
static int UnixFile_Trunc(void *pUserData, jx9_int64 nOfft)
{
	int rc;
	rc = ftruncate(SX_PTR_TO_INT(pUserData), (off_t)nOfft);
	if( rc != 0 ){
		return -1;
	}
	return JX9_OK;
}
/* int (*xSync)(void *); */
static int UnixFile_Sync(void *pUserData)
{
	int rc;	
	rc = fsync(SX_PTR_TO_INT(pUserData));
	return rc == 0 ? JX9_OK : - 1;
}
/* int (*xStat)(void *, jx9_value *, jx9_value *) */
static int UnixFile_Stat(void *pUserData, jx9_value *pArray, jx9_value *pWorker)
{
	struct stat st;
	int rc;
	rc = fstat(SX_PTR_TO_INT(pUserData), &st);
	if( rc != 0 ){ 
	 return -1;
	}
	/* dev */
	jx9_value_int64(pWorker, (jx9_int64)st.st_dev);
	jx9_array_add_strkey_elem(pArray, "dev", pWorker); /* Will make it's own copy */
	/* ino */
	jx9_value_int64(pWorker, (jx9_int64)st.st_ino);
	jx9_array_add_strkey_elem(pArray, "ino", pWorker); /* Will make it's own copy */
	/* mode */
	jx9_value_int(pWorker, (int)st.st_mode);
	jx9_array_add_strkey_elem(pArray, "mode", pWorker);
	/* nlink */
	jx9_value_int(pWorker, (int)st.st_nlink);
	jx9_array_add_strkey_elem(pArray, "nlink", pWorker); /* Will make it's own copy */
	/* uid, gid, rdev */
	jx9_value_int(pWorker, (int)st.st_uid);
	jx9_array_add_strkey_elem(pArray, "uid", pWorker);
	jx9_value_int(pWorker, (int)st.st_gid);
	jx9_array_add_strkey_elem(pArray, "gid", pWorker);
	jx9_value_int(pWorker, (int)st.st_rdev);
	jx9_array_add_strkey_elem(pArray, "rdev", pWorker);
	/* size */
	jx9_value_int64(pWorker, (jx9_int64)st.st_size);
	jx9_array_add_strkey_elem(pArray, "size", pWorker); /* Will make it's own copy */
	/* atime */
	jx9_value_int64(pWorker, (jx9_int64)st.st_atime);
	jx9_array_add_strkey_elem(pArray, "atime", pWorker); /* Will make it's own copy */
	/* mtime */
	jx9_value_int64(pWorker, (jx9_int64)st.st_mtime);
	jx9_array_add_strkey_elem(pArray, "mtime", pWorker); /* Will make it's own copy */
	/* ctime */
	jx9_value_int64(pWorker, (jx9_int64)st.st_ctime);
	jx9_array_add_strkey_elem(pArray, "ctime", pWorker); /* Will make it's own copy */
	/* blksize, blocks */
	jx9_value_int(pWorker, (int)st.st_blksize);		
	jx9_array_add_strkey_elem(pArray, "blksize", pWorker);
	jx9_value_int(pWorker, (int)st.st_blocks);
	jx9_array_add_strkey_elem(pArray, "blocks", pWorker);
	return JX9_OK;
}
/* Export the file:// stream */
static const jx9_io_stream sUnixFileStream = {
	"file", /* Stream name */
	JX9_IO_STREAM_VERSION, 
	UnixFile_Open,  /* xOpen */
	UnixDir_Open,   /* xOpenDir */
	UnixFile_Close, /* xClose */
	UnixDir_Close,  /* xCloseDir */
	UnixFile_Read,  /* xRead */
	UnixDir_Read,   /* xReadDir */
	UnixFile_Write, /* xWrite */
	UnixFile_Seek,  /* xSeek */
	UnixFile_Lock,  /* xLock */
	UnixDir_Rewind, /* xRewindDir */
	UnixFile_Tell,  /* xTell */
	UnixFile_Trunc, /* xTrunc */
	UnixFile_Sync,  /* xSeek */
	UnixFile_Stat   /* xStat */
};
#endif /* __WINNT__/__UNIXES__ */
#endif /* JX9_DISABLE_DISK_IO */
#endif /* JX9_DISABLE_BUILTIN_FUNC */
/* 
 * Export the builtin vfs.
 * Return a pointer to the builtin vfs if available.
 * Otherwise return the null_vfs [i.e: a no-op vfs] instead.
 * Note:
 *  The built-in vfs is always available for Windows/UNIX systems.
 * Note:
 *  If the engine is compiled with the JX9_DISABLE_DISK_IO/JX9_DISABLE_BUILTIN_FUNC
 *  directives defined then this function return the null_vfs instead.
 */
JX9_PRIVATE const jx9_vfs * jx9ExportBuiltinVfs(void)
{
#ifndef JX9_DISABLE_BUILTIN_FUNC
#ifdef JX9_DISABLE_DISK_IO
	return &null_vfs;
#else
#ifdef __WINNT__
	return &sWinVfs;
#elif defined(__UNIXES__)
	return &sUnixVfs;
#else
	return &null_vfs;
#endif /* __WINNT__/__UNIXES__ */
#endif /*JX9_DISABLE_DISK_IO*/
#else
	return &null_vfs;
#endif /* JX9_DISABLE_BUILTIN_FUNC */
}
#ifndef JX9_DISABLE_BUILTIN_FUNC
#ifndef JX9_DISABLE_DISK_IO
/*
 * The following defines are mostly used by the UNIX built and have
 * no particular meaning on windows.
 */
#ifndef STDIN_FILENO
#define STDIN_FILENO	0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO	1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO	2
#endif
/*
 * jx9:// Accessing various I/O streams
 * According to the JX9 langage reference manual
 * JX9 provides a number of miscellaneous I/O streams that allow access to JX9's own input
 * and output streams, the standard input, output and error file descriptors.
 * jx9://stdin, jx9://stdout and jx9://stderr:
 *  Allow direct access to the corresponding input or output stream of the JX9 process.
 *  The stream references a duplicate file descriptor, so if you open jx9://stdin and later
 *  close it, you close only your copy of the descriptor-the actual stream referenced by STDIN is unaffected.
 *  jx9://stdin is read-only, whereas jx9://stdout and jx9://stderr are write-only.
 * jx9://output
 *  jx9://output is a write-only stream that allows you to write to the output buffer 
 *  mechanism in the same way as print and print. 
 */
typedef struct jx9_stream_data jx9_stream_data;
/* Supported IO streams */
#define JX9_IO_STREAM_STDIN  1 /* jx9://stdin */
#define JX9_IO_STREAM_STDOUT 2 /* jx9://stdout */
#define JX9_IO_STREAM_STDERR 3 /* jx9://stderr */
#define JX9_IO_STREAM_OUTPUT 4 /* jx9://output */
 /* The following structure is the private data associated with the jx9:// stream */
struct jx9_stream_data
{
	jx9_vm *pVm; /* VM that own this instance */
	int iType;   /* Stream type */
	union{
		void *pHandle; /* Stream handle */
		jx9_output_consumer sConsumer; /* VM output consumer */
	}x;
};
/*
 * Allocate a new instance of the jx9_stream_data structure.
 */
static jx9_stream_data * JX9StreamDataInit(jx9_vm *pVm, int iType)
{
	jx9_stream_data *pData;
	if( pVm == 0 ){
		return 0;
	}
	/* Allocate a new instance */
	pData = (jx9_stream_data *)SyMemBackendAlloc(&pVm->sAllocator, sizeof(jx9_stream_data));
	if( pData == 0 ){
		return 0;
	}
	/* Zero the structure */
	SyZero(pData, sizeof(jx9_stream_data));
	/* Initialize fields */
	pData->iType = iType;
	if( iType == JX9_IO_STREAM_OUTPUT ){
		/* Point to the default VM consumer routine. */
		pData->x.sConsumer = pVm->sVmConsumer; 
	}else{
#ifdef __WINNT__
		DWORD nChannel;
		switch(iType){
		case JX9_IO_STREAM_STDOUT:	nChannel = STD_OUTPUT_HANDLE; break;
		case JX9_IO_STREAM_STDERR:  nChannel = STD_ERROR_HANDLE; break;
		default:
			nChannel = STD_INPUT_HANDLE; 
			break;
		}
		pData->x.pHandle = GetStdHandle(nChannel);
#else
		/* Assume an UNIX system */
		int ifd = STDIN_FILENO;
		switch(iType){
		case JX9_IO_STREAM_STDOUT:  ifd = STDOUT_FILENO; break;
		case JX9_IO_STREAM_STDERR:  ifd = STDERR_FILENO; break;
		default:
			break;
		}
		pData->x.pHandle = SX_INT_TO_PTR(ifd);
#endif
	}
	pData->pVm = pVm;
	return pData;
}
/* 
 * Implementation of the jx9:// IO streams routines
 * Authors:
 *  Symisc Systems, devel@symisc.net.
 *  Copyright (C) Symisc Systems, http://jx9.symisc.net
 * Status:
 *   Stable.
 */
/* int (*xOpen)(const char *, int, jx9_value *, void **) */
static int JX9StreamData_Open(const char *zName, int iMode, jx9_value *pResource, void ** ppHandle)
{
	jx9_stream_data *pData;
	SyString sStream;
	SyStringInitFromBuf(&sStream, zName, SyStrlen(zName));
	/* Trim leading and trailing white spaces */
	SyStringFullTrim(&sStream);
	/* Stream to open */
	if( SyStrnicmp(sStream.zString, "stdin", sizeof("stdin")-1) == 0 ){
		iMode = JX9_IO_STREAM_STDIN;
	}else if( SyStrnicmp(sStream.zString, "output", sizeof("output")-1) == 0 ){
		iMode = JX9_IO_STREAM_OUTPUT;
	}else if( SyStrnicmp(sStream.zString, "stdout", sizeof("stdout")-1) == 0 ){
		iMode = JX9_IO_STREAM_STDOUT;
	}else if( SyStrnicmp(sStream.zString, "stderr", sizeof("stderr")-1) == 0 ){
		iMode = JX9_IO_STREAM_STDERR;
	}else{
		/* unknown stream name */
		return -1;
	}
	/* Create our handle */
	pData = JX9StreamDataInit(pResource?pResource->pVm:0, iMode);
	if( pData == 0 ){
		return -1;
	}
	/* Make the handle public */
	*ppHandle = (void *)pData;
	return JX9_OK;
}
/* jx9_int64 (*xRead)(void *, void *, jx9_int64) */
static jx9_int64 JX9StreamData_Read(void *pHandle, void *pBuffer, jx9_int64 nDatatoRead)
{
	jx9_stream_data *pData = (jx9_stream_data *)pHandle;
	if( pData == 0 ){
		return -1;
	}
	if( pData->iType != JX9_IO_STREAM_STDIN ){
		/* Forbidden */
		return -1;
	}
#ifdef __WINNT__
	{
		DWORD nRd;
		BOOL rc;
		rc = ReadFile(pData->x.pHandle, pBuffer, (DWORD)nDatatoRead, &nRd, 0);
		if( !rc ){
			/* IO error */
			return -1;
		}
		return (jx9_int64)nRd;
	}
#elif defined(__UNIXES__)
	{
		ssize_t nRd;
		int fd;
		fd = SX_PTR_TO_INT(pData->x.pHandle);
		nRd = read(fd, pBuffer, (size_t)nDatatoRead);
		if( nRd < 1 ){
			return -1;
		}
		return (jx9_int64)nRd;
	}
#else
	return -1;
#endif
}
/* jx9_int64 (*xWrite)(void *, const void *, jx9_int64) */
static jx9_int64 JX9StreamData_Write(void *pHandle, const void *pBuf, jx9_int64 nWrite)
{
	jx9_stream_data *pData = (jx9_stream_data *)pHandle;
	if( pData == 0 ){
		return -1;
	}
	if( pData->iType == JX9_IO_STREAM_STDIN ){
		/* Forbidden */
		return -1;
	}else if( pData->iType == JX9_IO_STREAM_OUTPUT ){
		jx9_output_consumer *pCons = &pData->x.sConsumer;
		int rc;
		/* Call the vm output consumer */
		rc = pCons->xConsumer(pBuf, (unsigned int)nWrite, pCons->pUserData);
		if( rc == JX9_ABORT ){
			return -1;
		}
		return nWrite;
	}
#ifdef __WINNT__
	{
		DWORD nWr;
		BOOL rc;
		rc = WriteFile(pData->x.pHandle, pBuf, (DWORD)nWrite, &nWr, 0);
		if( !rc ){
			/* IO error */
			return -1;
		}
		return (jx9_int64)nWr;
	}
#elif defined(__UNIXES__)
	{
		ssize_t nWr;
		int fd;
		fd = SX_PTR_TO_INT(pData->x.pHandle);
		nWr = write(fd, pBuf, (size_t)nWrite);
		if( nWr < 1 ){
			return -1;
		}
		return (jx9_int64)nWr;
	}
#else
	return -1;
#endif
}
/* void (*xClose)(void *) */
static void JX9StreamData_Close(void *pHandle)
{
	jx9_stream_data *pData = (jx9_stream_data *)pHandle;
	jx9_vm *pVm;
	if( pData == 0 ){
		return;
	}
	pVm = pData->pVm;
	/* Free the instance */
	SyMemBackendFree(&pVm->sAllocator, pData);
}
/* Export the jx9:// stream */
static const jx9_io_stream sjx9Stream = {
	"jx9", 
	JX9_IO_STREAM_VERSION, 
	JX9StreamData_Open,  /* xOpen */
	0,   /* xOpenDir */
	JX9StreamData_Close, /* xClose */
	0,  /* xCloseDir */
	JX9StreamData_Read,  /* xRead */
	0,  /* xReadDir */
	JX9StreamData_Write, /* xWrite */
	0,  /* xSeek */
	0,  /* xLock */
	0,  /* xRewindDir */
	0,  /* xTell */
	0,  /* xTrunc */
	0,  /* xSeek */
	0   /* xStat */
};
#endif /* JX9_DISABLE_DISK_IO */
/*
 * Return TRUE if we are dealing with the jx9:// stream.
 * FALSE otherwise.
 */
static int is_jx9_stream(const jx9_io_stream *pStream)
{
#ifndef JX9_DISABLE_DISK_IO
	return pStream == &sjx9Stream;
#else
	SXUNUSED(pStream); /* cc warning */
	return 0;
#endif /* JX9_DISABLE_DISK_IO */
}

#endif /* JX9_DISABLE_BUILTIN_FUNC */
/*
 * Export the IO routines defined above and the built-in IO streams
 * [i.e: file://, jx9://].
 * Note:
 *  If the engine is compiled with the JX9_DISABLE_BUILTIN_FUNC directive
 *  defined then this function is a no-op.
 */
JX9_PRIVATE sxi32 jx9RegisterIORoutine(jx9_vm *pVm)
{
#ifndef JX9_DISABLE_BUILTIN_FUNC
	      /* VFS functions */
	static const jx9_builtin_func aVfsFunc[] = {
		{"chdir",   jx9Vfs_chdir   }, 
		{"chroot",  jx9Vfs_chroot  }, 
		{"getcwd",  jx9Vfs_getcwd  }, 
		{"rmdir",   jx9Vfs_rmdir   }, 
		{"is_dir",  jx9Vfs_is_dir  }, 
		{"mkdir",   jx9Vfs_mkdir   }, 
		{"rename",  jx9Vfs_rename  }, 
		{"realpath", jx9Vfs_realpath}, 
		{"sleep",   jx9Vfs_sleep   }, 
		{"usleep",  jx9Vfs_usleep  }, 
		{"unlink",  jx9Vfs_unlink  }, 
		{"delete",  jx9Vfs_unlink  }, 
		{"chmod",   jx9Vfs_chmod   }, 
		{"chown",   jx9Vfs_chown   }, 
		{"chgrp",   jx9Vfs_chgrp   }, 
		{"disk_free_space", jx9Vfs_disk_free_space  }, 
		{"disk_total_space", jx9Vfs_disk_total_space}, 
		{"file_exists", jx9Vfs_file_exists }, 
		{"filesize",    jx9Vfs_file_size   }, 
		{"fileatime",   jx9Vfs_file_atime  }, 
		{"filemtime",   jx9Vfs_file_mtime  }, 
		{"filectime",   jx9Vfs_file_ctime  }, 
		{"is_file",     jx9Vfs_is_file  }, 
		{"is_link",     jx9Vfs_is_link  }, 
		{"is_readable", jx9Vfs_is_readable   }, 
		{"is_writable", jx9Vfs_is_writable   }, 
		{"is_executable", jx9Vfs_is_executable}, 
		{"filetype",    jx9Vfs_filetype }, 
		{"stat",        jx9Vfs_stat     }, 
		{"lstat",       jx9Vfs_lstat    }, 
		{"getenv",      jx9Vfs_getenv   }, 
		{"setenv",      jx9Vfs_putenv   }, 
		{"putenv",      jx9Vfs_putenv   }, 
		{"touch",       jx9Vfs_touch    }, 
		{"link",        jx9Vfs_link     }, 
		{"symlink",     jx9Vfs_symlink  }, 
		{"umask",       jx9Vfs_umask    }, 
		{"sys_get_temp_dir", jx9Vfs_sys_get_temp_dir }, 
		{"get_current_user", jx9Vfs_get_current_user }, 
		{"getpid",      jx9Vfs_getmypid }, 
		{"getuid",      jx9Vfs_getmyuid }, 
		{"getgid",      jx9Vfs_getmygid }, 
		{"uname",       jx9Vfs_uname}, 
		     /* Path processing */ 
		{"dirname",     jx9Builtin_dirname  }, 
		{"basename",    jx9Builtin_basename }, 
		{"pathinfo",    jx9Builtin_pathinfo }, 
		{"strglob",     jx9Builtin_strglob  }, 
		{"fnmatch",     jx9Builtin_fnmatch  }, 
		     /* ZIP processing */
		{"zip_open",    jx9Builtin_zip_open }, 
		{"zip_close",   jx9Builtin_zip_close}, 
		{"zip_read",    jx9Builtin_zip_read }, 
		{"zip_entry_open", jx9Builtin_zip_entry_open }, 
		{"zip_entry_close", jx9Builtin_zip_entry_close}, 
		{"zip_entry_name", jx9Builtin_zip_entry_name }, 
		{"zip_entry_filesize",      jx9Builtin_zip_entry_filesize       }, 
		{"zip_entry_compressedsize", jx9Builtin_zip_entry_compressedsize }, 
		{"zip_entry_read", jx9Builtin_zip_entry_read }, 
		{"zip_entry_reset_cursor", jx9Builtin_zip_entry_reset_cursor}, 
		{"zip_entry_compressionmethod", jx9Builtin_zip_entry_compressionmethod}
	};
	    /* IO stream functions */
	static const jx9_builtin_func aIOFunc[] = {
		{"ftruncate", jx9Builtin_ftruncate }, 
		{"fseek",     jx9Builtin_fseek  }, 
		{"ftell",     jx9Builtin_ftell  }, 
		{"rewind",    jx9Builtin_rewind }, 
		{"fflush",    jx9Builtin_fflush }, 
		{"feof",      jx9Builtin_feof   }, 
		{"fgetc",     jx9Builtin_fgetc  }, 
		{"fgets",     jx9Builtin_fgets  }, 
		{"fread",     jx9Builtin_fread  }, 
		{"fgetcsv",   jx9Builtin_fgetcsv}, 
		{"fgetss",    jx9Builtin_fgetss }, 
		{"readdir",   jx9Builtin_readdir}, 
		{"rewinddir", jx9Builtin_rewinddir }, 
		{"closedir",  jx9Builtin_closedir}, 
		{"opendir",   jx9Builtin_opendir }, 
		{"readfile",  jx9Builtin_readfile}, 
		{"file_get_contents", jx9Builtin_file_get_contents}, 
		{"file_put_contents", jx9Builtin_file_put_contents}, 
		{"file",      jx9Builtin_file   }, 
		{"copy",      jx9Builtin_copy   }, 
		{"fstat",     jx9Builtin_fstat  }, 
		{"fwrite",    jx9Builtin_fwrite }, 
		{"fputs",     jx9Builtin_fwrite }, 
		{"flock",     jx9Builtin_flock  }, 
		{"fclose",    jx9Builtin_fclose }, 
		{"fopen",     jx9Builtin_fopen  }, 
		{"fpassthru", jx9Builtin_fpassthru }, 
		{"fputcsv",   jx9Builtin_fputcsv }, 
		{"fprintf",   jx9Builtin_fprintf }, 
#if !defined(JX9_DISABLE_HASH_FUNC)
		{"md5_file",  jx9Builtin_md5_file}, 
		{"sha1_file", jx9Builtin_sha1_file}, 
#endif /* JX9_DISABLE_HASH_FUNC */
		{"parse_ini_file", jx9Builtin_parse_ini_file}, 
		{"vfprintf",  jx9Builtin_vfprintf}
	};
	const jx9_io_stream *pFileStream = 0;
	sxu32 n = 0;
	/* Register the functions defined above */
	for( n = 0 ; n < SX_ARRAYSIZE(aVfsFunc) ; ++n ){
		jx9_create_function(&(*pVm), aVfsFunc[n].zName, aVfsFunc[n].xFunc, (void *)pVm->pEngine->pVfs);
	}
	for( n = 0 ; n < SX_ARRAYSIZE(aIOFunc) ; ++n ){
		jx9_create_function(&(*pVm), aIOFunc[n].zName, aIOFunc[n].xFunc, pVm);
	}
#ifndef JX9_DISABLE_DISK_IO
	/* Register the file stream if available */
#ifdef __WINNT__
	pFileStream = &sWinFileStream;
#elif defined(__UNIXES__)
	pFileStream = &sUnixFileStream;
#endif
	/* Install the jx9:// stream */
	jx9_vm_config(pVm, JX9_VM_CONFIG_IO_STREAM, &sjx9Stream);
#endif /* JX9_DISABLE_DISK_IO */
	if( pFileStream ){
		/* Install the file:// stream */
		jx9_vm_config(pVm, JX9_VM_CONFIG_IO_STREAM, pFileStream);
	}
#else
   SXUNUSED(pVm); /* cc warning */
#endif /* JX9_DISABLE_BUILTIN_FUNC */
	return SXRET_OK;
}
/*
 * Export the STDIN handle.
 */
JX9_PRIVATE void * jx9ExportStdin(jx9_vm *pVm)
{
#ifndef JX9_DISABLE_BUILTIN_FUNC
#ifndef JX9_DISABLE_DISK_IO
	if( pVm->pStdin == 0  ){
		io_private *pIn;
		/* Allocate an IO private instance */
		pIn = (io_private *)SyMemBackendAlloc(&pVm->sAllocator, sizeof(io_private));
		if( pIn == 0 ){
			return 0;
		}
		InitIOPrivate(pVm, &sjx9Stream, pIn);
		/* Initialize the handle */
		pIn->pHandle = JX9StreamDataInit(pVm, JX9_IO_STREAM_STDIN);
		/* Install the STDIN stream */
		pVm->pStdin = pIn;
		return pIn;
	}else{
		/* NULL or STDIN */
		return pVm->pStdin;
	}
#else
	return 0;
#endif
#else
	SXUNUSED(pVm); /* cc warning */
	return 0;
#endif
}
/*
 * Export the STDOUT handle.
 */
JX9_PRIVATE void * jx9ExportStdout(jx9_vm *pVm)
{
#ifndef JX9_DISABLE_BUILTIN_FUNC
#ifndef JX9_DISABLE_DISK_IO
	if( pVm->pStdout == 0  ){
		io_private *pOut;
		/* Allocate an IO private instance */
		pOut = (io_private *)SyMemBackendAlloc(&pVm->sAllocator, sizeof(io_private));
		if( pOut == 0 ){
			return 0;
		}
		InitIOPrivate(pVm, &sjx9Stream, pOut);
		/* Initialize the handle */
		pOut->pHandle = JX9StreamDataInit(pVm, JX9_IO_STREAM_STDOUT);
		/* Install the STDOUT stream */
		pVm->pStdout = pOut;
		return pOut;
	}else{
		/* NULL or STDOUT */
		return pVm->pStdout;
	}
#else
	return 0;
#endif
#else
	SXUNUSED(pVm); /* cc warning */
	return 0;
#endif
}
/*
 * Export the STDERR handle.
 */
JX9_PRIVATE void * jx9ExportStderr(jx9_vm *pVm)
{
#ifndef JX9_DISABLE_BUILTIN_FUNC
#ifndef JX9_DISABLE_DISK_IO
	if( pVm->pStderr == 0  ){
		io_private *pErr;
		/* Allocate an IO private instance */
		pErr = (io_private *)SyMemBackendAlloc(&pVm->sAllocator, sizeof(io_private));
		if( pErr == 0 ){
			return 0;
		}
		InitIOPrivate(pVm, &sjx9Stream, pErr);
		/* Initialize the handle */
		pErr->pHandle = JX9StreamDataInit(pVm, JX9_IO_STREAM_STDERR);
		/* Install the STDERR stream */
		pVm->pStderr = pErr;
		return pErr;
	}else{
		/* NULL or STDERR */
		return pVm->pStderr;
	}
#else
	return 0;
#endif
#else
	SXUNUSED(pVm); /* cc warning */
	return 0;
#endif
}
