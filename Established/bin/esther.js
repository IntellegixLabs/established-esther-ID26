#!/usr/bin/env node

const { spawnSync } = require('node:child_process');
const path = require('node:path');

const executable = path.join(__dirname, '..', 'native', process.platform === 'win32' ? 'esther.exe' : 'esther');
const result = spawnSync(executable, process.argv.slice(2), { stdio: 'inherit' });

if (result.error) {
    console.error(`Could not start the Established runtime: ${result.error.message}`);
    process.exit(1);
}

process.exit(result.status ?? 1);