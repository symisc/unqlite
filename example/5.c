/*
 * Compile this file together with the UnQLite database engine source code
 * to generate the executable. For example: 
 *  gcc -W -Wall -O6 unqlite_func_intro.c unqlite.c -o unqlite_unqlite_func
*/
/*
 * This simple program is a quick introduction on how to embed and start
 * experimenting with UnQLite without having to do a lot of tedious
 * reading and configuration.
 *
 * Introduction to the UnQLite (Via Jx9) Foreign Function Mechanism:
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
 * Foreign functions are used to add Jx9 functions or to redefine the behavior of existing
 * Jx9 functions from the outside environment (see below) to the underlying virtual machine.
 * This mechanism is know as "In-process extending". After successful call to [unqlite_create_function()],
 * the installed function is available immediately and can be called from the target Jx9 code.
 * 
 * For an introductory course to the [unqlite_create_function()] interface and the foreign
 * function mechanism in general, please refer to:
 *        http://unqlite.org/func_intro.html
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
/* $SymiscID: unqlite_func_intro.c v1.7 Linux 2013-05-17 03:17 stable <chm@symisc.net> $ */
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
	"UnQLite (Via Jx9) Foreign Functions                         \n"
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
 * int shift_func(int $num)
 *  Right shift a number by one and return the result.
 * Description
 *  Our first function perform a simple right shift operation on a given number
 *  and return that number shifted by one.
 *  This function expects a single parameter which must be numeric (either integer or float
 *  or a string that looks like a number).
 * Parameter
 *  $num
 *   Number to shift by one.
 * Return value
 *   Integer: Given number shifted by one.
 * Usage example:
 *   print shift_func(150); //Output 300
 *   print shift_func(50);  //Output 100
 */
int shift_func(
	unqlite_context *pCtx, /* Call Context */
	int argc,          /* Total number of arguments passed to the function */
	unqlite_value **argv   /* Array of function arguments */
	)
{
	int num;
	/* Make sure there is at least one argument and is of the
	 * expected type [i.e: numeric].
	 */
	if( argc < 1 || !unqlite_value_is_numeric(argv[0]) ){
		/*
		 * Missing/Invalid argument, throw a warning and return FALSE.
		 * Note that you do not need to log the function name, UnQLite will
		 * automatically append the function name for you.
		 */
		unqlite_context_throw_error(pCtx, UNQLITE_CTX_WARNING, "Missing numeric argument");
		/* Return false */
		unqlite_result_bool(pCtx, 0);
		return UNQLITE_OK;
	}
	/* Extract the number */
	num = unqlite_value_to_int(argv[0]);
	/* Shift by 1 */
	num <<= 1;
	/* Return the new value */
	unqlite_result_int(pCtx, num);
	/* All done */
	return UNQLITE_OK;
}
#include <time.h>
/*
 * string date_func(void)
 *  Return the current system date.
 * Description
 *  Our second function does not expects arguments and return the
 *  current system date.
 * Parameter
 *  None
 * Return value
 *   String: Current system date.
 * Usage example
 *   print date_func(); //would output: 2012-23-09 14:53:30
 */
int date_func(
	unqlite_context *pCtx, /* Call Context */
	int argc,          /* Total number of arguments passed to the function */
	unqlite_value **argv   /* Array of function arguments*/
	){
		time_t tt;
		struct tm *pNow;
		/* Get the current time */
		time(&tt);
		pNow = localtime(&tt);
		/* 
		 * Return the current date.
		 */
		unqlite_result_string_format(pCtx, 
			"%04d-%02d-%02d %02d:%02d:%02d", /* printf() style format */
			pNow->tm_year + 1900, /* Year */
			pNow->tm_mday,        /* Day of the month */
			pNow->tm_mon + 1,     /* Month number */
			pNow->tm_hour, /* Hour */
			pNow->tm_min,  /* Minutes */
			pNow->tm_sec   /* Seconds */
			);
		/* All done */
		return UNQLITE_OK;
}
/*
 * int64 sum_func(int $arg1, int $arg2, int $arg3, ...)
 *  Return the sum of the given arguments.
 * Description
 *  This function expects a variable number of arguments which must be of type
 *  numeric (either integer or float or a string that looks like a number) and
 *  returns the sum of the given numbers.
 * Parameter 
 *   int $n1, n2, ... (Variable number of arguments)
 * Return value
 *   Integer64: Sum of the given numbers.
 * Usage example
 *   print sum_func(7, 8, 9, 10); //would output 34
 */
int sum_func(unqlite_context *pCtx, int argc, unqlite_value **argv)
{
	unqlite_int64 iTotal = 0; /* Counter */
	int i;
	if( argc < 1 ){
		/*
		 * Missing arguments, throw a notice and return NULL.
		 * Note that you do not need to log the function name, UnQLite will
		 * automatically append the function name for you.
		 */
		unqlite_context_throw_error(pCtx, UNQLITE_CTX_NOTICE, "Missing function arguments $arg1, $arg2, ..");
		/* Return null */
		unqlite_result_null(pCtx);
		return UNQLITE_OK;
	}
	/* Sum the arguments */
	for( i = 0; i < argc ; i++ ){
		unqlite_value *pVal = argv[i];
		unqlite_int64 n;
		/* Make sure we are dealing with a numeric argument */
		if( !unqlite_value_is_numeric(pVal) ){
			/* Throw a notice and continue */
			unqlite_context_throw_error_format(pCtx, UNQLITE_CTX_NOTICE, 
				"Arg[%d]: Expecting a numeric value", /* printf() style format */
				i
				);
			/* Ignore */
			continue;
		}
		/* Get a 64-bit integer representation and increment the counter */
		n = unqlite_value_to_int64(pVal);
		iTotal += n;
	}
	/* Return the count  */
	unqlite_result_int64(pCtx, iTotal);
	/* All done */
	return UNQLITE_OK;
}
/*
 * array array_time_func(void)
 *  Return the current system time in a JSON array.
 * Description
 *  This function does not expects arguments and return the
 *  current system time in an array.
 * Parameter
 *  None
 * Return value
 *   Array holding the current system time.
 * Usage example
 * 
 *   print array_time_func() ; 
 * 
 * When running you should see something like that:
 * JSON array(3) [14,53,30]
 */
int array_time_func(unqlite_context *pCtx, int argc, unqlite_value **argv)
{
	unqlite_value *pArray;    /* Our JSON Array */
	unqlite_value *pValue;    /* Array entries value */
	time_t tt;
	struct tm *pNow;
	/* Get the current time first */
	time(&tt);
	pNow = localtime(&tt);
	/* Create a new array */
	pArray = unqlite_context_new_array(pCtx);
	/* Create a worker scalar value */
	pValue = unqlite_context_new_scalar(pCtx);
	if( pArray == 0 || pValue == 0 ){
		/*
		 * If the supplied memory subsystem is so sick that we are unable
		 * to allocate a tiny chunk of memory, there is no much we can do here.
		 * Abort immediately.
		 */
		unqlite_context_throw_error(pCtx, UNQLITE_CTX_ERR, "Fatal, out of memory");
		/* emulate the die() construct */
		return UNQLITE_ABORT; 
	}
	/* Populate the array.
	 * Note that we will use the same worker scalar value (pValue) here rather than
	 * allocating a new value for each array entry. This is due to the fact
	 * that the populated array will make it's own private copy of the inserted
	 * key(if available) and it's associated value.
	 */
	
	unqlite_value_int(pValue, pNow->tm_hour); /* Hour */
	/* Insert the hour at the first available index */
	unqlite_array_add_elem(pArray, 0/* NULL: Assign an automatic index*/, pValue /* Will make it's own copy */);

	/* Overwrite the previous value */
	unqlite_value_int(pValue, pNow->tm_min); /* Minutes */
	/* Insert minutes */
	unqlite_array_add_elem(pArray, 0/* NULL: Assign an automatic index*/, pValue /* Will make it's own copy */);

	/* Overwrite the previous value */
	unqlite_value_int(pValue, pNow->tm_sec); /* Seconds */
	/* Insert seconds */
	unqlite_array_add_elem(pArray, 0/* NULL: Assign an automatic index*/, pValue /* Will make it's own copy */);

	/* Return the array as the function return value */
	unqlite_result_value(pCtx, pArray);

	/* All done. Don't worry about freeing memory here, every
	 * allocated resource will be released automatically by the engine
	 * as soon we return from this foreign function.
	 */
	return UNQLITE_OK;
}
/*
 * object object_date_func(void)
 *  Return a copy of the 'struct tm' structure in a JSON array.
 * Description
 *  This function does not expects arguments and return a copy of
 *  the 'struct tm' structure found in the 'time.h' header file.
 *  This structure hold the current system date&time.
 * Parameter
 *  None
 * Return value
 *   Associative array holding a copy of the 'struct tm' structure.
 * Usage example
 * 
 *   print object_date_func();
 * 
 * When running you should see something like that:
 * JSON Object(6 {
 *  "tm_year":2012,
 *  "tm_mon":12,
 *  "tm_mday":29,
 *  "tm_hour":1,
 *  "tm_min":13,
 *  "tm_sec":58
 *  }
 * )
 */
int object_date_func(unqlite_context *pCtx, int argc /* unused */, unqlite_value **argv /* unused */)
{
	unqlite_value *pObject;    /* Our JSON object */
	unqlite_value *pValue;    /* Objecr entries value */
	time_t tt;
	struct tm *pNow;
	/* Get the current time first */
	time(&tt);
	pNow = localtime(&tt);
	/* Create a new JSON object */
	pObject = unqlite_context_new_array(pCtx);
	/* Create a worker scalar value */
	pValue = unqlite_context_new_scalar(pCtx);
	if( pObject == 0 || pValue == 0 ){
		/*
		 * If the supplied memory subsystem is so sick that we are unable
		 * to allocate a tiny chunk of memory, there is no much we can do here.
		 * Abort immediately.
		 */
		unqlite_context_throw_error(pCtx, UNQLITE_CTX_ERR, "Fatal, out of memory");
		/* emulate the die() construct */
		return UNQLITE_ABORT;
	}
	/* Populate the array.
	 * Note that we will use the same worker scalar value (pValue) here rather than
	 * allocating a new value for each array entry. This is due to the fact
	 * that the populated array will make it's own private copy of the inserted
	 * key(if available) and it's associated value.
	 */
	
	unqlite_value_int(pValue, pNow->tm_year + 1900); /* Year */
	/* Insert Year */
	unqlite_array_add_strkey_elem(pObject, "tm_year", pValue /* Will make it's own copy */);

	/* Overwrite the previous value */
	unqlite_value_int(pValue, pNow->tm_mon + 1); /* Month [1-12]*/
	/* Insert month number */
	unqlite_array_add_strkey_elem(pObject, "tm_mon", pValue /* Will make it's own copy */);

	/* Overwrite the previous value */
	unqlite_value_int(pValue, pNow->tm_mday); /* Day of the month [1-31] */
	/* Insert the day of the month */
	unqlite_array_add_strkey_elem(pObject, "tm_mday", pValue /* Will make it's own copy */);

	unqlite_value_int(pValue, pNow->tm_hour); /* Hour */
	/* Insert the hour */
	unqlite_array_add_strkey_elem(pObject, "tm_hour", pValue /* Will make it's own copy */);

	/* Overwrite the previous value */
	unqlite_value_int(pValue, pNow->tm_min); /* Minutes */
	/* Insert minutes */
	unqlite_array_add_strkey_elem(pObject, "tm_min", pValue /* Will make it's own copy */);

	/* Overwrite the previous value */
	unqlite_value_int(pValue, pNow->tm_sec); /* Seconds */
	/* Insert seconds */
	unqlite_array_add_strkey_elem(pObject, "tm_sec", pValue /* Will make it's own copy */);

	/* Return the JSON object as the function return value */
	unqlite_result_value(pCtx, pObject);
	/* All done. Don't worry about freeing memory here, every
	 * allocated resource will be released automatically by the engine
	 * as soon we return from this foreign function.
	 */
	return UNQLITE_OK;
}
/*
 * array array_string_split(string $str)
 *  Return a copy of each string character in an array.
 * Description
 *  This function splits a given string to its
 *  characters and return the result in an array.
 * Parameter
 *  $str
 *     Target string to split.
 * Return value
 *   Array holding string characters.
 * Usage example
 * 
 *   print array_str_split('Hello'); 
 * 
 * When running you should see something like that:
 *   JSON Array(5 ["H","e","l","l","o"])
 */
int array_string_split_func(unqlite_context *pCtx, int argc, unqlite_value **argv)
{
	unqlite_value *pArray;    /* Our JSON Array */
	unqlite_value *pValue;    /* Array entries value */
	const char *zString, *zEnd;  /* String to split */
	int nByte;            /* String length */
	/* Make sure there is at least one argument and is of the
	 * expected type [i.e: string].
	 */
	if( argc < 1 || !unqlite_value_is_string(argv[0]) ){
		/*
		 * Missing/Invalid argument, throw a warning and return FALSE.
		 * Note that you do not need to log the function name, UnQLite will
		 * automatically append the function name for you.
		 */
		unqlite_context_throw_error(pCtx, UNQLITE_CTX_WARNING, "Missing string to split");
		/* Return false */
		unqlite_result_bool(pCtx, 0);
		return UNQLITE_OK;
	}
	/* Extract the target string.
	 * Note that zString is null terminated and unqlite_value_to_string() never
	 * fail and always return a pointer to a null terminated string.
	 */
	zString = unqlite_value_to_string(argv[0], &nByte /* String length */);
	if( nByte < 1 /* Empty string [i.e: '' or ""] */ ){
		unqlite_context_throw_error(pCtx, UNQLITE_CTX_NOTICE, "Empty string");
		/* Return false */
		unqlite_result_bool(pCtx, 0);
		return UNQLITE_OK;
	}
	/* Create our array */
	pArray = unqlite_context_new_array(pCtx);
	/* Create a scalar worker value */
	pValue = unqlite_context_new_scalar(pCtx);
	/* Split the target string */
	zEnd = &zString[nByte]; /* Delimit the string */
	while( zString < zEnd ){
		int c = zString[0];
		/* Prepare the character for insertion */
		unqlite_value_string(pValue, (const char *)&c, (int)sizeof(char));
		/* Insert the character */
		unqlite_array_add_elem(pArray, 0/* NULL: Assign an automatic index */, pValue /* Will make it's own copy*/);
		/* Erase the previous data from the worker variable */
		unqlite_value_reset_string_cursor(pValue);
		/* Next character */
		zString++;
	}
	/* Return our array as the function return value */
	unqlite_result_value(pCtx, pArray);
	/* All done. Don't worry about freeing memory here, every
	 * allocated resource will be released automatically by the engine
	 * as soon we return from this foreign function.
	 */
	return UNQLITE_OK;
}
/* 
 * Container for the foreign functions defined above.
 * These functions will be registered later using a call
 * to [unqlite_create_function()].
 */
static const struct foreign_func {
	const char *zName; /* Name of the foreign function*/
	int (*xProc)(unqlite_context *, int, unqlite_value **); /* Pointer to the C function performing the computation*/
}aFunc[] = {
	{"shift_func", shift_func}, 
	{"date_func", date_func}, 
	{"sum_func",  sum_func  }, 
	{"array_time_func", array_time_func}, 
	{"array_str_split", array_string_split_func}, 
	{"object_date_func", object_date_func}
};
/* Forward declaration: VM output consumer callback */
static int VmOutputConsumer(const void *pOutput,unsigned int nOutLen,void *pUserData /* Unused */);
/*
 * The following is the Jx9 Program to be executed later by the UnQLite VM:
 * 
 * //Test the foreign function mechanism
 *  print 'shift_func(150) = ' .. shift_func(150) .. JX9_EOL;
 *  print 'sum_func(7,8,9,10) = ' .. sum_func(7,8,9,10) .. JX9_EOL;
 *  print 'date_func(5) = ' .. date_func() .. JX9_EOL;
 *  print 'array_time_func() =' .. array_time_func() .. JX9_EOL;
 *  print 'object_date_func() =' ..  JX9_EOL;
 *  dump(object_date_func());
 *  print 'array_str_split('Hello') ='  .. JX9_EOL;
 *  dump(array_str_split('Hello'))
 *  
 * When running, you should see something like that:
 *	
 * shift_func(150) = 300
 * sum_func(7,8,9,10) = 34
 * date_func(5) = 2013-06-12 01:13:58
 * array_time_func() = [1,13,58]
 * object_date_func() =
 * JSON Object(6 {
 *  "tm_year":2012,
 *  "tm_mon":12,
 *  "tm_mday":29,
 *  "tm_hour":1,
 *  "tm_min":13,
 *  "tm_sec":58
 *  }
 * )
 * array_str_split('Hello') =
 *  JSON Array(5 ["H","e","l","l","o"])
 * 
 * Note: '..' (Two dots) is the concatenation operator (i.e '+' for Javascript) 
 */
#define JX9_PROG \
  "print 'shift_func(150) = ' .. shift_func(150) .. JX9_EOL;"\
  "print 'sum_func(7,8,9,10) = ' .. sum_func(7,8,9,10) .. JX9_EOL;"\
  "print 'date_func(5) = ' .. date_func() .. JX9_EOL;"\
  "print 'array_time_func() =' .. array_time_func() .. JX9_EOL;"\
  "print 'object_date_func() =' ..JX9_EOL;"\
  "dump(object_date_func());"\
  "print 'array_str_split(\\'Hello\\') =' .. JX9_EOL;"\
  "dump(array_str_split('Hello'));"

 /* No need for command line arguments, everything is stored in-memory */
int main(void)
{
	unqlite *pDb;       /* Database handle */
	unqlite_vm *pVm;    /* UnQLite VM resulting from successful compilation of the target Jx9 script */
	int i,rc;

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

	/* Now we have our program compiled, it's time to register 
	 * our foreign functions.
	 */
	for( i = 0 ; i < (int)sizeof(aFunc)/sizeof(aFunc[0]) ;  ++i ){
		/* Install the foreign function */
		rc = unqlite_create_function(pVm, aFunc[i].zName, aFunc[i].xProc, 0 /* NULL: No private data */);
		if( rc != UNQLITE_OK ){
			Fatal(pDb,"Error while registering foreign functions");
		}
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
