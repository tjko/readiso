/* readiso.c -- program to read ISO9660 image from (scsi) cd-rom
 *
 * copyright (c) 1997  Timo Kokkonen <tjko@jyu.fi>
 *
 * 
 * This file may be copied under the terms and conditions of version 2
 * of the GNU General Public License, as published by the Free
 * Software Foundation (Cambridge, Massachusetts).
 */

#define VERSION "0.2"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#ifdef SGI
#include <dslib.h>
#include <signal.h>
#endif

#ifdef LINUX
#include <linux/version.h>
#include <linux/scsi.h>
#endif


/* debug macro to print buffer in hex */
#define PRINT_BUF(x,n) { int i; for(i=0;i<n;i++) printf("%02x ",(unsigned char*)x[i]); printf("\n"); fflush(stdout); }


/* scsi commands used by the program */
#define TESTREADY   0x00
#define INQUIRY     0x12
#define MODESENSE   0x1A
#define STOPUNIT    0x1B
#define REMOVAL     0x1E
#define READ10      0x28
#define READTOC     0x43

#ifndef SGI
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




#ifndef DEFAULT_DEV
#define DEFAULT_DEV "/dev/cdrom"
#endif
char *default_dev = DEFAULT_DEV;

static struct dsreq *dsp;
int verbose_mode = 0;


#define SCSIR_READ     0
#define SCSIR_WRITE    1
 
#define READBLOCKS     64
#define BLOCKSIZE      2048

/************************************************************************/

void die(char *msg)
{
  fprintf(stderr,"readiso: %s\n",msg);
  exit(1);
}


void p_usage(void) 
{
  fprintf(stderr,"readiso " VERSION 
	  "  Copyright (c) Timo Kokkonen, 1997.\n"); 

  fprintf(stderr,
	  "Usage: readiso [options] <imagefile>\n\n"
	  "  -d<device>      specifies the scsi device to use "
	  "(default: " DEFAULT_DEV ")\n"
	  "  -h              display this help and exit\n"
	  "  -i              only display TOC record and ISO9660 image info\n"
	  "  -t<number>      reads specified track (default is first data "
	  "track found)\n"
	  "  -v              verbose mode\n"
	  "\n");

  exit(1);
}



int scsi_request(char *note, unsigned char *reply, int *replylen, 
		 int cmdlen, int datalen, int mode, ...)
{
  va_list args;
  int result,i;
  unsigned char *buf,*databuf;

  buf=CMDBUF(dsp);
  databuf=(unsigned char*)malloc(datalen);

  va_start(args,mode);
  for (i=0;i<cmdlen;i++) buf[i]=va_arg(args,unsigned int);
  for (i=0;i<datalen;i++) databuf[i]=va_arg(args,unsigned int);
  va_end(args);

  CMDBUF(dsp)=buf;
  CMDLEN(dsp)=cmdlen;
  
  if (mode==SCSIR_READ) filldsreq(dsp,reply,*replylen,DSRQ_READ|DSRQ_SENSE);
  else filldsreq(dsp,databuf,datalen,DSRQ_WRITE|DSRQ_SENSE);
  dsp->ds_time = 15*1000;
  result = doscsireq(getfd(dsp),dsp);

  if (RET(dsp) && RET(dsp) != DSRT_SHORT)  {
    fprintf(stderr,"%s status=%d ret=%xh sensesent=%d datasent=%d "
	    "senselen=%d\n", note, STATUS(dsp), RET(dsp),
	    SENSESENT(dsp), DATASENT(dsp), SENSELEN(dsp));

  }

  if (mode==SCSIR_READ) { *replylen=DATASENT(dsp); } 

  free(buf);
  free(databuf);

  return result;
}



int inquiry(char *manufacturer, char *model, char *revision) {
  int i, result;
  char *reply;
  unsigned char bytes[255];
  int replylen = sizeof(bytes);

  manufacturer[0]=0;
  model[0]=0;
  revision[0]=0;

  result = scsi_request("inquiry",bytes,&replylen,6,0,SCSIR_READ,
			INQUIRY, /* 0 */
			0,0,0,255,0);

  if (result) return result;


  for(i=15; i>8; i--) if(bytes[i] != ' ') break;
  reply = (char *) &bytes[8];
  while(i-->=8) *manufacturer++ = *reply++;
  *manufacturer = 0;

  for(i=31; i>16; i--) if(bytes[i] != ' ') break;
  reply = (char *) &bytes[16];
  while(i-->=16) *model++ = *reply++;
  *model = 0;

  for(i=35; i>32; i--) if(bytes[i] != ' ') break;
  reply = (char *) &bytes[32];
  while(i-->=32) *revision++ = *reply++;
  *revision = 0;

  return result;
}

int mode_sense(unsigned char *buf, int *buflen)
{
  return scsi_request("mode_sense",buf,buflen,6,0,SCSIR_READ,
		      MODESENSE,0,0,0,0x0c,0);
}


int start_stop(int start)
{
  return scsi_request((start?"start_unit":"stop_unit"),0,0,6,0,SCSIR_WRITE,
		      STOPUNIT,0,0,0,(start?1:0),0);
}

int set_removable(int removable)
{
  return scsi_request("set_removable",0,0,6,0,SCSIR_WRITE,
		      REMOVAL,0,0,0,(removable?0:1),0);
}

int test_ready()
{
  return scsi_request("test_unit_ready",0,0,6,0,SCSIR_WRITE,
		      TESTREADY,0,0,0,0,0);
}

int read_toc(unsigned char *buf, int *buflen, int mode)
{
  int result;
  int len,i,o;


  result=scsi_request("read_toc",buf,buflen,10,0,SCSIR_READ,
		      READTOC,0,0,0,0,0,
		      0,
		      B2(*buflen),
		      0);

  if (result || !mode) return result;

  len=V2(&buf[0]); 
  printf("\nFirst track: %02d \nLast track:  %02d\n",buf[2],buf[3]);
  
  for (i=0;i<((len-2)/8)-1;i++) {
    o=4+i*8; /* offset to track descriptor */
    printf("Track %02d. %s (adr/ctrl=%02xh) start=%06d next=%06d\n",
	   i+1,(buf[o+1]&0x04?"data ":"audio"),buf[o+1],V4(&buf[o+4]),
	   V4(&buf[o+4+8]) );

  }

  return result;
}

int read_10(int lba, int len, unsigned char *buf, int *buflen)
{
  return scsi_request("read_10",buf,buflen,10,0,SCSIR_READ,
		      READ10, 0,
		      B4(lba),
		      0,
		      B2(len),
		      0);

}

int mode_select(int bsize)
{
  return scsi_request("mode_select",0,0,6,12,SCSIR_WRITE,
		     0x15,0x10,0,0,12,0,
		     0,0,0,8,
		     0,B3(0),0,B3(bsize) );
}

/*****************************************************************/
int main(int argc, char **argv) 
{
  FILE *outfile;
  iso_primary_descriptor_type  ipd;
  int fd;
  char *dev = default_dev;
  char vendor[9],model[17],rev[5];
  char reply[1024],tmpstr[255];
  int replylen=sizeof(reply);
  int trackno = 0;
  int info_only = 0;
  unsigned char *buffer;
  int buffersize = READBLOCKS*BLOCKSIZE;
  int start,stop,imagesize;
  int counter = 0;
  long readsize = 0;
  long imagesize_bytes;
  int drive_block_size;

  int i,c,o,len;

  buffer=(unsigned char*)malloc(READBLOCKS*BLOCKSIZE);
  if (!buffer) die("No memory");

  if (argc<2) {
    fprintf(stderr,"readiso: parameters missing\n"
	    "Try 'readiso -h' for more information.\n ");
    exit(1);
  }
 
  /* parse command line parameters */
  while(1) {
    if ((c=getopt(argc,argv,"vhid:t:"))==-1) break;
    switch (c) {
    case 'v':
      verbose_mode=1;
      break;
    case 'h':
      p_usage();
      break;
    case 'd':
      dev=strdup(optarg);
      break;
    case 't':
      if (sscanf(optarg,"%d",&trackno)!=1) trackno=0;
      break;
    case 'i':
      info_only=1; verbose_mode=1;
      break;

    case '?':
      break;

    default:
	fprintf(stderr,"readiso: error parsing parameters.\n");
	exit(1);
    }
  }


  if (!info_only) {
    outfile=fopen(argv[optind],"w");
    if (!outfile) die("Cannot open output file.");
  }

  /* open the scsi device */
  dsp=dsopen(dev, O_RDWR|O_EXCL);
  if (!dsp) die("error opening the scsi device");

  inquiry(vendor,model,rev);
  printf("readiso(9660) " VERSION "\n");
  printf("device:   %s\n",dev);
  printf("Vendor:   %s\nModel:    %s\nRevision: %s\n",vendor,model,rev);


  if (test_ready()!=0) {
    sleep(1);
    if (test_ready()!=0)  die("Unit not ready");
  }

  printf("Initializing..."); fflush(stdout);
  set_removable(1);
  replylen=sizeof(reply);
  mode_select(2048);
  if (mode_sense(reply,&replylen)!=0) die("cannot get sense data");
  drive_block_size=V3(&reply[9]);
  if (drive_block_size!=2048) die("Cannot set drive to 2048bytes/block mode.");
  start_stop(1);

  printf("done\nReading disc TOC..."); fflush(stdout);
  replylen=sizeof(reply);
  read_toc(reply,&replylen,verbose_mode);
  printf("\n");

  if (trackno==0) { /* try to find first data track */
    for (i=0;i<(replylen-2)/8-1;i++) {
      o=4+i*8;
      if (reply[o+1]&0x04) { trackno=i+1; break; }
    }
    if (trackno==0) die("No data track(s) found.");
  }

  printf("Reading track: %d\n",trackno);
  if ( (trackno < reply[2]) || (trackno > reply[3]) || 
      ((reply[(trackno-1)*8+4+1]&0x04)==0) ) die("Not a data track.");


  start=V4(&reply[(trackno-1)*8+4+4]);
  stop=V4(&reply[(trackno)*8+4+4]);
  if (verbose_mode) printf("Start LBA=%d\nStop  LBA=%d\n",start,stop);


  len=buffersize;
  read_10(start,1,buffer,&len);
  
  /* read the iso9660 primary descriptor */
  printf("Reading ISO9660 primary descriptor...\n"); fflush(stdout);
  len=buffersize;
  read_10(start+16,1,buffer,&len);
  if (len<sizeof(ipd)) die("Cannot read iso9660 primary descriptor.");
  memcpy(&ipd,buffer,sizeof(ipd));

  imagesize=ISONUM(ipd.volume_space_size);
  imagesize_bytes=imagesize*2048;

  /* we should check here if we really got primary descriptor */
  if (imagesize>(stop-start)) die("Invalid ISO primary descriptor.");

  if (verbose_mode) {
    printf("ISO9660 Image:\n");
    printf("Type:       %02xh\n",ipd.type[0]);  
    printf("ID:         %c%c%c%c%c\n",ipd.id[0],ipd.id[1],ipd.id[2],ipd.id[3],
	   ipd.id[4]);
    printf("Version:    %d\n",ipd.version[0]);
    memcpy(tmpstr,ipd.system_id,32); tmpstr[32]=0;
    printf("System id:  %s\n",tmpstr);
    memcpy(tmpstr,ipd.volume_id,32); tmpstr[32]=0;
    printf("Volume id:  %s\n",tmpstr);
    printf("Size:       %d blocks (%d bytes).\n",imagesize,imagesize_bytes);
  }


  /* read the image */

  if (!info_only) {
    printf("Reading the ISO9660 image (%dMb)...",imagesize_bytes/(1024*1024));

    do {
      len=buffersize;
      read_10(start+counter,READBLOCKS,buffer,&len);
      printf("."); fflush(stdout);
      counter+=(len/BLOCKSIZE);
      readsize+=len;
      fwrite(buffer,len,1,outfile);
    } while (len==BLOCKSIZE*READBLOCKS && readsize<imagesize*2048);
    
    printf("\ndone.\n");
    if (readsize > imagesize_bytes) ftruncate(fileno(outfile),imagesize_bytes);

    fclose(outfile);
  }

  start_stop(0);
  set_removable(1);
  dsclose(dsp);
}




