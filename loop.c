
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
	int inc = 0;
	while(1){
		sleep(3);
		++inc;
		printf("test\n");
		const char* s = getenv("SA_0");
		printf("SA_0 :%s\n",(s!=NULL)? s : "getenv returned NULL");
		printf("end test\n");
	}
}

/**

// dev
gdbserver host:2345 a.out

// host

(gdb) exec-file test
(gdb) b 2
No symbol table is loaded.  Use the "file" command.
(gdb) file test
Reading symbols from /home/user/test/test...done.
(gdb) b 2
Breakpoint 1 at 0x80483ea: file test.c, line 2.
(gdb) 

# stop service
monitor exit


*/