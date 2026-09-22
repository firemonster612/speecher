import { createHash, randomBytes } from "node:crypto";
import { createServer } from "node:http";
import { chmod, mkdir, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { createInterface } from "node:readline";
import { join } from "node:path";

const clientId = "app_EMoamEEZ73f0CkXaXp7hrann";
const redirectUri = "http://localhost:1455/auth/callback";
const scope = "openid profile email offline_access api.connectors.read api.connectors.invoke";

function base64url(bytes) { return Buffer.from(bytes).toString("base64url"); }
function redact(value) { return value ? `${value.slice(0, 8)}…` : "(missing)"; }
function parseResult(value) {
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
      if (url.pathname !== "/auth/callback") return response.writeHead(404).end("Not found");
      response.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
      response.end("Speecher received the authorization response. You can close this tab.");
      finish({ code: url.searchParams.get("code"), state: url.searchParams.get("state") });
    }).once("error", reject).listen(1455, "127.0.0.1");
    input = createInterface({ input: process.stdin, output: process.stdout });
    input.question("Paste the callback URL or code#state if needed: ", (value) => { finish(parseResult(value)); });
  });
  if (!result.code || result.state !== state) throw new Error("OAuth response did not contain the expected code and state");
  return result.code;
}
function accountId(idToken) {
  if (!idToken) return null;
  const [, payload] = idToken.split(".");
  if (!payload) return null;
  return JSON.parse(Buffer.from(payload, "base64url").toString("utf8")).chatgpt_account_id ?? null;
}

const verifier = base64url(randomBytes(32));
const state = base64url(randomBytes(24));
const challenge = base64url(createHash("sha256").update(verifier).digest());
const authorize = new URL("https://auth.openai.com/oauth/authorize");
authorize.search = new URLSearchParams({ client_id: clientId, redirect_uri: redirectUri, response_type: "code", code_challenge: challenge, code_challenge_method: "S256", state, scope, id_token_add_organizations: "true", codex_cli_simplified_flow: "true", originator: "codex_cli_rs" }).toString();
console.log("Open this URL in any browser:");
console.log(authorize.href);
const code = await waitForAuthorization(state);
const tokenResponse = await fetch("https://auth.openai.com/oauth/token", {
  method: "POST",
  headers: { "Content-Type": "application/x-www-form-urlencoded" },
  body: new URLSearchParams({ grant_type: "authorization_code", code, redirect_uri: redirectUri, client_id: clientId, code_verifier: verifier }),
});
const tokens = await tokenResponse.json();
if (!tokenResponse.ok || !tokens.access_token) throw new Error(`OpenAI token exchange failed (${tokenResponse.status}): ${tokens.error_description ?? tokens.error ?? "no access token returned"}`);
const expiresAt = tokens.expires_in ? new Date(Date.now() + Number(tokens.expires_in) * 1000).toISOString() : null;
const chatgptAccountId = accountId(tokens.id_token);
const directory = join(homedir(), ".speecher-m1");
const destination = join(directory, "openai.json");
await mkdir(directory, { recursive: true, mode: 0o700 });
await writeFile(destination, `${JSON.stringify({ ...tokens, chatgpt_account_id: chatgptAccountId, expires_at: expiresAt }, null, 2)}\n`, { mode: 0o600 });
await chmod(destination, 0o600);
console.log(`access_token: ${redact(tokens.access_token)}`);
console.log(`refresh_token: ${redact(tokens.refresh_token)}`);
console.log(`chatgpt_account_id: ${chatgptAccountId ?? "missing"}`);
console.log(`scope: ${tokens.scope ?? scope}`);
console.log(`expires_at: ${expiresAt ?? "not supplied"}`);
console.log(`Saved ${destination} with mode 600.`);
