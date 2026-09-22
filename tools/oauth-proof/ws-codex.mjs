import { createHash, randomBytes } from "node:crypto";
import { readFile } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";
import tls from "node:tls";

const tokenPath = join(homedir(), ".speecher-m1", "openai.json");
const userAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36";
const dictationUrl = new URL("wss://chatgpt.com/backend-api/dictation/stream");
const transcribeUrl = "https://chatgpt.com/backend-api/transcribe";

function wavPcm(buffer) {
  if (buffer.toString("ascii", 0, 4) !== "RIFF" || buffer.toString("ascii", 8, 12) !== "WAVE") throw new Error("Expected a RIFF/WAVE file");
  let offset = 12; let format; let data;
  while (offset + 8 <= buffer.length) { const size = buffer.readUInt32LE(offset + 4); const chunk = buffer.subarray(offset + 8, offset + 8 + size); if (buffer.toString("ascii", offset, offset + 4) === "fmt ") format = chunk; if (buffer.toString("ascii", offset, offset + 4) === "data") data = chunk; offset += 8 + size + (size % 2); }
  if (!format || !data || format.readUInt16LE(0) !== 1 || format.readUInt16LE(2) !== 1 || format.readUInt32LE(4) !== 16000 || format.readUInt16LE(14) !== 16) throw new Error("Expected 16 kHz mono PCM16 WAV audio");
  return data;
}
function websocket(url, headers) {
  return new Promise((resolve, reject) => { const key = randomBytes(16).toString("base64"); const socket = tls.connect({ host: url.hostname, port: 443, servername: url.hostname }); let response = Buffer.alloc(0);
    socket.once("error", reject).once("secureConnect", () => socket.write(`GET ${url.pathname}${url.search} HTTP/1.1\r\nHost: ${url.host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n${Object.entries(headers).map(([name, value]) => `${name}: ${value}\r\n`).join("")}\r\n`)).on("data", (chunk) => { response = Buffer.concat([response, chunk]); const end = response.indexOf("\r\n\r\n"); if (end === -1) return; const head = response.subarray(0, end).toString("utf8"); const accept = createHash("sha1").update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest("base64"); if (!head.startsWith("HTTP/1.1 101") || !head.toLowerCase().includes(`sec-websocket-accept: ${accept.toLowerCase()}`)) { socket.destroy(); reject(new Error(`WebSocket upgrade failed: ${head.split("\r\n")[0]}`)); return; } socket.removeAllListeners("data"); resolve({ socket, initialData: response.subarray(end + 4) }); }); });
}
function frame(socket, opcode, payload = Buffer.alloc(0)) { const mask = randomBytes(4); const length = payload.length; const header = length < 126 ? Buffer.from([0x80 | opcode, 0x80 | length]) : length < 65536 ? Buffer.from([0x80 | opcode, 0x80 | 126, length >> 8, length & 255]) : (() => { const value = Buffer.alloc(10); value[0] = 0x80 | opcode; value[1] = 0x80 | 127; value.writeBigUInt64BE(BigInt(length), 2); return value; })(); const masked = Buffer.from(payload); for (let index = 0; index < masked.length; index += 1) masked[index] ^= mask[index % 4]; socket.write(Buffer.concat([header, mask, masked])); }
function events(socket, initialData, onText) { let buffer = initialData; let fragments = []; const read = () => { while (buffer.length >= 2) { const first = buffer[0]; let length = buffer[1] & 127; let offset = 2; if (length === 126) { if (buffer.length < 4) return; length = buffer.readUInt16BE(2); offset = 4; } if (length === 127) { if (buffer.length < 10) return; length = Number(buffer.readBigUInt64BE(2)); offset = 10; } if (buffer[1] & 128) offset += 4; if (buffer.length < offset + length) return; const body = buffer.subarray(offset, offset + length); buffer = buffer.subarray(offset + length); const opcode = first & 15; if (opcode === 9) frame(socket, 10, body); else if (opcode === 8) socket.end(); else if (opcode === 0) fragments.push(body); else if (opcode === 1) { fragments.push(body); if (first & 128) { onText(Buffer.concat(fragments).toString("utf8")); fragments = []; } } } }; socket.on("data", (chunk) => { buffer = Buffer.concat([buffer, chunk]); read(); }); read(); }

async function retranscribe(wav, token, streamedFinalChars) {
  const boundary = `----speecher-m1-${randomBytes(12).toString("hex")}`;
  const body = Buffer.concat([Buffer.from(`--${boundary}\r\nContent-Disposition: form-data; name="file"; filename="dictation.wav"\r\nContent-Type: audio/wav\r\n\r\n`), wav, Buffer.from(`\r\n--${boundary}--\r\n`)]);
  try {
    const response = await fetch(transcribeUrl, { method: "POST", headers: { Authorization: `Bearer ${token}`, "User-Agent": userAgent, "Content-Type": `multipart/form-data; boundary=${boundary}` }, body });
    const result = await response.json();
    const text = result.text?.trim();
    if (!response.ok || !text || text.length * 10 < streamedFinalChars * 6) {
      console.log("Final retranscribe did not replace the streamed transcript.");
      return;
    }
    console.log(JSON.stringify({ type: "transcript.retranscribed", text }));
  } catch (error) {
    console.log(`Final retranscribe failed; keeping streamed transcript: ${error.message}`);
  }
}

const [wavFile] = process.argv.slice(2).filter((argument) => argument !== "--final-retranscribe");
if (!wavFile) throw new Error("Usage: node ws-codex.mjs [--final-retranscribe] recording.wav");
const finalRetranscribe = process.argv.includes("--final-retranscribe");
const { access_token: token } = JSON.parse(await readFile(tokenPath, "utf8"));
if (!token) throw new Error(`No access_token in ${tokenPath}`);
const wav = await readFile(wavFile);
const pcm = wavPcm(wav);
const { socket, initialData } = await websocket(dictationUrl, { "User-Agent": userAgent, "Sec-WebSocket-Protocol": `chatgpt-dictation, openai-bearer.${token}` });
console.log(`Connected to ${dictationUrl.href}`);
let started = false; let closed = false; let streamedFinalChars = 0;
const send = (message) => frame(socket, 1, Buffer.from(JSON.stringify(message)));
const sendAudio = async () => { for (let offset = 0; offset < pcm.length; offset += 640) { send({ type: "audio.append", audio: pcm.subarray(offset, offset + 640).toString("base64") }); await new Promise((resolve) => setTimeout(resolve, 20)); } send({ type: "audio.flush", reason: "client" }); send({ type: "session.close" }); };
events(socket, initialData, (text) => { console.log(text); try { const event = JSON.parse(text); if (event.type === "session.started" && !started) { started = true; void sendAudio(); } if (event.type === "transcript.final") streamedFinalChars += event.text?.trim().length ?? 0; if (event.type === "session.updated" && event.session?.status === "closed") { closed = true; socket.end(); if (finalRetranscribe) void retranscribe(wav, token, streamedFinalChars); } if (event.type === "session.error" || event.type === "transcript.failed") { closed = true; socket.end(); process.exitCode = 1; } } catch { /* print non-JSON events unchanged */ } });
socket.on("error", (error) => { console.error(`WebSocket error: ${error.message}`); process.exitCode = 1; });
socket.on("close", () => { if (!closed) process.exitCode = 1; });
send({ type: "session.start", config: { input_audio_format: "pcm16", sample_rate_hz: 16000, num_channels: 1, max_buffer_size_bytes: 4 * 1024 * 1024, max_utterance_duration_ms: 30000, session_ttl_ms: 300000, provider_mode: "streaming_sse", transcript_delivery_mode: "segment", vad: { type: "server_vad", threshold: 0.5, prefix_padding_ms: 300, silence_duration_ms: 500 } } });
