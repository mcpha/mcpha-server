/*
 *  WebSocket server for the Red Pitaya MCPHA application
 *  Copyright (C) 2017  Pavel Demin
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <math.h>

#include <QtCore/QCoreApplication>
#include <QtWebSockets/QWebSocketServer>
#include <QtWebSockets/QWebSocket>

#include "server.h"

using namespace std;

//------------------------------------------------------------------------------

Server::Server(int16_t port, QObject *parent):
  QObject(parent), m_Sts(0), m_Cfg(0),
  m_Scope(0), m_Trg(0),
  m_BufferTimer(0), m_BufferHist(0),
  m_BufferStatus(0), m_BufferScope(0),
  m_WebSocketServer(0), m_WebSocket(0)
{
  int fd;
  volatile uint32_t *slcr, *axihp0;

  if((fd = open("/dev/mem", O_RDWR)) < 0)
  {
    perror("open");
    qApp->quit();
  }

  slcr = (uint32_t *)mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0xF8000000);
  axihp0 = (uint32_t *)mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0xF8008000);
  m_Sts = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40000000);
  m_Cfg = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40001000);
  m_Trg = (uint32_t *)mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40002000);
  m_Hist[0] = mmap(NULL, 16*sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40010000);
  m_Hist[1] = mmap(NULL, 16*sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40020000);
  m_Scope = mmap(NULL, 8192*sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x1E000000);

  m_Rst[0] = (uint8_t *)(m_Cfg + 0);
  m_Rst[1] = (uint8_t *)(m_Cfg + 1);
  m_Rst[2] = (uint8_t *)(m_Cfg + 2);
  m_Rst[3] = (uint8_t *)(m_Cfg + 3);

  /* set FPGA clock to 143 MHz and HP0 bus width to 64 bits */
  slcr[2] = 0xDF0D;
  slcr[92] = (slcr[92] & ~0x03F03F30) | 0x00100700;
  slcr[144] = 0;
  axihp0[0] &= ~1;
  axihp0[5] &= ~1;

  /* set sample rate */
  *(uint16_t *)(m_Cfg + 4) = 125;

  /* set number of samples before trigger */
  *(uint32_t *)(m_Cfg + 72) = 5000 - 1;
  /* set total number of samples */
  *(uint32_t *)(m_Cfg + 76) = 65536 - 1;

  /* set trigger channel */
  m_Trg[16] = 0;
  m_Trg[0] = 2;

  /* reset timers and histogram */
  *m_Rst[0] &= ~3;
  *m_Rst[0] |= 3;
  *m_Rst[1] &= ~3;
  *m_Rst[1] |= 3;

  /* reset oscilloscope */
  *m_Rst[2] &= ~3;
  *m_Rst[2] |= 3;

  m_BufferTimer = new QByteArray();
  m_BufferTimer->resize(4 + 8);

  m_BufferHist = new QByteArray();
  m_BufferHist->resize(4 + 16384 * 4);

  m_BufferStatus = new QByteArray();
  m_BufferStatus->resize(4 + 4);

  m_BufferScope = new QByteArray();
  m_BufferScope->resize(4 + 65536 * 4);

  m_WebSocketServer = new QWebSocketServer(QString("MCPHA"), QWebSocketServer::NonSecureMode, this);
  if(m_WebSocketServer->listen(QHostAddress::Any, port))
  {
    connect(m_WebSocketServer, SIGNAL(newConnection()), this, SLOT(on_WebSocketServer_newConnection()));
    connect(m_WebSocketServer, SIGNAL(closed()), this, SLOT(on_WebSocketServer_closed()));
  }
}

//------------------------------------------------------------------------------

Server::~Server()
{
  m_WebSocketServer->close();
  if(m_WebSocket) delete m_WebSocket;
  if(m_BufferScope) delete m_BufferScope;
  if(m_BufferStatus) delete m_BufferStatus;
  if(m_BufferHist) delete m_BufferHist;
  if(m_BufferTimer) delete m_BufferTimer;
}

//------------------------------------------------------------------------------

void Server::on_WebSocket_binaryMessageReceived(QByteArray message)
{
  uint32_t start, pre, tot, one, two;
  uint32_t code, chan;
  double data;

  memcpy(&code, message.constData() + 0, 4);
  memcpy(&chan, message.constData() + 4, 4);
  memcpy(&data, message.constData() + 8, 8);

  if(code == 0)
  {
    /* reset timer */
    if(chan == 0)
    {
      *m_Rst[0] &= ~2;
      *m_Rst[0] |= 2;
    }
    else if(chan == 1)
    {
      *m_Rst[1] &= ~2;
      *m_Rst[1] |= 2;
    }
  }
  else if(code == 1)
  {
    /* reset histogram */
    if(chan == 0)
    {
      *m_Rst[0] &= ~1;
      *m_Rst[0] |= 1;
    }
    else if(chan == 1)
    {
      *m_Rst[1] &= ~1;
      *m_Rst[1] |= 1;
    }
  }
  else if(code == 2)
  {
    /* reset oscilloscope */
    *m_Rst[2] &= ~3;
    *m_Rst[2] |= 3;
  }
  else if(code == 3)
  {
    /* reset generator */
    *m_Rst[3] &= ~128;
    *m_Rst[3] |= 128;
  }
  else if(code == 4)
  {
    /* set sample rate */
    *(uint16_t *)(m_Cfg + 4) = data;
  }
  else if(code == 5)
  {
    /* set negator mode (0 for disabled, 1 for enabled) */
    if(chan == 0)
    {
      if(data == 0)
      {
        *m_Rst[0] &= ~16;
      }
      else if(data == 1)
      {
        *m_Rst[0] |= 16;
      }
    }
    else if(chan == 1)
    {
      if(data == 0)
      {
        *m_Rst[1] &= ~16;
      }
      else if(data == 1)
      {
        *m_Rst[1] |= 16;
      }
    }
  }
  else if(code == 6)
  {
    /* set baseline mode (0 for none, 1 for auto) */
    if(chan == 0)
    {
      if(data == 0)
      {
        *m_Rst[0] &= ~4;
      }
      else if(data == 1)
      {
        *m_Rst[0] |= 4;
      }
    }
    else if(chan == 1)
    {
      if(data == 0)
      {
        *m_Rst[1] &= ~4;
      }
      else if(data == 1)
      {
        *m_Rst[1] |= 4;
      }
    }
  }
  else if(code == 7)
  {
    /* set baseline level */
    if(chan == 0)
    {
      *(uint16_t *)(m_Cfg + 16) = data;
    }
    else if(chan == 1)
    {
      *(uint16_t *)(m_Cfg + 32) = data;
    }
  }
  else if(code == 8)
  {
    /* set pha delay */
    if(chan == 0)
    {
      *(uint16_t *)(m_Cfg + 18) = data;
    }
    else if(chan == 1)
    {
      *(uint16_t *)(m_Cfg + 34) = data;
    }
  }
  else if(code == 9)
  {
    /* set pha min threshold */
    if(chan == 0)
    {
      *(uint16_t *)(m_Cfg + 20) = data;
    }
    else if(chan == 1)
    {
      *(uint16_t *)(m_Cfg + 36) = data;
    }
  }
  else if(code == 10)
  {
    /* set pha max threshold */
    if(chan == 0)
    {
      *(uint16_t *)(m_Cfg + 22) = data;
    }
    else if(chan == 1)
    {
      *(uint16_t *)(m_Cfg + 38) = data;
    }
  }
  else if(code == 11)
  {
    /* set timer */
    if(chan == 0)
    {
      *(uint64_t *)(m_Cfg + 8) = data;
    }
    else if(chan == 1)
    {
      *(uint64_t *)(m_Cfg + 24) = data;
    }
  }
  else if(code == 12)
  {
    /* set timer mode (0 for stop, 1 for running) */
    if(chan == 0)
    {
      if(data == 0)
      {
        *m_Rst[0] &= ~8;
      }
      else if(data == 1)
      {
        *m_Rst[0] |= 8;
      }
    }
    else if(chan == 1)
    {
      if(data == 0)
      {
        *m_Rst[1] &= ~8;
      }
      else if(data == 1)
      {
        *m_Rst[1] |= 8;
      }
    }
  }
  else if(code == 13)
  {
    /* read timer */
    if(chan == 0)
    {
      code = 0;
      memcpy(m_BufferTimer->data() + 0, &code, 4);
      memcpy(m_BufferTimer->data() + 4, m_Sts + 12, 8);
      m_WebSocket->sendBinaryMessage(*m_BufferTimer);
    }
    else if(chan == 1)
    {
      code = 1;
      memcpy(m_BufferTimer->data() + 0, &code, 4);
      memcpy(m_BufferTimer->data() + 4, m_Sts + 20, 8);
      m_WebSocket->sendBinaryMessage(*m_BufferTimer);
    }
  }
  else if(code == 14)
  {
    /* read histogram */
    if(chan == 0)
    {
      code = 2;
      memcpy(m_BufferHist->data() + 0, &code, 4);
      memcpy(m_BufferHist->data() + 4, m_Hist[0], 65536);
      m_WebSocket->sendBinaryMessage(*m_BufferHist);
    }
    else if(chan == 1)
    {
      code = 3;
      memcpy(m_BufferHist->data() + 0, &code, 4);
      memcpy(m_BufferHist->data() + 4, m_Hist[1], 65536);
      m_WebSocket->sendBinaryMessage(*m_BufferHist);
    }
  }
  else if(code == 15)
  {
    /* set trigger source (0 for channel 1, 1 for channel 2) */
    if(chan == 0)
    {
      m_Trg[16] = 0;
      m_Trg[0] = 2;
    }
    else if(chan == 1)
    {
      m_Trg[16] = 1;
      m_Trg[0] = 2;
    }
  }
  else if(code == 16)
  {
    /* set trigger slope (0 for rising, 1 for falling) */
    if(data == 0)
    {
      *m_Rst[2] &= ~4;
    }
    else if(data == 1)
    {
      *m_Rst[2] |= 4;
    }
  }
  else if(code == 17)
  {
    /* set trigger mode (0 for normal, 1 for auto) */
    if(data == 0)
    {
      *m_Rst[2] &= ~8;
    }
    else if(data == 1)
    {
      *m_Rst[2] |= 8;
    }
  }
  else if(code == 18)
  {
    /* set trigger level */
    *(uint16_t *)(m_Cfg + 80) = data;
  }
  else if(code == 19)
  {
    /* set number of samples before trigger */
    *(uint32_t *)(m_Cfg + 72) = data - 1;
  }
  else if(code == 20)
  {
    /* set total number of samples */
    *(uint32_t *)(m_Cfg + 76) = data - 1;
    m_BufferScope->resize(4 + data * 4);
  }
  else if(code == 21)
  {
    /* start oscilloscope */
    *m_Rst[2] |= 16;
    *m_Rst[2] &= ~16;
  }
  else if(code == 22)
  {
    /* read oscilloscope status */
    code = 4;
    memcpy(m_BufferStatus->data() + 0, &code, 4);
    memcpy(m_BufferStatus->data() + 4, m_Sts + 44, 4);
    m_WebSocket->sendBinaryMessage(*m_BufferStatus);
  }
  else if(code == 23)
  {
    /* read oscilloscope data */
    pre = *(uint32_t *)(m_Cfg + 72) + 1;
    tot = *(uint32_t *)(m_Cfg + 76) + 1;
    memcpy(&start, m_Sts + 44, 4);
    start >>= 1;
    start = (start - pre) & 0x007FFFFF;
    code = 5;
    memcpy(m_BufferScope->data() + 0, &code, 4);
    if(start + tot <= 0x007FFFFF)
    {
      memcpy(m_BufferScope->data() + 4, m_Scope + start * 4, tot * 4);
    }
    else
    {
      one = (0x007FFFFF - start) * 4;
      two = (start + tot - 0x007FFFFF) * 4;
      memcpy(m_BufferScope->data() + 4, m_Scope + start * 4, one);
      memcpy(m_BufferScope->data() + 4 + one, m_Scope, two);
    }
    m_WebSocket->sendBinaryMessage(*m_BufferScope);
  }
}

//------------------------------------------------------------------------------

void Server::on_WebSocketServer_closed()
{
  qApp->quit();
}

//------------------------------------------------------------------------------

void Server::on_WebSocketServer_newConnection()
{
  QWebSocket *webSocket = m_WebSocketServer->nextPendingConnection();

  if(m_WebSocket && webSocket)
  {
    webSocket->close();
    return;
  }

  connect(webSocket, SIGNAL(binaryMessageReceived(QByteArray)), this, SLOT(on_WebSocket_binaryMessageReceived(QByteArray)));
  connect(webSocket, SIGNAL(disconnected()), this, SLOT(on_WebSocket_disconnected()));

  m_WebSocket = webSocket;
}

//------------------------------------------------------------------------------

void Server::on_WebSocket_disconnected()
{
  QWebSocket *webSocket = qobject_cast<QWebSocket *>(sender());

  if(!webSocket) return;

  if(m_WebSocket == webSocket) m_WebSocket = 0;

  webSocket->deleteLater();
}
