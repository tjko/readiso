/* scsi_linux.c 
 * $Id$
 *
 * Copyright (c) 1997-1999  Timo Kokkonen <tjko@iki.fi>
 *
 * 
 * This file may be copied under the terms and conditions 
 * of the GNU General Public License, as published by the Free
 * Software Foundation (Cambridge, Massachusetts).
 */

#define VERSION "v1.3b"
#define PRGNAME "readiso"


#ifndef DEFAULT_DEV
#define DEFAULT_DEV "/dev/cdrom"
#endif


/* macros for building MSF values from LBA */
#define LBA_MIN(x) ((x)/(60*75))
#define LBA_SEC(x) (((x)%(60*75))/75)
#define LBA_FRM(x) ((x)%75)


/* mode definitions for scsi_request() function */
#define SCSIR_READ     0x01
#define SCSIR_WRITE    0x02
#define SCSIR_QUIET    0x10

#ifdef IRIX
#define READBLOCKS     64    /* no of blocks to read at a time */
#else
#define READBLOCKS     1
#endif

#define BLOCKSIZE      2048  /* data block size */
#define AUDIOBLOCKSIZE 2368  /* cdda (2352) + subcode-q (16) */

#define MAX_DIFF_ALLOWED  512  /* how many blocks image size can be smaller
                                  than track size, before we override image
				  size with track size */

/* track types in TOC */
#define DATA_TRACK   0x04

/* SCSI commands used by the program */
#define TESTREADY     0x00
#define INQUIRY       0x12
#define MODESELECT    0x15
#define MODESENSE     0x1A
#define STOPUNIT      0x1B
#define REMOVAL       0x1E
#define READCAPACITY  0x25
#define READ10        0x28
#define READTOC       0x43
#define MODESELECT10  0x55
#define MODESENSE10   0x5A
#define LOAD_UNLOAD   0xE7

#ifdef LINUX
#define AF_FILE_AIFF 0
#define AF_FILE_AIFFC 1
#endif

#ifndef IRIX
/* macros for building byte array from number */
#define B(s,i) ((unsigned char)((s) >> i))
#define B1(s) ((unsigned char)(s))
#define B2(s) B((s),8), B1(s)
#define B3(s) B((s),16), B((s),8), B1(s)
#define B4(s) B((s),24), B((s),16), B((s),8), B1(s)

/* macros for converting array of bytes to binary  */
#define V1(s) (s)[0]
#define V2(s) (((s)[0] << 8) | (s)[1])
#define V3(s) (((((s)[0] << 8) | (s)[1]) << 8) | (s)[2])
#define V4(s) (((((((s)[0] << 8) | (s)[1]) << 8) | (s)[2]) << 8) | (s)[3])
#endif


#define ISODCL(from, to) (to - from + 1)
#define ISONUM(n) ( ((n)[0]&0xff) | (((n)[1]&0xff)<<8) | (((n)[2]&0xff)<<16) \
                    | (((n)[3]&0xff)<<24) )

#define ISOGETSTR(t,s,l) { char *r=((t)+(int)((l)-1)); memcpy((t),(s),(l)); \
		    	   while (*r==' '&&r>=(t)) r--;  *(r+1)=0; }
#define ISOGETDATE(t,s)  sprintf((t),"%c%c.%c%c.%c%c%c%c %c%c:%c%c:%c%c",\
				   s[6],s[7],s[4],s[5],s[0],s[1],s[2],s[3],\
				   s[8],s[9],s[10],s[11],s[12],s[13]); 
#define NULLISODATE(s)  (s[0]==s[1]&&s[1]==s[2]&&s[2]==s[3]&&s[3]==s[4]&& \
			 s[4]==s[5]&&s[5]==s[6]&&s[6]==s[7]&&s[7]==s[8])


/* ISO9660 primary descriptor definition */
typedef struct iso_primary_descriptor_type_ {
  unsigned char type			[ISODCL (  1,   1)]; /* 711 */
  unsigned char id			[ISODCL (  2,   6)];
  unsigned char version			[ISODCL (  7,   7)]; /* 711 */
  unsigned char unused1			[ISODCL (  8,   8)];
  unsigned char system_id		[ISODCL (  9,  40)]; /* aunsigned 
								chars */
  unsigned char volume_id		[ISODCL ( 41,  72)]; /* dunsigned 
								chars */
  unsigned char unused2			[ISODCL ( 73,  80)];
  unsigned char volume_space_size	[ISODCL ( 81,  88)]; /* 733 */
  unsigned char unused3			[ISODCL ( 89, 120)];
  unsigned char volume_set_size		[ISODCL (121, 124)]; /* 723 */
  unsigned char volume_sequence_number	[ISODCL (125, 128)]; /* 723 */
  unsigned char logical_block_size	[ISODCL (129, 132)]; /* 723 */
  unsigned char path_table_size		[ISODCL (133, 140)]; /* 733 */
  unsigned char type_l_path_table	[ISODCL (141, 144)]; /* 731 */
  unsigned char opt_type_l_path_table	[ISODCL (145, 148)]; /* 731 */
  unsigned char type_m_path_table	[ISODCL (149, 152)]; /* 732 */
  unsigned char opt_type_m_path_table	[ISODCL (153, 156)]; /* 732 */
  unsigned char root_directory_record	[ISODCL (157, 190)]; /* 9.1 */
  unsigned char volume_set_id	        [ISODCL (191, 318)]; /* dunsigned 
								chars*/
  unsigned char publisher_id		[ISODCL (319, 446)]; /* achars */
  unsigned char preparer_id		[ISODCL (447, 574)]; /* achars */
  unsigned char application_id		[ISODCL (575, 702)]; /* achars */
  unsigned char copyright_file_id	[ISODCL (703, 739)]; /* 7.5 dchars */
  unsigned char abstract_file_id	[ISODCL (740, 776)]; /* 7.5 dchars */
  unsigned char bibliographic_file_id	[ISODCL (777, 813)]; /* 7.5 dchars */
  unsigned char creation_date		[ISODCL (814, 830)]; /* 8.4.26.1 */
  unsigned char modification_date	[ISODCL (831, 847)]; /* 8.4.26.1 */
  unsigned char expiration_date		[ISODCL (848, 864)]; /* 8.4.26.1 */
  unsigned char effective_date		[ISODCL (865, 881)]; /* 8.4.26.1 */
  unsigned char file_structure_version	[ISODCL (882, 882)]; /* 711 */
  unsigned char unused4			[ISODCL (883, 883)];
  unsigned char application_data	[ISODCL (884, 1395)];
  unsigned char unused5			[ISODCL (1396, 2048)];
} iso_primary_descriptor_type;



/* function declarations */

int  scsi_open(const char *dev);
void scsi_close();
int  scsi_request(char *note, unsigned char *reply, int *replylen, 
	          int cmdlen, int datalen, int mode, ...);


