struct xdedup_params
{
	const char *infile1;
	const char *infile2;
	char *outfile;
	u_int flags;
};

#define FLAG_N 0x01
#define FLAG_P 0x02
#define FLAG_D 0x04

#define BUFFER_SIZE PAGE_SIZE

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))