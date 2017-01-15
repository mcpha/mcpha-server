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

#ifndef Server_h
#define Server_h

#include <stdint.h>

#include <QtCore/QObject>
#include <QtCore/QByteArray>

class QWebSocketServer;
class QWebSocket;

class Server: public QObject
{
  Q_OBJECT

public:
  Server(int16_t port, QObject *parent = 0);
  virtual ~Server();

private slots:
  void on_WebSocketServer_closed();
  void on_WebSocketServer_newConnection();
  void on_WebSocket_binaryMessageReceived(QByteArray message);
  void on_WebSocket_disconnected();

private:
  void *m_Sts;
  volatile void *m_Cfg;
  void *m_Hist[2], *m_Scope;
  volatile uint8_t *m_Rst[4];
  volatile uint32_t *m_Trg;
  QByteArray *m_BufferTimer;
  QByteArray *m_BufferHist;
  QByteArray *m_BufferStatus;
  QByteArray *m_BufferScope;
  QWebSocketServer *m_WebSocketServer;
  QWebSocket *m_WebSocket;
};

#endif
