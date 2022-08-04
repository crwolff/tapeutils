// https://web.archive.org/web/20200705162253/https://www.computerconservationsociety.org/timage.c

/*
	timage -- copy magnetic tape (double-buffered) to tape-image file

	derived from BRL/VLD "tcopy" (Natalie & Gwyn)

	last edit:	01/06/15	gwyn@arl.army.mil

	Usage:	timage /dev/rmt/0mbn tape_image	# for example

	First argument must be the name of a non-rewinding-on-close,
	readable-after-tape-mark (the latter is called ``BSD behavior'' by
	Sun) raw magtape device, or "-" to read from standard input (for
	special applications only).

	The tape-image file contains a sequence of variable-length records:
	each tape mark is represented by four 0-valued bytes, and end-of-tape
	is denoted by two consecutive tape marks.  Data records consist of a
	four-byte record size (in little-endian order), the data bytes, an
	additional 0-valued byte if the number of data bytes was odd, and
	another four-byte record size (to facilitate backspace operations).
	(This is the format used by Bob Supnick's SIMH computer emulators.)
	Such tape-image files can be copied to magtape by the timage utility,
	or unpacked into a set of disk files by the tunpack utility.

	Tape input is double-buffered, which typically makes a huge difference
	in throughput.
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<sys/wait.h>

#ifndef MAXSIZE
#define	MAXSIZE	(20*1024)
#endif

static char	buffer[MAXSIZE];

static void
error( msg )
	char	*msg;
	{
	perror( msg );
	exit( 1 );
	}

static void
PutCount( to, nc, oname )
	int		to;		/* output file descriptor */
	unsigned long	nc;		/* # characters in following record */
	char		*oname;		/* file name in case of write error */
	{
	static unsigned char	hdr[4];

	hdr[0] = nc & (unsigned long)0xFF;
	hdr[1] = (nc >> 8) & (unsigned long)0xFF;
	hdr[2] = (nc >> 16) & (unsigned long)0xFF;
	hdr[3] = (nc >> 24) & (unsigned long)0xFF;

	if ( write( to, (char *)hdr, 4 ) != 4 )
		error( oname );
	}

int
main( argc, argv )
	int	argc;
	char	*argv[];
	{
	static int	pfd[2][2];	/* pipe file descriptors */
	static char	eof;		/* token passed between processes */
	register int	from, to;	/* data I/O file descriptors */
	register int	nc;		/* # bytes read (may be odd) */
	register int	nx;		/* # bytes to write (will be even) */
	register long	count;		/* file block counter */
	int		bs;		/* file blocksize */
	int		pid;		/* child pid (0 in child) */

	if ( argc != 3 )
		{
		(void)fputs( "Usage: timage from-nrmt to-file\n", stderr );
		exit( 2 );
		}

	if ( (to = creat( argv[2], 0666 )) < 0 )
		error( argv[2] );

	do	{			/* for each input tape file: */
		if ( argv[1][0] == '-' && argv[1][1] == '\0' )
			from = 0;	/* stdin */
		else if ( (from = open( argv[1], 0 )) < 0 )
			error( argv[1] );

		/* For each tape file, we set up a pair of processes that pass a
		   "token" around to synchronize with each other, in order to
		   avoid race conditions as they both read and write on the same
		   file descriptors.  This permits reading of the input tape
		   concurrently with writing of the output file; this is a
		   "double buffering" scheme using standard UNIX facilities. */

		if ( pipe( pfd[0] ) != 0 || pipe( pfd[1] ) != 0 )
			error( "pipe" );

		switch( pid = fork() )
			{
		case -1:
			error( "fork" );
			/*NOTREACHED*/

		case 0:			/* child */
			if ( close( pfd[0][1] ) < 0 || close( pfd[1][0] ) < 0 )
				error( "close" );

			for ( eof = 0; ; )
				{
				/* assert( !eof ); */

				if ( write( pfd[1][1], &eof, 1 ) != 1 )
					error( "child pipe write W" );

				/* The token is in the pipe but not necessarily
				   read yet by the other process.  This is the
				   cute trick that achieves double-buffering. */

				if ( read( pfd[0][0], &eof, 1 ) != 1 )
					error( "child pipe read R" );

				if ( eof )
					break;	/* terminate child */

				if ( (nc = read( from, buffer, MAXSIZE )) < 0 )
					error( argv[1] );

				eof = nc == 0;

				if ( write( pfd[1][1], &eof, 1 ) != 1 )
					error( "child pipe write R" );

				/* The token is in the pipe but not necessarily
				   read yet by the other process.  This is the
				   cute trick that achieves double-buffering. */

				if ( read( pfd[0][0], &eof, 1 ) != 1 )
					error( "child pipe read W" );

				/* assert( !eof ); */

				PutCount( to, (unsigned long)nc, argv[2] );

				if ( nc == 0 )
					break;	/* terminate child */

				if ( (nx = nc) % 2 != 0 )
					buffer[nx++] = 0;	/* pad even */

				if ( write( to, buffer, nx ) != nx )
					error( argv[2] );

				PutCount( to, (unsigned long)nc, argv[2] );
				}

			_exit( 0 );	/* terminate child */
			/*NOTREACHED*/

		default:		/* parent */
			if ( close( pfd[0][0] ) < 0 || close( pfd[1][1] ) < 0 )
				error( "close" );

			for ( count = 0L; ; )
				{
				if ( (nc = read( from, buffer, MAXSIZE )) < 0 )
					error( argv[1] );

				if ( count == 0L )
					bs = nc;	/* use 1st blocksize */

				if ( !(eof = nc == 0) )
					++count;	/* parent read block */

				if ( write( pfd[0][1], &eof, 1 ) != 1 )
					error( "parent pipe write R" );

				/* The token is in the pipe but not necessarily
				   read yet by the other process.  This is the
				   cute trick that achieves double-buffering. */

				if ( read( pfd[1][0], &eof, 1 ) != 1 )
					error( "parent pipe read W" );

				/* assert( !eof ); */

				PutCount( to, (unsigned long)nc, argv[2] );

				if ( nc == 0 )
					break;	/* wait for child */

				if ( (nx = nc) % 2 != 0 )
					buffer[nx++] = 0;	/* pad even */

				if ( write( to, buffer, nx ) != nx )
					error( argv[2] );

				PutCount( to, (unsigned long)nc, argv[2] );

				/* assert( !eof ); */

				if ( write( pfd[0][1], &eof, 1 ) != 1 )
					error( "parent pipe write W" );

				/* The token is in the pipe but not necessarily
				   read yet by the other process.  This is the
				   cute trick that achieves double-buffering. */

				if ( read( pfd[1][0], &eof, 1 ) != 1 )
					error( "parent pipe read R" );

				if ( eof )
					break;	/* wait for child */

				++count;	/* child read block */
				}

			if ( wait( (int *)0 ) != pid )	/* looping not needed */
				error( "wait" );

			if ( close( pfd[0][1] ) < 0 || close( pfd[1][0] ) < 0 )
				error( "close" );
			}

		if ( argv[1][0] != '-' || argv[1][1] != '\0' )
			if ( close( from ) != 0 )
				error( argv[1] );

		if ( count == 0L )
			(void)fputs( "EOM\n", stderr );
		else
			(void)fprintf( stderr, "%ld records, blocksize %d\n",
				       count, bs
				     );
		}
	while ( count > 0L );

	PutCount( to, (unsigned long)0, argv[2] );

	if ( close( to ) != 0 )
		error( argv[2] );

	return 0;
	}
