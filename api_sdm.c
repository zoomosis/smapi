/*
 *  SMAPI; Modified Squish MSGAPI
 *
 *  Squish MSGAPI0 is copyright 1991 by Scott J. Dudley.  All rights reserved.
 *  Modifications released to the public domain.
 *
 *  Use of this file is subject to the restrictions contain in the Squish
 *  MSGAPI0 licence agreement.  Please refer to licence.txt for complete
 *  details of the licencing restrictions.  If you do not find the text
 *  of this agreement in licence.txt, or if you do not have this file,
 *  you should contact Scott Dudley at FidoNet node 1:249/106 or Internet
 *  e-mail Scott.Dudley@f106.n249.z1.fidonet.org.
 *
 *  In no event should you proceed to use any of the source files in this
 *  archive without having accepted the terms of the MSGAPI0 licensing
 *  agreement, or such other agreement as you are able to reach with the
 *  author.
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if !defined(UNIX) && !defined(SASC)
#include <io.h>
#endif

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#if !defined(UNIX) && !defined(SASC)
#include <share.h>
#endif

#ifdef __TURBOC__
#include <dos.h>
#endif

#define MSGAPI_HANDLERS

#include "prog.h"
#include "dr.h"
#include "alc.h"
#include "old_msg.h"
#include "msgapi.h"
#include "api_sdm.h"
#include "api_sdmp.h"
#include "apidebug.h"
#include "unused.h"

#define SDM_BLOCK 256
#define Mhd ((struct _sdmdata *)(mh->apidata))
#define MsghMhd ((struct _sdmdata *)(((struct _msgh *)msgh)->sq->apidata))

static byte *hwm_from = (byte *) "-=| SquishMail |=-";

MSG *MSGAPI SdmOpenArea(byte * name, word mode, word type)
{
    MSG *mh;

    unused(_junksqd);

    mh = palloc(sizeof(MSG));
    if (mh == NULL)
    {
        msgapierr = MERR_NOMEM;
        goto ErrOpen;
    }

    memset(mh, '\0', sizeof(MSG));

    mh->id = MSGAPI_ID;

    if (type & MSGTYPE_ECHO)
    {
        mh->isecho = TRUE;
    }

    mh->api = (struct _apifuncs *)palloc(sizeof(struct _apifuncs));
    if (mh->api == NULL)
    {
        msgapierr = MERR_NOMEM;
        goto ErrOpen;
    }

    memset(mh->api, '\0', sizeof(struct _apifuncs));

    mh->apidata = (void *)palloc(sizeof(struct _sdmdata));
    if (mh->apidata == NULL)
    {
        msgapierr = MERR_NOMEM;
        goto ErrOpen;
    }

    memset((byte *) mh->apidata, '\0', sizeof(struct _sdmdata));

    strcpy((char *) Mhd->base, (char *) name);
    Add_Trailing((char *) Mhd->base, PATH_DELIM);
    Mhd->hwm = (dword) - 1L;

    mh->len = sizeof(MSG);
    mh->num_msg = 0;
    mh->high_msg = 0;
    mh->high_water = (dword) - 1L;

    if (!direxist((char *) name) && (mode == MSGAREA_NORMAL || mymkdir((char *) name) == -1))
    {
        msgapierr = MERR_NOENT;
        goto ErrOpen;
    }

    if (!_SdmRescanArea(mh))
    {
        goto ErrOpen;
    }

    mh->type &= ~MSGTYPE_ECHO;

    *mh->api = sdm_funcs;
    mh->sz_xmsg = sizeof(XMSG);

    msgapierr = 0;
    return mh;

ErrOpen:

    if (mh)
    {
        if (mh->api)
        {
            if (mh->apidata)
            {
                pfree((char *)mh->apidata);
            }

            pfree(mh->api);
        }

        pfree(mh);
    }

    return NULL;
}

static sword EXPENTRY SdmCloseArea(MSG * mh)
{
    XMSG msg;
    MSGH *msgh;
    static byte *msgbody = (byte *)
    "NOECHO\r\rPlease ignore.  This message is only used by the SquishMail "
    "system to store\rthe high water mark for each conference area.\r\r\r\r"
    "\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r(Elvis was here!)\r\r\r";

    if (InvalidMh(mh))
    {
        return -1;
    }

    if (Mhd->hwm_chgd)
    {
        msgh = SdmOpenMsg(mh, MOPEN_CREATE, 1L);
        if (msgh != NULL)
        {
            Init_Xmsg(&msg);

            Get_Dos_Date((union stamp_combo *)&msg.date_arrived);
            Get_Dos_Date((union stamp_combo *)&msg.date_written);

            /*
             *  Use high-bit chars in the to/from field, so that (l)users
             *  can't log on as this userid and delete the HWM.
             */

            strcpy((char *) msg.from, (char *) hwm_from);
            strcpy((char *) msg.to, (char *) msg.from);
            strcpy((char *) msg.subj, "High wadda' mark");

            /* To prevent "INTL 0:0/0 0:0/0" kludges */
            msg.orig.zone = msg.dest.zone = mi.def_zone;

            msg.replyto = mh->high_water;
            msg.attr = MSGPRIVATE | MSGREAD | MSGLOCAL | MSGSENT;

            SdmWriteMsg(msgh, FALSE, &msg, msgbody, strlen((char *) msgbody) + 1,
              strlen((char *) msgbody) + 1, 0L, NULL);

            SdmCloseMsg(msgh);
        }
    }

    if (Mhd->msgs_open)
    {
        msgapierr = MERR_EOPEN;
        return -1;
    }

    if (Mhd->msgnum)
    {
        pfree(Mhd->msgnum);
    }

    pfree((char *)mh->apidata);
    pfree(mh->api);

    mh->id = 0L;
    pfree(mh);

    msgapierr = MERR_NONE;
    return 0;
}

static MSGH *EXPENTRY SdmOpenMsg(MSG * mh, word mode, dword msgnum)
{
    byte msgname[PATHLEN];
    int handle, filemode;
    word mn, owrite = FALSE;

    MSGH *msgh;

    if (InvalidMh(mh))
    {
        return NULL;
    }

    if (msgnum == MSGNUM_CUR)
    {
        msgnum = mh->cur_msg;
    }
    else if (msgnum == MSGNUM_PREV)
    {
        for (mn = (word) (mh->num_msg - 1); (sdword) mn < (sdword) mh->high_msg; mn--)
        {
            if ((dword) Mhd->msgnum[mn] < mh->cur_msg)
            {
                msgnum = mh->cur_msg = Mhd->msgnum[mn];
                break;
            }
        }

        /* if mn == -1, no message to go to */

        if (mn == (word) - 1)
        {
            msgapierr = MERR_NOENT;
            return NULL;
        }
    }
    else if (msgnum == MSGNUM_NEXT)
    {
        for (mn = 0; mn < (word) mh->num_msg; mn++)
        {
            if ((dword) Mhd->msgnum[mn] > mh->cur_msg)
            {
                msgnum = mh->cur_msg = Mhd->msgnum[mn];
                break;
            }
        }

        /* if mn == Mhd->msgnum_len, we can't go to any message */

        if ((dword) mn == mh->num_msg)
        {
            msgapierr = MERR_NOENT;
            return NULL;
        }
    }
    else if (mode != MOPEN_CREATE)
    {
        /*
         *  If we're not creating, make sure that the specified msg# can
         *  be found.
         */

        for (mn = 0; mn < (word) mh->num_msg; mn++)
        {
            if (msgnum == Mhd->msgnum[mn])
            {
                break;
            }
        }

        if ((dword) mn == mh->num_msg)
        {
            msgapierr = MERR_NOENT;
            return NULL;
        }
    }

    if (mode == MOPEN_CREATE)
    {
        /* If we're creating a new message... */

        if (msgnum == 0L)
        {
            /*
             *  If the base isn't locked, make sure that we avoid
             *  conflicts...
             */

            if (!mh->locked)
            {
                /* Check to see if the msg we're writing already exists */

                sprintf((char *) msgname, (char *) sd_msg, Mhd->base, (int)mh->high_msg + 1);

                if (fexist((char *) msgname))
                {
                    /* If so, rescan the base, to find out which msg# it is. */

                    if (Mhd->msgnum && Mhd->msgnum_len)
                    {
                        pfree(Mhd->msgnum);
                    }

                    if (!_SdmRescanArea(mh))
                    {
                        return NULL;
                    }
                }
            }

            msgnum = ++mh->high_msg;

            /*
             *  Make sure that we don't overwrite the high-water mark,
             *  unless we call with msgnum != 0L (a specific number).
             */

            if (mh->isecho && msgnum == 1)
            {
                msgnum = mh->high_msg = 2;
            }
        }
        else
        {
            /* otherwise, we're overwriting an existing message */
            owrite = TRUE;
        }

        filemode = O_CREAT | O_TRUNC | O_RDWR;
    }
    else if (mode == MOPEN_READ)
    {
        filemode = O_RDONLY;
    }
    else if (mode == MOPEN_WRITE)
    {
        filemode = O_WRONLY;
    }
    else
    {
        filemode = O_RDWR;
    }

    sprintf((char *) msgname, (char *) sd_msg, Mhd->base, (int)msgnum);

    handle = sopen((char *) msgname, filemode | O_BINARY, SH_DENYNONE, S_IREAD | S_IWRITE);
    if (handle == -1)
    {
        if (filemode & O_CREAT)
        {
            msgapierr = MERR_BADF;
        }
        else
        {
            msgapierr = MERR_NOENT;
        }
        return NULL;
    }

    mh->cur_msg = msgnum;

    msgh = palloc(sizeof(MSGH));
    if (msgh == NULL)
    {
        close(handle);
        msgapierr = MERR_NOMEM;
        return NULL;
    }

    memset(msgh, '\0', sizeof(MSGH));
    msgh->fd = handle;

    if (mode == MOPEN_CREATE)
    {
        if (mh->num_msg + 1 >= Mhd->msgnum_len)
        {
            Mhd->msgnum_len += (word) SDM_BLOCK;
            Mhd->msgnum = realloc(Mhd->msgnum, Mhd->msgnum_len * sizeof(unsigned));

            if (!Mhd->msgnum)
            {
                pfree(msgh);
                close(handle);
                msgapierr = MERR_NOMEM;
                return NULL;
            }
        }

        /*
         *  If we're writing a new msg, this is easy -- just add to
         *  end of list.
         */

        if (!owrite)
        {
            Mhd->msgnum[(size_t) (mh->num_msg++)] = (word) msgnum;
        }
        else
        {
            for (mn = 0; (dword) mn < mh->num_msg; mn++)
            {
                if ((dword) Mhd->msgnum[mn] >= msgnum)
                {
                    break;
                }
            }

            /*
             *  If this message is already in the list then do nothing --
             *  simply overwrite it, keeping the same message number, so
             *  no action is required.  Otherwise, we have to shift
             *  everything up by one since we're adding this new message
             *  in between two others.
             */

            if ((dword) Mhd->msgnum[mn] != msgnum)
            {
                memmove(Mhd->msgnum + mn + 1, Mhd->msgnum + mn,
                  ((size_t) mh->num_msg - mn) * sizeof(word));
                Mhd->msgnum[mn] = (word) msgnum;
                mh->num_msg++;
            }
        }
    }

    msgh->cur_pos = 0L;

    if (mode == MOPEN_CREATE)
    {
        msgh->msg_len = 0;
    }
    else
    {
        msgh->msg_len = (dword) - 1;
    }

    msgh->sq = mh;
    msgh->id = MSGH_ID;
    msgh->ctrl = NULL;
    msgh->clen = -1;
    msgh->zplen = 0;

    msgapierr = MERR_NONE;

    /* Keep track of how many messages were opened for this area */

    MsghMhd->msgs_open++;

    return msgh;
}

static sword EXPENTRY SdmCloseMsg(MSGH * msgh)
{
    if (InvalidMsgh(msgh))
    {
        return -1;
    }

    MsghMhd->msgs_open--;

    if (msgh->ctrl)
    {
        pfree(msgh->ctrl);
        msgh->ctrl = NULL;
    }

    close(msgh->fd);

    msgh->id = 0L;
    pfree(msgh);

    msgapierr = MERR_NONE;
    return 0;
}

static dword EXPENTRY SdmReadMsg(MSGH * msgh, XMSG * msg, dword offset, dword bytes, byte * text, dword clen, byte * ctxt)
{
    unsigned len;
    dword realbytes;
    struct _omsg fmsg;
    word need_ctrl, got;
    NETADDR *orig, *dest;
    byte *fake_msgbuf = NULL, *newtext;

    if (InvalidMsgh(msgh))
    {
        return -1L;
    }

    if (!(clen && ctxt))
    {
        clen = 0L;
        ctxt = NULL;
    }

    if (!(text && bytes))
    {
        bytes = 0L;
        text = NULL;
    }

    orig = &msg->orig;
    dest = &msg->dest;

    if (msg)
    {
        lseek(msgh->fd, 0L, SEEK_SET);

        if (!read_omsg(msgh->fd, &fmsg))
        {
            msgapierr = MERR_BADF;
            return -1L;
        }

        fmsg.to[sizeof(fmsg.to) - 1] = '\0';
        fmsg.from[sizeof(fmsg.from) - 1] = '\0';
        fmsg.subj[sizeof(fmsg.subj) - 1] = '\0';
        fmsg.date[sizeof(fmsg.date) - 1] = '\0';

        Convert_Fmsg_To_Xmsg(&fmsg, msg, mi.def_zone);

        StripNasties(msg->from);
        StripNasties(msg->to);
        StripNasties(msg->subj);
    }


    /*
     *  If we weren't instructed to read some message text (ie. only the
     *  header, read a block anyway.  We need to scan for kludge lines,
     *  to pick out the appropriate zone/point info.)
     */

    if (msgh->ctrl == NULL && ((msg || ctxt || text) ||
      (msg || ctxt || text) == 0))
    {
        need_ctrl = TRUE;
    }
    else
    {
        need_ctrl = FALSE;
    }

    realbytes = bytes;
    unused(realbytes);

    /*
     *  If we need to read the control information, and the user hasn't
     *  requested a read operation, we'll need to do one anyway.
     */

    if (need_ctrl && (text == NULL || bytes < MAX_SDM_CLEN))
    {
        text = fake_msgbuf = palloc(MAX_SDM_CLEN + 1);
        if (text == NULL)
        {
            msgapierr = MERR_NOMEM;
            return -1;
        }

        text[MAX_SDM_CLEN] = '\0';
        bytes = MAX_SDM_CLEN;
    }

    /* If we need to read in some text... */

    if (text)
    {
        /* Seek is superfluous if we just read msg header */

        if (!msg || msgh->msgtxt_start != 0)
        {
            lseek(msgh->fd, (dword) OMSG_SIZE +
              msgh->msgtxt_start + offset, SEEK_SET);

            msgh->cur_pos = offset;
        }

        got = (word) farread(msgh->fd, text, (unsigned int)bytes);

        /*
         *  Update counter only if we got some text, and only if we're
         *  doing a read requested by the user (as opposed to reading
         *  ahead to find kludge lines).
         */

        if (got > 0 && !fake_msgbuf)
        {
            msgh->cur_pos += got;
        }
    }
    else
    {
        got = 0;
    }

    /* Convert the kludges into 'ctxt' format */

    if (need_ctrl && got && offset == 0L)
    {
        len = got;

        msgh->ctrl = CopyToControlBuf(text, &newtext, &len);
        if (msgh->ctrl != NULL)
        {
            msgh->clen = (dword) strlen((char *) msgh->ctrl) + 1;
            msgh->msgtxt_start = newtext - text;

            /* Shift back the text buffer to counter absence of ^a strings */

            memmove(text, newtext, (size_t) (bytes - (newtext - text)));

            got -= (word) (msgh->clen - 1);
        }
    }

    /* Scan the ctxt ourselves to find zone/point info */

    if (msg && msgh->ctrl)
    {
        ConvertControlInfo(msgh->ctrl, orig, dest);
    }

    /* And if the app requested ctrlinfo, put it in its place. */

    if (ctxt && msgh->ctrl)
    {
        size_t slen;

        slen = strlen((char *) msgh->ctrl) + 1;
        memmove(ctxt, msgh->ctrl, min(slen, (size_t) clen));
    }

    if (fake_msgbuf)
    {
        pfree(fake_msgbuf);
        got = 0;
    }

    msgapierr = MERR_NONE;
    return got;
}

static sword EXPENTRY SdmWriteMsg(MSGH * msgh, word append, XMSG * msg, byte * text, dword textlen, dword totlen, dword clen, byte * ctxt)
{
    struct _omsg fmsg;
    byte *s;

    unused(totlen);

    if (clen == 0L || ctxt == NULL)
    {
        ctxt = NULL;
        clen = 0L;
    }

    if (InvalidMsgh(msgh))
    {
        return -1;
    }

    lseek(msgh->fd, 0L, SEEK_SET);

    if (msg)
    {
        Convert_Xmsg_To_Fmsg(msg, &fmsg);

        if (!write_omsg(msgh->fd, &fmsg))
        {
            msgapierr = MERR_NODS;
            return -1;
        }

        if (!append && msgh->clen <= 0 && msgh->zplen == 0)
        {
            statfd = msgh->fd;
            msgh->zplen = (word) WriteZPInfo(msg, WriteToFd, ctxt);
        }
    }
    else if (!append || ctxt)
    {
        /* Skip over old message header */
        lseek(msgh->fd, (dword) OMSG_SIZE +
          (dword) msgh->zplen, SEEK_SET);
    }

    /* Now write the control info / kludges */

    if (clen && ctxt)
    {
        if (!msg)
        {
            lseek(msgh->fd, (dword) OMSG_SIZE +
              (dword) msgh->zplen, SEEK_SET);
        }

        s = CvtCtrlToKludge(ctxt);

        if (s)
        {
            unsigned sl_s = (unsigned) strlen((char *)s);
            int ret;

            ret = farwrite(msgh->fd, s, sl_s);
            pfree(s);
            if (ret != (int) sl_s)
            {
                msgapierr = MERR_NODS;
                return -1;
            }
        }
    }

    if (append)
    {
        lseek(msgh->fd, 0L, SEEK_END);
    }

    if (text)
    {
        if (farwrite(msgh->fd, text, (unsigned)textlen) != (signed) textlen)
        {
            msgapierr = MERR_NODS;
            return -1;
        }
    }

    msgapierr = MERR_NONE;
    return 0;
}

static sword EXPENTRY SdmKillMsg(MSG * mh, dword msgnum)
{
    dword hwm;
    byte temp[PATHLEN];
    word mn;

    if (InvalidMh(mh))
    {
        return -1;
    }

    /* Remove the message number from our private index */

    for (mn = 0; (dword) mn < mh->num_msg; mn++)
    {
        if ((dword) Mhd->msgnum[mn] == msgnum)
        {
            memmove(Mhd->msgnum + mn, Mhd->msgnum + mn + 1,
              (int)(mh->num_msg - mn - 1) * sizeof(int));
            break;
        }
    }

    /* If we couldn't find it, return an error message */

    if (mn == (word) mh->num_msg)
    {
        msgapierr = MERR_NOENT;
        return -1;
    }

    sprintf((char *) temp, (char *) sd_msg, Mhd->base, (unsigned int)msgnum);

    if (unlink((char *) temp) == -1)
    {
        msgapierr = MERR_NOENT;
        return -1;
    }

    mh->num_msg--;

    /* Adjust the high message number */

    if (msgnum == mh->high_msg)
    {
        if (mh->num_msg)
        {
            mh->high_msg = (dword) Mhd->msgnum[(int)mh->num_msg - 1];
        }
        else
        {
            mh->high_msg = 0;
        }
    }

    /* Now adjust the high-water mark, if necessary */

    hwm = SdmGetHighWater(mh);

    if (hwm != (dword) - 1 && hwm > 0 && hwm >= msgnum)
    {
        SdmSetHighWater(mh, msgnum - 1);
    }

    msgapierr = MERR_NONE;
    return 0;
}

static sword EXPENTRY SdmLock(MSG * mh)
{
    if (InvalidMh(mh))
    {
        return -1;
    }

    msgapierr = MERR_NONE;
    return 0;
}

static sword EXPENTRY SdmUnlock(MSG * mh)
{
    if (InvalidMh(mh))
    {
        return -1;
    }

    msgapierr = MERR_NONE;
    return 0;
}

sword MSGAPI SdmValidate(byte * name)
{
    msgapierr = MERR_NONE;
    return ((sword) (direxist((char *) name) != FALSE));
}

static sword EXPENTRY SdmSetCurPos(MSGH * msgh, dword pos)
{
    if (InvalidMsgh(msgh))
    {
        return 0;
    }

    lseek(msgh->fd, msgh->cur_pos = pos, SEEK_SET);
    msgapierr = MERR_NONE;
    return 0;
}

static dword EXPENTRY SdmGetCurPos(MSGH * msgh)
{
    if (InvalidMsgh(msgh))
    {
        return -1L;
    }

    msgapierr = MERR_NONE;
    return msgh->cur_pos;
}

static UMSGID EXPENTRY SdmMsgnToUid(MSG * mh, dword msgnum)
{
    if (InvalidMh(mh))
    {
        return (UMSGID) - 1;
    }

    msgapierr = MERR_NONE;
    return (UMSGID) msgnum;
}

static dword EXPENTRY SdmUidToMsgn(MSG * mh, UMSGID umsgid, word type)
{
    word wmsgid, mn;

    if (InvalidMh(mh))
    {
        return -1L;
    }

    msgapierr = MERR_NONE;
    wmsgid = (word) umsgid;

    for (mn = 0; (dword) mn < mh->num_msg; mn++)
    {
        if (Mhd->msgnum[mn] == wmsgid ||
          (type == UID_NEXT && Mhd->msgnum[mn] >= wmsgid) ||
          (type == UID_PREV && Mhd->msgnum[mn] <= wmsgid &&
          ((dword) (mn + 1) >= mh->num_msg ||
          Mhd->msgnum[mn + 1] > wmsgid)))
        {
            return ((dword) Mhd->msgnum[mn]);
        }
    }

    msgapierr = MERR_NOENT;
    return 0L;
}

static dword EXPENTRY SdmGetHighWater(MSG * mh)
{
    MSGH *msgh;
    XMSG msg;

    if (InvalidMh(mh))
    {
        return -1;
    }

    /* If we've already fetched the highwater mark... */

    if (mh->high_water != (dword) - 1L)
    {
        return mh->high_water;
    }

    msgh = SdmOpenMsg(mh, MOPEN_READ, 1L);
    if (msgh == NULL)
    {
        return 0L;
    }

    if (SdmReadMsg(msgh, &msg, 0L, 0L, NULL, 0L, NULL) == (dword) - 1 ||
      !eqstr((char *) msg.from, (char *) hwm_from))
    {
        mh->high_water = 0L;
    }
    else
    {
        mh->high_water = (dword) msg.replyto;
    }

    SdmCloseMsg(msgh);

    return mh->high_water;
}

static sword EXPENTRY SdmSetHighWater(MSG * mh, dword hwm)
{
    if (InvalidMh(mh))
    {
        return -1;
    }

    /*
     *  Only write it to memory for now.  We'll do a complete update of
     *  the real HWM in 1.MSG only when doing a MsgCloseArea(), to save
     *  time.
     */

    if (hwm != mh->high_water)
    {
        Mhd->hwm_chgd = TRUE;
    }

    mh->high_water = hwm;
    return 0;
}

static dword EXPENTRY SdmGetTextLen(MSGH * msgh)
{
    dword pos, end;

    /* Figure out the physical length of the message */

    if (msgh->msg_len == (dword) - 1)
    {
        pos = (dword) tell(msgh->fd);
        end = lseek(msgh->fd, 0L, SEEK_END);

        if (end < sizeof(XMSG))
        {
            msgh->msg_len = 0L;
        }
        else
        {
            msgh->msg_len = end - (dword) OMSG_SIZE;
        }

        lseek(msgh->fd, pos, SEEK_SET);
    }

    /* If we've already figured out the length of the control info */

    if (msgh->clen == (dword) - 1 && _Grab_Clen(msgh) == -1)
    {
        return 0;
    }
    else
    {
        return (dword) (msgh->msg_len - msgh->msgtxt_start);
    }
}

static dword EXPENTRY SdmGetCtrlLen(MSGH * msgh)
{
    /* If we've already figured out the length of the control info */

    if (msgh->clen == (dword) - 1 && _Grab_Clen(msgh) == -1)
    {
        return 0;
    }
    else
    {
        return (dword) msgh->clen;
    }
}

static sword near _Grab_Clen(MSGH * msgh)
{
    if ((sdword) SdmReadMsg(msgh, NULL, 0L, 0L, NULL, 0L, NULL) < (sdword) 0)
    {
        return (sword) -1;
    }
    else
    {
        return (sword) 0;
    }
}

static sword near _SdmRescanArea(MSG * mh)
{
    FFIND *ff;
    char *temp;
    word mn, thismsg;

    mh->num_msg = 0;

    Mhd->msgnum = palloc(SDM_BLOCK * sizeof(unsigned));
    if (Mhd->msgnum == NULL)
    {
        msgapierr = MERR_NOMEM;
        return FALSE;
    }

    Mhd->msgnum_len = SDM_BLOCK;

    temp = malloc(strlen(Mhd->base)+6);
    sprintf((char *) temp, "%s*.msg", Mhd->base);

    ff = FFindOpen((char *) temp, 0);

    free(temp);

    if (ff != 0)
    {
        mn = 0;

        do
        {
            /* Don't count zero-length or invalid messages */

#ifndef UNIX
            if (ff->ff_fsize < OMSG_SIZE)
            {
                 continue;
            }
#endif

            if (mn >= Mhd->msgnum_len)
            {
            	Mhd->msgnum_len += (word) SDM_BLOCK;
                Mhd->msgnum = realloc(Mhd->msgnum, Mhd->msgnum_len * sizeof(unsigned));

                if (!Mhd->msgnum)
                {
                    msgapierr = MERR_NOMEM;
                    return FALSE;
                }
            }

            thismsg = (word) atoi(ff->ff_name);
            if (thismsg != 0)
            {
                Mhd->msgnum[mn++] = thismsg;

                if ((dword) thismsg > mh->high_msg)
                {
                    mh->high_msg = (dword) thismsg;
                }

                mh->num_msg = (dword) mn;
            }

#ifdef OS2
#ifdef __TURBOC__
            if ((mn % 128) == 127)
            {
                sleep(1);
            }
#else
            {
#ifdef __IBMC__
                extern void _System DosSleep(dword);
#else
                extern void far pascal DosSleep(dword);
#endif
                if ((mn % 128) == 127)
                {
                    DosSleep(1L);
                }
            }
#endif
#endif
        }
        while (FFindNext(ff) == 0);

        FFindClose(ff);

        /* Now sort the list of messages */

        qksort((int *)Mhd->msgnum, (word) mh->num_msg);
    }

    return TRUE;
}

static void MSGAPI Init_Xmsg(XMSG * msg)
{
    memset(msg, '\0', sizeof(XMSG));
}

static void MSGAPI Convert_Fmsg_To_Xmsg(struct _omsg *fmsg, XMSG * msg,
  word def_zone)
{
    NETADDR *orig, *dest;

    Init_Xmsg(msg);

    orig = &msg->orig;
    dest = &msg->dest;

    fmsg->to[sizeof(fmsg->to) - 1] = '\0';
    fmsg->from[sizeof(fmsg->from) - 1] = '\0';
    fmsg->subj[sizeof(fmsg->subj) - 1] = '\0';
    fmsg->date[sizeof(fmsg->date) - 1] = '\0';

    strcpy((char *) msg->from, (char *) fmsg->from);
    strcpy((char *) msg->to, (char *) fmsg->to);
    strcpy((char *) msg->subj, (char *) fmsg->subj);

    orig->zone = dest->zone = def_zone;
    orig->point = dest->point = 0;

    orig->net = fmsg->orig_net;
    orig->node = fmsg->orig;

    dest->net = fmsg->dest_net;
    dest->node = fmsg->dest;

    Get_Binary_Date(&msg->date_written, &fmsg->date_written, fmsg->date);
    Get_Binary_Date(&msg->date_arrived, &fmsg->date_arrived, fmsg->date);

    strcpy((char *) msg->__ftsc_date, (char *) fmsg->date);

    msg->utc_ofs = 0;

    msg->replyto = fmsg->reply;
    msg->replies[0] = fmsg->up;
    msg->attr = (dword) fmsg->attr;

    /* Convert 4d pointnets */

    if (fmsg->times == ~fmsg->cost && fmsg->times)
    {
        msg->orig.point = fmsg->times;
    }
}

static void MSGAPI Convert_Xmsg_To_Fmsg(XMSG * msg, struct _omsg *fmsg)
{
    NETADDR *orig, *dest;

    memset(fmsg, '\0', sizeof(struct _omsg));

    orig = &msg->orig;
    dest = &msg->dest;

    strncpy((char *) fmsg->from, (char *) msg->from, sizeof(fmsg->from));
    strncpy((char *) fmsg->to, (char *) msg->to, sizeof(fmsg->to));
    strncpy((char *) fmsg->subj, (char *) msg->subj, sizeof(fmsg->subj));

    fmsg->from[sizeof(fmsg->from) - 1] = '\0';
    fmsg->to[sizeof(fmsg->to) - 1] = '\0';
    fmsg->subj[sizeof(fmsg->subj) - 1] = '\0';

    fmsg->orig_net = orig->net;
    fmsg->orig = orig->node;

    fmsg->dest_net = dest->net;
    fmsg->dest = dest->node;

    if (*msg->__ftsc_date)
    {
        strncpy((char *) fmsg->date, (char *) msg->__ftsc_date, sizeof(fmsg->date));
        fmsg->date[sizeof(fmsg->date) - 1] = '\0';
    }
    else
    {
        sprintf((char *) fmsg->date, "%02d %s %02d  %02d:%02d:%02d",
          msg->date_written.date.da ? msg->date_written.date.da : 1,
          months_ab[msg->date_written.date.mo ? msg->date_written.date.mo - 1 : 0],
          (msg->date_written.date.yr + 80) % 100, msg->date_written.time.hh,
          msg->date_written.time.mm, msg->date_written.time.ss << 1);
    }

    fmsg->date_written = msg->date_written;
    fmsg->date_arrived = msg->date_arrived;

    fmsg->reply = (word) msg->replyto;
    fmsg->up = (word) msg->replies[0];
    fmsg->attr = (word) (msg->attr & 0xffffL);

    /*
     *  Non-standard point kludge to ensure that 4D pointmail works
     *  correctly.
     */

    if (orig->point)
    {
        fmsg->times = orig->point;
        fmsg->cost = (word) ~fmsg->times;
    }
}

int EXPENTRY WriteZPInfo(XMSG * msg, void (_stdc * wfunc) (byte * str), byte * kludges)
{
    byte temp[PATHLEN], *null = (byte *) "";
    int bytes = 0;

    if (!kludges)
    {
        kludges = null;
    }

    if ((msg->dest.zone != mi.def_zone || msg->orig.zone != mi.def_zone) &&
      !strstr((char *) kludges, "\x01INTL"))
    {
        sprintf((char *) temp, "\x01INTL %hu:%hu/%hu %hu:%hu/%hu\r", msg->dest.zone, msg->dest.net,
          msg->dest.node, msg->orig.zone, msg->orig.net, msg->orig.node);

        (*wfunc) (temp);
        bytes += strlen((char *) temp);
    }

    if (msg->orig.point && !strstr((char *) kludges, "\x01" "FMPT"))
    {
        sprintf((char *) temp, "\x01" "FMPT %hu\r", msg->orig.point);
        (*wfunc) (temp);
        bytes += strlen((char *) temp);
    }

    if (msg->dest.point && !strstr((char *) kludges, "\x01" "TOPT"))
    {
        sprintf((char *) temp, "\x01" "TOPT %hu\r", msg->dest.point);
        (*wfunc) (temp);
        bytes += strlen((char *) temp);
    }

    return bytes;
}

static void _stdc WriteToFd(byte * str)
{
    farwrite(statfd, str, strlen((char *) str));
}

static void near Get_Binary_Date(struct _stamp *todate, struct _stamp *fromdate, byte * asciidate)
{
    if (fromdate->date.da == 0 || fromdate->date.da > 31 || fromdate->date.yr > 50 ||
      fromdate->time.hh > 23 || fromdate->time.mm > 59 || fromdate->time.ss > 59 ||
      ((union stamp_combo *)&fromdate)->ldate == 0)
    {
        ASCII_Date_To_Binary((char *) asciidate, (union stamp_combo *)todate);
    }
    else
    {
        *todate = *fromdate;
    }
}
