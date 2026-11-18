const wasmWorker = new Worker("solver_worker.js");
document.getElementById("endgame-multithread").closest("label").hidden = true;

function wasmRequest(path, fields, update) {
    return new Promise((resolve, reject) => {
        const { port1, port2 } = new MessageChannel();
        port1.onmessage = ({ data }) => {
            if (data.progress) {
                try {
                    update(data.progress);
                } catch (error) {
                    port1.close();
                    reject(error);
                }
                return;
            }
            port1.close();
            resolve(data);
        };
        wasmWorker.postMessage({ path, body: new URLSearchParams(fields).toString() }, [port2]);
    });
}
