/*
 * Compile this file together with the UnQLite database engine source code
 * to generate the executable. For example: 
 *  gcc -W -Wall -O6 unqlite_hostapp_info.c unqlite.c -o unqlite_host
*/
/*
 * This simple program is a quick introduction on how to embed and start
 * experimenting with UnQLite without having to do a lot of tedious
 * reading and configuration.
 *
 * Sharing data between the host application and the underlying Jx9 script
 * via the document-store interface to UnQLite:
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
 *        http://unqlite.org/unqlite.html
 * For the full C/C++ API reference guide, please refer to:
 *        http://unqlite.org/c_api.html
 * UnQLite in 5 Minutes or Less:
 *        http://unqlite.org/intro.html
 * The Architecture of the UnQLite Database Engine:
 *        http://unqlite.org/arch.html
 */
/* $SymiscID: unqlite_hostapp_info.c v1.0 Win7 2013-05-17 22:37 stable <chm@symisc.net> $ */
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
	"UnQLite Document-Store (Via Jx9) Data Share Intro           \n"
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
/*
 * The following walker callback is made available to the [unqlite_array_walk()] interface 
 * which is used to iterate over the JSON object extracted from the running script.
 * (See below for more information).
 */
static int JsonObjectWalker(unqlite_value *pKey,unqlite_value *pData,void *pUserData /* Unused */)
{
	const char *zKey,*zData;
	/* Extract the key and the data field */
	zKey = unqlite_value_to_string(pKey,0);
	zData = unqlite_value_to_string(pData,0);
	/* Dump */
	printf(
		"%s ===> %s\n",
		zKey,
		zData
	);
	return UNQLITE_OK;
}
/* Forward declaration: VM output consumer callback */
static int VmOutputConsumer(const void *pOutput,unsigned int nOutLen,void *pUserData /* Unused */);
/*
 * The following is the Jx9 Program to be executed later by the UnQLite VM:
 *
 * This program demonstrate how data is shared between the host application
 * and the running JX9 script. The main() function defined below creates and install
 * two foreign variables named respectively $my_app and $my_data. The first is a simple
 * scalar value while the last is a complex JSON object. these foreign variables are
 * made available to the running script using the [unqlite_vm_config()] interface with
 * a configuration verb set to UNQLITE_VM_CONFIG_CREATE_VAR.
 * 
 * Jx9 Program:
 *
 * print "Showing foreign variables contents\n";
 * //Scalar foreign variable named $my_app
 * print "\$my_app =",$my_app..JX9_EOL;
 * //Foreign JSON object named $my_data
 * print "\$my_data = ",$my_data;
 * //Dump command line arguments
 * if( count($argv) > 0 ){
 *  print "\nCommand line arguments:\n";
 *  print $argv;
 * }else{
 *  print "\nEmpty command line";
 * }
 * //Return a simple JSON object to the host application
 * $my_config = {
 *        "unqlite_signature" : db_sig(), //UnQLite Unique signature
 *        "time" : __TIME__, //Current time
 *        "date" : __DATE__  //Current date
 * };
 */
#define JX9_PROG \
 "print \"Showing foreign variables contents\n\n\";"\
 " /*Scalar foreign variable named $my_app*/"\
 " print \"\\$my_app = \",$my_app..JX9_EOL;"\
 " /*JSON object foreign variable named $my_data*/"\
 " print \"\n\\$my_data = \",$my_data..JX9_EOL;"\
 " /*Dump command line arguments*/"\
 " if( count($argv) > 0 ){"\
 "  print \"\nCommand line arguments:\n\";"\
 "  print $argv..JX9_EOL;"\
 " }else{"\
 "  print \"\nEmpty command line\";"\
 " }"\
 " /*Return a simple JSON object to the host application*/"\
 " $my_config = {"\
 "        'unqlite_signature' : db_sig(),  /* UnQLite Unique version*/"\
 "        'time' : __TIME__, /*Current time*/"\
 "        'date' : __DATE__  /*Current date*/"\
 " };"
 
int main(int argc,char *argv[])
{
	unqlite_value *pScalar,*pObject; /* Foreign Jx9 variable to be installed later */
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
	
	/* 
	 * Create a simple scalar variable.
	 */
	pScalar = unqlite_vm_new_scalar(pVm);
	if( pScalar == 0 ){
		Fatal(0,"Cannot create foreign variable $my_app");
	}
	/* Populate the variable with the desired information */
	unqlite_value_string(pScalar,"My Host Application/1.2.5",-1/*Compule length automatically*/); /* Dummy signature*/
	/*
	 * Install the variable ($my_app).
	 */
	rc = unqlite_vm_config(
		  pVm,
		  UNQLITE_VM_CONFIG_CREATE_VAR, /* Create variable command */
		  "my_app", /* Variable name (without the dollar sign) */
		  pScalar   /* Value */
		);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Error while installing $my_app");
	}
	/* To access this foreign variable from the running script, simply invoke it
	 * as follows:
	 *  print $my_app;
	 * or
	 *  dump($my_app);
	 */

	/*
	 * Now, it's time to create and install a more complex variable which is a JSON
	 * object named $my_data.
	 * The JSON Object looks like this:
	 *  {
	 *     "path" : "/usr/local/etc",
	 *     "port" : 8082,
	 *     "fork" : true
	 *  };
	 */
	pObject = unqlite_vm_new_array(pVm); /* Unified interface for JSON Objects and Arrays */
	/* Populate the object with the fields defined above.
	*/
	unqlite_value_reset_string_cursor(pScalar);
	
	/* Add the "path" : "/usr/local/etc" entry */
	unqlite_value_string(pScalar,"/usr/local/etc",-1);
	unqlite_array_add_strkey_elem(pObject,"path",pScalar); /* Will make it's own copy of pScalar */
	
	/* Add the "port" : 8080 entry */
	unqlite_value_int(pScalar,8080);
	unqlite_array_add_strkey_elem(pObject,"port",pScalar); /* Will make it's own copy of pScalar */
	
	/* Add the "fork": true entry */
	unqlite_value_bool(pScalar,1 /* TRUE */);
	unqlite_array_add_strkey_elem(pObject,"fork",pScalar); /* Will make it's own copy of pScalar */

	/* Now, install the variable and associate the JSON object with it */
	rc = unqlite_vm_config(
		  pVm,
		  UNQLITE_VM_CONFIG_CREATE_VAR, /* Create variable command */
		  "my_data", /* Variable name (without the dollar sign) */
		  pObject    /*value */
		);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Error while installing $my_data");
	}

	/* Release the two values */
	unqlite_vm_release_value(pVm,pScalar);
	unqlite_vm_release_value(pVm,pObject);

	/* Execute our script */
	unqlite_vm_exec(pVm);
	
	/* Extract the content of the variable named $my_config defined in the 
	 * running script which hold a simple JSON object. 
	 */
	pObject = unqlite_vm_extract_variable(pVm,"my_config");
	if( pObject && unqlite_value_is_json_object(pObject) ){
		/* Iterate over object fields */
		printf("\n\nTotal fields in $my_config = %u\n",unqlite_array_count(pObject));
		unqlite_array_walk(pObject,JsonObjectWalker,0);
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
