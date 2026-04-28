// Mock WebSocket server for E2E tests.
// Simulates plugin messages on 127.0.0.1:7170.

import { WebSocketServer, WebSocket } from 'ws';

export class MockPluginServer {
  private wss: WebSocketServer;
  private clients: Set<WebSocket> = new Set();

  constructor(private port = 7170) {
    this.wss = new WebSocketServer({ host: '127.0.0.1', port });
    this.wss.on('connection', (ws) => {
      this.clients.add(ws);
      ws.on('close', () => this.clients.delete(ws));
    });
  }

  private broadcast(data: string): void {
    for (const client of this.clients) {
      if (client.readyState === WebSocket.OPEN) {
        client.send(data);
      }
    }
  }

  sendConfig(room: string, push: string): void {
    this.broadcast(JSON.stringify({
      type: 'config',
      room,
      push,
      noaudio: true,
      wsPort: this.port,
    }));
  }

  sendBufferDelay(delayMs: number): void {
    this.broadcast(JSON.stringify({
      type: 'bufferDelay',
      delayMs,
    }));
  }

  sendBeatHeartbeat(beat: number, bpi: number, interval: number): void {
    this.broadcast(JSON.stringify({
      type: 'beatHeartbeat',
      beat,
      bpi,
      interval,
    }));
  }

  sendRoster(users: Array<{ idx: number; name: string; streamId: string }>): void {
    this.broadcast(JSON.stringify({
      type: 'roster',
      users,
    }));
  }

  sendDeactivate(): void {
    this.broadcast(JSON.stringify({ type: 'deactivate' }));
  }

  get clientCount(): number {
    return this.clients.size;
  }

  async waitForClient(timeoutMs = 5000): Promise<void> {
    if (this.clients.size > 0) return;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Timeout waiting for WS client')), timeoutMs);
      this.wss.once('connection', () => {
        clearTimeout(timer);
        resolve();
      });
    });
  }

  async close(): Promise<void> {
    for (const client of this.clients) {
      client.close();
    }
    this.clients.clear();
    return new Promise((resolve) => {
      this.wss.close(() => resolve());
    });
  }
}
