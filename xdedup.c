#include <asm/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ctype.h>
#include "xdedup_header.h"

#ifndef __NR_xdedup
#error xdedup system call not defined
#endif

// typedef struct xdedup_params
// {
// 	const char *infile1;
// 	const char *infile2;
// 	char *outfile;
// 	u_int flags;
// } params;

int main(int argc, const char *argv[])
{
	int c;
	int nflag=0, pflag=0, dflag=0;
	int rc;
	struct xdedup_params param;
	//const char *infile1 = NULL, *infile2 = NULL;
	//char *outfile = NULL;

	while ((c = getopt (argc, (char **)argv, "npd")) != -1)
		switch(c)
		{
			case 'n':
				nflag=1;
				break;
			case 'p':
				pflag=1;
				break;
			case 'd':
				dflag=1;
				break;
			case '?':
				if (isprint(optopt))
					fprintf (stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf (stderr,"Unknown option character `\\x%x'.\n", optopt);
				return 1;
			default:
				abort();

		}

	// printf ("nflag = %d, pflag = %d, dflag = %d\n",
 //          nflag, pflag, dflag);

	if (((argc - optind) < 2) || ((argc - optind) > 3))
	{
		fprintf(stderr, "Incorrect number of arguments passed\n");
		return 1;
	}
	else
	{
		// printf("argc = %d, optind = %d\n", argc, optind);

		param.infile1 = argv[optind++];
		param.infile2 = argv[optind++];
		if(optind < argc)
			param.outfile = (char *)argv[optind];
	}

	param.flags = nflag*1 + pflag*2 + dflag*4;

	printf ("file1 = %s, file2 = %s, ofile = %s flag = %u \n",
          param.infile1, param.infile2, param.outfile, param.flags);

  	rc = syscall(__NR_xdedup, (void *) &param);
	if (rc == 0)
		printf("yo syscall returned %d\n", rc);
	else
		printf("syscall returned %d (errno=%d)\n", rc, errno);

	exit(rc);
}
