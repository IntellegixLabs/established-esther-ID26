const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const destination = path.join(root, 'native');
const packageInfo = require(path.join(root, 'package.json'));

async function install() {
    if (process.env.ESTHER_SKIP_DOWNLOAD === '1') return;

    const targets = {
        'darwin-x64': 'esther-darwin-x64',
        'darwin-arm64': 'esther-darwin-arm64',
        'linux-x64': 'esther-linux-x64',
        'win32-x64': 'esther-win32-x64.exe',
    };
    const target = `${process.platform}-${process.arch}`;
    const asset = targets[target];
    if (!asset) {
        throw new Error(`No prebuilt Established runtime is available for ${target}.`);
    }

    const repositoryUrl = packageInfo.repository?.url?.replace(/^git\+/, '').replace(/\.git$/, '');
    const repository = repositoryUrl?.match(/^https:\/\/github\.com\/([^/]+)\/([^/]+)$/);
    if (!repository || repository[1] === 'OWNER' || repository[2] === 'REPOSITORY') {
        throw new Error('The package is missing its GitHub repository URL.');
    }

    const tag = `v${packageInfo.version}`;
    const url = `https://github.com/${repository[1]}/${repository[2]}/releases/download/${tag}/${asset}`;
    const response = await fetch(url);
    if (!response.ok) {
        throw new Error(`Could not download Established ${tag} for ${target} (${response.status}): ${url}`);
    }

    fs.mkdirSync(destination, { recursive: true });
    const executable = path.join(destination, process.platform === 'win32' ? 'esther.exe' : 'esther');
    fs.writeFileSync(executable, Buffer.from(await response.arrayBuffer()));
    if (process.platform !== 'win32') fs.chmodSync(executable, 0o755);
}

install().catch((error) => {
    console.error(`Established install failed: ${error.message}`);
    process.exitCode = 1;
});