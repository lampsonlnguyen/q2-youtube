import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { createReadStream } from 'node:fs';
import { mkdtemp, readdir, rm, stat } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { randomBytes } from 'node:crypto';
import { pipeline } from 'node:stream/promises';

const token = randomBytes(18).toString('hex');
const route = new RegExp('^/' + token + '/([A-Za-z0-9_-]{11})$');
const searchRoute = '/' + token + '/search';
const host = process.env.Q2_YOUTUBE_HOST || '127.0.0.1';
const port = Number(process.env.Q2_YOUTUBE_PORT || 8765);
let busy = false;
let searching = false;

function run(command, args, signal, capture = false) {
  return new Promise((resolve, reject) => {
    const process = spawn(command, args, {
      signal,
      stdio: ['ignore', capture ? 'pipe' : 'ignore', 'inherit'],
      windowsHide: true,
    });
    let output = '';
    if (capture) {
      process.stdout.setEncoding('utf8');
      process.stdout.on('data', chunk => {
        output += chunk;
        if (output.length > 512 * 1024) {
          reject(new Error('Search response exceeded its size limit'));
          process.kill();
        }
      });
    }
    process.once('error', reject);
    process.once('close', code => {
      if (code === 0) resolve(output);
      else reject(new Error(command + ' exited with code ' + code));
    });
  });
}

function fail(response, status, message) {
  response.writeHead(status, { 'Content-Type': 'text/plain; charset=utf-8' });
  response.end(message + '\n');
}

function cell(value) {
  const clean = String(value || '').replace(/[\u0000-\u001f\u007f]/g, ' ').trim();
  let text = '';
  let bytes = 0;
  for (const character of clean) {
    bytes += Buffer.byteLength(character);
    if (bytes > 240) break;
    text += character;
  }
  return text;
}

async function searchVideos(query, response) {
  if (!query || !query.trim() || Buffer.byteLength(query) > 189 || /[\u0000-\u001f\u007f]/.test(query))
    return fail(response, 400, 'Enter a video title');
  if (searching) return fail(response, 503, 'A search is already running');
  searching = true;
  const abort = new AbortController();
  const timeout = setTimeout(() => abort.abort(), 45_000);
  response.once('close', () => {
    if (!response.writableFinished) abort.abort();
  });
  try {
    const json = await run(process.env.YT_DLP || 'yt-dlp', [
      '--ignore-config', '--flat-playlist', '--dump-single-json', '--skip-download',
      '--socket-timeout', '12', '--', 'ytsearch12:' + query.trim(),
    ], abort.signal, true);
    const entries = JSON.parse(json).entries || [];
    const rows = entries.filter(entry =>
      entry && /^[A-Za-z0-9_-]{11}$/.test(entry.id) &&
      entry.live_status !== 'is_live' && entry.live_status !== 'is_upcoming' &&
      (entry.duration == null || entry.duration <= 1200)
    ).slice(0, 12).map(entry =>
      [entry.id, cell(entry.title) || entry.id, cell(entry.channel || entry.uploader)].join('\t')
    );
    const body = rows.length ? rows.join('\n') + '\n' : '';
    response.writeHead(200, {
      'Content-Type': 'text/plain; charset=utf-8',
      'Content-Length': Buffer.byteLength(body),
      'Cache-Control': 'no-store',
    });
    response.end(body);
  } catch (error) {
    console.error('Search:', error.message);
    if (!response.destroyed) fail(response, 502, 'YouTube search failed');
  } finally {
    clearTimeout(timeout);
    searching = false;
  }
}

const server = createServer(async (request, response) => {
  if (request.method === 'GET' && request.url?.startsWith(searchRoute + '?')) {
    const query = new URLSearchParams(request.url.slice(searchRoute.length + 1)).get('q');
    return searchVideos(query, response);
  }
  const match = route.exec(request.url || '');
  if (request.method !== 'GET' || !match) return fail(response, 404, 'Not found');
  if (busy) return fail(response, 503, 'A transfer is already running');
  busy = true;
  const abort = new AbortController();
  const timeout = setTimeout(() => abort.abort(), 14 * 60 * 1000);
  response.once('close', () => {
    if (!response.writableFinished) abort.abort();
  });
  let folder;
  try {
    folder = await mkdtemp(join(tmpdir(), 'q2-youtube-'));
    await run(process.env.YT_DLP || 'yt-dlp', [
      '--ignore-config', '--no-playlist', '--no-progress', '--no-live-from-start',
      '--match-filter', '!is_live & duration <= 1200',
      '--max-filesize', '250M', '--max-downloads', '1', '--socket-timeout', '20',
      '-f', 'bv[height<=480]+ba/b[height<=480]',
      '-o', join(folder, 'source.%(ext)s'),
      '--', 'https://www.youtube.com/watch?v=' + match[1],
    ], abort.signal);
    const files = (await readdir(folder)).filter(name => /^source\.[a-z0-9]+$/i.test(name));
    if (files.length !== 1) throw new Error('No eligible video was downloaded');
    const output = join(folder, 'video.mp4');
    await run(process.env.FFMPEG || 'ffmpeg', [
      '-nostdin', '-hide_banner', '-loglevel', 'error',
      '-i', join(folder, files[0]), '-map', '0:v:0', '-map', '0:a:0?',
      '-vf', 'scale=374:320:force_original_aspect_ratio=decrease:force_divisible_by=2,setsar=1',
      '-t', '1200', '-r', '24', '-c:v', 'libx264', '-pix_fmt', 'yuv420p',
      '-preset', 'fast', '-crf', '23', '-c:a', 'aac', '-ac', '2', '-b:a', '96k',
      '-movflags', '+faststart', output,
    ], abort.signal);
    response.writeHead(200, {
      'Content-Type': 'video/mp4',
      'Content-Length': (await stat(output)).size,
      'Cache-Control': 'no-store',
    });
    await pipeline(createReadStream(output), response, { signal: abort.signal });
  } catch (error) {
    console.error(error.message);
    if (!response.destroyed && !response.headersSent) fail(response, 502, 'Video preparation failed');
    else if (!response.destroyed) response.destroy();
  } finally {
    clearTimeout(timeout);
    if (folder) await rm(folder, { recursive: true, force: true }).catch(console.error);
    busy = false;
  }
});

server.listen(port, host, () => {
  console.log('Save this line as .youtube/companion-url on the Q2 card:');
  console.log('http://' + host + ':' + server.address().port + '/' + token);
});
