// Exercise the real Web panel in a DOM model; this is not a browser file-origin test.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { Window } from 'happy-dom';

const web = new URL('../../assets/web/', import.meta.url);
const html = (await readFile(new URL('index.html', web), 'utf8')).replace(/<script\b[^>]*>[\s\S]*?<\/script>/g, '');
const i18n = await readFile(new URL('i18n.js', web), 'utf8');
const app = await readFile(new URL('app.js', web), 'utf8');
async function settle() { await new Promise(resolve => setImmediate(resolve)); await new Promise(resolve => setImmediate(resolve)); }
function panel(respond) {
  const window = new Window({ url: 'https://console.cysynck.invalid/', settings: {
    enableJavaScriptEvaluation: true, suppressInsecureJavaScriptEnvironmentWarning: true,
    disableJavaScriptFileLoading: true, enableCSSFileLoading: false
  } });
  const calls=[];
  window.document.write(html);
  window.fetch = async (url, options) => {
    const request={url,method:options.method,body:options.body?JSON.parse(options.body):null};
    calls.push(request);
    return respond(request);
  };
  window.eval(i18n);
  window.eval(app+'\nglobalThis.panelTest={select:(id)=>{selected=id;resetCyRevision();},skipGitReload:()=>{loadGitExchange=async()=>{};},runSafe:safe};');
  window.panelTest.select('fixture');
  return {window,calls,$:id=>window.document.getElementById(id)};
}
const ok = result => ({ok:true,json:async()=>result});

test('French API errors translate known messages and preserve unknown prototype names as literal text',async()=>{
  for(const [message,expected] of [
    ['Tool not found','Outil introuvable'],
    ['constructor','constructor'],
    ['__proto__','__proto__'],
    ['<img src=x onerror="globalThis.errorMarkupExecuted=true">','<img src=x onerror="globalThis.errorMarkupExecuted=true">']
  ]) {
    const {window,$}=panel(()=>({ok:false,json:async()=>({error:message})}));
    try {
      window.eval('CySynckI18n.setLanguage("fr")');
      $('cyrevision-inbox').click();await settle();
      assert.equal($('error').textContent,expected);
      assert.equal($('error').children.length,0);
      assert.equal(window.errorMarkupExecuted,undefined);
      window.eval('CySynckI18n.setLanguage("en")');
      assert.equal($('error').textContent,message);
      window.eval('CySynckI18n.setLanguage("fr")');
      assert.equal($('error').textContent,expected);
      await window.panelTest.runSafe(async()=>{});
      window.eval('CySynckI18n.setLanguage("en")');
      assert.equal($('error').textContent,'');
    } finally {await window.happyDOM.close();}
  }
});
test('CyRevision panel sends explicit project association and independent pinned fingerprint',async()=>{
  const {window,calls,$}=panel(request=>ok(request.url.endsWith('configure')?{project_id:request.body.project_id,signature_format:request.body.signature_format,devices:{}}:{}));
  try {
    $('cyrevision-guid').value='12345678-1234-4234-8234-123456789abc';$('cyrevision-legacy').checked=true;
    $('cyrevision-configure').click();await settle();
    assert.deepEqual(calls[0],{url:'/api/projects/fixture/cyrevision-configure',method:'POST',body:{project_id:'12345678-1234-4234-8234-123456789abc',signature_format:'windows-legacy'}});
    $('cyrevision-public').value=JSON.stringify({identity:{deviceId:'example',signingPublicKey:'public'},fingerprint:'unverified-shared-value'});
    $('cyrevision-fingerprint').value='independently-confirmed';$('cyrevision-trust').click();await settle();
    assert.equal(calls[1].body.fingerprint,'independently-confirmed');
    assert.equal(calls[1].body.identity.deviceId,'example');
    assert.equal($('error').textContent,'');
  } finally {await window.happyDOM.close();}
});
test('a delayed CyRevision response cannot populate another selected project',async()=>{
  let finish;
  const {window,$}=panel(()=>new Promise(resolve=>{finish=resolve;}));
  try {
    $('cyrevision-inbox').click();
    window.panelTest.select('another-project');
    finish(ok({publications:[{author:{displayName:'Old publisher'},createdAt:'date',transactionId:'old'}],rejected:[]}));
    await settle();
    assert.equal($('cyrevision-result').textContent,'');assert.equal($('cyrevision-publications').childElementCount,0);
    assert.equal($('cyrevision-inbox').disabled,false);
  } finally {await window.happyDOM.close();}
});
test('changing the recovery device invalidates a checked import recovery token',async()=>{
  const {window,calls,$}=panel(()=>ok({pending:true,can_recover:true,applied:false,outcome:'committed',plan:'exact-preview'}));
  try {
    $('cyrevision-device').value='first-device';$('cyrevision-recover').click();await settle();
    assert.equal($('cyrevision-recover-apply').disabled,false);
    $('cyrevision-device').value='another-device';$('cyrevision-device').dispatchEvent(new window.Event('input'));
    assert.equal($('cyrevision-recover-apply').disabled,true);
    $('cyrevision-recover-apply').click();await settle();assert.equal(calls.length,1);
  } finally {await window.happyDOM.close();}
});
test('publication names render as text and imports require their signed transaction ID',async()=>{
  const {window,calls,$}=panel(()=>ok({publications:[{author:{displayName:'<img src=x onerror=alert(1)>'},createdAt:'2026-11-08',transactionId:'signed-transaction'}],rejected:[]}));
  try {
    $('cyrevision-inbox').click();await settle();
    assert.equal($('cyrevision-publications').querySelectorAll('img').length,0);
    assert($('cyrevision-publications').textContent.includes('<img src=x'));
    // Check the semantic import binding without making unrelated Git-view requests.
    window.panelTest.skipGitReload();
    $('cyrevision-publications').querySelector('button').click();await settle();
    assert.deepEqual(calls[1],{url:'/api/projects/fixture/cyrevision-import',method:'POST',body:{transaction:'signed-transaction'}});
  } finally {await window.happyDOM.close();}
});
