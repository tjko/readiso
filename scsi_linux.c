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

#include "config.h"
#include <linux/version.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h> 

#include "readiso.h"

#define SCSI_HEADER_SIZE (sizeof(struct sg_header))
#define SCSI_BUFFER_SIZE (READBLOCKS*BLOCKSIZE+SCSI_HEADER_SIZE)

static int fd = -1;    /* file descriptor of the scsi device open */


int scsi_open(const char *dev)
{
  int i;

  i=open(dev,O_RDWR);
  if (i<0) return i;
  fd=i;

#if 0
  i=fcntl(fd,F_GETFL);
  fcntl(fd,F_SETFL,i|O_NONBLOCK);
  while(read(fd,reply,sizeof(reply))!=-1 || errno!=EAGAIN); /* empty buffer */
  fcntl(fd,F_SETFL,i&~O_NONBLOCK);
#endif

  return 0;
}

void scsi_close()
{
  if (fd < 0) return;
  close(fd);
  fd=-1;
}

int scsi_request(char *note, unsigned char *reply, int *replylen, 
		 int cmdlen, int datalen, int mode, ...)
{
  va_list args;
  int reply_len = 0;
  int result;

/* Linux... */

  static char  sg_outbuf[SCSI_BUFFER_SIZE];
  static char  sg_inbuf[SCSI_BUFFER_SIZE];
  struct sg_header *out_hdr = (struct sg_header *)sg_outbuf;
  struct sg_header *in_hdr = (struct sg_header *)sg_inbuf;

  int i,size,wasread;
  static int pack_id=0;

  if (replylen) reply_len=*replylen;

  memset(sg_outbuf,0,SCSI_BUFFER_SIZE);
  memset(sg_inbuf,0,SCSI_BUFFER_SIZE);

  size=SCSI_HEADER_SIZE+cmdlen+datalen;

  out_hdr->pack_len=size;
  out_hdr->reply_len=SCSI_HEADER_SIZE+reply_len;
  out_hdr->pack_id=++pack_id;
  out_hdr->result=0;
  

  va_start(args,mode);
  for (i=0;i<(cmdlen+datalen);i++) 
    sg_outbuf[SCSI_HEADER_SIZE+i]=va_arg(args,unsigned int);
  va_end(args);

  result = write(fd, sg_outbuf, size);
  if (result<0) {
    fprintf(stderr,"%s write error %d\n",note,result);
    return result;
  }
  else if (result!=size) {
    fprintf(stderr,"%s wrote only %dbytes of expected %dbytes\n",note,
	    result,size);
    return 2;
  }
  
  wasread=read(fd, sg_inbuf, SCSI_HEADER_SIZE+reply_len);
  if (wasread > SCSI_HEADER_SIZE) {
    if (replylen) {
      *replylen=wasread-SCSI_HEADER_SIZE;
      memcpy(reply,&sg_inbuf[SCSI_HEADER_SIZE],*replylen);
    }
  }
  else if (replylen) *replylen=0;
  
  /* HACK...Linux sg driver is rather stupid... */
  result=wasread<0 || wasread!=SCSI_HEADER_SIZE+reply_len || in_hdr->result ||
         in_hdr->sense_buffer[0]==0x70 || in_hdr->sense_buffer[0]==0x71;

  if ( (!(mode&SCSIR_QUIET) && result) || 0) {
    int i;
    fprintf(stderr,"SCSI error '%s' datareturned=%d result=%d\n",note,
	    wasread-SCSI_HEADER_SIZE,result);
    fprintf(stderr,"sense buffer: ");
    for (i=0;i<16;i++) fprintf(stderr,"%02x ", in_hdr->sense_buffer[i]);
    fprintf(stderr,"\n");
  }


  return result;
}

