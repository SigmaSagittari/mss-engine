const session = Promise.resolve().then(() => {
    importScripts("mss_engine.js");
    return createMssEngine();
}).then((engine) => new engine.Session());

onmessage = async ({ data: { path, body }, ports: [port] }) => {
    try {
        const engine = await session;
        const result = path === "/api/analyze-ioe/stream"
            ? engine.analyzeIoeStream(body, update => port.postMessage(JSON.parse(update)))
            : engine.handle(path, body);
        port.postMessage(JSON.parse(result));
    } catch (error) {
        port.postMessage({ ok: false, error: error.message });
    } finally {
        port.close();
    }
};
