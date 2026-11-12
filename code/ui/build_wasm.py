"""构建单线程网页版，或用 --publish 发布到 origin/gh-pages。"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def git(cwd, *args, env=None):
    return subprocess.run(["git", *args], cwd=cwd, env=env, check=True, timeout=90,
                          encoding="utf-8", stdout=subprocess.PIPE).stdout.strip()


def build(code, output):
    web = code / "ui" / "web"
    compiler = shutil.which("em++")
    env = os.environ.copy()
    if not compiler:
        try:
            sdk = Path(git(code.parent, "config", "--get", "mss.emsdk"))
        except subprocess.CalledProcessError:
            raise SystemExit("找不到 em++。请启用 Emscripten SDK，或配置 git config mss.emsdk <SDK 目录>。") from None
        compiler = sdk / "upstream" / "emscripten" / ("em++.bat" if os.name == "nt" else "em++")
        if not compiler.is_file():
            raise SystemExit(f"mss.emsdk 指向的 SDK 不可用：{sdk}")
        env["EM_CONFIG"] = str(sdk / ".emscripten")
    output.mkdir(parents=True, exist_ok=True)

    sources = sorted(source for folder in ("algo", "core", "game") for source in (code / folder).rglob("*.cpp"))
    sources += [code / "ui" / f"{name}.cpp" for name in ("session", "ui")]
    subprocess.run(
        [compiler, "-std=c++20", "-O3", "-fdeclspec", f"-ffile-prefix-map={code}=code", f"-I{code}",
         *map(str, sources), "--bind",
         "-sMODULARIZE=1", "-sEXPORT_NAME=createMssEngine", "-sENVIRONMENT=worker,node",
         "-sALLOW_MEMORY_GROWTH=1", "-sSTACK_SIZE=4194304", "-o", "mss_engine.js"],
        cwd=output, env=env, check=True, timeout=240,
    )

    for source in web.iterdir():
        if source.is_file():
            shutil.copyfile(source, output / source.name)
    subprocess.run(
        ["node", "-e", r"""
const fs = require("node:fs");
const createMssEngine = require("./mss_engine.js");
createMssEngine({ wasmBinary: fs.readFileSync("mss_engine.wasm") }).then((engine) => {
    const session = new engine.Session();
    const html = session.index(fs.readFileSync("index.html", "utf8"));
    session.delete();
    fs.writeFileSync("index.html", html.replace('<script src="state.js"', '<script src="wasm.js" defer></script>\n    <script src="state.js"'));
}).catch((error) => { console.error(error); process.exitCode = 1; });
"""],
        cwd=output, check=True, timeout=30,
    )
    (output / ".nojekyll").touch()
    print(output)


def publish(repository, output, checkout):
    files = sorted(output.iterdir())
    local_path = re.compile(rb"(?i)[a-z]:[\\/]+users[\\/]+")
    for file in files:
        if local_path.search(file.read_bytes()):
            raise SystemExit(f"{file.name} 含本机用户目录，发布已停止。")

    remote = git(repository, "remote", "get-url", "--push", "origin")
    git(checkout.parent, "clone", "--quiet", "--depth=1", "--single-branch", "--branch", "gh-pages",
        "--no-tags", "--", remote, str(checkout))
    git(checkout, "config", "core.autocrlf", "false")
    name, email = git(checkout, "log", "-1", "--format=%an%n%ae").splitlines()
    if not email.endswith("@users.noreply.github.com"):
        raise SystemExit("gh-pages 最新提交未使用 GitHub noreply 邮箱。")
    for file in files:
        shutil.copyfile(file, checkout / file.name)
    git(checkout, "add", "--", *(file.name for file in files))
    if not git(checkout, "status", "--porcelain"):
        print("网页发布文件相同，gh-pages 已是最新版。")
        return
    identity = os.environ | {"GIT_AUTHOR_NAME": name, "GIT_AUTHOR_EMAIL": email,
                             "GIT_COMMITTER_NAME": name, "GIT_COMMITTER_EMAIL": email}
    git(checkout, "commit", "--quiet", "-m", "Publish MSS WebAssembly site", env=identity)
    git(checkout, "push", "origin", "HEAD:refs/heads/gh-pages")
    print("已推送 gh-pages " + git(checkout, "rev-parse", "--short", "HEAD"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--publish", action="store_true")
    args = parser.parse_args()
    if (args.output is not None) == args.publish:
        parser.error("请指定构建目录，或使用 --publish。")
    code = Path(__file__).resolve().parents[1]
    if args.publish:
        with tempfile.TemporaryDirectory(prefix="mss-wasm-publish-") as directory:
            staging = Path(directory)
            output = staging / "site"
            build(code, output)
            publish(code.parent, output, staging / "gh-pages")
    else:
        build(code, args.output.resolve())


if __name__ == "__main__":
    main()
