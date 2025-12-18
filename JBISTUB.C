/*****************************************************************************/
/*                                                                           */
/* Module:           jbistub.c                                               */
/*                                                                           */
/*                   Copyright 2025 Altera Corporation                       */
/*                                                                           */
/* SPDX-License-Identifier: MIT-0                                            */
/*                                                                           */
/* Permission is hereby granted, free of charge, to any person obtaining a   */
/* copy of this software and associated documentation files (the             */
/* "Software"),to deal in the Software without restriction, including        */
/* without limitation the rights to use, copy, modify, merge, publish,       */
/* distribute, sublicense, and/or sell copies of the Software, and to permit */
/* persons to whom the Software is furnished to do so.                       */
/*                                                                           */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS   */
/* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF                */
/* MERCHANTABILITY,  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY      */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT */
/* OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR  */
/* THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                */
/*                                                                           */
/* Description:      Jam STAPL ByteCode Player main source file              */
/*                                                                           */
/*                   Supports Intel ByteBlaster hardware download cable      */
/*                   on Windows 95 and Windows NT operating systems.         */
/*                   (A device driver is required for Windows NT.)           */
/*                                                                           */
/*                   Also supports BitBlaster hardware download cable on     */
/*                   Windows 95, Windows NT, and UNIX platforms.             */
/*                                                                           */
/* Revisions:        1.1 fixed control port initialization for ByteBlaster   */
/*                   2.0 added support for STAPL bytecode format, added code */
/*                       to get printer port address from Windows registry   */
/*                   2.1 improved messages, fixed delay-calibration bug in   */
/*                       16-bit DOS port, added support for "alternative     */
/*                       cable X", added option to control whether to reset  */
/*                       the TAP after execution, moved porting macros into  */
/*                       jbiport.h                                           */
/*                   2.2 added support for static memory                     */
/*                       fixed /W4 warnings                                  */
/*                                                                           */
/*****************************************************************************/
//Removed dos port support for this version
//Implemented only Windows port support, only PicoBitBlaster support on serial port

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE
#pragma warning(disable:4244 4267 4334 4456 4996)
#endif

#include "jbiport.h"

#if PORT == WINDOWS
#include <windows.h>
static HANDLE com_handle = INVALID_HANDLE_VALUE;
#else
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
#define TRUE 1
#define FALSE 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#if defined(USE_STATIC_MEMORY)
	#define N_STATIC_MEMORY_KBYTES ((unsigned int) USE_STATIC_MEMORY)
	#define N_STATIC_MEMORY_BYTES (N_STATIC_MEMORY_KBYTES * 1024)
	#define POINTER_ALIGNMENT sizeof(DWORD)
#else /* USE_STATIC_MEMORY */
	#include <malloc.h>
	#define POINTER_ALIGNMENT sizeof(BYTE)
#endif /* USE_STATIC_MEMORY */
#include <time.h>
#include <conio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "jbiexprt.h"

/************************************************************************
*
*	Global variables
*/

/* file buffer for Jam STAPL ByteCode input file */
unsigned char *file_buffer = NULL;
long file_pointer = 0L;
long file_length = 0L;

/* delay count for one millisecond delay */
long one_ms_delay = 0L;

/* serial port interface available on all platforms */
BOOL jtag_hardware_initialized = FALSE;
char *serial_port_name = NULL;
BOOL specified_com_port = FALSE;
int com_port = -1;
void initialize_jtag_hardware(void);
void close_jtag_hardware(void);

#if defined(USE_STATIC_MEMORY)
	unsigned char static_memory_heap[N_STATIC_MEMORY_BYTES] = { 0 };
#endif /* USE_STATIC_MEMORY */

#if defined(USE_STATIC_MEMORY) || defined(MEM_TRACKER)
	unsigned int n_bytes_allocated = 0;
#endif /* USE_STATIC_MEMORY || MEM_TRACKER */

#if defined(MEM_TRACKER)
	unsigned int peak_memory_usage = 0;
	unsigned int peak_allocations = 0;
	unsigned int n_allocations = 0;
#if defined(USE_STATIC_MEMORY)
	unsigned int n_bytes_not_recovered = 0;
#endif /* USE_STATIC_MEMORY */
	const DWORD BEGIN_GUARD = 0x01234567;
	const DWORD END_GUARD = 0x76543210;
#endif /* MEM_TRACKER */


/* function prototypes to allow forward reference */
extern void delay_loop(long count);

BOOL verbose = FALSE;

/************************************************************************
*
*	Customized interface functions for Jam STAPL ByteCode Player I/O:
*
*	jbi_jtag_io()
*	jbi_message()
*	jbi_delay()
*/

int jbi_jtag_io(int tms, int tdi, int read_tdo)
{
	//printf("DEBUG: jbi_jtag_io called with tms=%d, tdi=%d, read_tdo=%d\n",tms, tdi, read_tdo);
	int tdo = 0;
	char ch_data = 0;

	if (!jtag_hardware_initialized)
	{
		initialize_jtag_hardware();
		jtag_hardware_initialized = TRUE;
	}

	if (specified_com_port)
	{
		ch_data = (char)
			((tdi ? 0x01 : 0) | (tms ? 0x02 : 0) | (read_tdo ? 0x04 : 0));
		ch_data |= '0'; /* ASCII '0'..'7' */

#if PORT == WINDOWS
		/* write one byte to the serial port using Win32 API */
		{
			DWORD written = 0;
			BOOL ok = FALSE;

			if (com_handle == INVALID_HANDLE_VALUE)
			{
				fprintf(stderr, "Error: serial port not opened\n");
			}
			else
			{
				ok = WriteFile(com_handle, &ch_data, 1, &written, NULL);
				if (!ok || (written != 1))
				{
					fprintf(stderr, "Error: WriteFile failed (err=%lu)\n", (unsigned long)GetLastError());
				}
			}
		}

		if (read_tdo)
		{
			char resp = 0;
			DWORD readn = 0;
			BOOL ok = FALSE;
			int attempts = 0;

			/* try a few times to read a single byte (timeout controlled by COMMTIMEOUTS) */
			for (attempts = 0; (attempts < 100) && (readn == 0); ++attempts)
			{
				ok = ReadFile(com_handle, &resp, 1, &readn, NULL);
				if (!ok)
				{
					/* ReadFile can fail if timeouts occur; break on fatal error */
					DWORD err = GetLastError();
					if (err != ERROR_IO_PENDING && err != ERROR_SUCCESS)
					{
						fprintf(stderr, "Error: ReadFile failed (err=%lu)\n", (unsigned long)err);
						break;
					}
				}
			}

			if (readn == 1)
			{
				tdo = (resp == '1') ? 1 : 0;
			}
			else
			{
				fprintf(stderr, "Error: PicoBlaster not responding\n");
			}
		}
#else
		/* POSIX-style write/read (existing behaviour) */
		write(com_port, &ch_data, 1);

		if (read_tdo)
		{
			for (i = 0; (i < 100) && (result != 1); ++i)
			{
				result = read(com_port, &ch_data, 1);
			}
			if (result == 1)
			{
				tdo = (ch_data == '1') ? 1 : 0;
			}
			else
			{
				fprintf(stderr, "Error:  PicoBlaster not responding\n");
			}
		}
#endif
	}
	else
	{
		fprintf(stderr, "Error:  Only PicoBlaster on Serial supported\n");
	}

	return (tdo);
}

void initialize_jtag_hardware()
{
	if (!specified_com_port)
	{
		fprintf(stderr, "Error: Only serial port jtag supported \n");
		return;
	}

#if PORT == WINDOWS
	/* Open the serial port (use CreateFileA for ANSI string) */
	com_handle = CreateFileA(
		serial_port_name,
		GENERIC_READ | GENERIC_WRITE,
		0,              /* exclusive access */
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (com_handle == INVALID_HANDLE_VALUE)
	{
		fprintf(stderr, "Error: can't open serial port \"%s\" (err=%lu)\n",
			serial_port_name, (unsigned long)GetLastError());
		return;
	}

	/* Configure port: 230400, 8N1, DTR/RTS, raw mode */
	DCB dcb;
	COMMTIMEOUTS timeouts;

	ZeroMemory(&dcb, sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);
	if (!GetCommState(com_handle, &dcb))
	{
		fprintf(stderr, "Error: GetCommState failed (err=%lu)\n", (unsigned long)GetLastError());
		CloseHandle(com_handle);
		com_handle = INVALID_HANDLE_VALUE;
		return;
	}

	dcb.BaudRate = 230400;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.fBinary = TRUE;
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	dcb.fRtsControl = RTS_CONTROL_ENABLE;
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	dcb.fInX = FALSE;
	dcb.fOutX = FALSE;
	dcb.fNull = FALSE;

	if (!SetCommState(com_handle, &dcb))
	{
		fprintf(stderr, "Error: SetCommState failed (err=%lu)\n", (unsigned long)GetLastError());
		CloseHandle(com_handle);
		com_handle = INVALID_HANDLE_VALUE;
		return;
	}

	/* Set timeouts: ReadFile will wait up to 100 ms for each byte (adjust as needed) */
	ZeroMemory(&timeouts, sizeof(timeouts));
	timeouts.ReadIntervalTimeout = 50;            /* ms */
	timeouts.ReadTotalTimeoutConstant = 100;      /* ms */
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = 1000;
	timeouts.WriteTotalTimeoutMultiplier = 0;

	if (!SetCommTimeouts(com_handle, &timeouts))
	{
		fprintf(stderr, "Error: SetCommTimeouts failed (err=%lu)\n", (unsigned long)GetLastError());
		/* not fatal: continue */
	}

	if (!PurgeComm(com_handle, PURGE_RXCLEAR | PURGE_TXCLEAR)) {
		fprintf(stderr, "Error: PurgeComm failed (err=%lu)\n", (unsigned long)GetLastError());
		/* not fatal: continue */
	}

	fprintf(stderr, "Debug: opened %s, com_handle = %p\n", serial_port_name, com_handle);
#else
	com_port = open(serial_port_name, O_RDWR);
	if (com_port == -1)
	{
		fprintf(stderr, "Error: can't open serial port \"%s\"\n",
			serial_port_name);
	}
	else
	{
		fprintf(stderr, "Debug: opened %s, com_port = %d\n", serial_port_name, com_port);
	}
#endif
}

void close_jtag_hardware()
{
	if (!specified_com_port) return;

#if PORT == WINDOWS
	if (com_handle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(com_handle);
		com_handle = INVALID_HANDLE_VALUE;
	}
#else
	if (com_port != -1) close(com_port);
#endif
}

void jbi_message(char *message_text)
{
	puts(message_text);
	fflush(stdout);
}

void jbi_export_integer(char *key, long value)
{
	if (verbose)
	{
		printf("Export: key = \"%s\", value = %ld\n", key, value);
		fflush(stdout);
	}
}

#define HEX_LINE_CHARS 72
#define HEX_LINE_BITS (HEX_LINE_CHARS * 4)

char conv_to_hex(unsigned long value)
{
	char c;

	if (value > 9)
	{
		c = (char) (value + ('A' - 10));
	}
	else
	{
		c = (char) (value + '0');
	}

	return (c);
}

void jbi_export_boolean_array(char *key, unsigned char *data, long count)
{
	char string[HEX_LINE_CHARS + 1];
	long i, offset;
	unsigned long size, line, lines, linebits, value, j, k;

	if (verbose)
	{
		if (count > HEX_LINE_BITS)
		{
			printf("Export: key = \"%s\", %ld bits, value = HEX\n", key, count);
			lines = (count + (HEX_LINE_BITS - 1)) / HEX_LINE_BITS;

			for (line = 0; line < lines; ++line)
			{
				if (line < (lines - 1))
				{
					linebits = HEX_LINE_BITS;
					size = HEX_LINE_CHARS;
					offset = count - ((line + 1) * HEX_LINE_BITS);
				}
				else
				{
					linebits = count - ((lines - 1) * HEX_LINE_BITS);
					size = (linebits + 3) / 4;
					offset = 0L;
				}

				string[size] = '\0';
				j = size - 1;
				value = 0;

				for (k = 0; k < linebits; ++k)
				{
					i = k + offset;
					if (data[i >> 3] & (1 << (i & 7))) value |= (1 << (i & 3));
					if ((i & 3) == 3)
					{
						string[j] = conv_to_hex(value);
						value = 0;
						--j;
					}
				}
				if ((k & 3) > 0) string[j] = conv_to_hex(value);

				printf("%s\n", string);
			}

			fflush(stdout);
		}
		else
		{
			size = (count + 3) / 4;
			string[size] = '\0';
			j = size - 1;
			value = 0;

			for (i = 0; i < count; ++i)
			{
				if (data[i >> 3] & (1 << (i & 7))) value |= (1 << (i & 3));
				if ((i & 3) == 3)
				{
					string[j] = conv_to_hex(value);
					value = 0;
					--j;
				}
			}
			if ((i & 3) > 0) string[j] = conv_to_hex(value);

			printf("Export: key = \"%s\", %ld bits, value = HEX %s\n",
				key, count, string);
			fflush(stdout);
		}
	}
}

void jbi_delay(long microseconds)
{
    if (microseconds <= 0) return;

#if PORT == WINDOWS
    LARGE_INTEGER freq, start, now;

    /* Use high-resolution performance counter if available */
    if (QueryPerformanceFrequency(&freq) && QueryPerformanceCounter(&start))
    {
        LONGLONG target = start.QuadPart + (freq.QuadPart * (LONGLONG)microseconds) / 1000000LL;
        do {
            QueryPerformanceCounter(&now);
        } while (now.QuadPart < target);
    }
    else
    {
        /* fallback to Sleep for coarse delays */
        DWORD ms = (DWORD)((microseconds + 999) / 1000);
        Sleep(ms);
    }

#else
    /* For non-Windows platforms use calibrated busy-wait loop based on one_ms_delay */
    if (one_ms_delay <= 0) calibrate_delay();

    /* one_ms_delay is number of loop iterations for approximately 1 ms */
    long loops = (long)((one_ms_delay * (double)microseconds) / 1000.0);
    if (loops <= 0) loops = 1;
    delay_loop(loops);
#endif
}

void *jbi_malloc(unsigned int size)
{
	unsigned int n_bytes_to_allocate = 
#if defined(USE_STATIC_MEMORY) || defined(MEM_TRACKER)
		sizeof(unsigned int) +
#endif /* USE_STATIC_MEMORY || MEM_TRACKER */
#if defined(MEM_TRACKER)
		(2 * sizeof(DWORD)) +
#endif /* MEM_TRACKER */
		(POINTER_ALIGNMENT * ((size + POINTER_ALIGNMENT - 1) / POINTER_ALIGNMENT));

	unsigned char *ptr = 0;


#if defined(MEM_TRACKER)
	if ((n_bytes_allocated + n_bytes_to_allocate) > peak_memory_usage)
	{
		peak_memory_usage = n_bytes_allocated + n_bytes_to_allocate;
	}
	if ((n_allocations + 1) > peak_allocations)
	{
		peak_allocations = n_allocations + 1;
	}
#endif /* MEM_TRACKER */

#if defined(USE_STATIC_MEMORY)
	if ((n_bytes_allocated + n_bytes_to_allocate) <= N_STATIC_MEMORY_BYTES)
	{
		ptr = (&(static_memory_heap[n_bytes_allocated]));
	}
#else /* USE_STATIC_MEMORY */ 
	ptr = (unsigned char *) malloc(n_bytes_to_allocate);
#endif /* USE_STATIC_MEMORY */

#if defined(USE_STATIC_MEMORY) || defined(MEM_TRACKER)
	if (ptr != 0)
	{
		unsigned int i = 0;

#if defined(MEM_TRACKER)
		for (i = 0; i < sizeof(DWORD); ++i)
		{
			*ptr = (unsigned char) (BEGIN_GUARD >> (8 * i));
			++ptr;
		}
#endif /* MEM_TRACKER */

		for (i = 0; i < sizeof(unsigned int); ++i)
		{
			*ptr = (unsigned char) (size >> (8 * i));
			++ptr;
		}

#if defined(MEM_TRACKER)
		for (i = 0; i < sizeof(DWORD); ++i)
		{
			*(ptr + size + i) = (unsigned char) (END_GUARD >> (8 * i));
			/* don't increment ptr */
		}

		++n_allocations;
#endif /* MEM_TRACKER */

		n_bytes_allocated += n_bytes_to_allocate;
	}
#endif /* USE_STATIC_MEMORY || MEM_TRACKER */

	return ptr;
}

void jbi_free(void *ptr)
{
	if
	(
#if defined(MEM_TRACKER)
		(n_allocations > 0) &&
#endif /* MEM_TRACKER */
		(ptr != 0)
	)
	{
		unsigned char *tmp_ptr = (unsigned char *) ptr;

#if defined(USE_STATIC_MEMORY) || defined(MEM_TRACKER)
		unsigned int n_bytes_to_free = 0;
		unsigned int i = 0;
		unsigned int size = 0;
#endif /* USE_STATIC_MEMORY || MEM_TRACKER */
#if defined(MEM_TRACKER)
		DWORD begin_guard = 0;
		DWORD end_guard = 0;


		tmp_ptr -= sizeof(DWORD);
#endif /* MEM_TRACKER */
#if defined(USE_STATIC_MEMORY) || defined(MEM_TRACKER)
		tmp_ptr -= sizeof(unsigned int);
#endif /* USE_STATIC_MEMORY || MEM_TRACKER */
		ptr = tmp_ptr;

#if defined(MEM_TRACKER)
		for (i = 0; i < sizeof(DWORD); ++i)
		{
			begin_guard |= (((DWORD)(*tmp_ptr)) << (8 * i));
			++tmp_ptr;
		}
#endif /* MEM_TRACKER */

#if defined(USE_STATIC_MEMORY) || defined(MEM_TRACKER)
		for (i = 0; i < sizeof(unsigned int); ++i)
		{
			size |= (((unsigned int)(*tmp_ptr)) << (8 * i));
			++tmp_ptr;
		}
#endif /* USE_STATIC_MEMORY || MEM_TRACKER */

#if defined(MEM_TRACKER)
		tmp_ptr += size;

		for (i = 0; i < sizeof(DWORD); ++i)
		{
			end_guard |= (((DWORD)(*tmp_ptr)) << (8 * i));
			++tmp_ptr;
		}

		if ((begin_guard != BEGIN_GUARD) || (end_guard != END_GUARD))
		{
			fprintf(stderr, "Error: memory corruption detected for allocation #%d... bad %s guard\n",
				n_allocations, (begin_guard != BEGIN_GUARD) ? "begin" : "end");
		}

		--n_allocations;
#endif /* MEM_TRACKER */

#if defined(USE_STATIC_MEMORY) || defined(MEM_TRACKER)
		n_bytes_to_free = 
#if defined(MEM_TRACKER)
		(2 * sizeof(DWORD)) +
#endif /* MEM_TRACKER */
		sizeof(unsigned int) +
		(POINTER_ALIGNMENT * ((size + POINTER_ALIGNMENT - 1) / POINTER_ALIGNMENT));
#endif /* USE_STATIC_MEMORY || MEM_TRACKER */

#if defined(USE_STATIC_MEMORY)
		if ((((unsigned long) ptr - (unsigned long) static_memory_heap) + n_bytes_to_free) == (unsigned long) n_bytes_allocated)
		{
			n_bytes_allocated -= n_bytes_to_free;
		}
#if defined(MEM_TRACKER)
		else
		{
			n_bytes_not_recovered += n_bytes_to_free;
		}
#endif /* MEM_TRACKER */
#else /* USE_STATIC_MEMORY */
#if defined(MEM_TRACKER)
		n_bytes_allocated -= n_bytes_to_free;
#endif /* MEM_TRACKER */
		free(ptr);
		ptr = NULL;
#endif /* USE_STATIC_MEMORY */
	}
#if defined(MEM_TRACKER)
	else
	{
		if (ptr != 0)
		{
			fprintf(stderr, "Error: attempt to free unallocated memory\n");
		}
	}
#endif /* MEM_TRACKER */
}

/************************************************************************
*
*	get_tick_count() -- Get system tick count in milliseconds
*
*	for WINDOWS use GetTickCount() function
*	for UNIX use clock() system function
*/
DWORD get_tick_count(void)
{
	DWORD tick_count = 0L;

#if PORT == WINDOWS
	tick_count = GetTickCount();
#else
	/* assume clock() function returns microseconds */
	tick_count = (DWORD) (clock() / 1000L);
#endif

	return (tick_count);
}

#define DELAY_SAMPLES 10
#define DELAY_CHECK_LOOPS 10000

void calibrate_delay(void)
{
	int sample = 0;
	int count = 0;
	DWORD tick_count1 = 0L;
	DWORD tick_count2 = 0L;

	one_ms_delay = 0L;

#if PORT == WINDOWS
	for (sample = 0; sample < DELAY_SAMPLES; ++sample)
	{
		count = 0;
		tick_count1 = get_tick_count();
		while ((tick_count2 = get_tick_count()) == tick_count1) {};
		do { delay_loop(DELAY_CHECK_LOOPS); count++; } while
			((tick_count1 = get_tick_count()) == tick_count2);
		one_ms_delay += ((DELAY_CHECK_LOOPS * (DWORD)count) /
// 			(tick_count1 - tick_count2));
			(tick_count1 - tick_count2));
	}

	one_ms_delay /= DELAY_SAMPLES;
#else
	/* This is system-dependent!  Update this number for target system */
	one_ms_delay = 1000L;
#endif
}

char *error_text[] =
{
/* JBIC_SUCCESS            0 */ "success",
/* JBIC_OUT_OF_MEMORY      1 */ "out of memory",
/* JBIC_IO_ERROR           2 */ "file access error",
/* JBIC_SYNTAX_ERROR       3 */ "syntax error",
/* JBIC_UNEXPECTED_END     4 */ "unexpected end of file",
/* JBIC_UNDEFINED_SYMBOL   5 */ "undefined symbol",
/* JBIC_REDEFINED_SYMBOL   6 */ "redefined symbol",
/* JBIC_INTEGER_OVERFLOW   7 */ "integer overflow",
/* JBIC_DIVIDE_BY_ZERO     8 */ "divide by zero",
/* JBIC_CRC_ERROR          9 */ "CRC mismatch",
/* JBIC_INTERNAL_ERROR    10 */ "internal error",
/* JBIC_BOUNDS_ERROR      11 */ "bounds error",
/* JBIC_TYPE_MISMATCH     12 */ "type mismatch",
/* JBIC_ASSIGN_TO_CONST   13 */ "assignment to constant",
/* JBIC_NEXT_UNEXPECTED   14 */ "NEXT unexpected",
/* JBIC_POP_UNEXPECTED    15 */ "POP unexpected",
/* JBIC_RETURN_UNEXPECTED 16 */ "RETURN unexpected",
/* JBIC_ILLEGAL_SYMBOL    17 */ "illegal symbol name",
/* JBIC_VECTOR_MAP_FAILED 18 */ "vector signal name not found",
/* JBIC_USER_ABORT        19 */ "execution cancelled",
/* JBIC_STACK_OVERFLOW    20 */ "stack overflow",
/* JBIC_ILLEGAL_OPCODE    21 */ "illegal instruction code",
/* JBIC_PHASE_ERROR       22 */ "phase error",
/* JBIC_SCOPE_ERROR       23 */ "scope error",
/* JBIC_ACTION_NOT_FOUND  24 */ "action not found",
};

#define MAX_ERROR_CODE (int)((sizeof(error_text)/sizeof(error_text[0]))+1)

/************************************************************************/

int main(int argc, char **argv)
{
	BOOL help = FALSE;
	BOOL error = FALSE;
	char *filename = NULL;
	long offset = 0L;
	long error_address = 0L;
	JBI_RETURN_TYPE crc_result = JBIC_SUCCESS;
	JBI_RETURN_TYPE exec_result = JBIC_SUCCESS;
	unsigned short expected_crc = 0;
	unsigned short actual_crc = 0;
	char key[33] = {0};
	char value[257] = {0};
	int exit_status = 0;
	int arg = 0;
	int exit_code = 0;
	int format_version = 0;
	time_t start_time = 0;
	time_t end_time = 0;
	int time_delta = 0;
	char *workspace = NULL;
	char *action = NULL;
	char *init_list[10];
	int init_count = 0;
	FILE *fp = NULL;
	struct stat sbuf;
	long workspace_size = 0;
	char *exit_string = NULL;
	int reset_jtag = 1;
	int execute_program = 1;
	int action_count = 0;
	int procedure_count = 0;
	int index = 0;
	char *action_name = NULL;
	char *description = NULL;
	JBI_PROCINFO *procedure_list = NULL;
	JBI_PROCINFO *procptr = NULL;

	verbose = FALSE;

	init_list[0] = NULL;

	/* print out the version string and copyright message */
	fprintf(stderr, "Jam STAPL ByteCode Player Version 2.3 (20231228)\n");
	fprintf(stderr, "Copyright (C) 2023 Intel Corporation\n\n");
	fprintf(stderr, "Port for 64bit systems\n");
	fprintf(stderr, "Adapted for PicoBitBlaster JTAG programmer (on USB-serial)\n");
	fprintf(stderr, "PF, 17.12.2025\n");

	for (arg = 1; arg < argc; arg++)
	{
#if PORT == UNIX
		if (argv[arg][0] == '-')
#else
		if ((argv[arg][0] == '-') || (argv[arg][0] == '/'))
#endif
		{
			switch(toupper(argv[arg][1]))
			{
			case 'A':				/* set action name */
				if (action == NULL)
				{
					action = &argv[arg][2];
				}
				else
				{
					error = TRUE;
				}
				break;

			case 'D':				/* initialization list */
				if (argv[arg][2] == '"')
				{
					init_list[init_count] = &argv[arg][3];
				}
				else
				{
					init_list[init_count] = &argv[arg][2];
				}
				init_list[++init_count] = NULL;
				break;

			case 'R':		/* don't reset the JTAG chain after use */
				reset_jtag = 0;
				break;

			case 'S':				/* set serial port address */
				serial_port_name = &argv[arg][2];
				specified_com_port = TRUE;
				break;

			case 'M':				/* set memory size */
				if (sscanf(&argv[arg][2], "%ld", &workspace_size) != 1)
					error = TRUE;
				if (workspace_size == 0) error = TRUE;
				break;

			case 'H':				/* help */
				help = TRUE;
				break;

			case 'V':				/* verbose */
				verbose = TRUE;
				break;

			case 'I':				/* show info only, do not execute */
				verbose = TRUE;
				execute_program = 0;
				break;

			default:
				error = TRUE;
				break;
			}
		}
		else
		{
			/* it's a filename */
			if (filename == NULL)
			{
				filename = argv[arg];
			}
			else
			{
				/* error -- we already found a filename */
				error = TRUE;
			}
		}

		if (error)
		{
			fprintf(stderr, "Illegal argument: \"%s\"\n", argv[arg]);
			help = TRUE;
			error = FALSE;
		}
	}

	if (help || (filename == NULL))
	{
		fprintf(stderr, "Usage:  jbi [options] <filename>\n");
		fprintf(stderr, "\nAvailable options:\n");
		fprintf(stderr, "    -h          : show help message\n");
		fprintf(stderr, "    -v          : show verbose messages\n");
		fprintf(stderr, "    -i          : show file info only - does not execute any action\n");
		fprintf(stderr, "    -a<action>  : specify an action name (Jam STAPL)\n");
		fprintf(stderr, "    -d<proc=1>  : enable optional procedure (Jam STAPL)\n");
		fprintf(stderr, "    -d<proc=0>  : disable recommended procedure (Jam STAPL)\n");
		fprintf(stderr, "    -s<port>    : serial port name (Picoblaster: 230400, 8N1, DTR/RTS)\n");
		fprintf(stderr, "    -r          : don't reset JTAG TAP after use\n");
		exit_status = 1;
	}
	else if ((workspace_size > 0) &&
		((workspace = (char *) jbi_malloc((size_t) workspace_size)) == NULL))
	{
		fprintf(stderr, "Error: can't allocate memory (%d Kbytes)\n",
			(int) (workspace_size / 1024L));
		exit_status = 1;
	}
	else if (access(filename, 0) != 0)
	{
		fprintf(stderr, "Error: can't access file \"%s\"\n", filename);
		exit_status = 1;
	}
	else
	{
		/* get length of file */
		if (stat(filename, &sbuf) == 0) file_length = sbuf.st_size;

		if ((fp = fopen(filename, "rb")) == NULL)
		{
			fprintf(stderr, "Error: can't open file \"%s\"\n", filename);
			exit_status = 1;
		}
		else
		{
			/*
			*	Read entire file into a buffer
			*/
			file_buffer = (unsigned char *) jbi_malloc((size_t) file_length);

			if (file_buffer == NULL)
			{
				fprintf(stderr, "Error: can't allocate memory (%d Kbytes)\n",
					(int) (file_length / 1024L));
				exit_status = 1;
			}
			else
			{
				if (fread(file_buffer, 1, (size_t) file_length, fp) !=
					(size_t) file_length)
				{
					fprintf(stderr, "Error reading file \"%s\"\n", filename);
					exit_status = 1;
				}
			}

			fclose(fp);
		}

		if (exit_status == 0)
		{

			/*
			*	Calibrate the delay loop function
			*/
			calibrate_delay();

			/*
			*	Check CRC
			*/
			crc_result = jbi_check_crc(file_buffer, file_length,
				&expected_crc, &actual_crc);

			if (verbose || (crc_result == JBIC_CRC_ERROR))
			{
				switch (crc_result)
				{
				case JBIC_SUCCESS:
					printf("CRC matched: CRC value = %04X\n", actual_crc);
					break;

				case JBIC_CRC_ERROR:
					printf("CRC mismatch: expected %04X, actual %04X\n",
						expected_crc, actual_crc);
					break;

				case JBIC_UNEXPECTED_END:
					printf("Expected CRC not found, actual CRC value = %04X\n",
						actual_crc);
					break;

				case JBIC_IO_ERROR:
					printf("Error: File format is not recognized.\n");
					exit(1);
					break;

				default:
					printf("CRC function returned error code %d\n", crc_result);
					break;
				}
			}

			if (verbose)
			{
				/*
				*	Display file format version
				*/
				jbi_get_file_info(file_buffer, file_length,
					&format_version, &action_count, &procedure_count);

				printf("File format is %s ByteCode format\n",
					(format_version == 2) ? "Jam STAPL" : "pre-standardized Jam 1.1");

				/*
				*	Dump out NOTE fields
				*/
				while (jbi_get_note(file_buffer, file_length,
					&offset, key, value, 256) == 0)
				{
					printf("NOTE \"%s\" = \"%s\"\n", key, value);
				}

				/*
				*	Dump the action table
				*/
				if ((format_version == 2) && (action_count > 0))
				{
					printf("\nActions available in this file:\n");

					for (index = 0; index < action_count; ++index)
					{
						jbi_get_action_info(file_buffer, file_length,
							index, &action_name, &description, &procedure_list);

						if (description == NULL)
						{
							printf("%s\n", action_name);
						}
						else
						{
							printf("%s \"%s\"\n", action_name, description);
						}

						procptr = procedure_list;
						while (procptr != NULL)
						{
							if (procptr->attributes != 0)
							{
								printf("    %s (%s)\n", procptr->name,
									(procptr->attributes == 1) ?
									"optional" : "recommended");
							}

							procedure_list = procptr->next;
							jbi_free(procptr);
							procptr = procedure_list;
						}
					}

					/* add a blank line before execution messages */
					if (execute_program) printf("\n");
				}
			}

			if (execute_program)
			{
				/*
				*	Execute the Jam STAPL ByteCode program
				*/
				time(&start_time);
				exec_result = jbi_execute(file_buffer, file_length, workspace,
					workspace_size, action, init_list, reset_jtag,
					&error_address, &exit_code, &format_version);
				time(&end_time);

				if (exec_result == JBIC_SUCCESS)
				{
					if (format_version == 2)
					{
						switch (exit_code)
						{
						case  0: exit_string = "Success"; break;
						case  1: exit_string = "Checking chain failure"; break;
						case  2: exit_string = "Reading IDCODE failure"; break;
						case  3: exit_string = "Reading USERCODE failure"; break;
						case  4: exit_string = "Reading UESCODE failure"; break;
						case  5: exit_string = "Entering ISP failure"; break;
						case  6: exit_string = "Unrecognized device"; break;
						case  7: exit_string = "Device revision is not supported"; break;
						case  8: exit_string = "Erase failure"; break;
						case  9: exit_string = "Device is not blank"; break;
						case 10: exit_string = "Device programming failure"; break;
						case 11: exit_string = "Device verify failure"; break;
						case 12: exit_string = "Read failure"; break;
						case 13: exit_string = "Calculating checksum failure"; break;
						case 14: exit_string = "Setting security bit failure"; break;
						case 15: exit_string = "Querying security bit failure"; break;
						case 16: exit_string = "Exiting ISP failure"; break;
						case 17: exit_string = "Performing system test failure"; break;
						default: exit_string = "Unknown exit code"; break;
						}
					}
					else
					{
						switch (exit_code)
						{
						case 0: exit_string = "Success"; break;
						case 1: exit_string = "Illegal initialization values"; break;
						case 2: exit_string = "Unrecognized device"; break;
						case 3: exit_string = "Device revision is not supported"; break;
						case 4: exit_string = "Device programming failure"; break;
						case 5: exit_string = "Device is not blank"; break;
						case 6: exit_string = "Device verify failure"; break;
						case 7: exit_string = "SRAM configuration failure"; break;
						default: exit_string = "Unknown exit code"; break;
						}
					}

					printf("Exit code = %d... %s\n", exit_code, exit_string);
				}
				else if ((format_version == 2) &&
					(exec_result == JBIC_ACTION_NOT_FOUND))
				{
					if ((action == NULL) || (*action == '\0'))
					{
						printf("Error: no action specified for Jam STAPL file.\nProgram terminated.\n");
					}
					else
					{
						printf("Error: action \"%s\" is not supported for this Jam STAPL file.\nProgram terminated.\n", action);
					}
				}
				else if (exec_result < MAX_ERROR_CODE)
				{
					printf("Error at address %ld: %s.\nProgram terminated.\n",
						error_address, error_text[exec_result]);
				}
				else
				{
					printf("Unknown error code %ld\n", exec_result);
				}

				/*
				*	Print out elapsed time
				*/
				if (verbose)
				{
					time_delta = (int) (end_time - start_time);
					printf("Elapsed time = %02u:%02u:%02u\n",
						time_delta / 3600,			/* hours */
						(time_delta % 3600) / 60,	/* minutes */
						time_delta % 60);			/* seconds */
				}
			}
		}
	}

	if (jtag_hardware_initialized) close_jtag_hardware();

	if (workspace != NULL) jbi_free(workspace);
	if (file_buffer != NULL) jbi_free(file_buffer);

#if defined(MEM_TRACKER)
	if (verbose)
	{
#if defined(USE_STATIC_MEMORY)
		fprintf(stdout, "Memory Usage Info: static memory size = %ud (%dKB)\n", N_STATIC_MEMORY_BYTES, N_STATIC_MEMORY_KBYTES);
#endif /* USE_STATIC_MEMORY */
		fprintf(stdout, "Memory Usage Info: peak memory usage = %ud (%dKB)\n", peak_memory_usage, (peak_memory_usage + 1023) / 1024);
		fprintf(stdout, "Memory Usage Info: peak allocations = %d\n", peak_allocations);
#if defined(USE_STATIC_MEMORY)
		if ((n_bytes_allocated - n_bytes_not_recovered) != 0)
		{
			fprintf(stdout, "Memory Usage Info: bytes still allocated = %d (%dKB)\n", (n_bytes_allocated - n_bytes_not_recovered), ((n_bytes_allocated - n_bytes_not_recovered) + 1023) / 1024);
		}
#else /* USE_STATIC_MEMORY */
		if (n_bytes_allocated != 0)
		{
			fprintf(stdout, "Memory Usage Info: bytes still allocated = %d (%dKB)\n", n_bytes_allocated, (n_bytes_allocated + 1023) / 1024);
		}
#endif /* USE_STATIC_MEMORY */
		if (n_allocations != 0)
		{
			fprintf(stdout, "Memory Usage Info: allocations not freed = %d\n", n_allocations);
		}
	}
#endif /* MEM_TRACKER */

	return (exit_status);
}

#if !defined (DEBUG)
#if !defined(_MSC_VER)
#pragma optimize ("ceglt", off)
#endif
#endif

void delay_loop(long count)
{
	while (count != 0L) count--;
}
