/*
 * Compile this file together with the UnQLite database engine source code
 * to generate the executable. For example: 
 *  gcc -W -Wall -O6 unqlite_mp3_tag.c unqlite.c -o unqlite_mp3
*/
/*
 * This simple program is a quick introduction on how to embed and start
 * experimenting with UnQLite without having to do a lot of tedious
 * reading and configuration.
 *
 * Introduction to Jx9 IO facility:
 *
 * The Document store to UnQLite which is used to store JSON docs (i.e. Objects, Arrays, Strings, etc.)
 * in the database is powered by the Jx9 programming language.
 *
 * Jx9 is an embeddable scripting language also called extension language designed
 * to support general procedural programming with data description facilities.
 * Jx9 is a Turing-Complete, dynamically typed programming language based on JSON
 * and implemented as a library in the UnQLite core.
 *
 * Jx9 is built with a tons of features and has a clean and familiar syntax similar
 * to C and Javascript.
 * Being an extension language, Jx9 has no notion of a main program, it only works
 * embedded in a host application.
 * The host program (UnQLite in our case) can write and read Jx9 variables and can
 * register C/C++ functions to be called by Jx9 code. 
 *
 * For an introduction to the UnQLite C/C++ interface, please refer to:
 *        http://unqlite.org/api_intro.html
 * For an introduction to Jx9, please refer to:
 *        http://unqlite.org/jx9.html
 * For the full C/C++ API reference guide, please refer to:
 *        http://unqlite.org/c_api.html
 * UnQLite in 5 Minutes or Less:
 *        http://unqlite.org/intro.html
 * The Architecture of the UnQLite Database Engine:
 *        http://unqlite.org/arch.html
 */
/* $SymiscID: unqlite_mp3_tag.c v1.0 Win7 2013-05-17 22:37 stable <chm@symisc.net> $ */
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
	"UnQLite Document-Store (Via Jx9) IO Intro (2)               \n"
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
/* Forward declaration: VM output consumer callback */
static int VmOutputConsumer(const void *pOutput,unsigned int nOutLen,void *pUserData /* Unused */);
/*
 * The following is the Jx9 Program to be executed later by the UnQLite VM:
 *
 * The program expects a command line
 * path from which all available MP3 are extrcated and their ID3v1 tags (if available)
 * are outputted.
 * To run this program, simply enter the executable name and the target directory as follows:
 *
 *  unqlite_mp3 /path/to/my/mp3s
 * 
 *  if( count($argv) < 1 ){
 *   die("Missing MP3s directory");
 * }
 * $dir = $argv[0];
 * if( !chdir($dir) ){
 *	 die("Error while changing directory");
 * }
 * //Collect MP3 files
 * $pFiles = glob("*.mp3");
 * if( is_array($pFiles) ){
 *  foreach($pFiles as $pEntry){
 *    print "\nMP3: $pEntry ",size_format(filesize($pEntry)),JX9_EOL;// Give a nice presentation
 *	 // Open the file in a read only mode
 *	 $pHandle = fopen($pEntry,'r');
 *	 if( $pHandle == FALSE ){
 *	   print "IO error while opening $pEntry\n";
 *	   continue; // Ignore 
 *	 }
 *	 // Seek 128 bytes from the end
 *	 fseek($pHandle,-128,SEEK_END);
 *	 // Read the 128 raw data
 *	 $zBuf = fread($pHandle,128);
 *	 if( !$zBuf || strlen($zBuf) != 128 ){
 *		print "$pEntry: Read error\n";
 *		// Ignore
 *		continue;
 *	 }
 *	 if( $zBuf[0] == 'T' && $zBuf[1] == 'A' && $zBuf[2] == 'G'  ){
 *	    $nOfft = 3;
 *		// Extract the title
 *		$zTitle = substr($zBuf,$nOfft,30);
 *		// Remove trailing and leading NUL bytes and white spaces
 *		$zTitle = trim($zTitle);
 *		if( strlen($zTitle) > 0 ){
 *				print "Title: $zTitle\n";
 *		}
 *		// Extract artist name
 *		$zArtist = substr($zBuf,$nOfft+30,30);
 *		// Remove trailing and leading NUL bytes and white spaces
 *		$zArtist = trim($zArtist);
 *		if( strlen($zArtist) > 0 ){
 *				print "Artist: $zArtist\n";
 *		}
 *		// Extract album name 
 *		$zAlbum = substr($zBuf,$nOfft+30+30,30);
 *		// Remove trailing and leading NUL bytes and white spaces
 *		$zAlbum = trim($zAlbum);
 *		if( strlen($zAlbum) > 0 ){
 *				print "Album: $zAlbum\n";
 *		}
 *		// Extract the Year 
 *		$zYear = substr($zBuf,$nOfft+30+30+30,4);
 *		// Remove trailing and leading NUL bytes and white spaces
 *		$zYear = trim($zYear);
 *		if( strlen($zYear) > 0 ){
 *				print "Year: $zYear\n";
 *		}
 *		// Next entry
 *		print "------------------------------------------------------\n";
 *	 }
 *	 // All done whith this file, close the handle
 *	 fclose($pHandle);
 *  }
 * }
 * 
 */
#define JX9_PROG \
 "if( count($argv) < 1 ){"\
  " EXIT('Missing MP3s directory');"\
  "}"\
  "$dir = $argv[0];"\
  "if( !chdir($dir) ){"\
  "	EXIT('Error while changing directory');"\
  "}"\
  "/* Collect MP3 files */"\
  "$pFiles = glob('*.mp3');"\
  "if( is_array($pFiles) ){"\
  "foreach($pFiles as $pEntry){"\
  "   print \"\nMP3: $pEntry \",size_format(filesize($pEntry)),JX9_EOL;/* Give a nice presentation */"\
  "	 /* Open the file in a read only mode */"\
  "	 $pHandle = fopen($pEntry,'r');"\
  "	 if( $pHandle == FALSE ){"\
  "	   print \"IO error while opening $pEntry\n\";"\
  "	   continue; /* Ignore */"\
  "	 }"\
  "	 /* Seek 128 bytes from the end */"\
  "	 fseek($pHandle,-128,SEEK_END);"\
  "	 /* Read the 128 raw data */"\
  "	 $zBuf = fread($pHandle,128);"\
  "	 if( !$zBuf || strlen($zBuf) != 128 ){"\
  "		print \"$pEntry: Read error\n\";"\
  "		/* Ignore */"\
  "		continue;"\
  "	 }"\
  "	 if( $zBuf[0] == 'T' && $zBuf[1] == 'A' && $zBuf[2] == 'G'  ){"\
  "	    $nOfft = 3 /* TAG */;"\
  "		/* Extract the title */"\
  "		$zTitle = substr($zBuf,$nOfft,30);"\
  "		/* Remove trailing and leading NUL bytes and white spaces */"\
  "		$zTitle = trim($zTitle);"\
  "		if( strlen($zTitle) > 0 ){"\
  "				print \"Title: $zTitle\n\";"\
  "		}"\
  "		/* Extract artist name */"\
  "		$zArtist = substr($zBuf,$nOfft+30,30);"\
  "		/* Remove trailing and leading NUL bytes and white spaces */"\
  "		$zArtist = trim($zArtist);"\
  "		if( strlen($zArtist) > 0 ){"\
  "				print \"Artist: $zArtist\n\";"\
  "		}"\
  "		/* Extract album name */"\
  "		$zAlbum = substr($zBuf,$nOfft+30+30,30);"\
  "		/* Remove trailing and leading NUL bytes and white spaces */"\
  "		$zAlbum = trim($zAlbum);"\
  "		if( strlen($zAlbum) > 0 ){"\
  "				print \"Album: $zAlbum\n\";"\
  "		}"\
  "		/* Extract the Year */"\
  "		$zYear = substr($zBuf,$nOfft+30+30+30,4);"\
  "		/* Remove trailing and leading NUL bytes and white spaces */"\
  "		$zYear = trim($zYear);"\
  "		if( strlen($zYear) > 0 ){"\
  "				print \"Year: $zYear\n\";"\
  "		}"\
  "		/* Next entry */"\
  "		print \"------------------------------------------------------\n\";"\
  "	 }"\
  "	 /* All done whith this file,close the handle */"\
  "	 fclose($pHandle);"\
  " }"\
  "}"


int main(int argc,char *argv[])
{
	unqlite *pDb;       /* Database handle */
	unqlite_vm *pVm;    /* UnQLite VM resulting from successful compilation of the target Jx9 script */
	int n,rc;

	puts(zBanner);

	/* Open our database */
	rc = unqlite_open(&pDb,":mem:" /* In-mem DB */,UNQLITE_OPEN_CREATE);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Out of memory");
	}
	
	/* Compile our Jx9 script defined above */
	rc = unqlite_compile(pDb,JX9_PROG,sizeof(JX9_PROG)-1,&pVm);
	if( rc != UNQLITE_OK ){
		/* Compile error, extract the compiler error log */
		const char *zBuf;
		int iLen;
		/* Extract error log */
		unqlite_config(pDb,UNQLITE_CONFIG_JX9_ERR_LOG,&zBuf,&iLen);
		if( iLen > 0 ){
			puts(zBuf);
		}
		Fatal(0,"Jx9 compile error");
	}

	/* Register script agruments so we can access them later using the $argv[]
	 * array from the compiled Jx9 program.
	 */
	for( n = 1; n < argc ; ++n ){
		unqlite_vm_config(pVm, UNQLITE_VM_CONFIG_ARGV_ENTRY, argv[n]/* Argument value */);
	}

	/* Install a VM output consumer callback */
	rc = unqlite_vm_config(pVm,UNQLITE_VM_CONFIG_OUTPUT,VmOutputConsumer,0);
	if( rc != UNQLITE_OK ){
		Fatal(pDb,0);
	}
	
	/* Execute our script */
	rc = unqlite_vm_exec(pVm);
	if( rc != UNQLITE_OK ){
		Fatal(pDb,0);
	}

	/* Release our VM */
	unqlite_vm_release(pVm);
	
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
 * VM output consumer callback.
 * Each time the UnQLite VM generates some outputs, the following
 * function gets called by the underlying virtual machine to consume
 * the generated output.
 *
 * All this function does is redirecting the VM output to STDOUT.
 * This function is registered via a call to [unqlite_vm_config()]
 * with a configuration verb set to: UNQLITE_VM_CONFIG_OUTPUT.
 */
static int VmOutputConsumer(const void *pOutput,unsigned int nOutLen,void *pUserData /* Unused */)
{
#ifdef __WINNT__
	BOOL rc;
	rc = WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),pOutput,(DWORD)nOutLen,0,0);
	if( !rc ){
		/* Abort processing */
		return UNQLITE_ABORT;
	}
#else
	ssize_t nWr;
	nWr = write(STDOUT_FILENO,pOutput,nOutLen);
	if( nWr < 0 ){
		/* Abort processing */
		return UNQLITE_ABORT;
	}
#endif /* __WINT__ */
	
	/* All done, data was redirected to STDOUT */
	return UNQLITE_OK;
}
