import { createHash, randomBytes } from "node:crypto";
import { readFile } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";
import tls from "node:tls";

const tokenPath = join(homedir(), ".speecher-m1", "anthropic.json");
const userAgent = process.env.CLAUDE_CODE_VERSION ? `Claude-Code/${process.env.CLAUDE_CODE_VERSION}` : "Claude-Code";
const voiceUrl = new URL("wss://claude.ai/api/ws/speech_to_text/voice_stream");
voiceUrl.search = new URLSearchParams({ encoding: "linear16", sample_rate: "16000", channels: "1", endpointing_ms: "300", utterance_end_ms: "1000", language: "en", use_conversation_engine: "true", forward_interims: "typed", stt_provider: "deepgram-nova3" }).toString();

function wavPcm(buffer) {
  if (buffer.toString("ascii", 0, 4) !== "RIFF" || buffer.toString("ascii", 8, 12) !== "WAVE") throw new Error("Expected a RIFF/WAVE file");
  let offset = 12;
  let format;
  let data;
  while (offset + 8 <= buffer.length) {
    const size = buffer.readUInt32LE(offset + 4);
    const chunk = buffer.subarray(offset + 8, offset + 8 + size);
    if (buffer.toString("ascii", offset, offset + 4) === "fmt ") format = chunk;
    if (buffer.toString("ascii", offset, offset + 4) === "data") data = chunk;
    offset += 8 + size + (size % 2);
  }
  if (!format || !data || format.readUInt16LE(0) !== 1 || format.readUInt16LE(2) !== 1 || format.readUInt32LE(4) !== 16000 || format.readUInt16LE(14) !== 16) {
    throw new Error("Expected 16 kHz mono PCM16 WAV audio");
  }
  return data;
}

function websocket(url, headers) {
  return new Promise((resolve, reject) => {
    const key = randomBytes(16).toString("base64");
    const socket = tls.connect({ host: url.hostname, port: 443, servername: url.hostname });
    let response = Buffer.alloc(0);
    socket.once("error", reject).once("secureConnect", () => {
      socket.write(`GET ${url.pathname}${url.search} HTTP/1.1\r\nHost: ${url.host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n${Object.entries(headers).map(([name, value]) => `${name}: ${value}\r\n`).join("")}\r\n`);
    }).on("data", (chunk) => {
      response = Buffer.concat([response, chunk]);
      const end = response.indexOf("\r\n\r\n");
      if (end === -1) return;
      const head = response.subarray(0, end).toString("utf8");
      const rest = response.subarray(end + 4);
      const accept = createHash("sha1").update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest("base64");
      if (!head.startsWith("HTTP/1.1 101") || !head.toLowerCase().includes(`sec-websocket-accept: ${accept.toLowerCase()}`)) {
        socket.destroy();
        reject(new Error(`WebSocket upgrade failed: ${head.split("\r\n")[0]}`));
        return;
      }
      socket.removeAllListeners("data");
      resolve({ socket, initialData: rest });
    });
  });
}

function frame(socket, opcode, payload = Buffer.alloc(0)) {
  const mask = randomBytes(4);
  const length = payload.length;
  const header = length < 126 ? Buffer.from([0x80 | opcode, 0x80 | length]) : length < 65536 ? Buffer.from([0x80 | opcode, 0x80 | 126, length >> 8, length & 255]) : (() => { const value = Buffer.alloc(10); value[0] = 0x80 | opcode; value[1] = 0x80 | 127; value.writeBigUInt64BE(BigInt(length), 2); return value; })();
  const masked = Buffer.from(payload);
  for (let index = 0; index < masked.length; index += 1) masked[index] ^= mask[index % 4];
  socket.write(Buffer.concat([header, mask, masked]));
}

function events(socket, initialData, onText) {
  let buffer = initialData;
  let fragments = [];
  const read = () => {
    while (buffer.length >= 2) {
      const first = buffer[0]; let length = buffer[1] & 127; let offset = 2;
      if (length === 126) { if (buffer.length < 4) return; length = buffer.readUInt16BE(2); offset = 4; }
      if (length === 127) { if (buffer.length < 10) return; length = Number(buffer.readBigUInt64BE(2)); offset = 10; }
      const masked = (buffer[1] & 128) !== 0; if (masked) offset += 4;
      if (buffer.length < offset + length) return;
      let body = buffer.subarray(offset, offset + length); buffer = buffer.subarray(offset + length);
      if (masked) { const mask = buffer.subarray(2 + (buffer[1] & 127 === 126 ? 2 : buffer[1] & 127 === 127 ? 8 : 0), offset); body = Buffer.from(body); for (let index = 0; index < body.length; index += 1) body[index] ^= mask[index % 4]; }
      const opcode = first & 15; if (opcode === 9) frame(socket, 10, body); else if (opcode === 8) socket.end(); else if (opcode === 0) fragments.push(body); else if (opcode === 1) { fragments.push(body); if (first & 128) { onText(Buffer.concat(fragments).toString("utf8")); fragments = []; } }
    }
  };
  socket.on("data", (chunk) => { buffer = Buffer.concat([buffer, chunk]); read(); });
  read();
}

const wavFile = process.argv[2];
if (!wavFile) throw new Error("Usage: node ws-claude.mjs recording.wav");
const { access_token: token } = JSON.parse(await readFile(tokenPath, "utf8"));
if (!token) throw new Error(`No access_token in ${tokenPath}`);
const pcm = wavPcm(await readFile(wavFile));
const { socket, initialData } = await websocket(voiceUrl, { Authorization: `Bearer ${token}`, "User-Agent": userAgent, "x-app": "cli", "anthropic-client-platform": "linux" });
console.log(`Connected to ${voiceUrl.href}`);
let finalizing = false;
const finish = (code = 0) => { socket.end(); process.exitCode = code; };
events(socket, initialData, (text) => {
  console.log(text);
  try {
    const event = JSON.parse(text);
    if (event.type === "TranscriptEndpoint" || event.type === "TranscriptError" || event.type === "error" || event.error) finish(event.type === "TranscriptEndpoint" ? 0 : 1);
  } catch { /* print non-JSON events unchanged */ }
});
socket.on("error", (error) => { console.error(`WebSocket error: ${error.message}`); process.exitCode = 1; });
socket.on("close", () => { if (!finalizing) process.exitCode = 1; });
frame(socket, 1, Buffer.from('{"type":"KeepAlive"}'));
const frameBytes = 640;
for (let offset = 0; offset < pcm.length; offset += frameBytes) {
  frame(socket, 2, pcm.subarray(offset, offset + frameBytes));
  await new Promise((resolve) => setTimeout(resolve, 20));
}
finalizing = true;
frame(socket, 1, Buffer.from('{"type":"CloseStream"}'));
