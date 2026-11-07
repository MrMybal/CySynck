import assert from 'node:assert/strict';
import { readFile, readdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
export async function validate(directory, { root, here }) {
  const manifest = JSON.parse(await readFile(join(directory, 'DocData/manifest.json')));
  for (const filename of ['README.md', 'README.fr.md']) {
    const entry = await readFile(join(here, 'distribution', filename), 'utf8');
    assert(entry.includes(`# CySynck ${manifest.version}`), `Update the package entry version: ${filename}`);
    for (const match of entry.matchAll(/\]\(Documentation\/Documentation\.html#\/(en|fr)\/([^/)\s]+)\)/g))
      assert(manifest.pages.some(page => page.id === match[2] && page.locale === match[1]), `Broken package entry: ${filename}/${match[2]}`);
  }
  assert.equal(manifest.defaultLocale, 'en');
  assert.deepEqual(manifest.locales.map(item => item.code).sort(), ['en', 'fr']);
  const pages = new Map();
  for (const page of manifest.pages) {
    assert(page.text.trim(), `Empty page: ${page.id}/${page.locale}`);
    assert(!pages.has(page.id + '/' + page.locale), `Duplicate page: ${page.id}`);
    pages.set(page.id + '/' + page.locale, page);
    assert(!/Source files|Fichiers source/.test(page.section), 'No implementation browser');
    if (page.id.startsWith('source-')) assert(!page.code, 'Do not distribute raw source listings');
    for (const href of page.html.matchAll(/(?:href|src)="([^"]+)"/g)) {
      assert(!/^(?:\.\.\/|\/)?(?:docs\/|src\/|include\/)/.test(href[1]), `Unmigrated link: ${href[1]}`);
      assert(!/^https?:/.test(href[1]) || href[0].startsWith('href='), 'Runtime resources must be local');
    }
  }
  for (const page of manifest.pages.filter(page => page.locale === 'en')) {
    const translated = pages.get(page.id + '/fr');
    assert(translated, `Missing French: ${page.id}`);
    assert.deepEqual(translated.headings.map(h => h.id).sort(), page.headings.map(h => h.id).sort(), `Anchor parity: ${page.id}`);
    assert.deepEqual(translated.anchors?.slice().sort(), page.anchors?.slice().sort(), `Code anchor parity: ${page.id}`);
    if (!page.id.startsWith('source-') && !['cpp-api', 'c-api', 'plugin-api'].includes(page.id)) {
      assert.equal(translated.sourceRevision, page.revision, `Outdated French: ${page.id}`);
      assert.notEqual(translated.text, page.text, `Untranslated page: ${page.id}`);
    }
  }
  const data = await readFile(join(directory, 'DocData/data.js'), 'utf8');
  assert(/"modules":\[\]/.test(data), 'Unexpected optional module');
  const files = await readdir(join(directory, 'DocData'));
  assert(!files.some(file => file.startsWith('media-')), 'Disabled module was shipped');
  const viewer = await readFile(join(directory, 'DocData/viewer.js'), 'utf8');
  assert(!/three\.module|WebGLRenderer|THREE\.REVISION/.test(viewer), 'Unexpected Three.js');
  const sources = manifest.pages.filter(page => page.id.startsWith('source-'));
  assert.equal(sources.length, 6, 'Index only the three distributed public headers');
  // CyDoc's explicit source extractor has no @sourceRevision tag. Pin reviewed EN/FR
  // compiled revisions outside the source instead of silently declaring them current.
  const pins = JSON.parse(await readFile(join(here, 'api-translations.json')));
  assert.equal(Object.keys(pins).length, 6);
  for (const [id, locales] of Object.entries(pins)) {
    for (const locale of ['en', 'fr']) {
      const page = pages.get(id + '/' + locale);
      assert(page, `Missing public API translation: ${id}/${locale}`);
      assert.equal(page.revision, locales[locale], `Review the changed API translation before updating api-translations.json: ${id}/${locale}`);
    }
  }
  const report = JSON.parse(await readFile(join(directory, 'DocData/translation-report.json')));
  assert.equal(report.missing.length, 0);
  assert.equal(report.outdated.length, 0);
  assert.deepEqual(report.untracked.map(item => item.id).sort(), Object.keys(pins).sort());
  await writeFile(join(directory, 'DocData/application-translation-report.json'), JSON.stringify({
    defaultLocale: 'en', current: report.current + Object.keys(pins).length,
    missing: [], outdated: [], untracked: [], publicApiPins: pins,
    note: 'CyDoc source annotations and generated include references are gated by reviewed EN/FR revision pins; authored pages use revision/sourceRevision.'
  }, null, 2) + '\n');
  const commandSource = await readFile(join(root, 'src/commands.cpp'), 'utf8');
  const syntax = commandSource.match(/\{"help", R"\(([\s\S]*?)\)"\}\}/)?.[1];
  assert(syntax);
  for (const locale of ['en', 'fr']) {
    const reference = await readFile(join(here, 'manual', locale, 'cli.md'), 'utf8');
    assert(reference.includes(syntax), `Regenerate the CLI reference with node .CyDocBuild/update-cli.mjs (${locale})`);
  }
  const contexts = JSON.parse(await readFile(join(root, 'assets/web/help-contexts.json')));
  for (const [context, target] of Object.entries(contexts)) {
    const [id, anchor] = target.split('#');
    for (const locale of ['en', 'fr']) {
      const page = pages.get(id + '/' + locale);
      assert(page, `Help context ${context}: missing page ${id}/${locale}`);
      assert(!anchor || page.headings.some(h => h.id === anchor), `Help context ${context}: missing anchor ${anchor}`);
    }
  }
  const inventory = JSON.parse(await readFile(join(here, 'internal/migration.json')));
  for (const item of inventory.documents) {
    assert(item.destination && item.disposition, `Unclassified legacy document ${item.source}`);
    if (item.disposition === 'manual') assert(pages.has(item.destination + '/en'), `Unmigrated document ${item.source}`);
    const original = await readFile(join(here, 'internal/legacy', item.source.split('/').at(-1)), 'utf8');
    assert.equal(createHash('sha256').update(original.replaceAll('\r\n', '\n')).digest('hex'), item.archiveSha256Lf,
      `Preserved original changed: ${item.source}`);
  }
  for (const filename of (await readdir(join(root, 'docs'))).filter(name => name.endsWith('.md'))) {
    const stub = await readFile(join(root, 'docs', filename), 'utf8');
    for (const match of stub.matchAll(/\.\.\/Documentation\/Documentation\.html#\/(en|fr)\/([^/)\s]+)\/([^\s)]+)/g)) {
      const page = pages.get(match[2] + '/' + match[1]);
      assert(page?.headings.some(heading => heading.id === match[3]), `Broken legacy compatibility link: ${filename}/${match[3]}`);
    }
  }
  console.log(`Verified ${manifest.pages.length} localized pages, matching anchors, public API scope, help contexts and legacy inventory`);
  return manifest;
}
