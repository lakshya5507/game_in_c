import { execFile, spawn, type ChildProcess } from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import { promisify } from 'node:util';
import express, { type Request, type Response } from 'express';
import { createServer as createViteServer } from 'vite';

const execFileAsync = promisify(execFile);

const ROOT_DIR = process.cwd();
const C_SOURCE_PATH = path.join(ROOT_DIR, 'game.c');
const BIN_PATH = '/tmp/vector_drop_c_bin';
const PORT = Number(process.env.PORT) || 3000;

const PTY_BRIDGE_SCRIPT = `
import os, pty, select, sys

cmd = sys.argv[1]
pid, master = pty.fork()
if pid == 0:
    os.execv(cmd, [cmd])
else:
    exit_code = 0
    try:
        while True:
            r, _, _ = select.select([master, 0], [], [], 0.05)
            if master in r:
                try:
                    data = os.read(master, 4096)
                except OSError:
                    break
                if not data:
                    break
                os.write(1, data)
            if 0 in r:
                inp = os.read(0, 1024)
                if not inp:
                    break
                os.write(master, inp)
    finally:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            _, status = os.waitpid(pid, 0)
            exit_code = os.waitstatus_to_exitcode(status)
        except Exception:
            pass
    sys.exit(exit_code)
`;

let activeProcess: ChildProcess | null = null;
const sseClients = new Set<Response>();

function broadcastEvent(event: string, payload: unknown): void {
  const data = `event: ${event}\ndata: ${JSON.stringify(payload)}\n\n`;
  for (const client of sseClients) {
    client.write(data);
  }
}

function stopActiveProcess(): void {
  if (activeProcess) {
    try {
      activeProcess.kill('SIGTERM');
    } catch {
      // Ignore already exited
    }
    activeProcess = null;
  }
}

async function startServer(): Promise<void> {
  const app = express();
  app.use(express.json({ limit: '128kb' }));

  const defaultSource = await fs.readFile(C_SOURCE_PATH, 'utf8');

  app.get('/api/source', async (_req: Request, res: Response) => {
    try {
      const source = await fs.readFile(C_SOURCE_PATH, 'utf8');
      res.json({ source, running: activeProcess !== null });
    } catch (err) {
      res.status(500).json({ error: err instanceof Error ? err.message : 'Failed to read game.c' });
    }
  });

  app.post('/api/source/reset', async (_req: Request, res: Response) => {
    try {
      await fs.writeFile(C_SOURCE_PATH, defaultSource, 'utf8');
      res.json({ source: defaultSource });
    } catch (err) {
      res.status(500).json({ error: err instanceof Error ? err.message : 'Failed to reset game.c' });
    }
  });

  app.get('/api/stream', (req: Request, res: Response) => {
    res.setHeader('Content-Type', 'text/event-stream');
    res.setHeader('Cache-Control', 'no-cache');
    res.setHeader('Connection', 'keep-alive');
    res.flushHeaders();

    sseClients.add(res);
    res.write(`event: status\ndata: ${JSON.stringify({ running: activeProcess !== null })}\n\n`);

    req.on('close', () => {
      sseClients.delete(res);
    });
  });

  app.post('/api/run', async (req: Request, res: Response) => {
    const body = req.body as { source?: unknown } | undefined;
    if (body && typeof body.source === 'string') {
      if (body.source.length > 65536) {
        res.status(400).json({ ok: false, error: 'Source file exceeds 64 KB limit.' });
        return;
      }
      await fs.writeFile(C_SOURCE_PATH, body.source, 'utf8');
    }

    stopActiveProcess();

    try {
      const { stderr } = await execFileAsync(
        'gcc',
        [C_SOURCE_PATH, '-O2', '-Wall', '-Wextra', '-o', BIN_PATH],
        { timeout: 10000 }
      );

      const child = spawn('python3', ['-u', '-c', PTY_BRIDGE_SCRIPT, BIN_PATH], {
        stdio: ['pipe', 'pipe', 'pipe'],
      });
      activeProcess = child;

      broadcastEvent('status', { running: true });

      child.stdout.on('data', (chunk: Buffer) => {
        broadcastEvent('output', { text: chunk.toString('utf8') });
      });

      child.stderr.on('data', (chunk: Buffer) => {
        broadcastEvent('output', { text: chunk.toString('utf8') });
      });

      child.on('exit', (code) => {
        if (activeProcess === child) {
          activeProcess = null;
        }
        broadcastEvent('status', { running: false, exitCode: code ?? 0 });
      });

      res.json({ ok: true, warnings: stderr.trim() || null });
    } catch (err) {
      const errorObj = err as { stderr?: string; message?: string };
      const message = errorObj.stderr?.trim() || errorObj.message || 'Compilation failed';
      broadcastEvent('status', { running: false });
      res.status(400).json({ ok: false, error: message });
    }
  });

  app.post('/api/input', (req: Request, res: Response) => {
    const body = req.body as { key?: unknown } | undefined;
    if (!body || typeof body.key !== 'string' || body.key.length === 0 || body.key.length > 16) {
      res.status(400).json({ ok: false });
      return;
    }

    if (activeProcess && activeProcess.stdin && !activeProcess.stdin.destroyed) {
      activeProcess.stdin.write(body.key);
      res.json({ ok: true });
      return;
    }

    res.status(409).json({ ok: false, error: 'No C process is currently running.' });
  });

  app.post('/api/stop', (_req: Request, res: Response) => {
    stopActiveProcess();
    broadcastEvent('status', { running: false });
    res.json({ ok: true });
  });

  if (process.env.NODE_ENV === 'production') {
    const distDir = path.join(ROOT_DIR, 'dist');
    app.use(express.static(distDir));
    app.get('*', (_req: Request, res: Response) => {
      res.sendFile(path.join(distDir, 'index.html'));
    });
  } else {
    const vite = await createViteServer({
      server: { middlewareMode: true },
      appType: 'spa',
    });
    app.use(vite.middlewares);
  }

  const server = app.listen(PORT, '0.0.0.0', () => {
    console.log(`Server listening on http://0.0.0.0:${PORT}`);
  });

  const shutdown = () => {
    stopActiveProcess();
    server.close(() => process.exit(0));
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

void startServer();
