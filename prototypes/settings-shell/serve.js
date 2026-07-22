// Throwaway prototype server. Serves this directory only. Run: bun run serve
const port = Number(process.env.PORT ?? 4173);

Bun.serve({
  port,
  async fetch(req) {
    const pathname = new URL(req.url).pathname;
    const name = pathname === "/" ? "index.html" : pathname.slice(1);
    if (name.includes("..")) {
      return new Response("Bad request", { status: 400 });
    }
    const file = Bun.file(`${import.meta.dir}/${name}`);
    if (!(await file.exists())) {
      return new Response("Not found", { status: 404 });
    }
    return new Response(file);
  },
});

console.log(`Speecher settings-shell prototype: http://localhost:${port}/?variant=A`);
