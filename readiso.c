/* readiso.c -- program to read ISO9660 image from (scsi) cd-rom
 * $Id$
 *
 * Copyright (c) 1997-1999  Timo Kokkonen <tjko@iki.fi>
 *
 * 
 * This file may be copied under the terms and conditions 
 * of the GNU General Public License, as published by the Free
 * Software Foundation (Cambridge, Massachusetts).
 */


#include "config.h"

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
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <time.h>

#ifdef IRIX
#include <sigfpe.h>
#include <dmedia/cdaudio.h>
#include <dmedia/audio.h>
#include <dmedia/audiofile.h>
#include <signal.h>
#include <dslib.h>
#endif

#include "md5.h"
#include "readiso.h"


/* debug macro to print buffer in hex */
#define PRINT_BUF(x,n) { int i; for(i=0;i<n;i++) printf("%02x ",(unsigned char*)x[i]); printf("\n"); fflush(stdout); }

#ifdef IRIX
static ALport audioport;
static ALconfig audioconf;
static AFfilehandle aiffoutfile;
static AFfilesetup aiffsetup;
#endif

char *default_dev = DEFAULT_DEV;
static char rcsid[] = "$Id$";
static int verbose_mode = 0;
static int dump_mode = 0;
static int audio_mode = 0;
static FILE *outfile=NULL;

static struct option long_options[] = {
  {"verbose",0,0,'v'},
  {"help",0,0,'h'},
  {"info",0,0,'i'},
  {"device",1,0,'d'},
  {"track",1,0,'t'},
  {"force",1,0,'f'},
  {"md5",0,0,'m'},
  {"MD5",0,0,'M'},
  {"dump",1,0,'c'},
#ifdef IRIX
  {"dumpaudio",1,0,'C'},
  {"aiff",0,0,'a'},
  {"aiffc",0,0,'A'},
  {"sound",0,0,'s'},
#endif
  {"version",0,0,'V'},
  {"scanbus",0,0,'S'},
  {NULL,0,0,0}
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

void warn(char *format, ...)
{
  va_list args;

  fprintf(stderr, PRGNAME ": ");
  va_start(args,format);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr,"\n");
  fflush(stderr);
}


void p_usage(void) 
{
  fprintf(stderr, PRGNAME " " VERSION "  "
	  "Copyright (c) Timo Kokkonen, 1997-2000.\n"); 

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
	  "  --scanbus       scan SCSI bus and exit\n"
	  "  --version       display program version and exit\n"
#ifdef IRIX
	  " CD-DA parameters:\n"
	  "  --dumpaudio=<lba,n>\n"
	  "                  dump raw audio sectors with subcode-q data\n"
	  "  --aiff          select AIFF as output file format\n"
	  "  --aiffc         select AIFF-C as output file format\n"
#endif	  

	  "\n");

  exit(1);
}




int inquiry(char *manufacturer, char *model, char *revision) {
  int i, result;
  unsigned char bytes[255];
  int replylen = sizeof(bytes);

  manufacturer[0]=model[0]=revision[0]=0;

  result = scsi_request("inquiry",bytes,&replylen,6,0,SCSIR_READ,
			INQUIRY, /* 0 */
			0,0,0,255,0);

  if (result) return -1;

  for(i=35;i>32;i--) if(bytes[i]!=' ') break;
  bytes[i+1]=0;
  strncpy(revision,(const char*)&bytes[32],5);

  for(i=31;i>16;i--) if(bytes[i]!=' ') break;
  bytes[i+1]=0;
  strncpy(model,(const char*)&bytes[16],17);

  for(i=15;i>8;i--) if(bytes[i]!=' ') break;
  bytes[i+1]=0;
  strncpy(manufacturer,(const char*)&bytes[8],9);

  return bytes[0];
}


int mode_sense(unsigned char *buf, int *buflen)
{
  int len = *buflen;
  if (len >255) len=255;
  return scsi_request("mode_sense(6)",buf,buflen,6,0,SCSIR_READ|SCSIR_QUIET,
		      MODESENSE,0,0x01,0,len,0);
}

int mode_sense10(unsigned char *buf, int *buflen)
{
  int len = *buflen;
  if (len >65000) len=65000;
  return scsi_request("mode_sense(10)",buf,buflen,10,0,SCSIR_READ,
		      MODESENSE10,0,0x01,0,0,0,0,B2(len),0);
}

int read_capacity(int *lba, int *bsize)
{
  char buf[256];
  int r,len = 255;
  if (!lba || !bsize) return -1;

  
  r = scsi_request("read_capacity",buf,&len,10,0,SCSIR_READ,
		   READCAPACITY,0,B4(0),0,0,0x00,0);

  if (r) return r;
  *lba=V4(&buf[0]);
  *bsize=V4(&buf[4]);
  /* fprintf(stderr,"readcapacity: lba=%d bsize=%d\n",*lba,*bsize); */
  return 0;
}

int get_block_size()
{
  char buf[255];
  int len,lba=0,bsize=0;

  read_capacity(&lba,&bsize);

  len=255;
  if (mode_sense(buf,&len)==0 && buf[3]>=8) {
    return V3(&buf[4+5]);
  }

  if (mode_sense10(buf,&len)==0 && V2(&buf[6])>=8) {
    return V3(&buf[8+5]);
  }

  if (read_capacity(&lba,&bsize)==0) {
    return bsize;
  }

  return -1;
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
	   i+1,(buf[o+1]&DATA_TRACK?"data ":"audio"),buf[o+1],V4(&buf[o+4]),
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


int mode_select(int bsize, int density)
{
  return scsi_request("mode_select",0,0,6,12,SCSIR_WRITE,
		     MODESELECT,0x10,0,0,12,0,
		     0,0,0,8,
		     density,B3(0),0,B3(bsize) );
}

void scan_bus()
{
  char vendor[9],model[17],rev[5];
  char d[1024];
  int bus,dev,type;
  
  int firstbus,lastbus;
  int firstdev,lastdev;

  firstbus=0;
  firstdev=0;
  lastbus=0;
  lastdev=0;

#ifdef LINUX
  lastdev=16;
#endif
#ifdef IRIX
  firstdev=1;
  lastdev=15;
  lastbus=1;
#endif

  printf("Device                   Manufacturer  Model              Revision\n"
	 "-----------------------  ------------- ------------------ --------\n"
	 );

  for (bus=firstbus;bus<=lastbus;bus++) {

    for (dev=firstdev;dev<=lastdev;dev++) {
#ifdef LINUX
      snprintf(d,1024,"/dev/sg%c",'a'+dev);
#else
      snprintf(d,1024,"/dev/scsi/sc%dd%dl0",bus,dev);
#endif
      /* fprintf(stderr,"try: '%s'\n",d); */

      if (scsi_open(d)==0) {
	if ((type=inquiry(vendor,model,rev))>=0) {
	  type=type&0x1f;
	  printf("%-23s  %-13s %-18s %-8s %s\n",d,vendor,model,rev,
		 (type==0x5?"[CD-ROM]":""));
	}
	scsi_close();
      }

    }

  }
    
    
}


#ifdef IRIX
void playaudio(void *arg, CDDATATYPES type, short *audio)
{
  if (audio_mode) ALwritesamps(audioport, audio, CDDA_NUMSAMPLES);
  if (!dump_mode) AFwriteframes(aiffoutfile,AF_DEFAULT_TRACK,audio,
				CDDA_NUMSAMPLES/2);
}
#endif




/************************************************************************/

int main(int argc, char **argv) 
{
  iso_primary_descriptor_type  ipd;
  char *dev = default_dev;
  char vendor[9],model[17],rev[5];
  unsigned char reply[1024];
  char tmpstr[255];
  int replylen=sizeof(reply);
  int trackno = 0;
  int info_only = 0;
  unsigned char *buffer;
  int buffersize = READBLOCKS*BLOCKSIZE;
  int start,stop,imagesize=0,tracksize=0;
  int counter = 0;
  long readsize = 0;
  long imagesize_bytes = 0;
  int drive_block_size, init_bsize;
  int force_mode = 0;
  int scanbus_mode = 0;
  int dump_start, dump_count;
  MD5_CTX *MD5; 
  char digest[16],digest_text[33];
  int md5_mode = 0;
  int opt_index = 0;
  int audio_track = 0;
  int readblocksize = BLOCKSIZE;
  int file_format = AF_FILE_AIFFC;
#ifdef IRIX
  CDPARSER *cdp = CDcreateparser();
  CDFRAME  cdframe;
#endif
  int dev_type;
  int i,c,o;
  int len;
  int start_time,cur_time,kbps;

  if (rcsid); 

  MD5 = malloc(sizeof(MD5_CTX));
  buffer=(unsigned char*)malloc(READBLOCKS*AUDIOBLOCKSIZE);
  if (!buffer || !MD5) die("No memory");

  if (argc<2) die("parameter(s) missing\n"
	          "Try '%s --help' for more information.\n",PRGNAME);

 
  /* parse command line parameters */
  while(1) {
    if ((c=getopt_long(argc,argv,"SMmvhid:",long_options,&opt_index))
	== -1) break;
    switch (c) {
    case 'a':
      file_format=AF_FILE_AIFF;
      break;
    case 'A':
      file_format=AF_FILE_AIFFC;
      break;
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
#ifdef IRIX
    case 'C':
      if (sscanf(optarg,"%d,%d",&dump_start,&dump_count)!=2)
	die("invalid parameters");
      dump_mode=2;
      break;
#endif
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
    case 's':
      audio_mode=1;
      break;
    case 'S':
      scanbus_mode=1;
      break;
    case 'V':
      printf(PRGNAME " "  VERSION "  " HOST_TYPE
	     "\nCopyright (c) Timo Kokkonen, 1997-1998.\n\n"); 
      exit(0);
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
    if (!outfile) {
      if (argv[optind]) die("cannot open output file '%s'",argv[optind]);
      info_only=1;
    }
  }

  printf("readiso(9660) " VERSION "\n");

  /* open the scsi device */
  if (scsi_open(dev)) die("error opening scsi device '%s'",dev); 

  if (scanbus_mode) {
    printf("\n");
    scan_bus();
    exit(0);
  }
  
  memset(reply,0,sizeof(reply));
  if ((dev_type=inquiry(vendor,model,rev))<0) 
    die("error accessing scsi device");

  if (verbose_mode) {
    printf("device:   %s\n",dev);
    printf("Vendor:   %s\nModel:    %s\nRevision: %s\n",vendor,model,rev);
  }

  if ( (dev_type&0x1f) != 0x5 ) {
    die("Device doesn't seem to be a CD-ROM!");
  }

#ifdef IRIX
  if (strcmp(vendor,"TOSHIBA")) {
    warn("NOTE! Audio track reading probably not supported on this device.\n");
  }
#endif

  test_ready();
  if (test_ready()!=0) {
    sleep(2);
    if (test_ready()!=0)  die("device not ready");
  }

  fprintf(stderr,"Initializing...\n");
  if (audio_mode) {
#ifdef IRIX
    audioport=ALopenport("readiso","w",0);
    if (!audioport) {
      warn("Cannot initialize audio.");
      audio_mode=0;
    }
#else
    audio_mode=0;
#endif
  }


#ifdef IRIX
  /* Make sure we get sane underflow exception handling */
  sigfpe_[_UNDERFL].repls = _ZERO;
  handle_sigfpes(_ON, _EN_UNDERFL, NULL, _ABORT_ON_ERROR, NULL);
#endif

  /* set_removable(1); */

#if 0
  replylen=255;
  if (mode_sense10(reply,&replylen)==0) {
    printf("replylen=%d blocks=%d blocklen=%d\n",replylen,
	   V3(&reply[8+1]),V3(&reply[8+5]));
    PRINT_BUF(reply,replylen);
  }
  replylen=255; /* sizeof(reply); */
  if (mode_sense(reply,&replylen)==0) {
    printf("replylen=%d blocks=%d blocklen=%d\n",replylen,
	   V3(&reply[4+1]),V3(&reply[4+5]));
    PRINT_BUF(reply,replylen);
  }
#endif

  if (dump_mode==2) init_bsize=AUDIOBLOCKSIZE;
  else init_bsize=BLOCKSIZE;

  start_stop(0);

  if ( (drive_block_size=get_block_size()) < 0 ) {
    warn("cannot get current block size");
    drive_block_size=init_bsize;
  }

  if (drive_block_size != init_bsize) {
    mode_select(init_bsize,(dump_mode==2?0x82:0x00));
    drive_block_size=get_block_size();
    if (drive_block_size!=init_bsize) warn("cannot set drive block size.");
  }

  start_stop(1);

  if (dump_mode && !info_only) {
#ifdef IRIX
    CDFRAME buf;
    if (dump_mode==2) {
      if (cdp) {
	CDaddcallback(cdp, cd_audio, (CDCALLBACKFUNC)playaudio, 0);
      } else die("No audioparser");
    }
#endif
    fprintf(stderr,"Dumping %d sector(s) starting from LBA=%d\n",
	    dump_count,dump_start);
    for (i=dump_start;i<dump_start+dump_count;i++) {
      len=buffersize;
      read_10(i,1,buffer,&len);
      if (len<init_bsize) break;

#ifdef IRIX
      if (dump_mode==2) {
	memcpy(&buf,buffer,CDDA_BLOCKSIZE);
	CDparseframe(cdp,&buf);
      }
#endif
	
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
    for (i=0;i<(reply[3]-reply[2]+1);i++) {
      o=4+i*8;
      if (reply[o+1]&DATA_TRACK) { trackno=i+1; break; }
    }
    if (trackno==0) die("No data track(s) found.");
  }

  fprintf(stderr,"Reading track %d...\n",trackno); 

  if ( (trackno < reply[2]) || (trackno > reply[3]) )
    die("Invalid track specified.");
    
  if ( ((reply[(trackno-1)*8+4+1]&DATA_TRACK)==0) ) {
    fprintf(stderr,"Not a data track.\n");
    mode_select(AUDIOBLOCKSIZE,0x82);
    if (mode_sense(reply,&replylen)!=0) die("cannot get sense data");
    drive_block_size=V3(&reply[9]);
    fprintf(stderr,"Selecting CD-DA mode, output file format: %s\n",
	    file_format==AF_FILE_AIFFC?"AIFFC":"AIFF");
    audio_track=1;
  } else {
    audio_track=0;
  }


  start=V4(&reply[(trackno-1)*8+4+4]);
  stop=V4(&reply[(trackno)*8+4+4]);
  tracksize=abs(stop-start);
  /* if (verbose_mode) printf("Start LBA=%d\nStop  LBA=%d\n",start,stop); */

  len=buffersize;
  read_10(start-0,1,buffer,&len);
  /* PRINT_BUF(buffer,32); */

  
  if (!audio_track) {
    /* read the iso9660 primary descriptor */
    fprintf(stderr,"Reading ISO9660 primary descriptor...\n");
    len=buffersize;
    read_10(start+16,1,buffer,&len);
    if (len<sizeof(ipd)) die("cannot read iso9660 primary descriptor.");
    memcpy(&ipd,buffer,sizeof(ipd));
    
    imagesize=ISONUM(ipd.volume_space_size);
    
    /* we should really check here if we really got a valid primary 
       descriptor or not... */
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
	fprintf(stderr,
		"Using track size from TOC record (%d blocks) instead.\n", 
		imagesize);
	fprintf(stderr,
		"(option -f can be used to override this behaviour.)\n");
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
      
      printf("Image size:        %02d:%02d:%02d, %d blocks (%ld bytes)\n",
	      LBA_MIN(ISONUM(ipd.volume_space_size)),
	      LBA_SEC(ISONUM(ipd.volume_space_size)),
	      LBA_FRM(ISONUM(ipd.volume_space_size)),
	      ISONUM(ipd.volume_space_size),
	      (long)ISONUM(ipd.volume_space_size)*BLOCKSIZE
	     );
      printf("Track size:        %02d:%02d:%02d, %d blocks (%ld bytes)\n",
	      LBA_MIN(tracksize),
	      LBA_SEC(tracksize),
	      LBA_FRM(tracksize),
	      tracksize,
	      (long)tracksize*BLOCKSIZE
	     );
    }
  } else { 
#ifdef IRIX
    /* if reading audio track */
    imagesize=tracksize;
    imagesize_bytes=imagesize*CDDA_DATASIZE;
    buffersize = READBLOCKS*AUDIOBLOCKSIZE;
    readblocksize = AUDIOBLOCKSIZE;

    if (cdp) {
      CDaddcallback(cdp, cd_audio, (CDCALLBACKFUNC)playaudio, 0);
    } else die("No audioparser");
    
    fclose(outfile);
    aiffsetup=AFnewfilesetup();
    AFinitrate(aiffsetup,AF_DEFAULT_TRACK,44100.0); /* 44.1 kHz */
    AFinitfilefmt(aiffsetup,file_format);           /* set file format */
    AFinitchannels(aiffsetup,AF_DEFAULT_TRACK,2);   /* stereo */
    AFinitsampfmt(aiffsetup,AF_DEFAULT_TRACK,
		  AF_SAMPFMT_TWOSCOMP,16);          /* 16bit */
    aiffoutfile=AFopenfile(argv[optind],"w",aiffsetup);
    if (!aiffoutfile) die("Cannot open target file (%s).",argv[optind]);
#endif
  }

  /* read the image */

  if (md5_mode) MD5Init(MD5);

  if (!info_only) {
    start_time=(int)time(NULL);
    fprintf(stderr,"Reading %s (%ldMb)...\n",
	    audio_track?"audio track":"ISO9660 image",
	    imagesize_bytes/(1024*1024));

    do {
      len=buffersize;
      if(readsize/readblocksize+READBLOCKS>imagesize) {
	read_10(start+counter,imagesize-readsize/readblocksize,buffer,&len);
      }
      else
	read_10(start+counter,READBLOCKS,buffer,&len);
      if ((counter%(1024*1024/readblocksize))<READBLOCKS) {
	cur_time=(int)time(NULL);
	if ((cur_time-start_time)>0) {
	  kbps=(readsize/1024)/(cur_time-start_time);
	} else {
	  kbps=0;
	}
	
	fprintf(stderr,"%3dM of %dM read. (%d kb/s)         \r",
		counter/512,imagesize/512,kbps);
      }
      counter+=READBLOCKS;
      readsize+=len;
      if (!audio_track) {
	fwrite(buffer,len,1,outfile);
      } else {
#ifdef IRIX
	/* audio track */
	for(i=0;i<(len/CDDA_BLOCKSIZE);i++) {
	  CDparseframe(cdp,(CDFRAME*)&buffer[i*CDDA_BLOCKSIZE]);
	}
#endif
      }
      if (md5_mode) MD5Update(MD5,buffer,(readsize>imagesize_bytes?
				       len-(readsize-imagesize_bytes):len) );
    } while (len==readblocksize*READBLOCKS&&readsize<imagesize*readblocksize);
    
    fprintf(stderr,"\n");
    if (!audio_track) {
      if (readsize > imagesize_bytes) 
	ftruncate(fileno(outfile),imagesize_bytes);
      if (readsize < imagesize_bytes) 
	fprintf(stderr,"Image not complete!\n");
      else fprintf(stderr,"Image complete.\n");
      fclose(outfile);
    } else {
#ifdef IRIX
      AFclosefile(aiffoutfile);
#endif
    }
  }

  if (md5_mode && !info_only) {
    MD5Final((unsigned char*)digest,MD5);
    md2str((unsigned char*)digest,digest_text);
    fprintf(stderr,"MD5 (%s) = %s\n",(md5_mode==2?"'image'":argv[optind]),
	    digest_text);
  }

 quit:
  start_stop(0);
  /* set_removable(1); */

  /* close the scsi device */
  scsi_close();

  return 0;
}




