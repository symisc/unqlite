/*
 * Compile this file together with the UnQLite database engine source code
 * to generate the executable. For example: 
 *  gcc -W -Wall -O6 unqlite_const_intro.c unqlite.c -o unqlite_jx9_const
*/
/*
 * This simple program is a quick introduction on how to embed and start
 * experimenting with UnQLite without having to do a lot of tedious
 * reading and configuration.
 *
 * Introduction to the UnQLite (Via Jx9) Constant Expansion Mechanism:
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
 * The constant expansion mechanism under Jx9 is extremely powerful yet simple and work
 * as follows:
 * Each registered constant have a C procedure associated with it. This procedure known
 * as the constant expansion callback is responsible of expanding the invoked constant
 * to the desired value, for example:
 * The C procedure associated with the "__PI__" constant expands to 3.14 (the value of PI).
 * the "__OS__" constant procedure expands to the name of the host Operating
 * Systems (Windows, Linux, ...), the "__TIME__" constant expands to the current system time
 * and so forth.
 * 
 * For an introductory course to the constant expansion meachanism, you can refer
 * to the following tutorial:
 *        http://unqlite.org/const_intro.html
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
/* $SymiscID: unqlite_const_intro.c v1.5 Unix 2013-05-17 00:17 stable <chm@symisc.net> $ */
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
	"UnQLite (Via Jx9) Constant Expansion Mechanism              \n"
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
 * __PI__: expand the value of PI (3.14...)
 * Our first constant is the __PI__ constant. The following procedure
 * is the callback associated with the __PI__ identifier. That is, when
 * the __PI__ identifier is seen in the running script, this procedure
 * gets called by the underlying Jx9 virtual machine.
 * This procedure is responsible of expanding the constant identifier to
 * the desired value (3.14 in our case).
 */
static void PI_Constant(
	unqlite_value *pValue, /* Store expanded value here */
	void *pUserData    /* User private data (unused in our case) */
	){
		/* Expand the value of PI */
		unqlite_value_double(pValue, 3.1415926535898);
}
/*
 * __TIME__: expand the current local time.
 * Our second constant is the __TIME__ constant.
 * When the __TIME__ identifier is seen in the running script, this procedure
 * gets called by the underlying Jx9 virtual machine.
 * This procedure is responsible of expanding the constant identifier to
 * the desired value (local time in our case).
 */
#include <time.h>
static void TIME_Constant(unqlite_value *pValue, void *pUserData /* Unused */)
{
	struct tm *pLocal;
	time_t tt;
	/* Get the current local time */
	time(&tt);
	pLocal = localtime(&tt);
	/* Expand the current time now */
	unqlite_value_string_format(pValue, "%02d:%02d:%02d", 
		pLocal->tm_hour, 
		pLocal->tm_min, 
		pLocal->tm_sec
		);
}
/*
 * __OS__: expand the name of the Host Operating System.
 * Our last constant is the __OS__ constant.
 * When the __OS__ identifier is seen in the running script, this procedure
 * gets called by the underlying Jx9 virtual machine.
 * This procedure is responsible of expanding the constant identifier to
 * the desired value (OS name in our case).
 */
static void OS_Constant(unqlite_value *pValue, void *pUserData /* Unused */ )
{
#ifdef __WINNT__
	unqlite_value_string(pValue, "Windows", -1 /*Compute input length automatically */);
#else
	/* Assume UNIX */
	unqlite_value_string(pValue, "UNIX", -1 /*Compute input length automatically */);
#endif /* __WINNT__ */
}
/* Forward declaration: VM output consumer callback */
static int VmOutputConsumer(const void *pOutput,unsigned int nOutLen,void *pUserData /* Unused */);
/*
 * The following is the Jx9 Program to be executed later by the UnQLite VM:
 * 
 * //Test the constant expansion mechanism
 *    print '__PI__   value: ' .. __PI__ .. JX9_EOL;
 *    print '__TIME__ value: ' .. __TIME__  .. JX9_EOL;
 *    print '__OS__   value: ' .. __OS__ .. JX9_EOL;
 * When running, you should see something like that:
 *	__PI__   value: 3.1415926535898
 *  __TIME__ value: 15:02:27
 *  __OS__   value: UNIX
 * 
 */
#define JX9_PROG \
 "print '__PI__   value: ' .. __PI__ ..   JX9_EOL;"\
 "print '__TIME__ value: ' .. __TIME__ .. JX9_EOL;"\
 "print '__OS__   value: ' .. __OS__ ..   JX9_EOL;"

 /* No need for command line arguments, everything is stored in-memory */
int main(void)
{
	unqlite *pDb;       /* Database handle */
	unqlite_vm *pVm;    /* UnQLite VM resulting from successful compilation of the target Jx9 script */
	int rc;

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

	/* Now we have our program compiled, it's time to register our constants
	 * and their associated C procedure.
	 */
	rc = unqlite_create_constant(pVm, "__PI__", PI_Constant, 0);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Error while installing the __PI__ constant");
	}
	
	rc = unqlite_create_constant(pVm, "__TIME__", TIME_Constant, 0);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Error while installing the __TIME__ constant");
	}
	
	rc = unqlite_create_constant(pVm, "__OS__", OS_Constant, 0);
	if( rc != UNQLITE_OK ){
		Fatal(0,"Error while installing the __OS__ constant");
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
