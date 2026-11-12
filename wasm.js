const wasmWorker = new Worker("solver_worker.js");
document.getElementById("endgame-multithread").closest("label").hidden = true;

function wasmRequest(path, fields) {
    return new Promise((resolve) => {
        const { port1, port2 } = new MessageChannel();
        port1.onmessage = ({ data }) => {
            port1.close();
            resolve(data);
        };
        wasmWorker.postMessage({ path, body: new URLSearchParams(fields).toString() }, [port2]);
    });
}
