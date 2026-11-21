let wasmWorker = new Worker("solver_worker.js");
const wasmRequests = new Set();
document.getElementById("endgame-multithread").closest("label").hidden = true;

function wasmRequest(path, fields, update, signal) {
    return new Promise((resolve, reject) => {
        signal?.throwIfAborted();
        wasmWorker ??= new Worker("solver_worker.js");
        const { port1, port2 } = new MessageChannel();
        const close = () => {
            port1.close();
            signal?.removeEventListener("abort", stop);
            wasmRequests.delete(fail);
        };
        const fail = error => {
            close();
            reject(error);
        };
        const stop = () => {
            wasmWorker.terminate();
            wasmWorker = null;
            cachedAnalysis = null;
            for (const failRequest of wasmRequests) failRequest(signal.reason);
        };
        wasmRequests.add(fail);
        signal?.addEventListener("abort", stop);
        port1.onmessage = ({ data }) => {
            if (data.progress) {
                try {
                    update(data.progress);
                } catch (error) {
                    fail(error);
                }
                return;
            }
            close();
            resolve(data);
        };
        wasmWorker.postMessage({ path, body: new URLSearchParams(fields).toString() }, [port2]);
    });
}
