/*
    NINJAM - netmsg.cpp
    Copyright (C) 2005-2007 Cockos Incorporated

    NINJAM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    NINJAM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NINJAM; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*

  This file provides the implementations of the Net_Messsage class, and
  Net_Connection class (handles sending and receiving Net_Messages to
  a JNetLib JNL_Connection).

*/


#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <memory.h>
#endif

#include "netmsg.h"
#include "crypto/nj_crypto.h"

int Net_Message::parseBytesNeeded()
{
  return get_size()-m_parsepos;
}

int Net_Message::parseAddBytes(void *data, int len)
{
  char *p=(char*)get_data();
  if (!p) return 0;
  if (len > parseBytesNeeded()) len = parseBytesNeeded();
  memcpy(p+m_parsepos,data,len);
  m_parsepos+=len;
  return len;
}

int Net_Message::parseMessageHeader(void *data, int len) // returns bytes used, if any (or 0 if more data needed) or -1 if invalid
{
  unsigned char *dp=(unsigned char *)data;
  if (len < 5) return 0;

  int type=*dp++;

  int size = *dp++;
  size |= ((int)*dp++)<<8;
  size |= ((int)*dp++)<<16;
  size |= ((int)*dp++)<<24;
  len -= 5;
  if (type == MESSAGE_INVALID || size < 0 || size > NET_MESSAGE_MAX_SIZE_ENCRYPTED) return -1;

  m_type=type;
  set_size(size);

  m_parsepos=0;

  return 5;
}

int Net_Message::makeMessageHeader(void *data) // makes message header, data should be at least 16 bytes to be safe
{
  if (!data) return 0;

  unsigned char *dp=(unsigned char *)data;
  *dp++ = (unsigned char) m_type;
  int size=get_size();
  *dp++=size&0xff; size>>=8;
  *dp++=size&0xff; size>>=8;
  *dp++=size&0xff; size>>=8;
  *dp++=size&0xff;

  return (dp-(unsigned char *)data);
}



Net_Message *Net_Connection::Run(int *wantsleep)
{
  if (!m_con || m_error) return 0;

  {
    int s=0,r=0;
    m_con->run(-1,-1,&s,&r);
    if (wantsleep && (s||r)) *wantsleep=0;
  }

  time_t now=time(NULL);

  if (m_sendq.Available() > 0) m_last_send=now;
  else if (now > m_last_send + m_keepalive)
  {
    Net_Message *keepalive= new Net_Message;
    keepalive->set_type(MESSAGE_KEEPALIVE);
    keepalive->set_size(0);
    Send(keepalive);
    m_last_send=now;
  }

  // handle sending
  while (m_con->send_bytes_available()>64 && m_sendq.Available()>0)
  {
    Net_Message **topofq = (Net_Message **)m_sendq.Get();

    if (!topofq) break;
    Net_Message *sendm=*topofq;
    if (sendm)
    {
      if (wantsleep) *wantsleep=0;
      // Encrypt payload before first send if encryption active and payload non-empty
      // Zero-length payloads (keepalive, type 0xFD) skip encryption — no security value,
      // saves 32 bytes overhead every 3 seconds. Justified: keepalive type/size already
      // in cleartext headers per D-07, encrypting nothing adds no confidentiality.
      if (m_msgsendpos < 0 && m_encryption_active && sendm->get_size() > 0) {
          auto enc = encrypt_payload(
              (const unsigned char*)sendm->get_data(),
              sendm->get_size(),
              m_encryption_key
          );
          if (!enc.ok) {
              m_error = -4;  // encryption failure
              break;
          }
          // Encrypt into a NEW message to avoid mutating the shared original
          // (Net_Message is refcounted; callers may retain a reference)
          Net_Message *enc_msg = new Net_Message;
          enc_msg->set_type(sendm->get_type());
          enc_msg->set_size((int)enc.data.size());
          if (enc_msg->get_size() != (int)enc.data.size() || enc_msg->get_data() == nullptr) {
              delete enc_msg;
              m_error = -4;  // allocation failed
              break;
          }
          memcpy(enc_msg->get_data(), enc.data.data(), enc.data.size());
          enc_msg->addRef();
          // Replace the queue slot pointer and release the original
          sendm->releaseRef();
          *topofq = enc_msg;
          sendm = enc_msg;
      }
      if (m_msgsendpos<0) // send header
      {
        char buf[32];
        int hdrlen=sendm->makeMessageHeader(buf);
        m_con->send_bytes(buf,hdrlen);

        m_msgsendpos=0;
      }

      int sz=sendm->get_size()-m_msgsendpos;
      if (sz < 1) // end of message, discard and move to next
      {
        sendm->releaseRef();
        m_sendq.Advance(sizeof(Net_Message*));
        m_msgsendpos=-1;
      }
      else
      {
        int avail=m_con->send_bytes_available();
        if (sz > avail) sz=avail;
        if (sz>0)
        {
          m_con->send_bytes((char*)sendm->get_data()+m_msgsendpos,sz);
          m_msgsendpos+=sz;
        }
      }
    }
    else
    {
      m_sendq.Advance(sizeof(Net_Message*));
      m_msgsendpos=-1;
    }
    {
      int s=0,r=0;
      m_con->run(-1,-1,&s,&r);
      if (wantsleep && (s||r)) *wantsleep=0;
    }
  }

  m_sendq.Compact();

  Net_Message *retv=0;

  // handle receive now
  if (!m_recvmsg)
  {
    m_recvmsg=new Net_Message;
    m_recvstate=0;
  }

  while (!retv && m_con->recv_bytes_available()>0)
  {
    char buf[8192];
    int bufl=m_con->peek_bytes(buf,sizeof(buf));
    int a=0;

    if (!m_recvstate)
    {
      a=m_recvmsg->parseMessageHeader(buf,bufl);
      if (a<0)
      {
        m_error=-1;
        break;
      }
      if (a==0) break;
      m_recvstate=1;
    }
    int b2=m_recvmsg->parseAddBytes(buf+a,bufl-a);

    m_con->recv_bytes(buf,b2+a); // dump our bytes that we used

    if (m_recvmsg->parseBytesNeeded()<1)
    {
      retv=m_recvmsg;
      m_recvmsg=0;
      m_recvstate=0;

      // Decrypt payload if encryption active and payload is non-empty
      // Zero-length payloads (keepalive) were not encrypted on send, so skip decrypt
      if (retv && m_encryption_active && retv->get_size() > 0) {
          auto dec = decrypt_payload(
              (const unsigned char*)retv->get_data(),
              retv->get_size(),
              m_encryption_key
          );
          if (!dec.ok) {
              retv->releaseRef();
              retv = 0;
              // Generic error code — do NOT reveal padding details (padding oracle mitigation)
              m_error = -5;
              break;
          }
          // Replace payload with decrypted plaintext
          retv->set_size((int)dec.data.size());
          if (retv->get_size() != (int)dec.data.size()) {
              retv->releaseRef();
              retv = 0;
              m_error = -5;  // allocation failed
              break;
          }
          if (dec.data.size() > 0) {
              if (retv->get_data() == nullptr) {
                  retv->releaseRef();
                  retv = 0;
                  m_error = -5;
                  break;
              }
              memcpy(retv->get_data(), dec.data.data(), dec.data.size());
          }
      }
    }
    if (wantsleep) *wantsleep=0;
  }

  {
    int s=0,r=0;
    m_con->run(-1,-1,&s,&r);
    if (wantsleep && (s||r)) *wantsleep=0;
  }


  if (retv)
  {
    m_last_recv=now;
  }
  else if (now > m_last_recv + m_keepalive*3)
  {
    m_error=-3;
  }

  return retv;
}

int Net_Connection::Send(Net_Message *msg)
{
  if (msg)
  {
    msg->addRef();
    if (m_sendq.GetSize() < NET_CON_MAX_MESSAGES*(int)sizeof(Net_Message *))
      m_sendq.Add(&msg,sizeof(Net_Message *));
    else
    {
      m_error=-2;
      msg->releaseRef(); // todo: debug message to log overrun error
      return -1;
    }

#if 0
    if (m_con)
    {
      m_con->run();

      while (m_con->send_bytes_available()>64 && m_sendq.Available()>0)
      {
        Net_Message **topofq = (Net_Message **)m_sendq.Get();

        if (!topofq) break;
        Net_Message *sendm=*topofq;
        if (sendm)
        {
          if (m_msgsendpos<0) // send header
          {
            char buf[32];
            int hdrlen=sendm->makeMessageHeader(buf);
            m_con->send_bytes(buf,hdrlen);

            m_msgsendpos=0;
          }

          int sz=sendm->get_size()-m_msgsendpos;
          if (sz < 1) // end of message, discard and move to next
          {
            sendm->releaseRef();
            m_sendq.Advance(sizeof(Net_Message*));
            m_msgsendpos=-1;
          }
          else
          {
            int avail=m_con->send_bytes_available();
            if (sz > avail) sz=avail;
            if (sz>0)
            {
              m_con->send_bytes((char*)sendm->get_data()+m_msgsendpos,sz);
              m_msgsendpos+=sz;
            }
          }
        }
        else
        {
          m_sendq.Advance(sizeof(Net_Message*));
          m_msgsendpos=-1;
        }
        m_con->run();
      }

      m_sendq.Compact();
    }
  #endif

  }
  return 0;
}

int Net_Connection::GetStatus()
{
  if (m_error) return m_error;
  return !m_con || m_con->get_state()<JNL_Connection::STATE_RESOLVING || m_con->get_state()>=JNL_Connection::STATE_CLOSING; // 1 if disconnected somehow
}

Net_Connection::~Net_Connection()
{
  Net_Message **p=(Net_Message **)m_sendq.Get();
  if (p)
  {
    int n=m_sendq.Available()/sizeof(Net_Message *);
    while (n-->0)
    {
      (*p)->releaseRef();
      p++;
    }
    m_sendq.Advance(m_sendq.Available());

  }

  delete m_con;
  delete m_recvmsg;

}


void Net_Connection::Kill(int quick)
{
  m_con->close();
}
