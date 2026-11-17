const wasmWorker = new Worker("solver_worker.js");
document.getElementById("endgame-multithread").closest("label").hidden = true;

function wasmRequest(path, fields, update) {
    return new Promise((resolve) => {
        const { port1, port2 } = new MessageChannel();
        port1.onmessage = ({ data }) => {
            if (data.progress) {
                update(data.progress);
                return;
            }
            port1.close();
            resolve(data);
        };
        wasmWorker.postMessage({ path, body: new URLSearchParams(fields).toString() }, [port2]);
    });
}
