/* readiso.c -- program to read ISO9660 image from (scsi) cd-rom
 * $Id$
 *
 * Copyright (c) 1997-1998  Timo Kokkonen <tjko@iki.fi>
 *
 * 
 * This file may be copied under the terms and conditions 
 * of the GNU General Public License, as published by the Free
 * Software Foundation (Cambridge, Massachusetts).
 */

#define VERSION "v1.1"
#define PRGNAME "readiso"

#include <stdio.h>
#if HAVE_GETOPT_H && HAVE_GETOPT_LONG
#include <getopt.h>
#else
#include "getopt.h"
#endif
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

#include "config.h"
#include "md5.h"


#ifdef SGI
#include <dslib.h>
#include <signal.h>
#endif

#ifdef LINUX
#include <linux/version.h>
#include <scsi.h>
#include <sg.h>
#endif


/* debug macro to print buffer in hex */
#define PRINT_BUF(x,n) { int i; for(i=0;i<n;i++) printf("%02x ",(unsigned char*)x[i]); printf("\n"); fflush(stdout); }


/* mode definitions for scsi_request() function */
#define SCSIR_READ     0x01
#define SCSIR_WRITE    0x02
#define SCSIR_QUIET    0x10

#ifdef SGI
#define READBLOCKS     64    /* no of blocks to read at a time */
#else
#define READBLOCKS     1
#endif

#define BLOCKSIZE      2048  /* (read) block size */

#define MAX_DIFF_ALLOWED  512  /* how many blocks image size can be smaller
                                  than track size, before we override image
				  size with track size */


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



#ifndef DEFAULT_DEV
#define DEFAULT_DEV "/dev/cdrom"
#endif
char *default_dev = DEFAULT_DEV;

#ifdef SGI
static struct dsreq *dsp;   /* handle to scsi device */
#endif

#ifdef LINUX
#define FUDGE          10    
struct sg_request {
  struct sg_header header;
  unsigned char bytes[READBLOCKS*BLOCKSIZE+FUDGE];
} sg_request;

struct sg_reply {
  struct sg_header header;
  unsigned char bytes[100+READBLOCKS*BLOCKSIZE];
};

int fd;    /* file descriptor of the scsi device open */
#endif

static char rcsid[] = "$Id$";
int verbose_mode = 0;

static struct option long_options[] = {
  {"verbose",0,0,'v'},
  {"help",0,0,'h'},
  {"info",0,0,'i'},
  {"device",1,0,'d'},
  {"track",1,0,'t'},
  {"force",1,0,'f'},
  {"md5",1,0,'m'},
  {"MD5",1,0,'M'},
  {"copy",1,0,'c'}
};


/************************************************************************/

char *md2str(unsigned char *digest, char *s)
{
  int i;
  char buf[16],*r;

  if (!digest) return NULL;
  if (!s) {
    s=(char*)malloc(33);
    if (!s) return NULL;
  }

  r=s;
  for (i = 0; i < 16; i++) {
    sprintf (buf,"%02x", digest[i]);
    *(s++)=buf[0];
    *(s++)=buf[1];
  }
  *s=0;

  return r;
}




void die(char *format, ...)
{
  va_list args;

  fprintf(stderr, PRGNAME ": ");
  va_start(args,format);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr,"\n");
  fflush(stderr);
  exit(1);
}

void p_usage(void) 
{
  fprintf(stderr, PRGNAME " " VERSION "  " HOST_TYPE "   "
	  "Copyright (c) Timo Kokkonen, 1997-1998.\n"); 

  fprintf(stderr,
	  "Usage: " PRGNAME " [options] <imagefile>\n\n"
	  "  -d<device>, --device=<device>\n"
          "                  specifies the scsi device to use (default: " DEFAULT_DEV ")\n"
	  "  -h, --help      display this help and exit\n"
	  "  -i, --info      only display TOC record and ISO9660 image info\n"
	  "  -v, --verbose   verbose mode\n"
          "  -m, --md5       calculate MD5 checksum for imagefile\n"
	  "  -M, --MD5       calculate MD5 checksum for disc (don't create\n"
	  "                  image file).\n"
	  "  --dump=<lba,n>  dumb (copy) 'n' sectors from cd, starting from 'lba'\n"
	  "  --force=<mode>  force program to trust blindly either ISO primary\n"
	  "                  descriptor or TOC record for the size of image.\n"
          "                  mode = 1 (trust ISO primary descriptor)\n"
	  "                         2 (trust TOC record)\n"
	  "  --track=<n>     reads specified track (default is first data track found)\n"
	  "\n");

  exit(1);
}


int scsi_request(char *note, unsigned char *reply, int *replylen, 
		 int cmdlen, int datalen, int mode, ...)
{
#ifdef SGI
/* Irix... */
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
  
  if (mode&SCSIR_READ) 
    filldsreq(dsp,reply,*replylen,DSRQ_READ|DSRQ_SENSE);
  else filldsreq(dsp,databuf,datalen,DSRQ_WRITE|DSRQ_SENSE);
  dsp->ds_time = 15*1000;
  result = doscsireq(getfd(dsp),dsp);

  if (RET(dsp) && RET(dsp) != DSRT_SHORT && !(mode&SCSIR_QUIET)) {
    fprintf(stderr,"%s status=%d ret=%xh sensesent=%d datasent=%d "
	    "senselen=%d\n", note, STATUS(dsp), RET(dsp),
	    SENSESENT(dsp), DATASENT(dsp), SENSELEN(dsp));

  }

  if (mode==SCSIR_READ) { *replylen=DATASENT(dsp); } 


  free(databuf);

  return result;

#else 
/* Linux... */

  va_list args;
  struct sg_request sg_request;
  struct sg_reply sg_reply;
  int result,i,size,wasread;
  static int pack_id=0;

  memset(&sg_request,0,sizeof(sg_request));
  sg_request.header.pack_len=sizeof(struct sg_header) + 10;
  sg_request.header.reply_len=*replylen + sizeof(struct sg_header);
  sg_request.header.pack_id=pack_id++;
  sg_request.header.result=0;
  
  size=sizeof(struct sg_header)+cmdlen+datalen;

  va_start(args,mode);
  for (i=0;i<(cmdlen+datalen);i++) 
    sg_request.bytes[i]=va_arg(args,unsigned int);
  va_end(args);
  
  result = write(fd,&sg_request, size);
  if (result<0) {
    fprintf(stderr,"%s write error %d\n",note,result);
    return result;
  }
  else if (result!=size) {
    fprintf(stderr,"%s wrote only %dbytes of expected %dbytes\n",note,
	    result,size);
    return 2;
  }
  
  wasread=read(fd,&sg_reply,sizeof(struct sg_reply));
  if (wasread > sizeof(struct sg_header)) {
    *replylen=wasread-sizeof(struct sg_header);
    memcpy(reply,sg_reply.bytes,*replylen);
  }
  else *replylen=0;

  if (!(mode&SCSIR_QUIET) {
    fprintf(stderr,"%s status=%d result=%d\n",wasread,sg_reply.header.result);
  }

  return sg_reply.header.result;

#endif
}


int inquiry(char *manufacturer, char *model, char *revision) {
  int i, result;
  unsigned char bytes[255];
  int replylen = sizeof(bytes);

  manufacturer[0]=model[0]=revision[0]=0;

  result = scsi_request("inquiry",bytes,&replylen,6,0,SCSIR_READ,
			INQUIRY, /* 0 */
			0,0,0,255,0);

  if (result) return result;

  for(i=35;i>32;i--) if(bytes[i]!=' ') break;
  bytes[i+1]=0;
  strncpy(revision,&bytes[32],5);

  for(i=31;i>16;i--) if(bytes[i]!=' ') break;
  bytes[i+1]=0;
  strncpy(model,&bytes[16],17);

  for(i=15;i>8;i--) if(bytes[i]!=' ') break;
  bytes[i+1]=0;
  strncpy(manufacturer,&bytes[8],9);

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
  return scsi_request("test_unit_ready",0,0,6,0,SCSIR_WRITE|SCSIR_QUIET,
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
  printf("\nTracks: %d \t (first=%02d last=%02d)\n",
	(buf[3]-buf[2])+1,buf[2],buf[3]);
  
  for (i=0;i<((len-2)/8)-1;i++) {
    o=4+i*8; /* offset to track descriptor */
    printf("Track %02d: %s (adr/ctrl=%02xh) begin=%06d end=%06d  "
           "length<=%06d\n",
	   i+1,(buf[o+1]&0x04?"data ":"audio"),buf[o+1],V4(&buf[o+4]),
	   V4(&buf[o+4+8]),V4(&buf[o+4+8])-V4(&buf[o+4]) );

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



/************************************************************************/
int main(int argc, char **argv) 
{
  FILE *outfile=NULL;
  iso_primary_descriptor_type  ipd;
  char *dev = default_dev;
  char vendor[9],model[17],rev[5];
  char reply[1024],tmpstr[255];
  int replylen=sizeof(reply);
  int trackno = 0;
  int info_only = 0;
  unsigned char *buffer;
  int buffersize = READBLOCKS*BLOCKSIZE;
  int start,stop,imagesize,tracksize;
  int counter = 0;
  long readsize = 0;
  long imagesize_bytes;
  int drive_block_size;
  int force_mode = 0;
  int dump_mode = 0;
  int dump_start, dump_count;
  MD5_CTX *MD5; 
  char digest[16],digest_text[33];
  int md5_mode = 0;
  int opt_index = 0;
#ifdef LINUX
  int fd;
  char *s;
#endif

  int i,c,o;
  unsigned int len;

  if (rcsid); 

  MD5 = malloc(sizeof(MD5_CTX));
  buffer=(unsigned char*)malloc(READBLOCKS*BLOCKSIZE);
  if (!buffer || !MD5) die("No memory");

  if (argc<2) die("parameter(s) missing\n"
	          "Try '%s --help' for more information.\n",PRGNAME);

 
  /* parse command line parameters */
  while(1) {
    if ((c=getopt_long(argc,argv,"Mmvhid:",long_options,&opt_index))
	== -1) break;
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
      info_only=1; 
      break;
    case 'c':
      if (sscanf(optarg,"%d,%d",&dump_start,&dump_count)!=2)
	die("invalid parameters");
      dump_mode=1;
      break;
    case 'f':
      if (sscanf(optarg,"%d",&force_mode)!=1) die("invalid parameters");
      if (force_mode<1 || force_mode >2) {
	die("invalid parameters");
      }
      break;
    case 'm':
      md5_mode=1;
      break;
    case 'M':
      md5_mode=2;
      break;

    case '?':
      break;

    default:
      die("error parsing parameters");

    }
  }


  if (!info_only) {
    if (md5_mode==2) outfile=fopen("/dev/null","w");
    else outfile=fopen(argv[optind],"w");
    if (!outfile) die("cannot open output file '%s'",argv[optind]);
  }

  /* open the scsi device */
#ifdef SGI  
  dsp=dsopen(dev, O_RDWR);
  if (!dsp)  die("error opening scsi device '%s'",dev); 
#endif
#ifdef LINUX
  fd=open(dev,O_RDWR);
  if (fd<0) die("error opening scsi device '%s'",dev);
  i=fcntl(fd,F_GETFL);
  fcntl(fd,F_SETFL,i|O_NONBLOCK);
  while(read(fd,reply,sizeof(reply))!=-1 || errno!=EAGAIN); /* empty buffer */
  fcntl(fd,F_SETFL,i&~O_NONBLOCK);
#endif

  memset(reply,0,sizeof(reply));


  if (inquiry(vendor,model,rev)!=0) die("error accessing scsi device");

  printf("readiso(9660) " VERSION "\n");
  if (verbose_mode) {
    printf("device:   %s\n",dev);
    printf("Vendor:   %s\nModel:    %s\nRevision: %s\n",vendor,model,rev);
  }

  test_ready();
  if (test_ready()!=0) {
    sleep(2);
    if (test_ready()!=0)  die("device is busy");
  }

  fprintf(stderr,"Initializing...\n");
  set_removable(1);
  replylen=sizeof(reply);
  mode_select(2048);
  if (mode_sense(reply,&replylen)!=0) die("cannot get sense data");
  drive_block_size=V3(&reply[9]);
  if (drive_block_size!=2048) die("cannot set drive to 2048bytes/block mode.");
  start_stop(1);


  if (dump_mode) {
    fprintf(stderr,"Dumping %d sector(s) starting from LBA=%d\n",
	    dump_count,dump_start);
    for (i=dump_start;i<dump_start+dump_count;i++) {
      len=buffersize;
      read_10(i,1,buffer,&len);
      if (len<2048) break;
      fwrite(buffer,len,1,outfile);
      fprintf(stderr,".");
    }
    fprintf(stderr,"\ndone.\n");
    goto quit;
  }


  fprintf(stderr,"Reading disc TOC...");
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

  fprintf(stderr,"Reading track %d...\n",trackno); 

  if ( (trackno < reply[2]) || (trackno > reply[3]) || 
      ((reply[(trackno-1)*8+4+1]&0x04)==0) ) die("Not a data track.");


  start=V4(&reply[(trackno-1)*8+4+4]);
  stop=V4(&reply[(trackno)*8+4+4]);
  tracksize=abs(stop-start);
  /* if (verbose_mode) printf("Start LBA=%d\nStop  LBA=%d\n",start,stop); */

  len=buffersize;
  read_10(start-0,1,buffer,&len);
  /* PRINT_BUF(buffer,32); */
  
  /* read the iso9660 primary descriptor */
  fprintf(stderr,"Reading ISO9660 primary descriptor...\n");
  len=buffersize;
  read_10(start+16,1,buffer,&len);
  if (len<sizeof(ipd)) die("cannot read iso9660 primary descriptor.");
  memcpy(&ipd,buffer,sizeof(ipd));

  imagesize=ISONUM(ipd.volume_space_size);

  /* we should really check here if we really got a valid primary descriptor */
  if ( (imagesize>(stop-start)) || (imagesize<1) ) {
    fprintf(stderr,"\aInvalid ISO primary descriptor!!!\n");
    if (!info_only) fprintf(stderr,"Copying entire track to image file.\n");
    force_mode=2;
  }

  if (force_mode==1) {} /* use size from ISO primary descriptor */
  else if (force_mode==2) imagesize=tracksize; /* use size from TOC */
  else {
    if (  ( (tracksize-imagesize) > MAX_DIFF_ALLOWED ) || 
	  ( imagesize > tracksize )  )   {
      fprintf(stderr,"ISO primary descriptor has suspicious volume size"
	             " (%d blocks)\n",imagesize);
      imagesize=tracksize;
      fprintf(stderr,"Using track size from TOC record (%d blocks) instead.\n",
	      imagesize);
      fprintf(stderr,"(option -f can be used to override this behaviour.)\n");
    }
  }

  imagesize_bytes=imagesize*BLOCKSIZE;
    

  if (verbose_mode||info_only) {
    printf("ISO9660 image info:\n");
    printf("Type:              %02xh\n",ipd.type[0]);  
    ISOGETSTR(tmpstr,ipd.id,5);
    printf("ID:                %s\n",tmpstr);
    printf("Version:           %u\n",ipd.version[0]);
    ISOGETSTR(tmpstr,ipd.system_id,32);
    printf("System id:         %s\n",tmpstr);
    ISOGETSTR(tmpstr,ipd.volume_id,32);
    printf("Volume id:         %s\n",tmpstr);
    ISOGETSTR(tmpstr,ipd.volume_set_id,128);
    if (strlen(tmpstr)>0) printf("Volume set id:     %s\n",tmpstr);
    ISOGETSTR(tmpstr,ipd.publisher_id,128);
    if (strlen(tmpstr)>0) printf("Publisher id:      %s\n",tmpstr);
    ISOGETSTR(tmpstr,ipd.preparer_id,128);
    if (strlen(tmpstr)>0) printf("Preparer id:       %s\n",tmpstr);
    ISOGETSTR(tmpstr,ipd.application_id,128);
    if (strlen(tmpstr)>0) printf("Application id:    %s\n",tmpstr);
    ISOGETDATE(tmpstr,ipd.creation_date);
    printf("Creation date:     %s\n",tmpstr);
    ISOGETDATE(tmpstr,ipd.modification_date);
    if (!NULLISODATE(ipd.modification_date)) 
	printf("Modification date: %s\n",tmpstr);
    ISOGETDATE(tmpstr,ipd.expiration_date);
    if (!NULLISODATE(ipd.expiration_date))
	printf("Expiration date:   %s\n",tmpstr);
    ISOGETDATE(tmpstr,ipd.effective_date);
    if (!NULLISODATE(ipd.effective_date))
	printf("Effective date:    %s\n",tmpstr);

    printf("Image size:        %d blocks (%ld bytes)\n",
	   ISONUM(ipd.volume_space_size),
	   (long)ISONUM(ipd.volume_space_size)*BLOCKSIZE);
    printf("Track size:        %d blocks (%ld bytes)\n",
	   tracksize,
	   (long)tracksize*BLOCKSIZE);
  }


  /* read the image */

  if (md5_mode) MD5Init(MD5);

  if (!info_only) {
    fprintf(stderr,"Reading the ISO9660 image (%ldMb)...\n",
	    imagesize_bytes/(1024*1024));

    do {
      len=buffersize;
      if(readsize/BLOCKSIZE+READBLOCKS>imagesize)
	read_10(start+counter,imagesize-readsize/BLOCKSIZE,buffer,&len);
      else
	read_10(start+counter,READBLOCKS,buffer,&len);
      if ((counter%(1024*1024/BLOCKSIZE))<READBLOCKS) {
	fprintf(stderr,"%3dM of %dM read.         \r",
		counter/512,imagesize/512);
      }
      counter+=READBLOCKS;
      readsize+=len;
      fwrite(buffer,len,1,outfile);
      if (md5_mode) MD5Update(MD5,buffer,(readsize>imagesize_bytes?
				       len-(readsize-imagesize_bytes):len) );
    } while (len==BLOCKSIZE*READBLOCKS && readsize<imagesize*BLOCKSIZE);
    
    fprintf(stderr,"\n");
    if (readsize > imagesize_bytes) ftruncate(fileno(outfile),imagesize_bytes);
    if (readsize < imagesize_bytes) fprintf(stderr,"Image not complete!\n");
    else fprintf(stderr,"Image complete.\n");
    fclose(outfile);
  }

  if (md5_mode) {
    MD5Final(digest,MD5);
    md2str(digest,digest_text);
    fprintf(stderr,"MD5 (%s) = %s\n",(md5_mode==2?"'image'":argv[optind]),
	    digest_text);
  }

 quit:
  start_stop(0);
  set_removable(1);
#ifdef SGI
  dsclose(dsp);
#endif
#ifdef LINUX
  close(fd);
#endif

  return 0;
}




