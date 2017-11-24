/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * filetest.c
 *
 * 	Tests the filesystem by opening, writing to and reading from a
 * 	user specified file.
 *
 * This should run (on SFS) even before the file system assignment is started.
 * It should also continue to work once said assignment is complete.
 * It will not run fully on emufs, because emufs does not support remove().
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

int
main(int argc, char *argv[])
{
	static char writebuf1[8] = "01238901";
     static char writebuf2[8] = "45672345";
	static char readbuf[18];

	const char *file1, *file2, *mergefile;
	int fd, rv;
 
	if (argc == 2) {
          file1 = argv[1];  // bypass compile error
     }
     else if(argc > 2){
		errx(1, "Usage: meld");
	}

     file1 = "source1";
     file2 = "source2";
     mergefile= "merged";


     printf("\nCreating source1 file...\n");
	fd = open(file1, O_WRONLY|O_CREAT|O_TRUNC, 0664);
	if (fd<0) {
		err(1, "%s: open for write", file1);
	}

	rv = write(fd, writebuf1, 8);
	if (rv<0) {
		err(1, "%s: write", file1);
	}

	rv = close(fd);
	if (rv<0) {
		err(1, "%s: close", file1);
	}

     printf("Creating source2 file...\n");
     fd = open(file2, O_WRONLY|O_CREAT|O_TRUNC, 0664);
     if (fd<0) {
          err(1, "%s: open for write", file2);
     }

     rv = write(fd, writebuf2, 8);
     if (rv<0) {
          err(1, "%s: write", file2);
     }

     rv = close(fd);
     if (rv<0) {
          err(1, "%s: close (1st time)", file2);
     }

     printf("Melding...\n");
     int bytes = meld(file1, file2, mergefile);

     if (bytes<0) {
          err(1, "%s: merging", mergefile);
     }

     printf("Reading merged file...\n");
	fd = open(mergefile, O_RDONLY);
	if (fd<0) {
		err(1, "%s: open for read", mergefile);
	}

	rv = read(fd, readbuf, 16);
	if (rv<0) {
		err(1, "%s: read", mergefile);
	}

	rv = close(fd);

	if (rv<0) {
		err(1, "%s: close (merge)", mergefile);
	}
	/* ensure null termination */
	readbuf[17] = 0;

     printf("Bytes written= %d \n Contents:\n %s\n", bytes, readbuf);

	printf("Passed meld test if Contents line = 0123456789012345\n");
	return 0;
}
