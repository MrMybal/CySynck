// Unit tests in a DOM model, not a substitute for native file-origin browser testing.
import { before, test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile, stat } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Window } from 'happy-dom';
import { createDocumentation, createLink, parseLink } from 'cydoc';

const here = join(dirname(fileURLToPath(import.meta.url)), '..');
const output = join(here, '../Documentation');
let manifest, data, bundle;
before(async () => {
  const build = spawnSync(process.execPath, ['build.mjs'], { cwd: here, stdio: 'inherit' });
  assert.equal(build.status, 0);
  manifest = JSON.parse(await readFile(join(output, 'DocData/manifest.json')));
  data = await readFile(join(output, 'DocData/data.js'), 'utf8');
  bundle = await readFile(join(output, 'DocData/viewer.js'), 'utf8');
});
function reader(hash = '#/en/overview', savedTheme) {
  const window = new Window({ url: 'https://manual.cysynck.invalid/Documentation.html' + hash,
    settings: { enableJavaScriptEvaluation: true, suppressInsecureJavaScriptEnvironmentWarning: true,
      disableJavaScriptFileLoading: true, enableCSSFileLoading: false } });
  window.document.body.innerHTML = '<div id="app"></div>';
  // Exercise only local compiled code. A network or dynamic module request fails this test.
  window.fetch = () => { throw new Error('The offline reader attempted a network request'); };
  window.XMLHttpRequest = class { constructor() { throw new Error('The offline reader attempted XHR'); } };
  if (savedTheme) window.localStorage.setItem('cysynck-help:theme', savedTheme);
  window.eval(data);
  window.eval(bundle);
  return window;
}
function navigate(window, hash) {
  window.history.replaceState(null, '', hash);
  window.dispatchEvent(new window.HashChangeEvent('hashchange'));
}
test('every translated topic and anchor has a local compiled target', async () => {
  const docs = createDocumentation(manifest);
  assert.deepEqual(docs.availableLocales().map(locale => locale.code), ['en', 'fr']);
  for (const page of manifest.pages) {
    for (const heading of page.headings) {
      const route = { locale: page.locale, pageId: page.id, anchor: heading.id };
      assert.deepEqual(parseLink(createLink(route)), route);
    }
  }
  assert.equal(docs.resolve('git-cache', 'de').page.locale, 'en');
  assert.equal(docs.canonicalRoute({ locale: 'fr', pageId: 'roadmap' }).pageId, 'availability');
  assert.equal(docs.canonicalRoute({ locale: 'fr', pageId: 'docs/backups.md' }).pageId, 'backups');
  for (const locale of ['en', 'fr']) assert(docs.search(locale === 'en' ? 'materialization' : 'matérialisation', locale).some(item => item.page.id === 'git-materialization'));
  const html = await readFile(join(output, 'Documentation.html'), 'utf8');
  assert(!/<script[^>]+type="module"/.test(html));
  for (const match of html.matchAll(/(?:src|href)="([^"]+)"/g)) {
    assert(!/^(?:https?:|\/\/)/.test(match[1]));
    assert((await stat(join(output, match[1]))).isFile());
  }
});
test('the reader renders all topics in both languages without a network provider', async () => {
  const window = reader();
  try {
    assert.equal(window.document.querySelector('#app').dataset.theme, 'dark');
    for (const page of manifest.pages) {
      navigate(window, createLink({ locale: page.locale, pageId: page.id }));
      assert.equal(window.document.documentElement.lang, page.locale);
      assert.equal(window.document.querySelector('.article-title h1')?.textContent, page.title);
      for (const heading of page.headings) assert(window.document.getElementById(heading.id), `${page.id}/${heading.id}`);
    }
    assert(window.localStorage.getItem('cydoc:reading:cysynck-help'));
    assert.equal(window.document.querySelectorAll('[data-cydoc-module]').length, 0);
  } finally { await window.happyDOM.close(); }
});
test('deep links, locale switching, fallback, search keyboard and narrow navigation', async () => {
  const window = reader('#/fr/backups/verifier-et-restaurer', 'contrast');
  try {
    assert.equal(window.document.querySelector('#app').dataset.theme, 'contrast');
    assert.equal(window.document.documentElement.lang, 'fr');
    const select = window.document.querySelector('.locale-control select');
    select.value = 'en'; select.dispatchEvent(new window.Event('change'));
    window.dispatchEvent(new window.HashChangeEvent('hashchange'));
    assert.equal(parseLink(window.location.hash).anchor, 'verifier-et-restaurer');
    assert.equal(window.document.documentElement.lang, 'en');
    navigate(window, '#/zz/git-materialization/file-states-and-preservation');
    assert.equal(window.document.documentElement.lang, 'en');
    assert.equal(parseLink(window.location.hash).pageId, 'git-materialization');
    window.document.dispatchEvent(new window.KeyboardEvent('keydown', {key: 'k', ctrlKey: true, bubbles: true}));
    assert(window.document.querySelector('.search-dialog').open);
    const search = window.document.querySelector('.search-form input');
    search.value = 'materialization'; search.dispatchEvent(new window.Event('input', {bubbles: true}));
    assert(window.document.querySelectorAll('.search-result').length);
    search.dispatchEvent(new window.KeyboardEvent('keydown', {key: 'Escape', bubbles: true}));
    assert(!window.document.querySelector('.search-dialog').open);
    const menu = window.document.querySelector('[data-menu]');
    menu.click(); assert.equal(menu.getAttribute('aria-expanded'), 'true');
    window.document.querySelector('[data-dismiss-menu]').click();
    assert(!window.document.querySelector('#app').classList.contains('menu-open'));
    window.document.querySelector('[data-set-theme="light"]').click();
    assert.equal(window.localStorage.getItem('cysynck-help:theme'), 'light');
  } finally { await window.happyDOM.close(); }
});
test('Web contextual Help and F1 use the selected language and keep remote launching disabled', async () => {
  const window = new Window({ url: 'https://console.cysynck.invalid/', settings: {
    enableJavaScriptEvaluation: true, suppressInsecureJavaScriptEnvironmentWarning: true,
    disableJavaScriptFileLoading: true, enableCSSFileLoading: false
  } });
  try {
    window.document.write((await readFile(join(here, '../assets/web/index.html'), 'utf8')).replace(/<script\b[^>]*>[\s\S]*?<\/script>/g, ''));
    window.eval(await readFile(join(here, '../assets/web/i18n.js'), 'utf8'));
    const calls = [];
    window.fetch = async (url, options) => {
      assert.equal(url, '/api/help');
      const request = JSON.parse(options.body); calls.push(request);
      return { ok: true, json: async () => ({url: 'file:///server/Documentation/Documentation.html#/fr/backups/verifier-et-restaurer',
        route: '#/fr/backups/verifier-et-restaurer', can_open: false}) };
    };
    window.eval(await readFile(join(here, '../assets/web/app.js'), 'utf8'));
    window.CySynckI18n.setLanguage('fr');
    window.document.querySelector('[data-help-topic="Backups"]').click();
    await new Promise(resolve => setImmediate(resolve));
    assert.deepEqual(calls.at(-1), {context: 'Backups', language: 'fr'});
    assert(window.document.querySelector('#help-dialog').open);
    assert(window.document.querySelector('#help-open').disabled);
    assert.equal(window.document.querySelector('#help-route').value, '#/fr/backups/verifier-et-restaurer');
    assert.equal(window.document.querySelector('#help-download').textContent, 'Télécharger le manuel hors ligne');
    window.document.querySelector('#help-dialog').close();
    const input = window.document.querySelector('#backup-scope-panel input, #backup-scope-panel textarea');
    assert(input);
    input.dispatchEvent(new window.FocusEvent('focusin', {bubbles: true}));
    window.document.dispatchEvent(new window.KeyboardEvent('keydown', {key: 'F1', bubbles: true, cancelable: true}));
    await new Promise(resolve => setImmediate(resolve));
    assert.deepEqual(calls.at(-1), {context: 'Backup scope', language: 'fr'});
  } finally { await window.happyDOM.close(); }
});
test('bookmarks, notes and a reading position survive a reader remount', async () => {
  const window = reader('#/en/git-materialization');
  try {
    window.document.querySelector('[data-bookmark]').click();
    window.document.querySelector('.personal-notes textarea').value = 'Fixture note';
    window.document.querySelector('[data-save-note]').click();
    window.scrollTo({top: 720});
    window.dispatchEvent(new window.Event('scroll'));
    await new Promise(resolve => setTimeout(resolve, 220));
    const saved = JSON.parse(window.localStorage.getItem('cydoc:reading:cysynck-help'));
    assert.equal(saved.positions['en:git-materialization'], 720);
    assert.equal(saved.notes['en:git-materialization'], 'Fixture note');
    assert(saved.bookmarks.some(item => item.id === 'git-materialization'));
    window.CySynckHelp.destroy();
    window.eval(bundle);
    assert.equal(window.document.querySelector('[data-bookmark]').getAttribute('aria-pressed'), 'true');
    assert.equal(window.document.querySelector('.personal-notes textarea').value, 'Fixture note');
    assert(window.document.querySelector('.resume-reading'));
  } finally { await window.happyDOM.close(); }
});
