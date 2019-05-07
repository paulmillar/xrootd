/******************************************************************************/
/*                                                                            */
/*                             X r d P s s . c c                              */
/*                                                                            */
/* (c) 2007 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*                DE-AC02-76-SFO0515 with the Deprtment of Energy             */
/*                                                                            */
/* This file is part of the XRootD software suite.                            */
/*                                                                            */
/* XRootD is free software: you can redistribute it and/or modify it under    */
/* the terms of the GNU Lesser General Public License as published by the     */
/* Free Software Foundation, either version 3 of the License, or (at your     */
/* option) any later version.                                                 */
/*                                                                            */
/* XRootD is distributed in the hope that it will be useful, but WITHOUT      */
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public       */
/* License for more details.                                                  */
/*                                                                            */
/* You should have received a copy of the GNU Lesser General Public License   */
/* along with XRootD in a file called COPYING.LESSER (LGPL license) and file  */
/* COPYING (GPL license).  If not, see <http://www.gnu.org/licenses/>.        */
/*                                                                            */
/* The copyright holder's institutional names and contributor's names may not */
/* be used to endorse or promote products derived from this software without  */
/* specific prior written permission of the institution or contributor.       */
/******************************************************************************/

/******************************************************************************/
/*                             I n c l u d e s                                */
/******************************************************************************/
  
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <strings.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __solaris__
#include <sys/vnode.h>
#endif

#include "XrdVersion.hh"

#include "XrdFfs/XrdFfsPosix.hh"
#include "XrdNet/XrdNetSecurity.hh"
#include "XrdPss/XrdPss.hh"
#include "XrdPss/XrdPssTrace.hh"
#include "XrdPss/XrdPssUrlInfo.hh"
#include "XrdPosix/XrdPosixConfig.hh"
#include "XrdPosix/XrdPosixInfo.hh"
#include "XrdPosix/XrdPosixXrootd.hh"

#include "XrdOss/XrdOssError.hh"
#include "XrdOuc/XrdOucEnv.hh"
#include "XrdOuc/XrdOucExport.hh"
#include "XrdSec/XrdSecEntity.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdSys/XrdSysHeaders.hh"
#include "XrdSys/XrdSysPlatform.hh"

/******************************************************************************/
/*                               D e f i n e s                                */
/******************************************************************************/

#define isNOSTAGE(_x_) !(XRDEXP_STAGE & XrdPssSys::XPList.Find(_x_))

#define isREADONLY(_x_) (XRDEXP_NOTRW & XrdPssSys::XPList.Find(_x_))

/******************************************************************************/
/*                               G l o b a l s                                */
/******************************************************************************/

class XrdScheduler;

namespace XrdProxy
{
static XrdPssSys   XrdProxySS;
  
       XrdSysError eDest(0, "pss_");

       XrdScheduler *schedP = 0;

       XrdOucSid    *sidP   = 0;

       XrdOucEnv    *envP   = 0;

static const char *ofslclCGI = "ofs.lcl=1";

static const char *osslclCGI = "oss.lcl=1";

static const int   PBsz = 4096;

       XrdSysTrace SysTrace("Pss",0);
}

using namespace XrdProxy;

/******************************************************************************/
/*                XrdOssGetSS (a.k.a. XrdOssGetStorageSystem)                 */
/******************************************************************************/

XrdVERSIONINFO(XrdOssGetStorageSystem,XrdPss);
  
// This function is called by the OFS layer to retrieve the Storage System
// object. We return our proxy storage system object if configuration succeeded.
//
extern "C"
{
XrdOss *XrdOssGetStorageSystem2(XrdOss       *native_oss,
                                XrdSysLogger *Logger,
                                const char   *cFN,
                                const char   *parms,
                                XrdOucEnv    *envp)
{

// Ignore the parms (we accept none for now) and call the init routine
//
   envP = envp;
   return (XrdProxySS.Init(Logger, cFN) ? 0 : (XrdOss *)&XrdProxySS);
}
}
 
/******************************************************************************/
/*                      o o s s _ S y s   M e t h o d s                       */
/******************************************************************************/
/******************************************************************************/
/*                           C o n s t r u c t o r                            */
/******************************************************************************/
  
XrdPssSys::XrdPssSys() : LocalRoot(0), theN2N(0), DirFlags(0),
                         myVersion(&XrdVERSIONINFOVAR(XrdOssGetStorageSystem))
                         {}

/******************************************************************************/
/*                                  i n i t                                   */
/******************************************************************************/
  
/*
  Function: Initialize proxy subsystem

  Input:    None

  Output:   Returns zero upon success otherwise (-errno).
*/
int XrdPssSys::Init(XrdSysLogger *lp, const char *cFN)
{
   int NoGo;
   const char *tmp;

// Do the herald thing
//
   SysTrace.SetLogger(lp);
   eDest.logger(lp);
   eDest.Say("Copr.  2019, Stanford University, Pss Version " XrdVSTRING);

// Initialize the subsystems
//
   tmp = ((NoGo = Configure(cFN)) ? "failed." : "completed.");
   eDest.Say("------ Proxy storage system initialization ", tmp);

// All done.
//
   return NoGo;
}

/******************************************************************************/
/*                                 C h m o d                                  */
/******************************************************************************/
/*
  Function: Change file mode.

  Input:    path        - Is the fully qualified name of the target file.
            mode        - The new mode that the file is to have.
            envP        - Environmental information.

  Output:   Returns XrdOssOK upon success and -errno upon failure.

  Notes:    This function is currently unsupported.
*/

int XrdPssSys::Chmod(const char *path, mode_t mode, XrdOucEnv *eP)
{
// We currently do not support chmod()
//
   return -ENOTSUP;
}

/******************************************************************************/
/*                                c r e a t e                                 */
/******************************************************************************/

/*
  Function: Create a file named `path' with 'file_mode' access mode bits set.

  Input:    path        - The fully qualified name of the file to create.
            access_mode - The Posix access mode bits to be assigned to the file.
                          These bits correspond to the standard Unix permission
                          bits (e.g., 744 == "rwxr--r--").
            env         - Environmental information.
            opts        - Set as follows:
                          XRDOSS_mkpath - create dir path if it does not exist.
                          XRDOSS_new    - the file must not already exist.
                          x00000000     - x are standard open flags (<<8)

  Output:   Returns XrdOssOK upon success; (-errno) otherwise.

  Notes:    We always return ENOTSUP as we really want the create options to be
            promoted to the subsequent open().
*/
int XrdPssSys::Create(const char *tident, const char *path, mode_t Mode,
                        XrdOucEnv &env, int Opts)
{

   return -ENOTSUP;
}

/******************************************************************************/
/*                               E n v I n f o                                */
/******************************************************************************/
  
void        XrdPssSys::EnvInfo(XrdOucEnv *envP)
{
// We only need to extract the scheduler pointer from the environment. Propogate
// the information to the POSIX layer.
//
   if (envP)
      {schedP = (XrdScheduler *)envP->GetPtr("XrdScheduler*");
       XrdPosixConfig::EnvInfo(*envP);
      }
}
  
/******************************************************************************/
/*                               L f n 2 P f n                                */
/******************************************************************************/
  
int         XrdPssSys::Lfn2Pfn(const char *oldp, char *newp, int blen)
{
    if (theN2N) return -(theN2N->lfn2pfn(oldp, newp, blen));
    if ((int)strlen(oldp) >= blen) return -ENAMETOOLONG;
    strcpy(newp, oldp);
    return 0;
}

const char *XrdPssSys::Lfn2Pfn(const char *oldp, char *newp, int blen, int &rc)
{
    if (!theN2N) {rc = 0; return oldp;}
    if ((rc = -(theN2N->lfn2pfn(oldp, newp, blen)))) return 0;
    return newp;
}

/******************************************************************************/
/*                                 M k d i r                                  */
/******************************************************************************/
/*
  Function: Create a directory

  Input:    path        - Is the fully qualified name of the new directory.
            mode        - The new mode that the directory is to have.
            mkpath      - If true, makes the full path.
            envP        - Environmental information.

  Output:   Returns XrdOssOK upon success and -errno upon failure.

  Notes:    Directories are only created in the local disk cache.
            Currently, we do not propogate the mkpath option.
*/

int XrdPssSys::Mkdir(const char *path, mode_t mode, int mkpath, XrdOucEnv *eP)
{
   EPNAME("Mkdir");
   XrdPssUrlInfo uInfo(eP, path);
   int rc;
   char pbuff[PBsz];

// Verify we can write here
//
   if (isREADONLY(path)) return -EROFS;

// Convert path to URL
//
   if ((rc = P2URL(pbuff, PBsz, uInfo, xLfn2Pfn))) return rc;

// Some tracing
//
   DEBUG(uInfo.Tident(),"url="<<pbuff);

// Simply return the proxied result here
//
   return (XrdPosixXrootd::Mkdir(pbuff, mode) ? -errno : XrdOssOK);
}
  
/******************************************************************************/
/*                                R e m d i r                                 */
/******************************************************************************/

/*
  Function: Removes the directory 'path'

  Input:    path      - Is the fully qualified name of the directory to remove.
            envP      - Environmental information.

  Output:   Returns XrdOssOK upon success and -errno upon failure.
*/
int XrdPssSys::Remdir(const char *path, int Opts, XrdOucEnv *eP)
{
   EPNAME("Remdir");
   const char *Cgi = "";
   int rc;
   char pbuff[PBsz];

// Verify we can write here
//
   if (isREADONLY(path)) return -EROFS;

// Setup any required cgi information
//
   if (*path == '/' && !outProxy && (Opts & XRDOSS_Online)) Cgi = ofslclCGI;

// Setup url information
//
   XrdPssUrlInfo uInfo(eP, path, Cgi);

// Convert path to URL
//
   if ((rc = P2URL(pbuff, PBsz, uInfo, xLfn2Pfn))) return rc;

// Do some tracing
//
   DEBUG(uInfo.Tident(),"url="<<pbuff);

// Issue unlink and return result
//
   return (XrdPosixXrootd::Rmdir(pbuff) ? -errno : XrdOssOK);
}

/******************************************************************************/
/*                                R e n a m e                                 */
/******************************************************************************/

/*
  Function: Renames a file with name 'old_name' to 'new_name'.

  Input:    old_name  - Is the fully qualified name of the file to be renamed.
            new_name  - Is the fully qualified name that the file is to have.
            old_envP  - Environmental information for old_name.
            new_envP  - Environmental information for new_name.

  Output:   Returns XrdOssOK upon success and -errno upon failure.
*/
int XrdPssSys::Rename(const char *oldname, const char *newname,
                      XrdOucEnv  *oldenvP, XrdOucEnv  *newenvP)
{
   EPNAME("Rename");
   int rc;
   char oldName[PBsz], newName[PBsz];

// Verify we can write in the source and target
//
   if (isREADONLY(oldname) || isREADONLY(newname)) return -EROFS;

// Setup url info
//
   XrdPssUrlInfo uInfoOld(oldenvP, oldname);
   XrdPssUrlInfo uInfoNew(newenvP, newname, "", true, false);

// Convert path to URL
//
   if ((rc = P2URL(oldName, PBsz, uInfoOld, xLfn2Pfn))
   ||  (rc = P2URL(newName, PBsz, uInfoNew, xLfn2Pfn))) return rc;

// Do some tracing
//
   DEBUG(uInfoOld.Tident(),"old url="<<oldName <<" new url=" <<newName);

// Execute the rename and return result
//
   return (XrdPosixXrootd::Rename(oldName, newName) ? -errno : XrdOssOK);
}

/******************************************************************************/
/*                                 s t a t                                    */
/******************************************************************************/

/*
  Function: Determine if file 'path' actually exists.

  Input:    path        - Is the fully qualified name of the file to be tested.
            buff        - pointer to a 'stat' structure to hold the attributes
                          of the file.
            Opts        - stat() options.
            envP        - Environmental information.

  Output:   Returns XrdOssOK upon success and -errno upon failure.

  Notes:    The XRDOSS_resonly flag in Opts is not supported.
*/

int XrdPssSys::Stat(const char *path, struct stat *buff, int Opts, XrdOucEnv *eP)
{
   EPNAME("Stat");
   const char *Cgi = "";
   int rc;
   char pbuff[PBsz];

// Setup any required special cgi information
//
   if (*path == '/' && !outProxy && ((Opts & XRDOSS_resonly)||isNOSTAGE(path)))
      Cgi = osslclCGI;

// We can now establish the url information to be used
//
   XrdPssUrlInfo uInfo(eP, path, Cgi);

// Generate an ID if we need to
//
   if (sidP) uInfo.setID(sidP);

// Convert path to URL
//
   if ((rc = P2URL(pbuff, PBsz, uInfo, xLfn2Pfn))) return rc;

// Do some tracing
//
   DEBUG(uInfo.Tident(),"url="<<pbuff);

// Return proxied stat
//
   return (XrdPosixXrootd::Stat(pbuff, buff) ? -errno : XrdOssOK);
}

/******************************************************************************/
/*                              T r u n c a t e                               */
/******************************************************************************/
/*
  Function: Truncate a file.

  Input:    path        - Is the fully qualified name of the target file.
            flen        - The new size that the file is to have.
            envP        - Environmental information.

  Output:   Returns XrdOssOK upon success and -errno upon failure.
*/

int XrdPssSys::Truncate(const char *path, unsigned long long flen,
                        XrdOucEnv *envP)
{
   EPNAME("Trunc");
   XrdPssUrlInfo uInfo(envP, path);
   int rc;
   char pbuff[PBsz];

// Make sure we can write here
//
   if (isREADONLY(path)) return -EROFS;

// Convert path to URL
//
   if ((rc = P2URL(pbuff, PBsz, uInfo, xLfn2Pfn))) return rc;

// Do some tracing
//
   DEBUG(uInfo.Tident(),"url="<<pbuff);

// Return proxied truncate. We only do this on a single machine because the
// redirector will forbid the trunc() if multiple copies exist.
//
   return (XrdPosixXrootd::Truncate(pbuff, flen) ? -errno : XrdOssOK);
}
  
/******************************************************************************/
/*                                U n l i n k                                 */
/******************************************************************************/

/*
  Function: Delete a file from the namespace and release it's data storage.

  Input:    path      - Is the fully qualified name of the file to be removed.
            envP      - Environmental information.

  Output:   Returns XrdOssOK upon success and -errno upon failure.
*/
int XrdPssSys::Unlink(const char *path, int Opts, XrdOucEnv *envP)
{
   EPNAME("Unlink");
   const char *Cgi = "";
   int rc;
   char pbuff[PBsz];

// Make sure we can write here
//
   if (isREADONLY(path)) return -EROFS;

// Setup any required cgi information
//
   if (*path == '/' && !outProxy && (Opts & XRDOSS_Online)) Cgi = ofslclCGI;

// Setup url info
//
   XrdPssUrlInfo uInfo(envP, path, Cgi);

// Convert path to URL
//
   if ((rc = P2URL(pbuff, PBsz, uInfo, xLfn2Pfn))) return rc;

// Do some tracing
//
   DEBUG(uInfo.Tident(),"url="<<pbuff);

// Unlink the file and return result.
//
   return (XrdPosixXrootd::Unlink(pbuff) ? -errno : XrdOssOK);
}

/******************************************************************************/
/*                        P s s D i r   M e t h o d s                         */
/******************************************************************************/
/******************************************************************************/
/*                               o p e n d i r                                */
/******************************************************************************/
  
/*
  Function: Open the directory `path' and prepare for reading.

  Input:    path      - The fully qualified name of the directory to open.
            envP      - Environmental information.

  Output:   Returns XrdOssOK upon success; (-errno) otherwise.
*/
int XrdPssDir::Opendir(const char *dir_path, XrdOucEnv &Env)
{
   EPNAME("Opendir");
   int rc;
   char pbuff[PBsz];

// Return an error if this object is already open
//
   if (myDir) return -XRDOSS_E8001;

// Open directories are not supported for object id's
//
   if (*dir_path != '/') return -ENOTSUP;

// Setup url info
//
   XrdPssUrlInfo uInfo(&Env, dir_path);
   uInfo.setID();

// Convert path to URL
//
   if ((rc = XrdPssSys::P2URL(pbuff, PBsz, uInfo, XrdPssSys::xLfn2Pfn)))
      return rc;

// Do some tracing
//
   DEBUG(uInfo.Tident(),"url="<<pbuff);

// Open the directory
//
   myDir = XrdPosixXrootd::Opendir(pbuff);
   if (!myDir) return -errno;
   return XrdOssOK;
}

/******************************************************************************/
/*                               r e a d d i r                                */
/******************************************************************************/

/*
  Function: Read the next entry if directory associated with this object.

  Input:    buff       - Is the address of the buffer that is to hold the next
                         directory name.
            blen       - Size of the buffer.

  Output:   Upon success, places the contents of the next directory entry
            in buff. When the end of the directory is encountered buff
            will be set to the null string.

            Upon failure, returns a (-errno).

  Warning: The caller must provide proper serialization.
*/
int XrdPssDir::Readdir(char *buff, int blen)
{
// Check if we are directly reading the directory
//
   if (myDir)
      {dirent *entP, myEnt;
       int    rc = XrdPosixXrootd::Readdir_r(myDir, &myEnt, &entP);
       if (rc) return -rc;
       if (!entP) *buff = 0;
          else strlcpy(buff, myEnt.d_name, blen);
       return XrdOssOK;
      }

// The directory is not open
//
   return -XRDOSS_E8002;
}

/******************************************************************************/
/*                                 C l o s e                                  */
/******************************************************************************/
  
/*
  Function: Close the directory associated with this object.

  Input:    None.

  Output:   Returns XrdOssOK upon success and (errno) upon failure.
*/
int XrdPssDir::Close(long long *retsz)
{
   DIR *theDir;

// Close the directory proper if it exists. POSIX specified that directory
// stream is no longer available after closedir() regardless if return value.
//
   if ((theDir = myDir))
      {myDir = 0;
       if (XrdPosixXrootd::Closedir(theDir)) return -errno;
       return XrdOssOK;
      }

// Directory is not open
//
   return -XRDOSS_E8002;
}

/******************************************************************************/
/*                     o o s s _ F i l e   M e t h o d s                      */
/******************************************************************************/
/******************************************************************************/
/*                                  o p e n                                   */
/******************************************************************************/

#define IS_FWDPATH(x) (!strncmp("/xroot:/",x,8) || !strncmp("/root:/",x,7))

/*
  Function: Open the file `path' in the mode indicated by `Mode'.

  Input:    path      - The fully qualified name of the file to open.
            Oflag     - Standard open flags.
            Mode      - Create mode (i.e., rwx).
            env       - Environmental information.

  Output:   XrdOssOK upon success; -errno otherwise.
*/
int XrdPssFile::Open(const char *path, int Oflag, mode_t Mode, XrdOucEnv &Env)
{
   EPNAME("Open");
   unsigned long long popts = XrdPssSys::XPList.Find(path);
   const char *Cgi = "";
   char pbuff[PBsz];
   int  rc;
   bool tpcMode = (Oflag & O_NOFOLLOW) != 0;
   bool rwMode  = (Oflag & (O_WRONLY | O_RDWR | O_APPEND)) != 0;
   bool ucgiOK  = true;
   bool ioCache = (Oflag & O_DIRECT);

// Turn off direct flag if set (we record it separately
//
   if (ioCache) Oflag &= ~O_DIRECT;

// Return an error if the object is already open
//
   if (fd >= 0 || tpcPath) return -XRDOSS_E8003;

// If we are opening this in r/w mode make sure we actually can
//
   if (rwMode && (popts & XRDEXP_NOTRW))
      {if (popts & XRDEXP_FORCERO && !tpcMode) Oflag = O_RDONLY;
          else return -EROFS;
      }

// If this is a third party copy open, then strange rules apply. If this is an
// outgoing proxy we let everything pass through as this may be a TPC request
// elsewhere.  Otherwise, if it's an open for reading, we open the file but
// strip off all CGI (technically, we should only remove the "tpc" tokens)
// because the source might not support direct TPC mode. If we are opening for
// writing, then we skip the open and mark this as a TPC handle which can only
// be used for fstat() and close(). Any other actions return an error.
//
   if (tpcMode)
      {Oflag &= ~O_NOFOLLOW;
       if (!XrdPssSys::outProxy || !IS_FWDPATH(path))
          {if (rwMode) {tpcPath = strdup(path); return XrdOssOK;}
           ucgiOK = false;
          }
      }

// Setup any required cgi information. Don't mess with it if it's an objectid
// or if the we are an outgoing proxy server.
//
   if (!XrdPssSys::outProxy && *path == '/' && !(XRDEXP_STAGE & popts))
      Cgi = osslclCGI;

// Construct the url info
//
   XrdPssUrlInfo uInfo(&Env, path, Cgi, ucgiOK);
   uInfo.setID();

// Convert path to URL
//
   if ((rc = XrdPssSys::P2URL(pbuff, PBsz, uInfo, XrdPssSys::xLfn2Pfn)))
      return rc;

// Do some tracing
//
   DEBUG(uInfo.Tident(),"url="<<pbuff);

// Try to open and if we failed, return an error
//
   if (!XrdPssSys::dcaCheck || !ioCache)
      {if ((fd = XrdPosixXrootd::Open(pbuff,Oflag,Mode)) < 0) return -errno;
      } else {
       XrdPosixInfo Info;
       if (XrdPosixConfig::OpenFC(pbuff,Oflag,Mode,Info))
          {Env.Put("FileURL", Info.cacheURL);
           return -EDESTADDRREQ;
          }
       fd = Info.fileFD;
       if (fd < 0) return -errno;
      }

// All done
//
   return XrdOssOK;
}

/******************************************************************************/
/*                                 c l o s e                                  */
/******************************************************************************/

/*
  Function: Close the file associated with this object.

  Input:    None.

  Output:   Returns XrdOssOK upon success aud -errno upon failure.
*/
int XrdPssFile::Close(long long *retsz)
{   int rc;

// We don't support returning the size (we really should fix this)
//
    if (retsz) *retsz = 0;

// If the file is not open, then this may be OK if it is a 3rd party copy
//
    if (fd < 0)
       {if (!tpcPath) return -XRDOSS_E8004;
        free(tpcPath);
        tpcPath = 0;
        return XrdOssOK;
       }

// Close the file
//
    rc = XrdPosixXrootd::Close(fd);
    fd = -1;
    return (rc == 0 ? XrdOssOK : -errno);
}

/******************************************************************************/
/*                                  r e a d                                   */
/******************************************************************************/

/*
  Function: Preread `blen' bytes from the associated file.

  Input:    offset    - The absolute 64-bit byte offset at which to read.
            blen      - The size to preread.

  Output:   Returns zero read upon success and -errno upon failure.
*/

ssize_t XrdPssFile::Read(off_t offset, size_t blen)
{
     if (fd < 0) return (ssize_t)-XRDOSS_E8004;

     return 0;  // We haven't implemented this yet!
}


/******************************************************************************/
/*                                  r e a d                                   */
/******************************************************************************/

/*
  Function: Read `blen' bytes from the associated file, placing in 'buff'
            the data and returning the actual number of bytes read.

  Input:    buff      - Address of the buffer in which to place the data.
            offset    - The absolute 64-bit byte offset at which to read.
            blen      - The size of the buffer. This is the maximum number
                        of bytes that will be read.

  Output:   Returns the number bytes read upon success and -errno upon failure.
*/

ssize_t XrdPssFile::Read(void *buff, off_t offset, size_t blen)
{
     ssize_t retval;

     if (fd < 0) return (ssize_t)-XRDOSS_E8004;

     return (retval = XrdPosixXrootd::Pread(fd, buff, blen, offset)) < 0
            ? (ssize_t)-errno : retval;
}

/******************************************************************************/
/*                                  r e a d v                                 */
/******************************************************************************/

ssize_t XrdPssFile::ReadV(XrdOucIOVec     *readV,     // In
                          int              readCount) // In
/*
  Function: Perform all the reads specified in the readV vector.

  Input:    readV     - A description of the reads to perform; includes the
                        absolute offset, the size of the read, and the buffer
                        to place the data into.
            readCount - The size of the readV vector.

  Output:   Returns the number of bytes read upon success and -errno upon failure.
            If the number of bytes read is less than requested, it is considered
            an error.
*/
{
    ssize_t retval;

    if (fd < 0) return (ssize_t)-XRDOSS_E8004;

    return (retval = XrdPosixXrootd::VRead(fd, readV, readCount)) < 0 ? (ssize_t)-errno : retval;;
}

/******************************************************************************/
/*                               R e a d R a w                                */
/******************************************************************************/

/*
  Function: Read `blen' bytes from the associated file, placing in 'buff'
            the data and returning the actual number of bytes read.

  Input:    buff      - Address of the buffer in which to place the data.
            offset    - The absolute 64-bit byte offset at which to read.
            blen      - The size of the buffer. This is the maximum number
                        of bytes that will be read.

  Output:   Returns the number bytes read upon success and -errno upon failure.
*/

ssize_t XrdPssFile::ReadRaw(void *buff, off_t offset, size_t blen)
{
     return Read(buff, offset, blen);
}

/******************************************************************************/
/*                                 w r i t e                                  */
/******************************************************************************/

/*
  Function: Write `blen' bytes to the associated file, from 'buff'
            and return the actual number of bytes written.

  Input:    buff      - Address of the buffer from which to get the data.
            offset    - The absolute 64-bit byte offset at which to write.
            blen      - The number of bytes to write from the buffer.

  Output:   Returns the number of bytes written upon success and -errno o/w.
*/

ssize_t XrdPssFile::Write(const void *buff, off_t offset, size_t blen)
{
     ssize_t retval;

     if (fd < 0) return (ssize_t)-XRDOSS_E8004;

     return (retval = XrdPosixXrootd::Pwrite(fd, buff, blen, offset)) < 0
            ? (ssize_t)-errno : retval;
}

/******************************************************************************/
/*                                 f s t a t                                  */
/******************************************************************************/

/*
  Function: Return file status for the associated file.

  Input:    buff      - Pointer to buffer to hold file status.

  Output:   Returns XrdOssOK upon success and -errno upon failure.
*/

int XrdPssFile::Fstat(struct stat *buff)
{
    if (fd < 0)
       {if (!tpcPath) return -XRDOSS_E8004;
        if (XrdProxySS.Stat(tpcPath, buff))
           memset(buff, 0, sizeof(struct stat));
        return XrdOssOK;
       }

    return (XrdPosixXrootd::Fstat(fd, buff) ? -errno : XrdOssOK);
}

/******************************************************************************/
/*                               f s y n c                                    */
/******************************************************************************/

/*
  Function: Synchronize associated file.

  Input:    None.

  Output:   Returns XrdOssOK upon success and -errno upon failure.
*/
int XrdPssFile::Fsync(void)
{
    if (fd < 0) return -XRDOSS_E8004;

    return (XrdPosixXrootd::Fsync(fd) ? -errno : XrdOssOK);
}

/******************************************************************************/
/*                             f t r u n c a t e                              */
/******************************************************************************/

/*
  Function: Set the length of associated file to 'flen'.

  Input:    flen      - The new size of the file.

  Output:   Returns XrdOssOK upon success and -errno upon failure.

  Notes:    If 'flen' is smaller than the current size of the file, the file
            is made smaller and the data past 'flen' is discarded. If 'flen'
            is larger than the current size of the file, a hole is created
            (i.e., the file is logically extended by filling the extra bytes 
            with zeroes).

            If compiled w/o large file support, only lower 32 bits are used.
            used.

            Currently not supported for proxies.
*/
int XrdPssFile::Ftruncate(unsigned long long flen)
{
    if (fd < 0) return -XRDOSS_E8004;

    return (XrdPosixXrootd::Ftruncate(fd, flen) ?  -errno : XrdOssOK);
}

/******************************************************************************/
/*                               g e t M m a p                                */
/******************************************************************************/
  
/*
  Function: Indicate whether or not file is memory mapped.

  Input:    addr      - Points to an address which will receive the location
                        memory where the file is mapped. If the address is
                        null, true is returned if a mapping exist.

  Output:   Returns the size of the file if it is memory mapped (see above).
            Otherwise, zero is returned and addr is set to zero.
*/
off_t XrdPssFile::getMmap(void **addr)   // Not Supported for proxies
{
   if (addr) *addr = 0;
   return 0;
}
  
/******************************************************************************/
/*                          i s C o m p r e s s e d                           */
/******************************************************************************/
  
/*
  Function: Indicate whether or not file is compressed.

  Input:    cxidp     - Points to a four byte buffer to hold the compression
                        algorithm used if the file is compressed or null.

  Output:   Returns the region size which is 0 if the file is not compressed.
            If cxidp is not null, the algorithm is returned only if the file
            is compressed.
*/
int XrdPssFile::isCompressed(char *cxidp)  // Not supported for proxies
{
    return 0;
}
  
/******************************************************************************/
/*                                 P 2 D S T                                  */
/******************************************************************************/

int XrdPssSys::P2DST(int &retc, char *hBuff, int hBlen, XrdPssSys::PolAct pEnt,
                     const char *path)
{
   const char *Slash;
   int n;

// Extract out the destination
//
   Slash = index(path, '/');
   if (!Slash || (n = (Slash - path)) == 0) {retc = -EINVAL; return 0;}
   if (n >= hBlen) {retc = -ENAMETOOLONG; return 0;}
   strncpy(hBuff, path, n); hBuff[n] = 0;

// Check if we need to authorize the outgoing connection
//
   if (Police[pEnt] && !Police[pEnt]->Authorize(hBuff))
      {retc = -EACCES; return 0;}

// All is well
//
   return n;
}

/******************************************************************************/
/*                                 P 2 O U T                                  */
/******************************************************************************/
  
int XrdPssSys::P2OUT(char *pbuff, int pblen, XrdPssUrlInfo &uInfo)
{  const char *theID = uInfo.getID();
   const char *pname, *path, *thePath;
   char  hBuff[288];
   int retc, n;

// Setup the path
//
   thePath = path = uInfo.thePath();

// Make sure the path is valid for an outgoing proxy
//
   if (*path == '/') path++;
   if ((pname = XrdPssSys::valProt(path, n, 1))) path += n;
      else {if (!hdrLen) return -ENOTSUP;
            n = snprintf(pbuff, pblen, hdrData, theID, thePath);
            if (n >= pblen || !uInfo.addCGI(pbuff+n, pblen-n))
               return -ENAMETOOLONG;
            return 0;
           }

// Objectid must be handled differently as they have not been refalgomized
//
   if (*thePath != '/')
      {if (*path == '/') 
          {path++;
           if (*path == '/') theID = "";
          }
       if (Police[PolObj] && !P2DST(retc, hBuff, sizeof(hBuff), PolObj,
                                    path+(*path == '/' ? 1:0))) return 0;
       n = snprintf(pbuff, pblen, "%s%s%s", pname, theID, path);
       if (n >= pblen || !uInfo.addCGI(pbuff+n, pblen-n))
          return -ENAMETOOLONG;
       return 0;
      }

// Extract out the destination. We need to do this because the front end
// will have extracted out double slashes and we need to add them back. We
// also authorize the outgoing connection if we need to in the process.
//
   if (!(n = P2DST(retc, hBuff, sizeof(hBuff), PolPath, path))) return 0;
   path += n;

// Create the new path
//
   n = snprintf(pbuff,pblen,"%s%s%s/%s",pname,theID,hBuff,path);

// Make sure the path will fit
//
   if (n >= pblen || !uInfo.addCGI(pbuff+n, pblen-n))
      return -ENAMETOOLONG;

// All done
//
   return 0;
}

/******************************************************************************/
/*                                 P 2 U R L                                  */
/******************************************************************************/
  
int XrdPssSys::P2URL(char *pbuff, int pblen, XrdPssUrlInfo &uInfo, bool doN2N)
{

// If this is an outgoing proxy then we need to do someother work
//
   if (outProxy) return P2OUT(pbuff, pblen, uInfo);

// Do url generation for actual known origin
//
   const char *path = uInfo.thePath();
   int   retc, pfxLen;
   char  Apath[MAXPATHLEN+1];

// Setup to process url generation
//
   path = uInfo.thePath();

// First, apply the N2N mapping if necessary. If N2N fails then the whole
// mapping fails and ENAMETOOLONG will be returned.
//
   if (doN2N && XrdProxySS.theN2N)
      {if ((retc = XrdProxySS.theN2N->lfn2pfn(path, Apath, sizeof(Apath))))
          {if (retc > 0) return -retc;}
       path = Apath;
      }

// Format the header into the buffer and check if we overflowed. Note that we
// defer substitution of the path as we need to know where the path is.
//
   pfxLen = snprintf(pbuff, pblen, hdrData, uInfo.getID(), path);
   if (pfxLen >= pblen) return -ENAMETOOLONG;

// Add any cgi information
//
   if (uInfo.hasCGI())
      {if (!uInfo.addCGI(pbuff+pfxLen, pblen-pfxLen)) return -ENAMETOOLONG;}

// All done
//
   return 0;
}
