import { Bridge } from './bridge/Bridge.js';
import { log }    from './config.js';

const bridge = new Bridge();

process.on('SIGINT',  () => void shutdown('SIGINT'));
process.on('SIGTERM', () => void shutdown('SIGTERM'));

async function shutdown(signal: string): Promise<void> {
  log('info', `Received ${signal}, shutting down`);
  try {
    await bridge.stop();
  } finally {
    process.exit(0);
  }
}

bridge.start().catch((err: unknown) => {
  log('error', 'Bridge start failed', err);
  process.exit(1);
});
