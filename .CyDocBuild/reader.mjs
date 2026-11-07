// Application-owned embedding, bundled as a classic script for file:// origins.
import { mountViewer } from 'cydoc/viewer';
import { createLink, parseLink } from 'cydoc';

// Older Markdown anchors could contain accents; CyDoc explicit IDs are ASCII.
const route = parseLink(location.hash, 'en', 'overview');
if (route.anchor) {
  const normalized = route.anchor.normalize('NFD').replace(/\p{M}/gu, '');
  const page = globalThis.CYDOC_DATA.pages.find(page => page.id === route.pageId && page.locale === route.locale);
  if (normalized !== route.anchor && page?.headings.some(heading => heading.id === normalized))
    history.replaceState(null, '', createLink({ ...route, anchor: normalized }, location.href));
}
let theme = 'dark';
try { theme = localStorage.getItem('cysynck-help:theme') || theme; } catch { /* File-origin storage can be disabled. */ }
document.addEventListener('click', event => {
  const button = event.target.closest('[data-set-theme]');
  if (button) try { localStorage.setItem('cysynck-help:theme', button.dataset.setTheme); } catch { /* In-memory reader still works. */ }
});

globalThis.CySynckHelp = mountViewer(document.querySelector('#app'), globalThis.CYDOC_DATA, {
  homePage: 'overview',
  initialLocale: 'en',
  initialTheme: theme,
  storageId: 'cysynck-help',
  assetBase: 'DocData/',
  modules: {}
});
