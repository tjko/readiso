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
#include <stdarg.h>
#include <dslib.h>

#include "readiso.h"

static struct dsreq *dsp = 0;   /* handle to scsi device */


int scsi_open(const char *dev)
{
  dsp=dsopen(dev, O_RDWR);
  if (!dsp) return -1;
  return 0;
}

void scsi_close()
{
  if (dsp) dsclose(dsp);
  dsp=0;
}


int scsi_request(char *note, unsigned char *reply, int *replylen, 
		 int cmdlen, int datalen, int mode, ...)
{
  va_list args;
  int reply_len = 0;
  int result;

/* Irix... */
  int i;
  unsigned char *buf,*databuf;

  if (replylen) reply_len=*replylen;

  buf=(unsigned char*)CMDBUF(dsp);
  databuf=(unsigned char*)malloc(datalen);

  va_start(args,mode);
  for (i=0;i<cmdlen;i++) buf[i]=va_arg(args,unsigned int);
  for (i=0;i<datalen;i++) databuf[i]=va_arg(args,unsigned int);
  va_end(args);

  CMDBUF(dsp)=(caddr_t)buf;
  CMDLEN(dsp)=cmdlen;
  
  if (mode&SCSIR_READ) 
    filldsreq(dsp,reply,reply_len,DSRQ_READ|DSRQ_SENSE);
  else filldsreq(dsp,databuf,datalen,DSRQ_WRITE|DSRQ_SENSE);
  dsp->ds_time = 15*1000;
  result = doscsireq(getfd(dsp),dsp);

  if (RET(dsp) && RET(dsp) != DSRT_SHORT && !(mode&SCSIR_QUIET)) {
    fprintf(stderr,"%s status=%d ret=%xh sensesent=%d datasent=%d "
	    "senselen=%d\n", note, STATUS(dsp), RET(dsp),
	    SENSESENT(dsp), DATASENT(dsp), SENSELEN(dsp));

  }

  if (mode==SCSIR_READ) { if (replylen) *replylen=DATASENT(dsp); } 


  free(databuf);

  return result;
}


