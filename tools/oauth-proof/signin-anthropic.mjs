import { createHash, randomBytes } from "node:crypto";
import { createServer } from "node:http";
import { chmod, mkdir, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { createInterface } from "node:readline";
import { join } from "node:path";

const clientId = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
const redirectUri = "http://localhost:54545/callback";
const scopes = "user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload";

function base64url(bytes) {
  return Buffer.from(bytes).toString("base64url");
}

function redact(value) {
  return value ? `${value.slice(0, 8)}…` : "(missing)";
}

function parseAuthorizationResult(value) {
  const text = value.trim();
  if (!text.includes("=") && text.includes("#")) {
    const [code, state] = text.split("#", 2);
    return { code, state };
  }
  const query = text.includes("://") ? new URL(text).searchParams : new URLSearchParams(text.replace(/^\?/, "").replace("#", "&"));
  return { code: query.get("code"), state: query.get("state") };
}

async function waitForAuthorization(state) {
  let server;
  let input;
  let finish;
  const result = await new Promise((resolve, reject) => {
    finish = (value) => {
      server.close();
      input.close();
      resolve(value);
    };
    server = createServer((request, response) => {
      const url = new URL(request.url, redirectUri);
      if (url.pathname !== "/callback") {
        response.writeHead(404).end("Not found");
        return;
      }
      response.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
      response.end("Speecher received the authorization response. You can close this tab.");
      finish({ code: url.searchParams.get("code"), state: url.searchParams.get("state") });
    }).once("error", reject).listen(54545, "127.0.0.1");
    input = createInterface({ input: process.stdin, output: process.stdout });
    input.question("Paste code#state if the callback cannot reach this machine: ", (value) => {
      finish(parseAuthorizationResult(value));
    });
  });
  if (!result.code || result.state !== state) {
    throw new Error("OAuth response did not contain the expected code and state");
  }
  return result.code;
}

const verifier = base64url(randomBytes(32));
const state = base64url(randomBytes(24));
const challenge = base64url(createHash("sha256").update(verifier).digest());
const authorize = new URL("https://claude.ai/oauth/authorize");
authorize.search = new URLSearchParams({
  client_id: clientId,
  redirect_uri: redirectUri,
  response_type: "code",
  code_challenge: challenge,
  code_challenge_method: "S256",
  state,
  code: "true",
  scope: scopes,
}).toString();

console.log("Open this URL in any browser:");
console.log(authorize.href);
const code = await waitForAuthorization(state);
const tokenResponse = await fetch("https://platform.claude.com/v1/oauth/token", {
  method: "POST",
  headers: {
    "Content-Type": "application/json",
    Accept: "application/json, text/plain, */*",
    "User-Agent": "axios/1.15.2",
  },
  // Order matches the native token traffic documented in oauth-flows.md.
  body: JSON.stringify({ grant_type: "authorization_code", code, redirect_uri: redirectUri, client_id: clientId, code_verifier: verifier, state }),
});
const tokens = await tokenResponse.json();
if (!tokenResponse.ok || !tokens.access_token) {
  throw new Error(`Anthropic token exchange failed (${tokenResponse.status}): ${tokens.error_description ?? tokens.error ?? "no access token returned"}`);
}
const expiresAt = tokens.expires_in ? new Date(Date.now() + Number(tokens.expires_in) * 1000).toISOString() : null;
const destination = join(homedir(), ".speecher-m1", "anthropic.json");
await mkdir(join(homedir(), ".speecher-m1"), { recursive: true, mode: 0o700 });
await writeFile(destination, `${JSON.stringify({ ...tokens, expires_at: expiresAt }, null, 2)}\n`, { mode: 0o600 });
await chmod(destination, 0o600);
console.log(`access_token: ${redact(tokens.access_token)}`);
console.log(`refresh_token: ${redact(tokens.refresh_token)}`);
console.log(`scope: ${tokens.scope ?? scopes}`);
console.log(`expires_at: ${expiresAt ?? "not supplied"}`);
console.log(`Saved ${destination} with mode 600.`);
