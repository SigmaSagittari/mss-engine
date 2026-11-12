const session = Promise.resolve().then(() => {
    importScripts("mss_engine.js");
    return createMssEngine();
}).then((engine) => new engine.Session());

onmessage = async ({ data: { path, body }, ports: [port] }) => {
    try {
        const engine = await session;
        port.postMessage(JSON.parse(engine.handle(path, body)));
    } catch (error) {
        port.postMessage({ ok: false, error: error.message });
    } finally {
        port.close();
    }
};
