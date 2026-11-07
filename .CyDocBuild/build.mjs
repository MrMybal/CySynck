import { readFile, writeFile, mkdir, cp, rm, rename, stat, realpath, lstat } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { join, dirname, resolve, relative } from 'node:path';
import { build } from 'esbuild';
import { validate } from './validate.mjs';
import { zipDirectory } from './zip.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');
const config = JSON.parse(await readFile(join(here, 'cydoc.config.json')));
const project = await readFile(join(root, 'CMakeLists.txt'), 'utf8');
const version = project.match(/project\(CySynck VERSION ([\d.]+)/)?.[1];
if (!version || version !== config.version) throw new Error('Keep the manual version aligned with CMakeLists.txt');
const staging = join(here, '.cache', 'export');
const output = join(root, 'Documentation');
async function assertOwned(path) {
  const canonicalRoot = await realpath(root);
  let existing = path;
  for (;;) {
    try {
      const info = await lstat(existing);
      if (info.isSymbolicLink()) throw new Error('Refusing a linked documentation output');
      const resolved = await realpath(existing), delta = relative(canonicalRoot, resolved);
      if (!delta || delta.startsWith('..') || resolve(canonicalRoot, delta) !== resolved)
        throw new Error('Documentation output must remain inside the application workspace');
      return;
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
      existing = dirname(existing);
    }
  }
}
// Only this owned staging directory is cleared. Public output is replaced after validation.
await assertOwned(staging);
await rm(staging, { recursive: true, force: true });
await mkdir(staging, { recursive: true });
const temporaryConfig = join(here, '.cydoc-staging.json');
try {
  const inventory = JSON.parse(await readFile(join(here, 'internal/migration.json')));
  const aliases = { ...config.aliases };
  for (const item of inventory.documents.filter(item => item.disposition === 'manual')) {
    aliases[item.source] = item.destination;
    if (item.source.startsWith('docs/')) aliases[item.source.slice(5)] = item.destination;
  }
  await writeFile(temporaryConfig, JSON.stringify({ ...config, aliases, out: relative(here, staging) }));
  const cli = spawnSync(process.execPath, [join(here, 'node_modules/cydoc/tools/cli.mjs'), 'check', temporaryConfig], {
    cwd: here, stdio: 'inherit'
  });
  if (cli.error) throw cli.error;
  if (cli.status !== 0) throw new Error(`CyDoc validation failed (${cli.status})`);
  // Use the public mountViewer contract in an application-owned host template.
  // The official exporter still compiles the content, SVG diagrams and local data provider.
  await cp(join(here, 'reader.html'), join(staging, 'Documentation.html'));
  await rm(join(staging, 'DocData/assets/.gitkeep'), { force: true });
  await build({ entryPoints: [join(here, 'reader.mjs')], outfile: join(staging, 'DocData/viewer.js'), bundle: true,
    format: 'iife', platform: 'browser', target: 'es2020', minify: true, legalComments: 'eof' });
  const notices = join(staging, 'DocData/licenses');
  await mkdir(notices, { recursive: true });
  for (const [source, name] of [['cydoc/LICENSE', 'CyDoc-MIT.txt'], ['prismjs/LICENSE', 'Prism-MIT.txt']])
    await cp(join(here, 'node_modules', source), join(notices, name));
  await cp(join(root, 'LICENSE'), join(notices, 'CySynck-MIT.txt'));
  await writeFile(join(notices, 'README.txt'), 'CySynck manual: MIT. CyDoc 0.1.0 (e038c474577e1f305df3eba421f2b9c03b0f1d8d): MIT.\nPrism syntax highlighting: MIT. See the accompanying notices. No optional media modules or Three.js are bundled.\n');
  await validate(staging, { root, here });
  // Guard the managed public destination; never remove an unrelated tree.
  const exists = await stat(output).catch(error => { if (error.code !== 'ENOENT') throw error; });
  if (exists) {
    const marker = await readFile(join(output, '.cysynck-generated'), 'utf8').catch(() => '');
    if (marker !== 'CySynck CyDoc export\n') throw new Error('Documentation/ is not a managed CySynck export; preserve it before regenerating');
    await assertOwned(output);
    await rm(output, { recursive: true });
  }
  await writeFile(join(staging, '.cysynck-generated'), 'CySynck CyDoc export\n');
  await rename(staging, output);
  await zipDirectory(output, join(here, '.cache', 'CySynck-Documentation.zip'));
  console.log(`CySynck documentation ${version}: validated English/French export and offline archive`);
} finally {
  await rm(temporaryConfig, { force: true });
}
