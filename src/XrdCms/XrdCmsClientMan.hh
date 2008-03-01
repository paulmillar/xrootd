#ifndef __CMS_CLIENTMAN__
#define __CMS_CLIENTMAN__
/******************************************************************************/
/*                                                                            */
/*                    X r d C m s C l i e n t M a n . h h                     */
/*                                                                            */
/* (c) 2007 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*                            All Rights Reserved                             */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*              DE-AC02-76-SFO0515 with the Department of Energy              */
/******************************************************************************/
  
//          $Id$

// Based on: XrdOdcManager.hh,v 1.6 2006/09/26 07:49:14 abh

#include <stdio.h>
#include <sys/uio.h>

#include "XProtocol/YProtocol.hh"

#include "XrdCms/XrdCmsResp.hh"
#include "XrdNet/XrdNetBuffer.hh"
#include "XrdOuc/XrdOucErrInfo.hh"
#include "XrdSys/XrdSysPthread.hh"

class XrdLink;
class XrdNetBuffer;
class XrdOdcFinder;

class XrdCmsClientMan
{
public:

static char          doDebug;
static char          v1Mode;
static XrdOdcFinder *oldFinder;

int                  delayResp(XrdOucErrInfo &Resp);

inline int           isActive() {return Active;}

XrdCmsClientMan     *nextManager() {return Next;}

char                *Name() {return Host;}
char                *NPfx() {return HPfx;}

int                  Send(char *msg, int mlen=0);
int                  Send(const struct iovec *iov, int iovcnt, int iotot=0);

void                *Start();

inline int           Suspended() {if (Suspend) chkStatus(); return Suspend;}

void                 setNext(XrdCmsClientMan *np) {Next = np;}

static void          setConfig(char *cfn) {ConfigFN = cfn;}

void                 whatsUp(const char *user, const char *path);

inline int           waitTime() {return repWait;}

                  XrdCmsClientMan(char *host, int port, int cw, int nr, int rw);
                 ~XrdCmsClientMan();

private:
int   Hookup();
int   Receive();
void  relayResp();
void  chkStatus();
void  setStatus();

static XrdSysMutex   manMutex;
static XrdNetBufferQ BuffQ;
static char         *ConfigFN;
static const int     chkVal = 256;

XrdSysSemaphore   syncResp;
XrdCmsRespQ       RespQ;

XrdCmsClientMan  *Next;
XrdSysMutex       myData;
XrdLink          *Link;
char             *Host;
char             *HPfx;
int               Port;
int               manMask;
int               dally;
int               Active;
int               Silent;
int               Suspend;
int               RecvCnt;
int               nrMax;
int               maxMsgID;
int               repWait;
int               repWMax;
int               chkCount;
time_t            lastUpdt;
XrdCms::CmsRRHdr  Response;
XrdNetBuffer     *NetBuff;
};
#endif
